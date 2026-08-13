#include "nexsdf/nexsdf.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace
{

using Clock = std::chrono::steady_clock;
using nexsdf::Vec3;

struct Statistics
{
    double rms{0.0};
    double p95{0.0};
    double maximum{0.0};
};

struct Options
{
    nexsdf::BuildOptions build{};
    std::size_t field_samples{4096};
    std::size_t surface_samples{4096};
    std::size_t query_repetitions{10};
    std::uint64_t seed{0x4e65785364664d30ULL};
    bool append{false};
};

void usage()
{
    std::cerr
        << "usage: nexsdfvalidate INPUT.(obj|nsm) OUTPUT.tsv [options]\n"
        << "  --representation grid|exact-octree|adaptive-octree\n"
        << "  --reconstruction trilinear|tricubic|gradient|exact\n"
        << "  --resolution N  --max-depth N  --start-depth N\n"
        << "  --max-triangles N  --tolerance X  --padding X\n"
        << "  --field-samples N  --surface-samples N\n"
        << "  --query-repetitions N  --seed N\n"
        << "  --influence aabb|gjk|frank-wolfe\n"
        << "  --composition separate|union|parity\n"
        << "  --append  append a result row to an existing matching TSV\n";
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string next(int& index, int count, char** values, const char* option)
{
    if (++index >= count)
        throw std::invalid_argument(std::string("missing value for ") + option);
    return values[index];
}

std::uint32_t u32(const std::string& value, const char* option)
{
    const auto parsed = std::stoull(value);
    if (parsed > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument(std::string("value is too large for ") + option);
    return static_cast<std::uint32_t>(parsed);
}

std::size_t positive_size(const std::string& value, const char* option)
{
    const auto parsed = std::stoull(value);
    if (parsed == 0 || parsed > std::numeric_limits<std::size_t>::max())
        throw std::invalid_argument(std::string("value must be positive for ") + option);
    return static_cast<std::size_t>(parsed);
}

nexsdf::Representation parse_representation(const std::string& value)
{
    if (value == "grid") return nexsdf::Representation::DenseGrid;
    if (value == "exact-octree") return nexsdf::Representation::ExactInfluenceOctree;
    if (value == "adaptive-octree") return nexsdf::Representation::AdaptivePiecewiseOctree;
    throw std::invalid_argument("unknown representation: " + value);
}

nexsdf::Reconstruction parse_reconstruction(const std::string& value)
{
    if (value == "trilinear") return nexsdf::Reconstruction::Trilinear;
    if (value == "tricubic") return nexsdf::Reconstruction::TricubicHermite;
    if (value == "gradient") return nexsdf::Reconstruction::GradientTaylor;
    if (value == "exact") return nexsdf::Reconstruction::Exact;
    throw std::invalid_argument("unknown reconstruction: " + value);
}

nexsdf::InfluenceFilter parse_influence_filter(const std::string& value)
{
    if (value == "aabb") return nexsdf::InfluenceFilter::AabbLipschitz;
    if (value == "gjk") return nexsdf::InfluenceFilter::PaperGjk;
    if (value == "frank-wolfe") return nexsdf::InfluenceFilter::PaperFrankWolfe;
    throw std::invalid_argument("unknown influence filter: " + value);
}

nexsdf::CompositionPolicy parse_composition(const std::string& value)
{
    if (value == "separate") return nexsdf::CompositionPolicy::SeparateAssets;
    if (value == "union") return nexsdf::CompositionPolicy::SolidUnion;
    if (value == "parity") return nexsdf::CompositionPolicy::NestedParity;
    throw std::invalid_argument("unknown composition policy: " + value);
}

const char* composition_name(nexsdf::CompositionPolicy value)
{
    switch (value)
    {
    case nexsdf::CompositionPolicy::SeparateAssets: return "separate";
    case nexsdf::CompositionPolicy::SolidUnion: return "union";
    case nexsdf::CompositionPolicy::NestedParity: return "parity";
    }
    return "unknown";
}

const char* influence_filter_name(nexsdf::InfluenceFilter value)
{
    switch (value)
    {
    case nexsdf::InfluenceFilter::AabbLipschitz: return "aabb";
    case nexsdf::InfluenceFilter::PaperGjk: return "gjk";
    case nexsdf::InfluenceFilter::PaperFrankWolfe: return "frank-wolfe";
    }
    return "unknown";
}

const char* representation_name(nexsdf::Representation value)
{
    switch (value)
    {
    case nexsdf::Representation::DenseGrid: return "grid";
    case nexsdf::Representation::ExactInfluenceOctree: return "exact-octree";
    case nexsdf::Representation::AdaptivePiecewiseOctree: return "adaptive-octree";
    }
    return "unknown";
}

const char* reconstruction_name(nexsdf::Reconstruction value)
{
    switch (value)
    {
    case nexsdf::Reconstruction::Exact: return "exact";
    case nexsdf::Reconstruction::Trilinear: return "trilinear";
    case nexsdf::Reconstruction::TricubicHermite: return "tricubic";
    case nexsdf::Reconstruction::GradientTaylor: return "gradient";
    }
    return "unknown";
}

double halton(std::uint64_t index, std::uint32_t base)
{
    double result = 0.0;
    double factor = 1.0 / static_cast<double>(base);
    while (index != 0)
    {
        result += factor * static_cast<double>(index % base);
        index /= base;
        factor /= static_cast<double>(base);
    }
    return result;
}

std::vector<Vec3> domain_points(
    const nexsdf::Aabb& domain,
    std::size_t count,
    std::uint64_t seed)
{
    const Vec3 extent = domain.extent();
    std::vector<Vec3> points(count);
    const std::uint64_t offset = 1 + (seed % 104729ULL);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::uint64_t index = offset + static_cast<std::uint64_t>(i);
        points[i] = {
            domain.minimum.x + extent.x * halton(index, 2),
            domain.minimum.y + extent.y * halton(index, 3),
            domain.minimum.z + extent.z * halton(index, 5)};
    }
    return points;
}

std::vector<Vec3> surface_points(
    const nexsdf::SurfaceMesh& mesh,
    std::size_t count,
    std::uint64_t seed)
{
    std::vector<double> cumulative;
    cumulative.reserve(mesh.triangles.size());
    double total_area = 0.0;
    for (const auto& triangle : mesh.triangles)
    {
        const Vec3 a = mesh.vertices[triangle.vertex[0]];
        const Vec3 b = mesh.vertices[triangle.vertex[1]];
        const Vec3 c = mesh.vertices[triangle.vertex[2]];
        total_area += 0.5 * nexsdf::norm(nexsdf::cross(b - a, c - a));
        cumulative.push_back(total_area);
    }
    if (!(total_area > 0.0))
        throw std::runtime_error("surface sampling requires positive mesh area");

    std::vector<Vec3> points;
    points.reserve(count);
    const std::uint64_t offset = 1 + ((seed >> 17U) % 104729ULL);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::uint64_t index = offset + static_cast<std::uint64_t>(i);
        const double target = halton(index, 2) * total_area;
        const auto it = std::lower_bound(cumulative.begin(), cumulative.end(), target);
        const std::size_t triangle_index = std::min<std::size_t>(
            static_cast<std::size_t>(it - cumulative.begin()), mesh.triangles.size() - 1);
        const auto& triangle = mesh.triangles[triangle_index];
        const Vec3 a = mesh.vertices[triangle.vertex[0]];
        const Vec3 b = mesh.vertices[triangle.vertex[1]];
        const Vec3 c = mesh.vertices[triangle.vertex[2]];
        const double root_u = std::sqrt(halton(index, 3));
        const double v = halton(index, 5);
        const double wa = 1.0 - root_u;
        const double wb = root_u * (1.0 - v);
        const double wc = root_u * v;
        points.push_back(a * wa + b * wb + c * wc);
    }
    return points;
}

Statistics statistics(std::vector<double> values)
{
    if (values.empty()) return {};
    double squared_sum = 0.0;
    for (const double value : values) squared_sum += value * value;
    std::sort(values.begin(), values.end());
    const std::size_t p95_index = std::min<std::size_t>(
        values.size() - 1,
        static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(values.size()))) - 1);
    return {
        std::sqrt(squared_sum / static_cast<double>(values.size())),
        values[p95_index],
        values.back()};
}

std::uint64_t fnv1a64(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open input for hashing: " + path);
    std::uint64_t hash = 14695981039346656037ULL;
    std::array<char, 64 * 1024> buffer{};
    while (stream)
    {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = stream.gcount();
        for (std::streamsize i = 0; i < read; ++i)
        {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

std::uint64_t peak_working_set_bytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters)) == 0)
        return 0;
    return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
#endif
}

std::string compiler_name()
{
#if defined(_MSC_VER)
    return "msvc-" + std::to_string(_MSC_VER);
#elif defined(__clang__)
    return "clang-" + std::string(__clang_version__);
#elif defined(__GNUC__)
    return "gcc-" + std::string(__VERSION__);
#else
    return "unknown";
#endif
}

std::string build_configuration()
{
#if defined(NDEBUG)
    return "release";
#else
    return "debug";
#endif
}

std::string hex_u64(std::uint64_t value)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}

void write_header(std::ostream& stream)
{
    stream
        << "schema\tmodel\tmodel_hash_fnv1a64\trepresentation\treconstruction"
        << "\tresolution_x\tresolution_y\tresolution_z\tfield_samples\tsurface_samples"
        << "\tseed\tcompiler\tconfiguration\tbuild_backend\tworker_threads"
        << "\tinfluence_filter\tcandidate_index_count\tcomposition"
        << "\tcomponent_count\tactive_component_count"
        << "\tbuild_seconds\tquery_seconds\tqueries_per_second\tasset_bytes"
        << "\tprocess_peak_working_set_bytes"
        << "\tdistance_rms\tdistance_p95\tdistance_max"
        << "\tgradient_rms\tgradient_p95\tgradient_max"
        << "\tnormal_angle_rms_deg\tnormal_angle_p95_deg\tnormal_angle_max_deg"
        << "\teikonal_rms\teikonal_p95\teikonal_max"
        << "\tmesh_to_field_rms\tmesh_to_field_p95\tmesh_to_field_max"
        << "\tfield_to_mesh_rms\tfield_to_mesh_p95\tfield_to_mesh_max"
        << "\tsymmetric_surface_max\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--help")
    {
        usage();
        return 0;
    }
    if (argc < 3)
    {
        usage();
        return 2;
    }

    try
    {
        const std::string input_path = argv[1];
        const std::string output_path = argv[2];
        Options options;
        for (int i = 3; i < argc; ++i)
        {
            const std::string option = argv[i];
            if (option == "--representation") options.build.representation = parse_representation(next(i, argc, argv, option.c_str()));
            else if (option == "--reconstruction") options.build.reconstruction = parse_reconstruction(next(i, argc, argv, option.c_str()));
            else if (option == "--resolution")
            {
                const auto value = u32(next(i, argc, argv, option.c_str()), option.c_str());
                options.build.resolution = {value, value, value};
            }
            else if (option == "--max-depth") options.build.maximum_depth = u32(next(i, argc, argv, option.c_str()), option.c_str());
            else if (option == "--start-depth") options.build.start_depth = u32(next(i, argc, argv, option.c_str()), option.c_str());
            else if (option == "--max-triangles") options.build.maximum_triangles_per_leaf = u32(next(i, argc, argv, option.c_str()), option.c_str());
            else if (option == "--tolerance") options.build.error_tolerance = std::stod(next(i, argc, argv, option.c_str()));
            else if (option == "--padding") options.build.relative_padding = std::stod(next(i, argc, argv, option.c_str()));
            else if (option == "--field-samples") options.field_samples = positive_size(next(i, argc, argv, option.c_str()), option.c_str());
            else if (option == "--surface-samples") options.surface_samples = positive_size(next(i, argc, argv, option.c_str()), option.c_str());
            else if (option == "--query-repetitions") options.query_repetitions = positive_size(next(i, argc, argv, option.c_str()), option.c_str());
            else if (option == "--seed") options.seed = std::stoull(next(i, argc, argv, option.c_str()), nullptr, 0);
            else if (option == "--influence") options.build.influence_filter = parse_influence_filter(next(i, argc, argv, option.c_str()));
            else if (option == "--composition") options.build.composition = parse_composition(next(i, argc, argv, option.c_str()));
            else if (option == "--append") options.append = true;
            else if (option == "--help") { usage(); return 0; }
            else throw std::invalid_argument("unknown option: " + option);
        }

        nexsdf::SurfaceMesh mesh = nexsdf::load_surface_mesh(input_path);

        nexsdf::ExactSurface exact(mesh, options.build.composition);
        const auto start = Clock::now();
        nexsdf::Asset asset = nexsdf::build(mesh, options.build);
        const auto build_end = Clock::now();
        const double build_seconds = std::chrono::duration<double>(build_end - start).count();

        const std::filesystem::path temporary_asset =
            std::filesystem::path(output_path).string() + ".temporary.nsdf";
        asset.save(temporary_asset.string());
        const std::uint64_t asset_bytes = std::filesystem::file_size(temporary_asset);
        std::error_code remove_error;
        std::filesystem::remove(temporary_asset, remove_error);

        const auto points = domain_points(asset.info().domain, options.field_samples, options.seed);
        std::vector<nexsdf::QueryResult> approximate(points.size());
        std::vector<nexsdf::QueryResult> reference(points.size());
        asset.query_batch(points.data(), points.size(), approximate.data());
        for (std::size_t i = 0; i < points.size(); ++i) reference[i] = exact.query(points[i]);

        std::vector<double> distance_errors;
        std::vector<double> gradient_errors;
        std::vector<double> normal_errors;
        std::vector<double> eikonal_errors;
        distance_errors.reserve(points.size());
        gradient_errors.reserve(points.size());
        normal_errors.reserve(points.size());
        eikonal_errors.reserve(points.size());
        constexpr double radians_to_degrees = 57.2957795130823208768;
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            if (!approximate[i].valid || !reference[i].valid)
                throw std::runtime_error("a validation field query was invalid");
            distance_errors.push_back(std::abs(approximate[i].phi - reference[i].phi));
            gradient_errors.push_back(nexsdf::norm(approximate[i].raw_gradient - reference[i].raw_gradient));
            const double cosine = std::clamp(
                nexsdf::dot(approximate[i].unit_normal, reference[i].unit_normal), -1.0, 1.0);
            normal_errors.push_back(std::acos(cosine) * radians_to_degrees);
            eikonal_errors.push_back(std::abs(nexsdf::norm(approximate[i].raw_gradient) - 1.0));
        }

        nexsdf::SurfaceMesh active_mesh = exact.mesh();
        active_mesh.triangles.clear();
        for (const std::uint32_t triangle : exact.active_triangles())
            active_mesh.triangles.push_back(exact.mesh().triangles[triangle]);
        const auto samples = surface_points(
            active_mesh, options.surface_samples, options.seed);
        std::vector<double> mesh_to_field;
        std::vector<double> field_to_mesh;
        mesh_to_field.reserve(samples.size());
        field_to_mesh.reserve(samples.size());
        for (const Vec3 sample : samples)
        {
            const auto on_mesh = asset.query(sample);
            if (!on_mesh.valid) throw std::runtime_error("a mesh-to-field query was invalid");
            mesh_to_field.push_back(std::abs(on_mesh.phi));

            Vec3 projected = sample;
            bool valid_projection = true;
            for (int iteration = 0; iteration < 12; ++iteration)
            {
                const auto result = asset.query(projected);
                const double gradient_squared = nexsdf::squared_norm(result.raw_gradient);
                if (!result.valid || !(gradient_squared > 1.0e-24))
                {
                    valid_projection = false;
                    break;
                }
                projected = projected - result.raw_gradient * (result.phi / gradient_squared);
                if (std::abs(result.phi) <= 1.0e-12) break;
            }
            const auto exact_projected = exact.query(projected);
            if (valid_projection && exact_projected.valid)
                field_to_mesh.push_back(std::abs(exact_projected.phi));
        }
        if (field_to_mesh.empty()) throw std::runtime_error("all field-to-mesh projections failed");

        for (std::size_t warmup = 0; warmup < 2; ++warmup)
            asset.query_batch(points.data(), points.size(), approximate.data());
        const auto query_start = Clock::now();
        for (std::size_t repeat = 0; repeat < options.query_repetitions; ++repeat)
            asset.query_batch(points.data(), points.size(), approximate.data());
        const auto query_end = Clock::now();
        const double query_seconds = std::chrono::duration<double>(query_end - query_start).count();
        const double total_queries = static_cast<double>(points.size() * options.query_repetitions);

        const Statistics distance = statistics(std::move(distance_errors));
        const Statistics gradient = statistics(std::move(gradient_errors));
        const Statistics normal = statistics(std::move(normal_errors));
        const Statistics eikonal = statistics(std::move(eikonal_errors));
        const Statistics mesh_field = statistics(std::move(mesh_to_field));
        const Statistics field_mesh = statistics(std::move(field_to_mesh));

        const std::filesystem::path destination(output_path);
        if (!destination.parent_path().empty())
            std::filesystem::create_directories(destination.parent_path());
        const bool write_existing = options.append && std::filesystem::exists(destination) &&
            std::filesystem::file_size(destination) != 0;
        std::ofstream output(output_path, write_existing ? std::ios::app : std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open validation output: " + output_path);
        if (!write_existing) write_header(output);
        output << std::setprecision(17)
            << "nexsdf-validation-v1\t" << std::filesystem::path(input_path).generic_string()
            << '\t' << hex_u64(fnv1a64(input_path))
            << '\t' << representation_name(options.build.representation)
            << '\t' << reconstruction_name(options.build.reconstruction)
            << '\t' << options.build.resolution[0]
            << '\t' << options.build.resolution[1]
            << '\t' << options.build.resolution[2]
            << '\t' << options.field_samples
            << '\t' << options.surface_samples
            << '\t' << options.seed
            << '\t' << compiler_name()
            << '\t' << build_configuration()
            << "\tscalar\t1"
            << '\t' << influence_filter_name(asset.info().influence_filter)
            << '\t' << asset.info().candidate_index_count
            << '\t' << composition_name(asset.info().composition)
            << '\t' << asset.info().component_count
            << '\t' << asset.info().active_component_count
            << '\t' << build_seconds
            << '\t' << query_seconds
            << '\t' << (total_queries / query_seconds)
            << '\t' << asset_bytes
            << '\t' << peak_working_set_bytes()
            << '\t' << distance.rms << '\t' << distance.p95 << '\t' << distance.maximum
            << '\t' << gradient.rms << '\t' << gradient.p95 << '\t' << gradient.maximum
            << '\t' << normal.rms << '\t' << normal.p95 << '\t' << normal.maximum
            << '\t' << eikonal.rms << '\t' << eikonal.p95 << '\t' << eikonal.maximum
            << '\t' << mesh_field.rms << '\t' << mesh_field.p95 << '\t' << mesh_field.maximum
            << '\t' << field_mesh.rms << '\t' << field_mesh.p95 << '\t' << field_mesh.maximum
            << '\t' << std::max(mesh_field.maximum, field_mesh.maximum) << '\n';
        if (!output) throw std::runtime_error("failed to write validation output: " + output_path);

        std::cout << "wrote " << output_path
                  << " distance_max=" << distance.maximum
                  << " symmetric_surface_max=" << std::max(mesh_field.maximum, field_mesh.maximum)
                  << " queries_per_second=" << (total_queries / query_seconds) << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "nexsdfvalidate: " << error.what() << '\n';
        return 1;
    }
}
