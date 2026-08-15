#include "internal.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace nexsdf
{
namespace
{

constexpr double kTiny = 1.0e-14;

template <class Function>
void deterministic_parallel_for(
    std::size_t count,
    std::uint32_t requested_workers,
    Function function)
{
    if (count == 0) return;
    const std::size_t worker_count = std::min<std::size_t>(
        std::max<std::uint32_t>(1u, requested_workers), count);
    if (worker_count == 1)
    {
        for (std::size_t index = 0; index < count; ++index) function(index);
        return;
    }
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    std::atomic_bool failed{false};
    std::exception_ptr first_exception;
    std::mutex exception_mutex;
    for (std::size_t worker = 0; worker < worker_count; ++worker)
    {
        const std::size_t begin = count * worker / worker_count;
        const std::size_t end = count * (worker + 1) / worker_count;
        workers.emplace_back([=, &function, &failed, &first_exception, &exception_mutex]() {
            try
            {
                for (std::size_t index = begin; index < end && !failed.load(); ++index)
                    function(index);
            }
            catch (...)
            {
                failed.store(true);
                std::lock_guard<std::mutex> lock(exception_mutex);
                if (!first_exception) first_exception = std::current_exception();
            }
        });
    }
    for (std::thread& worker : workers) worker.join();
    if (first_exception) std::rethrow_exception(first_exception);
}

std::uint64_t hash_combine(std::uint64_t seed, std::uint64_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
    return seed;
}

std::size_t checked_product(std::array<std::uint32_t, 3> size)
{
    std::size_t result = 1;
    for (const std::uint32_t value : size)
    {
        if (value == 0 || result > std::numeric_limits<std::size_t>::max() / value)
        {
            throw std::invalid_argument("invalid or overflowing grid resolution");
        }
        result *= value;
    }
    return result;
}

std::size_t checked_multiply(
    std::size_t value,
    std::size_t factor,
    const char* label)
{
    if (factor != 0 && value > std::numeric_limits<std::size_t>::max() / factor)
    {
        throw std::invalid_argument(std::string(label) + " size overflows");
    }
    return value * factor;
}

Aabb child_box(const Aabb& parent, std::size_t child)
{
    const Vec3 middle = (parent.minimum + parent.maximum) * 0.5;
    Aabb box;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const bool upper = (child & (std::size_t{1} << axis)) != 0;
        box.minimum[axis] = upper ? middle[axis] : parent.minimum[axis];
        box.maximum[axis] = upper ? parent.maximum[axis] : middle[axis];
    }
    return box;
}

Vec3 box_corner(const Aabb& box, std::size_t corner)
{
    return {
        (corner & 1u) ? box.maximum.x : box.minimum.x,
        (corner & 2u) ? box.maximum.y : box.minimum.y,
        (corner & 4u) ? box.maximum.z : box.minimum.z};
}

Vec3 box_center(const Aabb& box)
{
    return (box.minimum + box.maximum) * 0.5;
}

void validate_options(const BuildOptions& options)
{
    if (!std::isfinite(options.relative_padding) || options.relative_padding < 0.0 ||
        !std::isfinite(options.absolute_padding) || options.absolute_padding < 0.0 ||
        !std::isfinite(options.error_tolerance) || options.error_tolerance <= 0.0 ||
        !std::isfinite(options.derivative_step) || options.derivative_step < 0.0)
    {
        throw std::invalid_argument("padding, tolerance, or derivative step is invalid");
    }
    if (options.maximum_depth > 20 || options.start_depth > options.maximum_depth)
    {
        throw std::invalid_argument("octree depths must satisfy start <= maximum <= 20");
    }
    if (options.maximum_triangles_per_leaf == 0)
    {
        throw std::invalid_argument("maximum triangles per leaf must be positive");
    }
    if (options.worker_threads == 0)
    {
        throw std::invalid_argument("worker thread count must be positive");
    }
    if (options.backend != ComputeBackend::CpuScalar &&
        options.backend != ComputeBackend::CpuParallel &&
        options.backend != ComputeBackend::CudaExperimental)
    {
        throw std::invalid_argument("unknown build backend");
    }
    if (options.backend == ComputeBackend::CpuScalar && options.worker_threads != 1)
    {
        throw std::invalid_argument("scalar build backend requires one worker thread");
    }
    if (options.backend == ComputeBackend::CudaExperimental &&
        (options.representation != Representation::DenseGrid ||
         options.reconstruction != Reconstruction::Trilinear))
    {
        throw std::invalid_argument(
            "experimental CUDA generation currently supports dense trilinear fields");
    }
    if (options.backend == ComputeBackend::CudaExperimental &&
        options.worker_threads != 1)
    {
        throw std::invalid_argument(
            "experimental CUDA generation uses one host submission thread");
    }
    if (options.influence_filter != InfluenceFilter::AabbLipschitz &&
        options.influence_filter != InfluenceFilter::PaperGjk &&
        options.influence_filter != InfluenceFilter::PaperFrankWolfe)
    {
        throw std::invalid_argument("unknown exact influence filter");
    }
    if (options.composition != CompositionPolicy::SeparateAssets &&
        options.composition != CompositionPolicy::SolidUnion &&
        options.composition != CompositionPolicy::NestedParity)
    {
        throw std::invalid_argument("unknown composition policy");
    }
    if (options.representation != Representation::ExactInfluenceOctree &&
        options.influence_filter != InfluenceFilter::AabbLipschitz)
    {
        throw std::invalid_argument(
            "paper influence filters apply only to exact influence octrees");
    }
    if (options.representation == Representation::DenseGrid)
    {
        for (const std::uint32_t value : options.resolution)
        {
            if (value == std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("grid resolution is too large");
        }
        checked_product(options.resolution);
        if (options.reconstruction == Reconstruction::Exact)
        {
            throw std::invalid_argument("dense grids require a polynomial reconstruction");
        }
    }
    else if (options.representation == Representation::ExactInfluenceOctree)
    {
        if (options.reconstruction != Reconstruction::Exact)
        {
            throw std::invalid_argument("exact influence octrees require exact reconstruction");
        }
    }
    else if (options.representation == Representation::AdaptivePiecewiseOctree)
    {
        if (options.reconstruction != Reconstruction::Trilinear &&
            options.reconstruction != Reconstruction::TricubicHermite)
        {
            throw std::invalid_argument(
                "adaptive octrees support trilinear or tricubic Hermite reconstruction");
        }
    }
    else
    {
        throw std::invalid_argument("unknown SDF representation");
    }
}

std::size_t grid_point_index(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    std::array<std::uint32_t, 3> dimensions)
{
    return static_cast<std::size_t>(x) +
        static_cast<std::size_t>(dimensions[0]) *
        (static_cast<std::size_t>(y) +
         static_cast<std::size_t>(dimensions[1]) * static_cast<std::size_t>(z));
}

Vec3 regular_position(
    const Aabb& domain,
    std::array<std::uint32_t, 3> resolution,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z,
    bool cell_center)
{
    const Vec3 extent = domain.extent();
    const double offset = cell_center ? 0.5 : 0.0;
    return {
        domain.minimum.x + extent.x * (static_cast<double>(x) + offset) / resolution[0],
        domain.minimum.y + extent.y * (static_cast<double>(y) + offset) / resolution[1],
        domain.minimum.z + extent.z * (static_cast<double>(z) + offset) / resolution[2]};
}

Aabb regular_cell_box(
    const Aabb& domain,
    std::array<std::uint32_t, 3> resolution,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z)
{
    return {
        regular_position(domain, resolution, x, y, z, false),
        regular_position(domain, resolution, x + 1, y + 1, z + 1, false)};
}

void build_dense(detail::AssetData& data, const BuildOptions& options)
{
    const auto resolution = options.resolution;
    data.info.resolution = resolution;
    data.info.node_count = checked_product(resolution);

    if (options.reconstruction == Reconstruction::GradientTaylor)
    {
        data.coefficients.resize(checked_multiply(
            checked_product(resolution), 4, "Gradient Taylor coefficient"));
        const std::size_t count = checked_product(resolution);
        deterministic_parallel_for(count, options.worker_threads, [&](std::size_t index) {
            const std::uint32_t x = static_cast<std::uint32_t>(index % resolution[0]);
            const std::uint32_t y = static_cast<std::uint32_t>(
                (index / resolution[0]) % resolution[1]);
            const std::uint32_t z = static_cast<std::uint32_t>(
                index / (static_cast<std::size_t>(resolution[0]) * resolution[1]));
            const Vec3 center = regular_position(
                data.info.domain, resolution, x, y, z, true);
            const auto sample = detail::gradient_taylor_sample(*data.exact_surface, center);
            const std::size_t offset = 4 * grid_point_index(x, y, z, resolution);
            std::copy(sample.begin(), sample.end(), data.coefficients.begin() + offset);
        });
        data.info.coefficient_count = data.coefficients.size();
        return;
    }

    const std::array<std::uint32_t, 3> point_dimensions{
        resolution[0] + 1, resolution[1] + 1, resolution[2] + 1};
    const std::size_t point_count = checked_product(point_dimensions);
    if (options.reconstruction == Reconstruction::Trilinear)
    {
        data.coefficients.resize(point_count);
        deterministic_parallel_for(point_count, options.worker_threads, [&](std::size_t index) {
            const std::uint32_t x = static_cast<std::uint32_t>(index % point_dimensions[0]);
            const std::uint32_t y = static_cast<std::uint32_t>(
                (index / point_dimensions[0]) % point_dimensions[1]);
            const std::uint32_t z = static_cast<std::uint32_t>(
                index / (static_cast<std::size_t>(point_dimensions[0]) * point_dimensions[1]));
            const Vec3 point = regular_position(
                data.info.domain, resolution, x, y, z, false);
            data.coefficients[grid_point_index(x, y, z, point_dimensions)] =
                data.exact_surface->query(point).phi;
        });
    }
    else
    {
        data.coefficients.resize(checked_multiply(
            point_count, 8, "tricubic coefficient"));
        const Vec3 cell_size{
            data.info.domain.extent().x / resolution[0],
            data.info.domain.extent().y / resolution[1],
            data.info.domain.extent().z / resolution[2]};
        const double step = options.derivative_step > 0.0
            ? options.derivative_step
            : std::max(1.0e-7, std::min({cell_size.x, cell_size.y, cell_size.z}) * 0.25);
        deterministic_parallel_for(point_count, options.worker_threads, [&](std::size_t index) {
            const std::uint32_t x = static_cast<std::uint32_t>(index % point_dimensions[0]);
            const std::uint32_t y = static_cast<std::uint32_t>(
                (index / point_dimensions[0]) % point_dimensions[1]);
            const std::uint32_t z = static_cast<std::uint32_t>(
                index / (static_cast<std::size_t>(point_dimensions[0]) * point_dimensions[1]));
            const Vec3 point = regular_position(
                data.info.domain, resolution, x, y, z, false);
            const auto jet = detail::tricubic_point_jet(*data.exact_surface, point, step);
            const std::size_t offset = 8 * grid_point_index(x, y, z, point_dimensions);
            std::copy(jet.begin(), jet.end(), data.coefficients.begin() + offset);
        });
    }
    data.info.coefficient_count = data.coefficients.size();
}

void build_exact_node(
    detail::AssetData& data,
    const BuildOptions& options,
    std::int32_t node_index,
    std::vector<std::uint32_t> candidates)
{
    const detail::Node node_copy = data.nodes[static_cast<std::size_t>(node_index)];
    const bool stop = node_copy.depth >= options.maximum_depth ||
        (node_copy.depth >= options.start_depth &&
         candidates.size() <= options.maximum_triangles_per_leaf);
    if (stop)
    {
        detail::Node& node = data.nodes[static_cast<std::size_t>(node_index)];
        node.data_offset = data.triangle_indices.size();
        node.data_count = static_cast<std::uint32_t>(candidates.size());
        data.triangle_indices.insert(
            data.triangle_indices.end(), candidates.begin(), candidates.end());
        return;
    }

    std::array<std::vector<std::uint32_t>, 8> child_candidates;
    const std::uint32_t filter_workers = node_copy.depth < 2
        ? options.worker_threads : 1u;
    deterministic_parallel_for(8, filter_workers, [&](std::size_t child)
    {
        const Aabb box = child_box(node_copy.box, child);
        std::vector<std::uint32_t>& filtered = child_candidates[child];
        filtered.reserve(candidates.size());
        if (options.influence_filter == InfluenceFilter::AabbLipschitz)
        {
            const Vec3 center = box_center(box);
            const QueryResult center_query = data.exact_surface->query_subset(
                center, candidates.data(), candidates.size());
            const double radius = 0.5 * norm(box.extent());
            const double upper_bound = std::abs(center_query.phi) + radius;
            for (const std::uint32_t triangle : candidates)
            {
                if (detail::aabb_distance(box, detail::triangle_bounds(data.mesh, triangle)) <=
                    upper_bound + 16.0 * std::numeric_limits<double>::epsilon() *
                        std::max(1.0, upper_bound))
                {
                    filtered.push_back(triangle);
                }
            }
        }
        else
        {
            std::array<std::uint32_t, 8> corner_reference{};
            for (std::size_t corner = 0; corner < 8; ++corner)
            {
                const Vec3 point = box_corner(box, corner);
                double nearest = std::numeric_limits<double>::infinity();
                for (const std::uint32_t triangle : candidates)
                {
                    const double distance =
                        detail::point_triangle_distance(data.mesh, triangle, point);
                    if (distance < nearest ||
                        (distance == nearest && triangle < corner_reference[corner]))
                    {
                        nearest = distance;
                        corner_reference[corner] = triangle;
                    }
                }
            }
            for (const std::uint32_t triangle_index : candidates)
            {
                const Triangle& triangle = data.mesh.triangles[triangle_index];
                const Vec3 centroid = (
                    data.mesh.vertices[triangle.vertex[0]] +
                    data.mesh.vertices[triangle.vertex[1]] +
                    data.mesh.vertices[triangle.vertex[2]]) / 3.0;
                std::size_t nearest_corner = 0;
                double nearest_squared = squared_norm(centroid - box_corner(box, 0));
                for (std::size_t corner = 1; corner < 8; ++corner)
                {
                    const double candidate_squared =
                        squared_norm(centroid - box_corner(box, corner));
                    if (candidate_squared < nearest_squared)
                    {
                        nearest_squared = candidate_squared;
                        nearest_corner = corner;
                    }
                }
                const std::uint32_t reference = corner_reference[nearest_corner];
                if (reference == triangle_index || detail::paper_influence_intersects(
                        data.mesh, box, reference, triangle_index,
                        options.influence_filter))
                {
                    filtered.push_back(triangle_index);
                }
            }
        }
        if (filtered.empty())
        {
            filtered = candidates;
        }
    });
    for (std::size_t child = 0; child < 8; ++child)
    {
        const Aabb box = child_box(node_copy.box, child);
        const std::int32_t child_index = static_cast<std::int32_t>(data.nodes.size());
        data.nodes[static_cast<std::size_t>(node_index)].child[child] = child_index;
        detail::Node child_node;
        child_node.box = box;
        child_node.depth = node_copy.depth + 1;
        data.nodes.push_back(child_node);
        build_exact_node(
            data, options, child_index, std::move(child_candidates[child]));
    }
}

void build_exact_octree(detail::AssetData& data, const BuildOptions& options)
{
    detail::Node root;
    root.box = data.info.domain;
    data.nodes.push_back(root);
    std::vector<std::uint32_t> triangles = data.exact_surface->active_triangles();
    build_exact_node(data, options, 0, std::move(triangles));
    data.info.node_count = data.nodes.size();
    data.info.coefficient_count = 0;
    data.info.candidate_index_count = data.triangle_indices.size();
}

std::array<Vec3, 19> error_points(const Aabb& box)
{
    const Vec3 center = box_center(box);
    const Vec3 half = box.extent() * 0.5;
    constexpr std::array<std::array<int, 3>, 19> coordinates{{
        {{0,-1,-1}}, {{-1,0,-1}}, {{0,0,-1}}, {{1,0,-1}}, {{0,1,-1}},
        {{-1,-1,0}}, {{0,-1,0}}, {{1,-1,0}}, {{-1,0,0}}, {{0,0,0}},
        {{1,0,0}}, {{-1,1,0}}, {{0,1,0}}, {{1,1,0}},
        {{0,-1,1}}, {{-1,0,1}}, {{0,0,1}}, {{1,0,1}}, {{0,1,1}}
    }};
    std::array<Vec3, 19> points{};
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        points[i] = {
            center.x + coordinates[i][0] * half.x,
            center.y + coordinates[i][1] * half.y,
            center.z + coordinates[i][2] * half.z};
    }
    return points;
}

detail::PolynomialSample evaluate_node_polynomial(
    const detail::AssetData& data,
    std::size_t node_index,
    Vec3 point)
{
    const detail::Node& node = data.nodes[node_index];
    const double* values = data.coefficients.data() + node.data_offset;
    if (data.info.reconstruction == Reconstruction::Trilinear)
    {
        return detail::evaluate_trilinear(values, node.box, point);
    }
    return detail::evaluate_tricubic(values, node.box, point);
}

struct ErrorMeasurement
{
    double maximum{0.0};
    double trapezoidal_rms{0.0};
};

ErrorMeasurement measure_node_error(const detail::AssetData& data, std::size_t node_index)
{
    constexpr std::array<double, 19> weights{
        2,2,4,2,2, 2,4,2,4,8,4,2,4,2, 2,2,4,2,2};
    ErrorMeasurement measurement;
    double weighted_squared = 0.0;
    const auto points = error_points(data.nodes[node_index].box);
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const double exact = data.exact_surface->query(points[i]).phi;
        const double approximate = evaluate_node_polynomial(data, node_index, points[i]).value;
        const double error = exact - approximate;
        measurement.maximum = std::max(measurement.maximum, std::abs(error));
        weighted_squared += weights[i] * error * error / 64.0;
    }
    measurement.trapezoidal_rms = std::sqrt(weighted_squared);
    return measurement;
}

void assign_adaptive_coefficients(
    detail::AssetData& data,
    std::size_t node_index,
    const BuildOptions& options)
{
    detail::Node& node = data.nodes[node_index];
    node.data_offset = data.coefficients.size();
    if (options.reconstruction == Reconstruction::Trilinear)
    {
        const auto values = detail::trilinear_samples(*data.exact_surface, node.box);
        node.data_count = static_cast<std::uint32_t>(values.size());
        data.coefficients.insert(data.coefficients.end(), values.begin(), values.end());
    }
    else
    {
        const auto values = detail::tricubic_samples(
            *data.exact_surface, node.box, options.derivative_step);
        node.data_count = static_cast<std::uint32_t>(values.size());
        data.coefficients.insert(data.coefficients.end(), values.begin(), values.end());
    }
}

void subdivide_adaptive(
    detail::AssetData& data,
    std::size_t node_index,
    const BuildOptions& options)
{
    const detail::Node parent = data.nodes[node_index];
    for (std::size_t child = 0; child < 8; ++child)
    {
        const std::int32_t child_index = static_cast<std::int32_t>(data.nodes.size());
        data.nodes[node_index].child[child] = child_index;
        detail::Node child_node;
        child_node.box = child_box(parent.box, child);
        child_node.depth = parent.depth + 1;
        data.nodes.push_back(child_node);
        assign_adaptive_coefficients(data, static_cast<std::size_t>(child_index), options);
    }
}

std::size_t leaf_at(const detail::AssetData& data, Vec3 point)
{
    std::size_t node_index = 0;
    while (!data.nodes[node_index].leaf())
    {
        const detail::Node& node = data.nodes[node_index];
        const Vec3 middle = box_center(node.box);
        const std::size_t child = (point.x >= middle.x ? 1u : 0u) |
            (point.y >= middle.y ? 2u : 0u) |
            (point.z >= middle.z ? 4u : 0u);
        node_index = static_cast<std::size_t>(node.child[child]);
    }
    return node_index;
}

std::vector<std::size_t> adaptive_leaves(const detail::AssetData& data)
{
    std::vector<std::size_t> leaves;
    for (std::size_t i = 0; i < data.nodes.size(); ++i)
    {
        if (data.nodes[i].leaf())
        {
            leaves.push_back(i);
        }
    }
    std::stable_sort(leaves.begin(), leaves.end(), [&data](std::size_t a, std::size_t b) {
        return data.nodes[a].depth < data.nodes[b].depth;
    });
    return leaves;
}

void impose_adaptive_vertex_constraints(detail::AssetData& data)
{
    const Vec3 domain_size = data.info.domain.extent();
    const double epsilon = 64.0 * std::numeric_limits<double>::epsilon() *
        std::max({1.0, domain_size.x, domain_size.y, domain_size.z});
    const std::vector<std::size_t> leaves = adaptive_leaves(data);
    for (const std::size_t leaf_index : leaves)
    {
        detail::Node& leaf = data.nodes[leaf_index];
        for (std::size_t corner = 0; corner < 8; ++corner)
        {
            const Vec3 point = box_corner(leaf.box, corner);
            std::size_t source = leaf_index;
            for (std::size_t octant = 0; octant < 8; ++octant)
            {
                Vec3 probe = point;
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    probe[axis] += (octant & (std::size_t{1} << axis)) ? epsilon : -epsilon;
                    probe[axis] = std::max(
                        data.info.domain.minimum[axis] + epsilon,
                        std::min(data.info.domain.maximum[axis] - epsilon, probe[axis]));
                }
                const std::size_t candidate = leaf_at(data, probe);
                if (data.nodes[candidate].depth < data.nodes[source].depth)
                {
                    source = candidate;
                }
            }
            if (source == leaf_index)
            {
                continue;
            }
            if (data.info.reconstruction == Reconstruction::Trilinear)
            {
                data.coefficients[leaf.data_offset + corner] =
                    evaluate_node_polynomial(data, source, point).value;
            }
            else
            {
                const detail::Node& source_node = data.nodes[source];
                const auto jet = detail::evaluate_tricubic_jet(
                    data.coefficients.data() + source_node.data_offset,
                    source_node.box,
                    point);
                std::copy(
                    jet.begin(), jet.end(),
                    data.coefficients.begin() + leaf.data_offset + 8 * corner);
            }
        }
    }
}

void build_adaptive_octree(detail::AssetData& data, const BuildOptions& options)
{
    detail::Node root;
    root.box = data.info.domain;
    data.nodes.push_back(root);
    assign_adaptive_coefficients(data, 0, options);

    for (;;)
    {
        impose_adaptive_vertex_constraints(data);
        std::vector<std::size_t> refine;
        double maximum_error = 0.0;
        const std::vector<std::size_t> leaves = adaptive_leaves(data);
        std::vector<ErrorMeasurement> measurements(leaves.size());
        deterministic_parallel_for(leaves.size(), options.worker_threads, [&](std::size_t index) {
            measurements[index] = measure_node_error(data, leaves[index]);
        });
        for (std::size_t index = 0; index < leaves.size(); ++index)
        {
            const std::size_t leaf = leaves[index];
            detail::Node& node = data.nodes[leaf];
            const ErrorMeasurement measurement = measurements[index];
            node.measured_error = measurement.maximum;
            maximum_error = std::max(maximum_error, node.measured_error);
            if (node.depth < options.start_depth ||
                (node.depth < options.maximum_depth &&
                 measurement.trapezoidal_rms > options.error_tolerance))
            {
                refine.push_back(leaf);
            }
        }
        data.info.measured_maximum_error = maximum_error;
        if (refine.empty())
        {
            break;
        }
        for (const std::size_t leaf : refine)
        {
            subdivide_adaptive(data, leaf, options);
        }
    }
    std::vector<double> compact;
    compact.reserve(data.coefficients.size());
    for (detail::Node& node : data.nodes)
    {
        if (!node.leaf())
        {
            node.data_offset = 0;
            node.data_count = 0;
            continue;
        }
        const std::uint64_t old_offset = node.data_offset;
        node.data_offset = compact.size();
        compact.insert(
            compact.end(),
            data.coefficients.begin() + static_cast<std::ptrdiff_t>(old_offset),
            data.coefficients.begin() + static_cast<std::ptrdiff_t>(old_offset + node.data_count));
    }
    data.coefficients = std::move(compact);
    data.info.node_count = data.nodes.size();
    data.info.coefficient_count = data.coefficients.size();
    data.info.has_measured_error = true;
}

detail::PolynomialSample query_dense_polynomial(
    const detail::AssetData& data,
    Vec3 point,
    std::uint64_t& cell_signature)
{
    const auto resolution = data.info.resolution;
    const Vec3 extent = data.info.domain.extent();
    std::array<std::uint32_t, 3> cell{};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const double coordinate = (point[axis] - data.info.domain.minimum[axis]) /
            extent[axis] * resolution[axis];
        cell[axis] = std::min(
            resolution[axis] - 1,
            static_cast<std::uint32_t>(std::max(0.0, std::floor(coordinate))));
    }
    cell_signature = grid_point_index(cell[0], cell[1], cell[2], resolution);
    const Aabb box = regular_cell_box(
        data.info.domain, resolution, cell[0], cell[1], cell[2]);

    if (data.info.reconstruction == Reconstruction::GradientTaylor)
    {
        const double* values = data.coefficients.data() + 4 * cell_signature;
        return detail::evaluate_gradient_taylor(values, box_center(box), point);
    }

    const std::array<std::uint32_t, 3> point_dimensions{
        resolution[0] + 1, resolution[1] + 1, resolution[2] + 1};
    if (data.info.reconstruction == Reconstruction::Trilinear)
    {
        std::array<double, 8> values{};
        for (std::size_t corner = 0; corner < 8; ++corner)
        {
            const std::uint32_t x = cell[0] + ((corner & 1u) ? 1u : 0u);
            const std::uint32_t y = cell[1] + ((corner & 2u) ? 1u : 0u);
            const std::uint32_t z = cell[2] + ((corner & 4u) ? 1u : 0u);
            values[corner] = data.coefficients[grid_point_index(x, y, z, point_dimensions)];
        }
        return detail::evaluate_trilinear(values.data(), box, point);
    }

    std::array<double, 64> jets{};
    for (std::size_t corner = 0; corner < 8; ++corner)
    {
        const std::uint32_t x = cell[0] + ((corner & 1u) ? 1u : 0u);
        const std::uint32_t y = cell[1] + ((corner & 2u) ? 1u : 0u);
        const std::uint32_t z = cell[2] + ((corner & 4u) ? 1u : 0u);
        const std::size_t source = 8 * grid_point_index(x, y, z, point_dimensions);
        std::copy_n(data.coefficients.begin() + source, 8, jets.begin() + 8 * corner);
    }
    return detail::evaluate_tricubic(jets.data(), box, point);
}

QueryResult polynomial_result(
    const detail::PolynomialSample& sample,
    std::uint64_t signature,
    double measured_error,
    bool has_measured_error)
{
    QueryResult result;
    result.phi = sample.value;
    result.raw_gradient = sample.gradient;
    const double gradient_length = norm(sample.gradient);
    result.unit_normal = gradient_length > kTiny
        ? sample.gradient / gradient_length
        : Vec3{};
    result.hessian = sample.hessian;
    result.measured_leaf_error = measured_error;
    result.has_measured_error = has_measured_error;
    result.branch_signature = signature;
    result.valid = std::isfinite(sample.value) &&
        std::isfinite(sample.gradient.x) &&
        std::isfinite(sample.gradient.y) &&
        std::isfinite(sample.gradient.z);
    result.exact = false;
    result.in_domain = true;
    result.has_hessian = true;
    return result;
}

} // namespace

struct Asset::Impl
{
    explicit Impl(std::shared_ptr<const detail::AssetData> source)
        : data(std::move(source))
    {
    }

    std::shared_ptr<const detail::AssetData> data;
};

Asset::Asset() = default;
Asset::~Asset() = default;
Asset::Asset(Asset&&) noexcept = default;
Asset& Asset::operator=(Asset&&) noexcept = default;
Asset::Asset(const Asset&) = default;
Asset& Asset::operator=(const Asset&) = default;

Asset::Asset(std::shared_ptr<const Impl> impl) : impl_(std::move(impl))
{
}

const AssetInfo& Asset::info() const noexcept
{
    static const AssetInfo empty{};
    return impl_ && impl_->data ? impl_->data->info : empty;
}

QueryResult Asset::query(Vec3 point) const
{
    return impl_ && impl_->data ? detail::query_asset(*impl_->data, point) : QueryResult{};
}

QueryResult Asset::query_certified(Vec3 point) const
{
    if (!impl_ || !impl_->data)
    {
        return QueryResult{};
    }
    const detail::AssetData& data = *impl_->data;
    if (!data.info.domain.contains(point))
    {
        return QueryResult{};
    }
    if (data.info.representation != Representation::ExactInfluenceOctree)
    {
        return detail::query_asset(data, point);
    }

    const std::size_t leaf = leaf_at(data, point);
    QueryResult result = data.exact_surface->query_certified(point);
    result.in_domain = true;
    result.branch_signature = hash_combine(result.branch_signature, leaf);
    result.cell_depth = data.nodes[leaf].depth;
    double domain_clearance = std::numeric_limits<double>::infinity();
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        domain_clearance = std::min(
            domain_clearance,
            std::min(
                point[axis] - data.info.domain.minimum[axis],
                data.info.domain.maximum[axis] - point[axis]));
    }
    result.branch_motion_clearance = std::min(
        result.branch_motion_clearance,
        std::max(0.0, domain_clearance));
    return result;
}

void Asset::query_batch(const Vec3* points, std::size_t count, QueryResult* out) const
{
    query_batch(points, count, out, BatchQueryOptions{});
}

void Asset::query_batch(
    const Vec3* points,
    std::size_t count,
    QueryResult* out,
    const BatchQueryOptions& options) const
{
    if ((count != 0 && (!points || !out)) || !impl_ || !impl_->data)
    {
        throw std::invalid_argument("invalid batch query arguments or empty asset");
    }
    if (options.worker_threads == 0)
    {
        throw std::invalid_argument("batch worker thread count must be positive");
    }
    if (options.backend == BatchBackend::CudaExperimental)
    {
        if (options.worker_threads != 1)
            throw std::invalid_argument(
                "experimental CUDA query uses one host submission thread");
        (void)detail::query_batch_cuda(*impl_->data, points, count, out);
        return;
    }
    if (options.backend == BatchBackend::AutoSimd)
    {
        if (options.worker_threads == 1 &&
            detail::query_batch_simd(*impl_->data, points, count, out))
            return;
        deterministic_parallel_for(count, options.worker_threads, [&](std::size_t i)
        {
            if (!detail::query_batch_simd(*impl_->data, points + i, 1, out + i))
                out[i] = detail::query_asset(*impl_->data, points[i]);
        });
        return;
    }
    if (options.backend != BatchBackend::Scalar)
    {
        throw std::invalid_argument("unknown batch query backend");
    }
    deterministic_parallel_for(count, options.worker_threads, [&](std::size_t i)
    {
        out[i] = detail::query_asset(*impl_->data, points[i]);
    });
}

void Asset::save(const std::string& path) const
{
    if (!impl_ || !impl_->data)
    {
        throw std::invalid_argument("cannot save an empty asset");
    }
    detail::save_asset(*impl_->data, path);
}

Asset Asset::load(const std::string& path)
{
    return Asset(std::make_shared<Impl>(detail::load_asset(path)));
}

Asset build(const SurfaceMesh& mesh, const BuildOptions& options)
{
    return Asset(std::make_shared<Asset::Impl>(detail::build_asset_data(mesh, options)));
}

bool is_cuda_backend_available() noexcept
{
    return detail::cuda_backend_available();
}

namespace detail
{

std::shared_ptr<const AssetData> build_asset_data(
    const SurfaceMesh& mesh,
    const BuildOptions& options)
{
    validate_options(options);
    auto data = std::make_shared<AssetData>();
    data->exact_surface = std::make_shared<ExactSurface>(mesh, options.composition);
    data->mesh = data->exact_surface->mesh();
    data->info.representation = options.representation;
    data->info.reconstruction = options.reconstruction;
    data->info.influence_filter = options.influence_filter;
    data->info.composition = options.composition;
    data->info.build_backend = options.backend;
    data->info.worker_threads = options.worker_threads;
    data->info.component_count = static_cast<std::uint32_t>(
        data->exact_surface->component_count());
    data->info.active_component_count = static_cast<std::uint32_t>(
        data->exact_surface->active_component_count());
    data->info.domain = padded_bounds(data->mesh, options);
    data->info.maximum_depth = options.maximum_depth;
    data->info.requested_error_tolerance =
        options.representation == Representation::AdaptivePiecewiseOctree
        ? options.error_tolerance : 0.0;
    data->info.triangle_count = data->mesh.triangles.size();

    switch (options.representation)
    {
    case Representation::DenseGrid:
        if (options.backend == ComputeBackend::CudaExperimental)
            build_dense_cuda(*data, options);
        else
            build_dense(*data, options);
        break;
    case Representation::ExactInfluenceOctree:
        build_exact_octree(*data, options);
        break;
    case Representation::AdaptivePiecewiseOctree:
        build_adaptive_octree(*data, options);
        break;
    }
    return data;
}

QueryResult query_asset(const AssetData& data, Vec3 point)
{
    if (!data.info.domain.contains(point))
    {
        QueryResult result;
        result.in_domain = false;
        return result;
    }

    if (data.info.representation == Representation::DenseGrid)
    {
        std::uint64_t cell = 0;
        return polynomial_result(
            query_dense_polynomial(data, point, cell),
            hash_combine(0x44454e5345ull, cell),
            0.0,
            false);
    }

    const std::size_t leaf = leaf_at(data, point);
    const Node& node = data.nodes[leaf];
    if (data.info.representation == Representation::ExactInfluenceOctree)
    {
        QueryResult result = data.exact_surface->query_subset(
            point,
            data.triangle_indices.data() + node.data_offset,
            node.data_count);
        result.in_domain = true;
        result.branch_signature = hash_combine(result.branch_signature, leaf);
        result.cell_depth = node.depth;
        return result;
    }

    QueryResult result = polynomial_result(
        evaluate_node_polynomial(data, leaf, point),
        hash_combine(0x4f4354524545ull, leaf),
        node.measured_error,
        true);
    result.cell_depth = node.depth;
    return result;
}

namespace
{

constexpr std::array<std::uint8_t, 8> kAssetMagic{
    'N', 'X', 'S', 'D', 'F', 0, '\r', '\n'};
constexpr std::uint64_t kMaximumSerializedItems = 500000000ull;

std::uint64_t fnv1a(const std::uint8_t* bytes, std::size_t count) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < count; ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

struct Writer
{
    std::vector<std::uint8_t> bytes;

    void u8(std::uint8_t value) { bytes.push_back(value); }
    void u32(std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void u64(std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
        {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void f64(double value)
    {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
        std::memcpy(&bits, &value, sizeof(bits));
        u64(bits);
    }
    void vec3(Vec3 value) { f64(value.x); f64(value.y); f64(value.z); }
    void box(Aabb value) { vec3(value.minimum); vec3(value.maximum); }
};

struct Reader
{
    const std::vector<std::uint8_t>& bytes;
    std::size_t offset{0};
    std::size_t payload_size{0};

    void require(std::size_t count, const char* label)
    {
        if (offset > payload_size || count > payload_size - offset)
        {
            throw std::runtime_error(std::string("truncated NSDF while reading ") + label);
        }
    }
    std::uint8_t u8(const char* label)
    {
        require(1, label);
        return bytes[offset++];
    }
    std::uint32_t u32(const char* label)
    {
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            value |= static_cast<std::uint32_t>(u8(label)) << shift;
        }
        return value;
    }
    std::int32_t i32(const char* label)
    {
        return static_cast<std::int32_t>(u32(label));
    }
    std::uint64_t u64(const char* label)
    {
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
        {
            value |= static_cast<std::uint64_t>(u8(label)) << shift;
        }
        return value;
    }
    double f64(const char* label)
    {
        const std::uint64_t bits = u64(label);
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    Vec3 vec3(const char* label) { return {f64(label), f64(label), f64(label)}; }
    Aabb box(const char* label) { return {vec3(label), vec3(label)}; }
};

void require_count(std::uint64_t value, const char* label)
{
    if (value > kMaximumSerializedItems ||
        value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw std::runtime_error(std::string("unreasonable NSDF ") + label + " count");
    }
}

std::uint64_t checked_serialized_size(
    std::uint64_t count,
    std::uint64_t item_size,
    const char* label)
{
    if (count != 0 && item_size > std::numeric_limits<std::uint64_t>::max() / count)
        throw std::runtime_error(std::string("NSDF ") + label + " byte size overflows");
    return count * item_size;
}

void add_serialized_size(
    std::uint64_t& total,
    std::uint64_t addition,
    const char* label)
{
    if (addition > std::numeric_limits<std::uint64_t>::max() - total)
        throw std::runtime_error(std::string("NSDF ") + label + " payload size overflows");
    total += addition;
}

bool finite_box(const Aabb& box) noexcept
{
    return std::isfinite(box.minimum.x) && std::isfinite(box.minimum.y) &&
        std::isfinite(box.minimum.z) && std::isfinite(box.maximum.x) &&
        std::isfinite(box.maximum.y) && std::isfinite(box.maximum.z) &&
        box.maximum.x > box.minimum.x && box.maximum.y > box.minimum.y &&
        box.maximum.z > box.minimum.z;
}

void validate_tree_structure(const AssetData& data)
{
    if (data.nodes.empty())
    {
        throw std::runtime_error("NSDF octree contains no nodes");
    }
    std::vector<std::uint8_t> state(data.nodes.size(), 0);
    std::vector<std::uint32_t> parent_count(data.nodes.size(), 0);
    std::vector<std::pair<std::size_t, bool>> stack{{0, false}};
    while (!stack.empty())
    {
        const auto current = stack.back();
        stack.pop_back();
        const std::size_t index = current.first;
        if (current.second)
        {
            state[index] = 2;
            continue;
        }
        if (state[index] == 1) throw std::runtime_error("NSDF octree contains a cycle");
        if (state[index] == 2) continue;
        state[index] = 1;
        stack.push_back({index, true});
        const Node& node = data.nodes[index];
        if (!finite_box(node.box) || !std::isfinite(node.measured_error) ||
            node.measured_error < 0.0)
        {
            throw std::runtime_error("NSDF octree node contains invalid numeric data");
        }
        std::size_t child_count = 0;
        for (const std::int32_t child : node.child) child_count += child >= 0 ? 1u : 0u;
        if (child_count != 0 && child_count != 8)
        {
            throw std::runtime_error("NSDF octree node has a partial child set");
        }
        for (const std::int32_t child : node.child)
        {
            if (child < 0) continue;
            const std::size_t child_index = static_cast<std::size_t>(child);
            if (++parent_count[child_index] != 1)
            {
                throw std::runtime_error("NSDF octree node has multiple parents");
            }
            if (data.nodes[child_index].depth != node.depth + 1)
            {
                throw std::runtime_error("NSDF octree child depth is inconsistent");
            }
            stack.push_back({child_index, false});
        }
    }
    if (std::find(state.begin(), state.end(), std::uint8_t{0}) != state.end())
    {
        throw std::runtime_error("NSDF octree contains unreachable nodes");
    }
    if (parent_count[0] != 0 ||
        std::find(parent_count.begin() + 1, parent_count.end(), 0u) != parent_count.end())
    {
        throw std::runtime_error("NSDF octree parent relationships are invalid");
    }
}

void validate_loaded_layout(const AssetData& data)
{
    if (!finite_box(data.info.domain))
    {
        throw std::runtime_error("NSDF domain is invalid");
    }
    if (!std::isfinite(data.info.requested_error_tolerance) ||
        !std::isfinite(data.info.measured_maximum_error) ||
        data.info.requested_error_tolerance < 0.0 ||
        data.info.measured_maximum_error < 0.0)
    {
        throw std::runtime_error("NSDF error metadata is invalid");
    }
    if (data.info.influence_filter != InfluenceFilter::AabbLipschitz &&
        data.info.influence_filter != InfluenceFilter::PaperGjk &&
        data.info.influence_filter != InfluenceFilter::PaperFrankWolfe)
    {
        throw std::runtime_error("NSDF influence-filter metadata is invalid");
    }
    if (data.info.composition != CompositionPolicy::SeparateAssets &&
        data.info.composition != CompositionPolicy::SolidUnion &&
        data.info.composition != CompositionPolicy::NestedParity)
    {
        throw std::runtime_error("NSDF composition metadata is invalid");
    }
    if (data.info.component_count == 0 ||
        data.info.active_component_count == 0 ||
        data.info.active_component_count > data.info.component_count)
    {
        throw std::runtime_error("NSDF component metadata is invalid");
    }
    if (data.info.representation != Representation::ExactInfluenceOctree &&
        data.info.influence_filter != InfluenceFilter::AabbLipschitz)
    {
        throw std::runtime_error("non-exact NSDF declares an exact influence filter");
    }
    if (data.info.representation == Representation::DenseGrid)
    {
        if (!data.nodes.empty() || !data.triangle_indices.empty())
        {
            throw std::runtime_error("dense NSDF contains unexpected tree data");
        }
        const std::size_t cells = checked_product(data.info.resolution);
        if (data.info.node_count != cells)
        {
            throw std::runtime_error("dense NSDF logical cell count is inconsistent");
        }
        std::array<std::uint32_t, 3> point_dimensions{
            data.info.resolution[0], data.info.resolution[1], data.info.resolution[2]};
        for (std::uint32_t& value : point_dimensions)
        {
            if (value == std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("dense NSDF resolution is too large");
            ++value;
        }
        std::size_t expected = 0;
        if (data.info.reconstruction == Reconstruction::Trilinear)
            expected = checked_product(point_dimensions);
        else if (data.info.reconstruction == Reconstruction::TricubicHermite)
        {
            const std::size_t points = checked_product(point_dimensions);
            if (points > std::numeric_limits<std::size_t>::max() / 8)
                throw std::runtime_error("dense tricubic coefficient count overflows");
            expected = 8 * points;
        }
        else if (data.info.reconstruction == Reconstruction::GradientTaylor)
            expected = 4 * cells;
        else
            throw std::runtime_error("dense NSDF reconstruction is invalid");
        if (data.coefficients.size() != expected || !data.mesh.triangles.empty())
        {
            throw std::runtime_error("dense NSDF payload layout is inconsistent");
        }
        return;
    }

    validate_tree_structure(data);
    if (data.info.node_count != data.nodes.size())
    {
        throw std::runtime_error("octree NSDF logical node count is inconsistent");
    }
    if (data.info.representation == Representation::ExactInfluenceOctree)
    {
        if (data.info.reconstruction != Reconstruction::Exact ||
            !data.coefficients.empty() || data.mesh.triangles.empty() ||
            data.info.triangle_count != data.mesh.triangles.size())
        {
            throw std::runtime_error("exact octree NSDF payload layout is inconsistent");
        }
        for (const Node& node : data.nodes)
        {
            if (node.leaf() && node.data_count == 0)
                throw std::runtime_error("exact octree leaf has no triangle candidates");
        }
        return;
    }
    if (data.info.representation != Representation::AdaptivePiecewiseOctree ||
        (data.info.reconstruction != Reconstruction::Trilinear &&
         data.info.reconstruction != Reconstruction::TricubicHermite) ||
        !data.mesh.triangles.empty() || !data.triangle_indices.empty())
    {
        throw std::runtime_error("adaptive octree NSDF payload layout is inconsistent");
    }
    const std::uint32_t expected = data.info.reconstruction == Reconstruction::Trilinear
        ? 8u : 64u;
    for (const Node& node : data.nodes)
    {
        if (node.leaf() && node.data_count != expected)
            throw std::runtime_error("adaptive octree leaf coefficient count is invalid");
        if (!node.leaf() && node.data_count != 0)
            throw std::runtime_error("adaptive octree internal node stores coefficients");
    }
}

} // namespace

void save_asset(const AssetData& data, const std::string& path)
{
    Writer writer;
    writer.bytes.insert(writer.bytes.end(), kAssetMagic.begin(), kAssetMagic.end());
    writer.u32(data.info.format_major);
    writer.u32(data.info.format_minor);
    writer.u32(static_cast<std::uint32_t>(data.info.representation));
    writer.u32(static_cast<std::uint32_t>(data.info.reconstruction));
    if (data.info.format_minor >= 1)
        writer.u32(static_cast<std::uint32_t>(data.info.influence_filter));
    if (data.info.format_minor >= 2)
    {
        writer.u32(static_cast<std::uint32_t>(data.info.composition));
        writer.u32(data.info.component_count);
        writer.u32(data.info.active_component_count);
    }
    if (data.info.format_minor >= 3)
    {
        writer.u32(static_cast<std::uint32_t>(data.info.build_backend));
        writer.u32(data.info.worker_threads);
    }
    writer.box(data.info.domain);
    for (const std::uint32_t value : data.info.resolution) writer.u32(value);
    writer.u32(data.info.maximum_depth);
    writer.f64(data.info.requested_error_tolerance);
    writer.f64(data.info.measured_maximum_error);
    writer.u32(data.info.has_measured_error ? 1u : 0u);
    writer.u64(data.info.node_count);
    writer.u64(data.nodes.size());
    writer.u64(data.coefficients.size());
    writer.u64(data.info.triangle_count);
    const bool store_mesh =
        data.info.representation == Representation::ExactInfluenceOctree;
    writer.u64(store_mesh ? data.mesh.triangles.size() : 0);
    writer.u64(store_mesh ? data.mesh.vertices.size() : 0);
    writer.u64(data.triangle_indices.size());

    if (store_mesh)
    {
        for (const Vec3 vertex : data.mesh.vertices) writer.vec3(vertex);
        for (const Triangle& triangle : data.mesh.triangles)
        {
            for (const std::uint32_t index : triangle.vertex) writer.u32(index);
            writer.u32(triangle.face_id);
            writer.u32(triangle.has_corner_normals ? 1u : 0u);
            for (const Vec3 normal : triangle.corner_normal) writer.vec3(normal);
        }
    }
    for (const Node& node : data.nodes)
    {
        writer.box(node.box);
        for (const std::int32_t child : node.child) writer.i32(child);
        writer.u64(node.data_offset);
        writer.u32(node.data_count);
        writer.u32(node.depth);
        writer.f64(node.measured_error);
    }
    for (const std::uint32_t index : data.triangle_indices) writer.u32(index);
    for (const double coefficient : data.coefficients) writer.f64(coefficient);
    writer.u64(fnv1a(writer.bytes.data(), writer.bytes.size()));

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("cannot create NSDF asset: " + path);
    }
    output.write(
        reinterpret_cast<const char*>(writer.bytes.data()),
        static_cast<std::streamsize>(writer.bytes.size()));
    if (!output)
    {
        throw std::runtime_error("failed to write NSDF asset: " + path);
    }
}

std::shared_ptr<const AssetData> load_asset(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        throw std::runtime_error("cannot open NSDF asset: " + path);
    }
    const std::streamoff end = input.tellg();
    if (end < static_cast<std::streamoff>(kAssetMagic.size() + sizeof(std::uint64_t)))
    {
        throw std::runtime_error("NSDF asset is too small: " + path);
    }
    if (static_cast<std::uint64_t>(end) >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw std::runtime_error("NSDF asset is too large for this process");
    }
    input.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.read(reinterpret_cast<char*>(bytes.data()), end);
    if (!input)
    {
        throw std::runtime_error("failed to read NSDF asset: " + path);
    }
    std::uint64_t stored_hash = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
    {
        stored_hash |= static_cast<std::uint64_t>(
            bytes[bytes.size() - 8 + shift / 8]) << shift;
    }
    const std::size_t payload_size = bytes.size() - 8;
    if (fnv1a(bytes.data(), payload_size) != stored_hash)
    {
        throw std::runtime_error("NSDF checksum mismatch: " + path);
    }

    Reader reader{bytes, 0, payload_size};
    for (const std::uint8_t expected : kAssetMagic)
    {
        if (reader.u8("magic") != expected)
        {
            throw std::runtime_error("invalid NSDF magic: " + path);
        }
    }
    auto data = std::make_shared<AssetData>();
    data->info.format_major = reader.u32("format major");
    data->info.format_minor = reader.u32("format minor");
    if (data->info.format_major != 1 || data->info.format_minor > 3)
    {
        throw std::runtime_error("unsupported NSDF major version");
    }
    data->info.representation = static_cast<Representation>(reader.u32("representation"));
    data->info.reconstruction = static_cast<Reconstruction>(reader.u32("reconstruction"));
    if (data->info.format_minor >= 1)
        data->info.influence_filter = static_cast<InfluenceFilter>(
            reader.u32("influence filter"));
    if (data->info.format_minor >= 2)
    {
        data->info.composition = static_cast<CompositionPolicy>(
            reader.u32("composition policy"));
        data->info.component_count = reader.u32("component count");
        data->info.active_component_count = reader.u32("active component count");
    }
    if (data->info.format_minor >= 3)
    {
        data->info.build_backend = static_cast<ComputeBackend>(
            reader.u32("build backend"));
        data->info.worker_threads = reader.u32("worker threads");
    }
    if ((data->info.build_backend != ComputeBackend::CpuScalar &&
         data->info.build_backend != ComputeBackend::CpuParallel &&
         data->info.build_backend != ComputeBackend::CudaExperimental) ||
        data->info.worker_threads == 0)
    {
        throw std::runtime_error("NSDF build backend metadata is invalid");
    }
    data->info.domain = reader.box("domain");
    for (std::uint32_t& value : data->info.resolution) value = reader.u32("resolution");
    data->info.maximum_depth = reader.u32("maximum depth");
    data->info.requested_error_tolerance = reader.f64("requested tolerance");
    data->info.measured_maximum_error = reader.f64("measured error");
    data->info.has_measured_error = reader.u32("measured error flag") != 0;
    const std::uint64_t logical_node_count = reader.u64("logical node count");
    const std::uint64_t stored_node_count = reader.u64("stored node count");
    const std::uint64_t coefficient_count = reader.u64("coefficient count");
    const std::uint64_t logical_triangle_count = reader.u64("logical triangle count");
    const std::uint64_t stored_triangle_count = reader.u64("stored triangle count");
    const std::uint64_t vertex_count = reader.u64("vertex count");
    const std::uint64_t index_count = reader.u64("candidate index count");
    require_count(logical_node_count, "logical node");
    require_count(stored_node_count, "stored node");
    require_count(coefficient_count, "coefficient");
    require_count(logical_triangle_count, "logical triangle");
    require_count(stored_triangle_count, "stored triangle");
    require_count(vertex_count, "vertex");
    require_count(index_count, "candidate index");
    std::uint64_t required_payload = 0;
    add_serialized_size(required_payload,
        checked_serialized_size(vertex_count, 24, "vertex"), "vertex");
    add_serialized_size(required_payload,
        checked_serialized_size(stored_triangle_count, 92, "triangle"), "triangle");
    add_serialized_size(required_payload,
        checked_serialized_size(stored_node_count, 104, "node"), "node");
    add_serialized_size(required_payload,
        checked_serialized_size(index_count, 4, "candidate index"), "candidate index");
    add_serialized_size(required_payload,
        checked_serialized_size(coefficient_count, 8, "coefficient"), "coefficient");
    if (required_payload != static_cast<std::uint64_t>(payload_size - reader.offset))
    {
        throw std::runtime_error("NSDF payload size does not match its count table");
    }
    data->mesh.source_path = path;
    data->mesh.vertices.resize(static_cast<std::size_t>(vertex_count));
    data->mesh.triangles.resize(static_cast<std::size_t>(stored_triangle_count));
    data->nodes.resize(static_cast<std::size_t>(stored_node_count));
    data->coefficients.resize(static_cast<std::size_t>(coefficient_count));
    data->triangle_indices.resize(static_cast<std::size_t>(index_count));

    for (Vec3& vertex : data->mesh.vertices) vertex = reader.vec3("vertex");
    for (Triangle& triangle : data->mesh.triangles)
    {
        for (std::uint32_t& index : triangle.vertex)
        {
            index = reader.u32("triangle vertex");
            if (index >= vertex_count) throw std::runtime_error("NSDF triangle index out of range");
        }
        triangle.face_id = reader.u32("face id");
        triangle.has_corner_normals = reader.u32("normal flag") != 0;
        for (Vec3& normal : triangle.corner_normal) normal = reader.vec3("corner normal");
    }
    for (Node& node : data->nodes)
    {
        node.box = reader.box("node box");
        for (std::int32_t& child : node.child) child = reader.i32("child index");
        node.data_offset = reader.u64("node data offset");
        node.data_count = reader.u32("node data count");
        node.depth = reader.u32("node depth");
        node.measured_error = reader.f64("node error");
        if (node.data_offset > std::numeric_limits<std::uint64_t>::max() - node.data_count)
        {
            throw std::runtime_error("NSDF node data range overflow");
        }
        const std::uint64_t end_offset = node.data_offset + node.data_count;
        const std::uint64_t available = data->info.representation ==
                Representation::ExactInfluenceOctree
            ? index_count : coefficient_count;
        if (node.leaf() && end_offset > available)
        {
            throw std::runtime_error("NSDF leaf data range is invalid");
        }
        for (const std::int32_t child : node.child)
        {
            if (child < -1 || (child >= 0 && static_cast<std::uint64_t>(child) >= stored_node_count))
            {
                throw std::runtime_error("NSDF child index is invalid");
            }
        }
    }
    for (std::uint32_t& index : data->triangle_indices)
    {
        index = reader.u32("candidate triangle");
        if (index >= stored_triangle_count) throw std::runtime_error("NSDF candidate index out of range");
    }
    for (double& coefficient : data->coefficients)
    {
        coefficient = reader.f64("coefficient");
        if (!std::isfinite(coefficient)) throw std::runtime_error("NSDF coefficient is not finite");
    }
    if (reader.offset != payload_size)
    {
        throw std::runtime_error("NSDF asset has unconsumed payload bytes");
    }
    data->info.node_count = logical_node_count;
    data->info.coefficient_count = coefficient_count;
    data->info.triangle_count = logical_triangle_count;
    data->info.candidate_index_count = index_count;
    validate_loaded_layout(*data);
    if (!data->mesh.triangles.empty())
    {
        data->exact_surface = std::make_shared<ExactSurface>(
            data->mesh, data->info.composition);
        data->mesh = data->exact_surface->mesh();
    }
    return data;
}

} // namespace detail
} // namespace nexsdf
