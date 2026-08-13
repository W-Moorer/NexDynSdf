#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace nexsdf
{
namespace
{

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kTiny = 1.0e-14;

bool finite(Vec3 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

std::uint64_t hash_combine(std::uint64_t seed, std::uint64_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
    return seed;
}

struct ClosestPoint
{
    Vec3 point{};
    std::array<double, 3> barycentric{};
    Feature feature{Feature::Unknown};
};

ClosestPoint closest_point_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c)
{
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = p - a;
    const double d1 = dot(ab, ap);
    const double d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0)
    {
        return {a, {1.0, 0.0, 0.0}, Feature::Vertex};
    }

    const Vec3 bp = p - b;
    const double d3 = dot(ab, bp);
    const double d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3)
    {
        return {b, {0.0, 1.0, 0.0}, Feature::Vertex};
    }

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
    {
        const double v = d1 / (d1 - d3);
        return {a + ab * v, {1.0 - v, v, 0.0}, Feature::Edge};
    }

    const Vec3 cp = p - c;
    const double d5 = dot(ab, cp);
    const double d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6)
    {
        return {c, {0.0, 0.0, 1.0}, Feature::Vertex};
    }

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
    {
        const double w = d2 / (d2 - d6);
        return {a + ac * w, {1.0 - w, 0.0, w}, Feature::Edge};
    }

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
    {
        const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {b + (c - b) * w, {0.0, 1.0 - w, w}, Feature::Edge};
    }

    const double denominator = 1.0 / (va + vb + vc);
    const double v = vb * denominator;
    const double w = vc * denominator;
    return {a + ab * v + ac * w, {1.0 - v - w, v, w}, Feature::Face};
}

Vec3 feature_normal(
    const SurfaceMesh& mesh,
    const Triangle& triangle,
    const ClosestPoint& closest)
{
    const Vec3 a = mesh.vertices[triangle.vertex[0]];
    const Vec3 b = mesh.vertices[triangle.vertex[1]];
    const Vec3 c = mesh.vertices[triangle.vertex[2]];
    Vec3 normal = normalized(cross(b - a, c - a));
    if (triangle.has_corner_normals)
    {
        Vec3 interpolated{};
        for (std::size_t i = 0; i < 3; ++i)
        {
            interpolated = interpolated +
                triangle.corner_normal[i] * closest.barycentric[i];
        }
        if (norm(interpolated) > kTiny)
        {
            normal = normalized(interpolated);
        }
    }
    return normal;
}

std::vector<detail::TrianglePseudoNormals> build_pseudo_normals(
    const SurfaceMesh& mesh)
{
    std::vector<detail::TrianglePseudoNormals> result(mesh.triangles.size());
    std::vector<Vec3> vertex_sum(mesh.vertices.size());
    using Edge = std::pair<std::uint32_t, std::uint32_t>;
    std::map<Edge, std::vector<std::pair<std::size_t, std::size_t>>> edge_uses;

    for (std::size_t triangle_index = 0;
         triangle_index < mesh.triangles.size(); ++triangle_index)
    {
        const Triangle& triangle = mesh.triangles[triangle_index];
        const Vec3 a = mesh.vertices[triangle.vertex[0]];
        const Vec3 b = mesh.vertices[triangle.vertex[1]];
        const Vec3 c = mesh.vertices[triangle.vertex[2]];
        const Vec3 face = normalized(cross(b - a, c - a));
        result[triangle_index].face = face;
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const std::uint32_t vertex = triangle.vertex[corner];
            const Vec3 origin = mesh.vertices[vertex];
            const Vec3 first = normalized(
                mesh.vertices[triangle.vertex[(corner + 1) % 3]] - origin);
            const Vec3 second = normalized(
                mesh.vertices[triangle.vertex[(corner + 2) % 3]] - origin);
            const double angle = std::acos(std::max(-1.0, std::min(1.0, dot(first, second))));
            vertex_sum[vertex] = vertex_sum[vertex] + angle * face;

            const std::uint32_t next = triangle.vertex[(corner + 1) % 3];
            const Edge edge{std::min(vertex, next), std::max(vertex, next)};
            edge_uses[edge].push_back({triangle_index, corner});
        }
    }

    for (const auto& item : edge_uses)
    {
        Vec3 sum{};
        for (const auto use : item.second)
        {
            sum = sum + result[use.first].face;
        }
        const Vec3 normal = normalized(sum);
        for (const auto use : item.second)
        {
            result[use.first].edge[use.second] = normal;
        }
    }
    for (std::size_t triangle_index = 0;
         triangle_index < mesh.triangles.size(); ++triangle_index)
    {
        const Triangle& triangle = mesh.triangles[triangle_index];
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            result[triangle_index].vertex[corner] =
                normalized(vertex_sum[triangle.vertex[corner]]);
        }
    }
    return result;
}

Vec3 pseudo_normal_for_feature(
    const detail::TrianglePseudoNormals& pseudo,
    const ClosestPoint& closest)
{
    if (closest.feature == Feature::Face)
    {
        return pseudo.face;
    }
    if (closest.feature == Feature::Vertex)
    {
        std::size_t vertex = 0;
        if (closest.barycentric[1] > closest.barycentric[vertex]) vertex = 1;
        if (closest.barycentric[2] > closest.barycentric[vertex]) vertex = 2;
        return pseudo.vertex[vertex];
    }
    if (closest.feature == Feature::Edge)
    {
        std::size_t zero = 0;
        if (closest.barycentric[1] < closest.barycentric[zero]) zero = 1;
        if (closest.barycentric[2] < closest.barycentric[zero]) zero = 2;
        return pseudo.edge[(zero + 1) % 3];
    }
    return pseudo.face;
}

void orient_outward(SurfaceMesh& mesh)
{
    using Edge = std::pair<std::uint32_t, std::uint32_t>;
    std::map<Edge, std::vector<std::size_t>> edge_triangles;
    for (std::size_t triangle_index = 0;
         triangle_index < mesh.triangles.size(); ++triangle_index)
    {
        const Triangle& triangle = mesh.triangles[triangle_index];
        for (std::size_t edge = 0; edge < 3; ++edge)
        {
            const std::uint32_t a = triangle.vertex[edge];
            const std::uint32_t b = triangle.vertex[(edge + 1) % 3];
            edge_triangles[{std::min(a, b), std::max(a, b)}].push_back(triangle_index);
        }
    }

    std::vector<std::vector<std::size_t>> adjacency(mesh.triangles.size());
    for (const auto& item : edge_triangles)
    {
        if (item.second.size() == 2)
        {
            adjacency[item.second[0]].push_back(item.second[1]);
            adjacency[item.second[1]].push_back(item.second[0]);
        }
    }
    std::vector<bool> visited(mesh.triangles.size(), false);
    for (std::size_t seed = 0; seed < mesh.triangles.size(); ++seed)
    {
        if (visited[seed]) continue;
        std::vector<std::size_t> component;
        std::vector<std::size_t> stack{seed};
        visited[seed] = true;
        double six_volume = 0.0;
        while (!stack.empty())
        {
            const std::size_t triangle_index = stack.back();
            stack.pop_back();
            component.push_back(triangle_index);
            const Triangle& triangle = mesh.triangles[triangle_index];
            const Vec3 a = mesh.vertices[triangle.vertex[0]];
            const Vec3 b = mesh.vertices[triangle.vertex[1]];
            const Vec3 c = mesh.vertices[triangle.vertex[2]];
            six_volume += dot(a, cross(b, c));
            for (const std::size_t neighbor : adjacency[triangle_index])
            {
                if (!visited[neighbor])
                {
                    visited[neighbor] = true;
                    stack.push_back(neighbor);
                }
            }
        }
        if (six_volume < 0.0)
        {
            for (const std::size_t triangle_index : component)
            {
                Triangle& triangle = mesh.triangles[triangle_index];
                std::swap(triangle.vertex[1], triangle.vertex[2]);
                std::swap(triangle.corner_normal[1], triangle.corner_normal[2]);
            }
        }
    }

    for (Triangle& triangle : mesh.triangles)
    {
        if (!triangle.has_corner_normals)
        {
            continue;
        }
        const Vec3 a = mesh.vertices[triangle.vertex[0]];
        const Vec3 b = mesh.vertices[triangle.vertex[1]];
        const Vec3 c = mesh.vertices[triangle.vertex[2]];
        const Vec3 face_normal = normalized(cross(b - a, c - a));
        for (Vec3& normal : triangle.corner_normal)
        {
            if (dot(normal, face_normal) < 0.0)
            {
                normal = -1.0 * normal;
            }
        }
    }
}

void weld_identical_vertices(SurfaceMesh& mesh)
{
    using Key = std::tuple<double, double, double>;
    std::map<Key, std::uint32_t> unique;
    std::vector<Vec3> vertices;
    vertices.reserve(mesh.vertices.size());
    std::vector<std::uint32_t> remap(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        const Vec3 vertex = mesh.vertices[i];
        const Key key{vertex.x, vertex.y, vertex.z};
        const auto found = unique.find(key);
        if (found != unique.end())
        {
            remap[i] = found->second;
            continue;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(vertices.size());
        unique.emplace(key, index);
        vertices.push_back(vertex);
        remap[i] = index;
    }
    for (Triangle& triangle : mesh.triangles)
    {
        for (std::uint32_t& index : triangle.vertex)
        {
            if (index < remap.size())
            {
                index = remap[index];
            }
        }
    }
    mesh.vertices = std::move(vertices);
}

} // namespace

double& Vec3::operator[](std::size_t i) noexcept
{
    return i == 0 ? x : (i == 1 ? y : z);
}

const double& Vec3::operator[](std::size_t i) const noexcept
{
    return i == 0 ? x : (i == 1 ? y : z);
}

Vec3 operator+(Vec3 a, Vec3 b) noexcept
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(Vec3 a, Vec3 b) noexcept
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(Vec3 a, double s) noexcept
{
    return {a.x * s, a.y * s, a.z * s};
}

Vec3 operator*(double s, Vec3 a) noexcept
{
    return a * s;
}

Vec3 operator/(Vec3 a, double s) noexcept
{
    return {a.x / s, a.y / s, a.z / s};
}

double dot(Vec3 a, Vec3 b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) noexcept
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

double squared_norm(Vec3 a) noexcept
{
    return dot(a, a);
}

double norm(Vec3 a) noexcept
{
    return std::sqrt(squared_norm(a));
}

Vec3 normalized(Vec3 a) noexcept
{
    const double length = norm(a);
    return length > kTiny ? a / length : Vec3{1.0, 0.0, 0.0};
}

bool Aabb::contains(Vec3 p) const noexcept
{
    return p.x >= minimum.x && p.x <= maximum.x &&
           p.y >= minimum.y && p.y <= maximum.y &&
           p.z >= minimum.z && p.z <= maximum.z;
}

Vec3 Aabb::extent() const noexcept
{
    return maximum - minimum;
}

bool MeshValidation::valid_for_signed_distance() const noexcept
{
    return finite && non_degenerate && closed_two_manifold &&
        consistently_oriented && connected_components == 1;
}

MeshValidation validate_mesh(const SurfaceMesh& mesh)
{
    MeshValidation result;
    result.finite = !mesh.vertices.empty() && !mesh.triangles.empty();
    for (const Vec3 vertex : mesh.vertices)
    {
        result.finite = result.finite && finite(vertex);
    }

    result.non_degenerate = result.finite;
    using Edge = std::pair<std::uint32_t, std::uint32_t>;
    struct EdgeUse
    {
        std::size_t count{0};
        int orientation_sum{0};
    };
    std::map<Edge, EdgeUse> edges;
    std::vector<std::uint32_t> parent(mesh.vertices.size());
    for (std::size_t i = 0; i < parent.size(); ++i)
        parent[i] = static_cast<std::uint32_t>(i);
    const auto find_root = [&parent](std::uint32_t vertex) {
        std::uint32_t root = vertex;
        while (parent[root] != root) root = parent[root];
        while (parent[vertex] != vertex)
        {
            const std::uint32_t next = parent[vertex];
            parent[vertex] = root;
            vertex = next;
        }
        return root;
    };
    const auto unite = [&parent, &find_root](std::uint32_t a, std::uint32_t b) {
        const std::uint32_t root_a = find_root(a);
        const std::uint32_t root_b = find_root(b);
        if (root_a != root_b) parent[root_b] = root_a;
    };
    std::vector<bool> used_vertex(mesh.vertices.size(), false);
    for (const Triangle& triangle : mesh.triangles)
    {
        for (std::uint32_t index : triangle.vertex)
        {
            if (index >= mesh.vertices.size())
            {
                result.non_degenerate = false;
                result.message = "triangle index is out of range";
                return result;
            }
        }
        const Vec3 a = mesh.vertices[triangle.vertex[0]];
        const Vec3 b = mesh.vertices[triangle.vertex[1]];
        const Vec3 c = mesh.vertices[triangle.vertex[2]];
        for (const std::uint32_t vertex : triangle.vertex) used_vertex[vertex] = true;
        unite(triangle.vertex[0], triangle.vertex[1]);
        unite(triangle.vertex[1], triangle.vertex[2]);
        if (norm(cross(b - a, c - a)) <= kTiny)
        {
            result.non_degenerate = false;
        }
        if (triangle.has_corner_normals)
        {
            for (const Vec3 normal : triangle.corner_normal)
            {
                if (!finite(normal) || norm(normal) <= kTiny)
                {
                    result.finite = false;
                }
            }
        }

        for (std::size_t i = 0; i < 3; ++i)
        {
            const std::uint32_t from = triangle.vertex[i];
            const std::uint32_t to = triangle.vertex[(i + 1) % 3];
            const Edge key{std::min(from, to), std::max(from, to)};
            EdgeUse& use = edges[key];
            ++use.count;
            use.orientation_sum += from < to ? 1 : -1;
        }
    }

    for (const auto& item : edges)
    {
        if (item.second.count == 1)
        {
            ++result.boundary_edges;
        }
        else if (item.second.count != 2)
        {
            ++result.non_manifold_edges;
        }
        if (item.second.count == 2 && item.second.orientation_sum != 0)
        {
            ++result.orientation_mismatches;
        }
    }
    result.closed_two_manifold =
        result.boundary_edges == 0 && result.non_manifold_edges == 0;
    result.consistently_oriented = result.orientation_mismatches == 0;
    std::set<std::uint32_t> components;
    for (std::size_t i = 0; i < used_vertex.size(); ++i)
    {
        if (used_vertex[i]) components.insert(find_root(static_cast<std::uint32_t>(i)));
    }
    result.connected_components = components.size();
    if (!result.finite)
    {
        result.message = "mesh contains non-finite data or is empty";
    }
    else if (!result.non_degenerate)
    {
        result.message = "mesh contains a degenerate triangle";
    }
    else if (!result.closed_two_manifold)
    {
        result.message = "mesh is not a closed two-manifold";
    }
    else if (!result.consistently_oriented)
    {
        result.message = "mesh triangle winding is inconsistent";
    }
    else if (result.connected_components != 1)
    {
        result.message = "mesh must contain exactly one connected surface component";
    }
    else
    {
        result.message = "valid closed oriented triangle mesh";
    }
    return result;
}

struct ExactSurface::Impl
{
    struct BvhNode
    {
        Aabb box{};
        std::uint32_t left{std::numeric_limits<std::uint32_t>::max()};
        std::uint32_t right{std::numeric_limits<std::uint32_t>::max()};
        std::uint32_t begin{0};
        std::uint32_t count{0};

        bool leaf() const noexcept
        {
            return left == std::numeric_limits<std::uint32_t>::max();
        }
    };

    explicit Impl(SurfaceMesh input)
        : mesh(std::move(input))
    {
        const MeshValidation input_validation = validate_mesh(mesh);
        if (!input_validation.finite || !input_validation.non_degenerate)
        {
            throw std::invalid_argument(input_validation.message);
        }
        weld_identical_vertices(mesh);
        validation = validate_mesh(mesh);
        if (!validation.valid_for_signed_distance())
        {
            throw std::invalid_argument(validation.message);
        }
        orient_outward(mesh);
        bounds = detail::mesh_bounds(mesh);
        pseudo_normals = build_pseudo_normals(mesh);
        bvh_triangles.resize(mesh.triangles.size());
        for (std::size_t i = 0; i < bvh_triangles.size(); ++i)
        {
            bvh_triangles[i] = static_cast<std::uint32_t>(i);
        }
        build_bvh(0, bvh_triangles.size());
    }

    std::uint32_t build_bvh(std::size_t begin, std::size_t end)
    {
        const std::uint32_t node_index = static_cast<std::uint32_t>(bvh.size());
        bvh.push_back(BvhNode{});
        Aabb node_box = detail::triangle_bounds(mesh, bvh_triangles[begin]);
        Aabb centroid_box;
        const Vec3 first_center =
            (node_box.minimum + node_box.maximum) * 0.5;
        centroid_box.minimum = first_center;
        centroid_box.maximum = first_center;
        for (std::size_t i = begin + 1; i < end; ++i)
        {
            const Aabb triangle_box = detail::triangle_bounds(mesh, bvh_triangles[i]);
            const Vec3 center = (triangle_box.minimum + triangle_box.maximum) * 0.5;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                node_box.minimum[axis] = std::min(node_box.minimum[axis], triangle_box.minimum[axis]);
                node_box.maximum[axis] = std::max(node_box.maximum[axis], triangle_box.maximum[axis]);
                centroid_box.minimum[axis] = std::min(centroid_box.minimum[axis], center[axis]);
                centroid_box.maximum[axis] = std::max(centroid_box.maximum[axis], center[axis]);
            }
        }
        bvh[node_index].box = node_box;
        const std::size_t count = end - begin;
        if (count <= 8)
        {
            bvh[node_index].begin = static_cast<std::uint32_t>(begin);
            bvh[node_index].count = static_cast<std::uint32_t>(count);
            return node_index;
        }

        const Vec3 centroid_extent = centroid_box.extent();
        std::size_t axis = 0;
        if (centroid_extent.y > centroid_extent.x) axis = 1;
        if (centroid_extent.z > centroid_extent[axis]) axis = 2;
        const std::size_t middle = begin + count / 2;
        std::nth_element(
            bvh_triangles.begin() + static_cast<std::ptrdiff_t>(begin),
            bvh_triangles.begin() + static_cast<std::ptrdiff_t>(middle),
            bvh_triangles.begin() + static_cast<std::ptrdiff_t>(end),
            [this, axis](std::uint32_t a, std::uint32_t b) {
                const Aabb first = detail::triangle_bounds(mesh, a);
                const Aabb second = detail::triangle_bounds(mesh, b);
                return first.minimum[axis] + first.maximum[axis] <
                    second.minimum[axis] + second.maximum[axis];
            });
        const std::uint32_t left = build_bvh(begin, middle);
        const std::uint32_t right = build_bvh(middle, end);
        bvh[node_index].left = left;
        bvh[node_index].right = right;
        return node_index;
    }

    std::uint32_t nearest_triangle(Vec3 point) const
    {
        const Aabb point_box{point, point};
        double best_squared = std::numeric_limits<double>::infinity();
        std::uint32_t best_triangle = 0;
        std::vector<std::uint32_t> stack{0};
        while (!stack.empty())
        {
            const std::uint32_t node_index = stack.back();
            stack.pop_back();
            const BvhNode& node = bvh[node_index];
            const double lower = detail::aabb_distance(point_box, node.box);
            if (lower * lower > best_squared) continue;
            if (node.leaf())
            {
                for (std::uint32_t i = 0; i < node.count; ++i)
                {
                    const std::uint32_t triangle_index =
                        bvh_triangles[node.begin + i];
                    const Triangle& triangle = mesh.triangles[triangle_index];
                    const ClosestPoint closest = closest_point_on_triangle(
                        point,
                        mesh.vertices[triangle.vertex[0]],
                        mesh.vertices[triangle.vertex[1]],
                        mesh.vertices[triangle.vertex[2]]);
                    const double distance_squared = squared_norm(point - closest.point);
                    if (distance_squared < best_squared ||
                        (distance_squared == best_squared && triangle_index < best_triangle))
                    {
                        best_squared = distance_squared;
                        best_triangle = triangle_index;
                    }
                }
                continue;
            }
            const double left_distance = detail::aabb_distance(point_box, bvh[node.left].box);
            const double right_distance = detail::aabb_distance(point_box, bvh[node.right].box);
            if (left_distance < right_distance)
            {
                stack.push_back(node.right);
                stack.push_back(node.left);
            }
            else
            {
                stack.push_back(node.left);
                stack.push_back(node.right);
            }
        }
        return best_triangle;
    }

    SurfaceMesh mesh;
    MeshValidation validation;
    Aabb bounds{};
    std::vector<detail::TrianglePseudoNormals> pseudo_normals;
    std::vector<std::uint32_t> bvh_triangles;
    std::vector<BvhNode> bvh;
};

ExactSurface::ExactSurface(SurfaceMesh mesh)
    : impl_(std::make_shared<Impl>(std::move(mesh)))
{
}

const SurfaceMesh& ExactSurface::mesh() const noexcept
{
    return impl_->mesh;
}

const MeshValidation& ExactSurface::validation() const noexcept
{
    return impl_->validation;
}

const Aabb& ExactSurface::bounds() const noexcept
{
    return impl_->bounds;
}

QueryResult ExactSurface::query(Vec3 point) const
{
    const std::uint32_t triangle = impl_->nearest_triangle(point);
    return detail::exact_query_triangle(
        impl_->mesh, &impl_->pseudo_normals,
        point, &triangle, 1, true);
}

QueryResult ExactSurface::query_subset(
    Vec3 point,
    const std::uint32_t* triangle_indices,
    std::size_t count) const
{
    return detail::exact_query_triangle(
        impl_->mesh, &impl_->pseudo_normals,
        point, triangle_indices, count, true);
}

const char* status_message(Status status) noexcept
{
    switch (status)
    {
    case Status::Ok: return "ok";
    case Status::InvalidArgument: return "invalid argument";
    case Status::IoError: return "I/O error";
    case Status::InvalidFormat: return "invalid format";
    case Status::InvalidMesh: return "invalid mesh";
    case Status::Unsupported: return "unsupported operation";
    case Status::OutOfDomain: return "point is outside the asset domain";
    case Status::CorruptAsset: return "corrupt asset";
    case Status::InternalError: return "internal error";
    }
    return "unknown status";
}

namespace detail
{

Aabb mesh_bounds(const SurfaceMesh& mesh)
{
    const double infinity = std::numeric_limits<double>::infinity();
    Aabb box{{infinity, infinity, infinity}, {-infinity, -infinity, -infinity}};
    for (Vec3 vertex : mesh.vertices)
    {
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            box.minimum[axis] = std::min(box.minimum[axis], vertex[axis]);
            box.maximum[axis] = std::max(box.maximum[axis], vertex[axis]);
        }
    }
    return box;
}

Aabb padded_bounds(const SurfaceMesh& mesh, const BuildOptions& options)
{
    Aabb box = mesh_bounds(mesh);
    const Vec3 size = box.extent();
    const double longest = std::max({size.x, size.y, size.z});
    const double padding = options.absolute_padding +
        options.relative_padding * longest;
    const Vec3 delta{padding, padding, padding};
    box.minimum = box.minimum - delta;
    box.maximum = box.maximum + delta;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        if (box.maximum[axis] <= box.minimum[axis])
        {
            box.minimum[axis] -= 0.5;
            box.maximum[axis] += 0.5;
        }
    }
    return box;
}

Aabb triangle_bounds(const SurfaceMesh& mesh, std::uint32_t triangle_index)
{
    const Triangle& triangle = mesh.triangles.at(triangle_index);
    Aabb box{mesh.vertices[triangle.vertex[0]], mesh.vertices[triangle.vertex[0]]};
    for (std::size_t i = 1; i < 3; ++i)
    {
        const Vec3 vertex = mesh.vertices[triangle.vertex[i]];
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            box.minimum[axis] = std::min(box.minimum[axis], vertex[axis]);
            box.maximum[axis] = std::max(box.maximum[axis], vertex[axis]);
        }
    }
    return box;
}

double point_triangle_distance(
    const SurfaceMesh& mesh,
    std::uint32_t triangle_index,
    Vec3 point)
{
    const Triangle& triangle = mesh.triangles.at(triangle_index);
    const ClosestPoint closest = closest_point_on_triangle(
        point,
        mesh.vertices.at(triangle.vertex[0]),
        mesh.vertices.at(triangle.vertex[1]),
        mesh.vertices.at(triangle.vertex[2]));
    return norm(point - closest.point);
}

double aabb_distance(Aabb a, Aabb b) noexcept
{
    double squared = 0.0;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        double separation = 0.0;
        if (a.maximum[axis] < b.minimum[axis])
        {
            separation = b.minimum[axis] - a.maximum[axis];
        }
        else if (b.maximum[axis] < a.minimum[axis])
        {
            separation = a.minimum[axis] - b.maximum[axis];
        }
        squared += separation * separation;
    }
    return std::sqrt(squared);
}

double signed_solid_angle(const SurfaceMesh& mesh, Vec3 point)
{
    double total = 0.0;
    for (const Triangle& triangle : mesh.triangles)
    {
        const Vec3 a = mesh.vertices[triangle.vertex[0]] - point;
        const Vec3 b = mesh.vertices[triangle.vertex[1]] - point;
        const Vec3 c = mesh.vertices[triangle.vertex[2]] - point;
        const double la = norm(a);
        const double lb = norm(b);
        const double lc = norm(c);
        const double numerator = dot(a, cross(b, c));
        const double denominator = la * lb * lc +
            dot(a, b) * lc + dot(b, c) * la + dot(c, a) * lb;
        total += 2.0 * std::atan2(numerator, denominator);
    }
    return total;
}

QueryResult exact_query_triangle(
    const SurfaceMesh& mesh,
    const std::vector<TrianglePseudoNormals>* pseudo_normals,
    Vec3 point,
    const std::uint32_t* indices,
    std::size_t count,
    bool determine_sign)
{
    QueryResult result;
    double best_squared = std::numeric_limits<double>::infinity();
    std::uint32_t best_index = 0;
    ClosestPoint best_closest;

    for (std::size_t item = 0; item < count; ++item)
    {
        const std::uint32_t index = indices ? indices[item] :
            static_cast<std::uint32_t>(item);
        if (index >= mesh.triangles.size())
        {
            continue;
        }
        const Triangle& triangle = mesh.triangles[index];
        const ClosestPoint closest = closest_point_on_triangle(
            point,
            mesh.vertices[triangle.vertex[0]],
            mesh.vertices[triangle.vertex[1]],
            mesh.vertices[triangle.vertex[2]]);
        const double distance_squared = squared_norm(point - closest.point);
        if (distance_squared < best_squared ||
            (distance_squared == best_squared && index < best_index))
        {
            best_squared = distance_squared;
            best_index = index;
            best_closest = closest;
        }
    }
    if (!std::isfinite(best_squared))
    {
        return result;
    }

    const Triangle& triangle = mesh.triangles[best_index];
    const double unsigned_distance = std::sqrt(best_squared);
    Vec3 sign_normal = feature_normal(mesh, triangle, best_closest);
    if (pseudo_normals && best_index < pseudo_normals->size())
    {
        sign_normal = pseudo_normal_for_feature(
            (*pseudo_normals)[best_index], best_closest);
    }
    const bool inside = determine_sign && (pseudo_normals
        ? dot(point - best_closest.point, sign_normal) < 0.0
        : std::abs(signed_solid_angle(mesh, point)) > 2.0 * kPi);
    const double sign = inside ? -1.0 : 1.0;
    const Vec3 outward = unsigned_distance > kTiny
        ? sign * (point - best_closest.point) / unsigned_distance
        : sign_normal;

    result.phi = sign * unsigned_distance;
    result.raw_gradient = outward;
    result.unit_normal = normalized(outward);
    result.witness = best_closest.point;
    result.face_id = triangle.face_id;
    result.feature = best_closest.feature;
    result.branch_signature = hash_combine(
        hash_combine(0x4e534446ull, triangle.face_id),
        static_cast<std::uint64_t>(best_closest.feature));
    result.valid = true;
    result.exact = true;
    result.in_domain = true;
    result.has_witness = true;
    return result;
}

} // namespace detail
} // namespace nexsdf
