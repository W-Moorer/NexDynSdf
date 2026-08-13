#include "internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace nexsdf::detail
{
namespace
{

struct SphereHull
{
    std::array<Vec3, 8> centers{};
    std::array<double, 8> radii{};
};

struct Simplex
{
    std::array<Vec3, 4> point{};
    std::size_t size{0};
};

struct TriangleClosest
{
    Vec3 point{};
    std::array<double, 3> weight{};
};

bool finite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double numeric_margin(const SphereHull& hull, const std::array<Vec3, 3>& triangle)
{
    double scale = 1.0;
    for (std::size_t i = 0; i < hull.centers.size(); ++i)
        scale = std::max(scale, norm(hull.centers[i]) + hull.radii[i]);
    for (const Vec3 value : triangle) scale = std::max(scale, norm(value));
    return 256.0 * std::numeric_limits<double>::epsilon() * scale;
}

Vec3 support_triangle(const std::array<Vec3, 3>& triangle, Vec3 direction)
{
    std::size_t best = 0;
    double projection = dot(triangle[0], direction);
    for (std::size_t i = 1; i < triangle.size(); ++i)
    {
        const double candidate = dot(triangle[i], direction);
        if (candidate > projection)
        {
            best = i;
            projection = candidate;
        }
    }
    return triangle[best];
}

Vec3 support_hull(const SphereHull& hull, Vec3 direction)
{
    const double length = norm(direction);
    if (!(length > 0.0)) return hull.centers[0];
    const Vec3 unit = direction / length;
    std::size_t best = 0;
    double projection = dot(hull.centers[0], direction) + hull.radii[0] * length;
    for (std::size_t i = 1; i < hull.centers.size(); ++i)
    {
        const double candidate =
            dot(hull.centers[i], direction) + hull.radii[i] * length;
        if (candidate > projection)
        {
            best = i;
            projection = candidate;
        }
    }
    return hull.centers[best] + hull.radii[best] * unit;
}

Vec3 support_cso(
    const SphereHull& hull,
    const std::array<Vec3, 3>& triangle,
    Vec3 direction)
{
    return support_hull(hull, direction) - support_triangle(triangle, -1.0 * direction);
}

TriangleClosest closest_origin_triangle(Vec3 a, Vec3 b, Vec3 c)
{
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = -1.0 * a;
    const double d1 = dot(ab, ap);
    const double d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return {a, {1.0, 0.0, 0.0}};

    const Vec3 bp = -1.0 * b;
    const double d3 = dot(ab, bp);
    const double d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return {b, {0.0, 1.0, 0.0}};

    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
    {
        const double v = d1 / (d1 - d3);
        return {a + ab * v, {1.0 - v, v, 0.0}};
    }

    const Vec3 cp = -1.0 * c;
    const double d5 = dot(ab, cp);
    const double d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return {c, {0.0, 0.0, 1.0}};

    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
    {
        const double w = d2 / (d2 - d6);
        return {a + ac * w, {1.0 - w, 0.0, w}};
    }

    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
    {
        const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {b + (c - b) * w, {0.0, 1.0 - w, w}};
    }

    const double sum = va + vb + vc;
    if (!(std::abs(sum) > std::numeric_limits<double>::min()))
    {
        // Degenerate source triangles are rejected before this code is reached.
        // Keeping the full simplex is nevertheless safer than manufacturing a
        // separating direction if arithmetic underflows.
        return {{}, {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0}};
    }
    const double inverse = 1.0 / sum;
    const double v = vb * inverse;
    const double w = vc * inverse;
    return {a + ab * v + ac * w, {1.0 - v - w, v, w}};
}

bool tetrahedron_contains_origin(const std::array<Vec3, 4>& point, double margin)
{
    constexpr std::array<std::array<std::size_t, 4>, 4> faces{{
        {{0, 1, 2, 3}}, {{0, 3, 1, 2}}, {{0, 2, 3, 1}}, {{1, 3, 2, 0}}}};
    for (const auto& face : faces)
    {
        const Vec3 a = point[face[0]];
        const Vec3 normal = cross(point[face[1]] - a, point[face[2]] - a);
        const double origin_side = dot(normal, -1.0 * a);
        const double opposite_side = dot(normal, point[face[3]] - a);
        const double tolerance = margin * norm(normal);
        if (origin_side * opposite_side < 0.0 &&
            std::abs(origin_side) > tolerance &&
            std::abs(opposite_side) > tolerance)
            return false;
    }
    return true;
}

bool reduce_simplex(Simplex& simplex, Vec3& direction, double margin)
{
    if (simplex.size == 1)
    {
        direction = -1.0 * simplex.point[0];
        return norm(direction) <= margin;
    }
    if (simplex.size == 2)
    {
        const Vec3 a = simplex.point[0];
        const Vec3 b = simplex.point[1];
        const Vec3 ab = b - a;
        const double denominator = squared_norm(ab);
        if (!(denominator > margin * margin)) return true;
        const double t = std::clamp(dot(-1.0 * a, ab) / denominator, 0.0, 1.0);
        if (t <= 0.0)
        {
            simplex.point[0] = a;
            simplex.size = 1;
            direction = -1.0 * a;
        }
        else if (t >= 1.0)
        {
            simplex.point[0] = b;
            simplex.size = 1;
            direction = -1.0 * b;
        }
        else
        {
            direction = -1.0 * (a + t * ab);
        }
        return norm(direction) <= margin;
    }
    if (simplex.size == 3)
    {
        const TriangleClosest closest = closest_origin_triangle(
            simplex.point[0], simplex.point[1], simplex.point[2]);
        std::array<Vec3, 3> reduced{};
        std::size_t count = 0;
        for (std::size_t i = 0; i < 3; ++i)
            if (closest.weight[i] > 0.0) reduced[count++] = simplex.point[i];
        if (count == 0) return true;
        for (std::size_t i = 0; i < count; ++i) simplex.point[i] = reduced[i];
        simplex.size = count;
        direction = -1.0 * closest.point;
        return norm(direction) <= margin;
    }

    if (tetrahedron_contains_origin(simplex.point, margin)) return true;
    constexpr std::array<std::array<std::size_t, 3>, 4> faces{{
        {{0, 1, 2}}, {{0, 3, 1}}, {{0, 2, 3}}, {{1, 3, 2}}}};
    double best_squared = std::numeric_limits<double>::infinity();
    TriangleClosest best{};
    std::array<std::size_t, 3> best_face{};
    for (const auto& face : faces)
    {
        const TriangleClosest closest = closest_origin_triangle(
            simplex.point[face[0]], simplex.point[face[1]], simplex.point[face[2]]);
        const double candidate = squared_norm(closest.point);
        if (candidate < best_squared)
        {
            best_squared = candidate;
            best = closest;
            best_face = face;
        }
    }
    std::array<Vec3, 3> reduced{};
    std::size_t count = 0;
    for (std::size_t i = 0; i < 3; ++i)
        if (best.weight[i] > 0.0) reduced[count++] = simplex.point[best_face[i]];
    if (count == 0) return true;
    for (std::size_t i = 0; i < count; ++i) simplex.point[i] = reduced[i];
    simplex.size = count;
    direction = -1.0 * best.point;
    return norm(direction) <= margin;
}

bool gjk_intersects(const SphereHull& hull, const std::array<Vec3, 3>& triangle)
{
    const double margin = numeric_margin(hull, triangle);
    Simplex simplex;
    simplex.point[0] = support_cso(hull, triangle, {1.0, 0.0, 0.0});
    simplex.size = 1;
    Vec3 direction = -1.0 * simplex.point[0];
    if (norm(direction) <= margin) return true;

    for (std::size_t iteration = 0; iteration < 64; ++iteration)
    {
        const double direction_length = norm(direction);
        if (!(direction_length > margin) || !finite(direction)) return true;
        const Vec3 support = support_cso(hull, triangle, direction);

        // A negative support projection is a separating-plane certificate:
        // every point of the Minkowski difference lies behind the plane
        // through the origin normal to `direction`. The margin only weakens
        // rejection, so round-off can retain but cannot incorrectly discard.
        if (dot(support, direction) < -margin * direction_length) return false;

        bool duplicate = false;
        for (std::size_t i = 0; i < simplex.size; ++i)
            duplicate = duplicate || norm(support - simplex.point[i]) <= margin;
        if (duplicate || simplex.size == simplex.point.size()) return true;
        for (std::size_t i = simplex.size; i > 0; --i)
            simplex.point[i] = simplex.point[i - 1];
        simplex.point[0] = support;
        ++simplex.size;
        if (reduce_simplex(simplex, direction, margin)) return true;
    }
    return true;
}

bool frank_wolfe_intersects(SphereHull hull, const std::array<Vec3, 3>& triangle)
{
    const double threshold = *std::min_element(hull.radii.begin(), hull.radii.end());
    for (double& radius : hull.radii) radius = std::max(0.0, radius - threshold);
    const double margin = numeric_margin(hull, triangle);
    Vec3 current = support_cso(hull, triangle, {1.0, 0.0, 0.0});

    for (std::size_t iteration = 0; iteration < 32; ++iteration)
    {
        const double distance = norm(current);
        if (!finite(current) || distance <= threshold + margin) return true;
        const Vec3 outward = current / distance;
        const Vec3 support = support_cso(hull, triangle, -1.0 * outward);

        // `support` minimizes projection on `outward`. If even this minimum
        // lies beyond the threshold sphere, the entire convex set is separated
        // from that sphere and the candidate triangle is provably redundant.
        if (dot(outward, support) > threshold + margin) return false;

        const Vec3 descent = support - current;
        const double denominator = squared_norm(descent);
        if (!(denominator > margin * margin)) return true;
        const double alpha = std::clamp(
            dot(descent, -1.0 * current) / denominator, 0.0, 1.0);
        const Vec3 next = current + alpha * descent;
        if (norm(next - current) <= margin) return true;
        current = next;
    }
    // Failure to converge is deliberately conservative.
    return true;
}

} // namespace

bool paper_influence_intersects(
    const SurfaceMesh& mesh,
    const Aabb& box,
    std::uint32_t reference_triangle,
    std::uint32_t candidate_triangle,
    InfluenceFilter filter)
{
    const Vec3 center = (box.minimum + box.maximum) * 0.5;
    SphereHull hull;
    for (std::size_t corner = 0; corner < 8; ++corner)
    {
        const Vec3 point{
            (corner & 1u) ? box.maximum.x : box.minimum.x,
            (corner & 2u) ? box.maximum.y : box.minimum.y,
            (corner & 4u) ? box.maximum.z : box.minimum.z};
        hull.centers[corner] = point - center;
        hull.radii[corner] = point_triangle_distance(mesh, reference_triangle, point);
    }

    const Triangle& source = mesh.triangles.at(candidate_triangle);
    const std::array<Vec3, 3> triangle{
        mesh.vertices.at(source.vertex[0]) - center,
        mesh.vertices.at(source.vertex[1]) - center,
        mesh.vertices.at(source.vertex[2]) - center};
    if (filter == InfluenceFilter::PaperGjk) return gjk_intersects(hull, triangle);
    return frank_wolfe_intersects(hull, triangle);
}

} // namespace nexsdf::detail
