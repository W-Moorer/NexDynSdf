#include "internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace nexsdf::detail
{
namespace
{

Vec3 corner(const Aabb& box, std::size_t index)
{
    return {
        (index & 1u) ? box.maximum.x : box.minimum.x,
        (index & 2u) ? box.maximum.y : box.minimum.y,
        (index & 4u) ? box.maximum.z : box.minimum.z};
}

double phi(const ExactSurface& surface, Vec3 point)
{
    return surface.query(point).phi;
}

double central_first(
    const ExactSurface& surface,
    Vec3 point,
    std::size_t axis,
    double step)
{
    Vec3 plus = point;
    Vec3 minus = point;
    plus[axis] += step;
    minus[axis] -= step;
    return (phi(surface, plus) - phi(surface, minus)) / (2.0 * step);
}

double central_mixed_second(
    const ExactSurface& surface,
    Vec3 point,
    std::size_t first,
    std::size_t second,
    double step)
{
    Vec3 pp = point;
    Vec3 pm = point;
    Vec3 mp = point;
    Vec3 mm = point;
    pp[first] += step; pp[second] += step;
    pm[first] += step; pm[second] -= step;
    mp[first] -= step; mp[second] += step;
    mm[first] -= step; mm[second] -= step;
    return (phi(surface, pp) - phi(surface, pm) -
            phi(surface, mp) + phi(surface, mm)) /
        (4.0 * step * step);
}

double central_mixed_third(
    const ExactSurface& surface,
    Vec3 point,
    double step)
{
    double sum = 0.0;
    for (int sx : {-1, 1})
    for (int sy : {-1, 1})
    for (int sz : {-1, 1})
    {
        const Vec3 sample{
            point.x + sx * step,
            point.y + sy * step,
            point.z + sz * step};
        sum += static_cast<double>(sx * sy * sz) * phi(surface, sample);
    }
    return sum / (8.0 * step * step * step);
}

struct Basis
{
    double value{0.0};
    double first{0.0};
    double second{0.0};
    double third{0.0};
};

Basis hermite_basis(
    double t,
    double length,
    bool upper,
    bool derivative)
{
    const double t2 = t * t;
    const double t3 = t2 * t;
    double v = 0.0;
    double d = 0.0;
    double d2 = 0.0;
    double d3 = 0.0;
    if (!derivative && !upper)
    {
        v = 2.0 * t3 - 3.0 * t2 + 1.0;
        d = 6.0 * t2 - 6.0 * t;
        d2 = 12.0 * t - 6.0;
        d3 = 12.0;
    }
    else if (!derivative && upper)
    {
        v = -2.0 * t3 + 3.0 * t2;
        d = -6.0 * t2 + 6.0 * t;
        d2 = -12.0 * t + 6.0;
        d3 = -12.0;
    }
    else if (derivative && !upper)
    {
        v = t3 - 2.0 * t2 + t;
        d = 3.0 * t2 - 4.0 * t + 1.0;
        d2 = 6.0 * t - 4.0;
        d3 = 6.0;
    }
    else
    {
        v = t3 - t2;
        d = 3.0 * t2 - 2.0 * t;
        d2 = 6.0 * t - 2.0;
        d3 = 6.0;
    }

    const double scale = derivative ? length : 1.0;
    return {
        scale * v,
        scale * d / length,
        scale * d2 / (length * length),
        scale * d3 / (length * length * length)};
}

std::array<double, 8> tricubic_jet(
    const double* jets,
    const Aabb& box,
    Vec3 point)
{
    const Vec3 size = box.extent();
    const Vec3 local{
        (point.x - box.minimum.x) / size.x,
        (point.y - box.minimum.y) / size.y,
        (point.z - box.minimum.z) / size.z};
    std::array<double, 8> out{};
    for (std::size_t c = 0; c < 8; ++c)
    for (std::size_t derivative_mask = 0; derivative_mask < 8; ++derivative_mask)
    {
        const Basis bx = hermite_basis(
            local.x, size.x, (c & 1u) != 0, (derivative_mask & 1u) != 0);
        const Basis by = hermite_basis(
            local.y, size.y, (c & 2u) != 0, (derivative_mask & 2u) != 0);
        const Basis bz = hermite_basis(
            local.z, size.z, (c & 4u) != 0, (derivative_mask & 4u) != 0);
        const double coefficient = jets[8 * c + derivative_mask];
        out[0] += coefficient * bx.value * by.value * bz.value;
        out[1] += coefficient * bx.first * by.value * bz.value;
        out[2] += coefficient * bx.value * by.first * bz.value;
        out[3] += coefficient * bx.first * by.first * bz.value;
        out[4] += coefficient * bx.value * by.value * bz.first;
        out[5] += coefficient * bx.first * by.value * bz.first;
        out[6] += coefficient * bx.value * by.first * bz.first;
        out[7] += coefficient * bx.first * by.first * bz.first;
    }
    return out;
}

} // namespace

std::array<double, 8> trilinear_samples(
    const ExactSurface& surface,
    const Aabb& box,
    const double* parent_coefficients,
    const Aabb* parent_box)
{
    std::array<double, 8> samples{};
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const Vec3 point = corner(box, i);
        bool use_parent = false;
        if (parent_coefficients && parent_box)
        {
            constexpr double epsilon = 1.0e-12;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const double scale = std::max(1.0, parent_box->extent()[axis]);
                use_parent = use_parent ||
                    std::abs(point[axis] - parent_box->minimum[axis]) <= epsilon * scale ||
                    std::abs(point[axis] - parent_box->maximum[axis]) <= epsilon * scale;
            }
        }
        samples[i] = use_parent
            ? evaluate_trilinear(parent_coefficients, *parent_box, point).value
            : surface.query(point).phi;
    }
    return samples;
}

PolynomialSample evaluate_trilinear(
    const double* values,
    const Aabb& box,
    Vec3 point)
{
    const Vec3 size = box.extent();
    const Vec3 t{
        (point.x - box.minimum.x) / size.x,
        (point.y - box.minimum.y) / size.y,
        (point.z - box.minimum.z) / size.z};
    const std::array<double, 2> bx{1.0 - t.x, t.x};
    const std::array<double, 2> by{1.0 - t.y, t.y};
    const std::array<double, 2> bz{1.0 - t.z, t.z};
    const std::array<double, 2> dx{-1.0 / size.x, 1.0 / size.x};
    const std::array<double, 2> dy{-1.0 / size.y, 1.0 / size.y};
    const std::array<double, 2> dz{-1.0 / size.z, 1.0 / size.z};

    PolynomialSample out;
    for (std::size_t z = 0; z < 2; ++z)
    for (std::size_t y = 0; y < 2; ++y)
    for (std::size_t x = 0; x < 2; ++x)
    {
        const std::size_t index = x + 2 * y + 4 * z;
        const double value = values[index];
        out.value += value * bx[x] * by[y] * bz[z];
        out.gradient.x += value * dx[x] * by[y] * bz[z];
        out.gradient.y += value * bx[x] * dy[y] * bz[z];
        out.gradient.z += value * bx[x] * by[y] * dz[z];
        out.hessian[1] += value * dx[x] * dy[y] * bz[z];
        out.hessian[2] += value * dx[x] * by[y] * dz[z];
        out.hessian[5] += value * bx[x] * dy[y] * dz[z];
    }
    out.hessian[3] = out.hessian[1];
    out.hessian[6] = out.hessian[2];
    out.hessian[7] = out.hessian[5];
    return out;
}

std::array<double, 64> tricubic_samples(
    const ExactSurface& surface,
    const Aabb& box,
    double derivative_step,
    const double* parent_coefficients,
    const Aabb* parent_box)
{
    const Vec3 extent = box.extent();
    const double minimum_extent = std::min({extent.x, extent.y, extent.z});
    const double step = derivative_step > 0.0
        ? derivative_step
        : std::max(1.0e-7, minimum_extent * 0.25);
    std::array<double, 64> jets{};
    for (std::size_t c = 0; c < 8; ++c)
    {
        const Vec3 point = corner(box, c);
        bool use_parent = false;
        if (parent_coefficients && parent_box)
        {
            constexpr double epsilon = 1.0e-12;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const double scale = std::max(1.0, parent_box->extent()[axis]);
                use_parent = use_parent ||
                    std::abs(point[axis] - parent_box->minimum[axis]) <= epsilon * scale ||
                    std::abs(point[axis] - parent_box->maximum[axis]) <= epsilon * scale;
            }
        }
        if (use_parent)
        {
            const std::array<double, 8> parent = tricubic_jet(
                parent_coefficients, *parent_box, point);
            for (std::size_t component = 0; component < 8; ++component)
            {
                jets[8 * c + component] = parent[component];
            }
            continue;
        }

        const std::array<double, 8> point_jet =
            tricubic_point_jet(surface, point, step);
        std::copy(point_jet.begin(), point_jet.end(), jets.begin() + 8 * c);
    }
    return jets;
}

std::array<double, 8> tricubic_point_jet(
    const ExactSurface& surface,
    Vec3 point,
    double derivative_step)
{
    const QueryResult exact = surface.query(point);
    return {
        exact.phi,
        central_first(surface, point, 0, derivative_step),
        central_first(surface, point, 1, derivative_step),
        central_mixed_second(surface, point, 0, 1, derivative_step),
        central_first(surface, point, 2, derivative_step),
        central_mixed_second(surface, point, 0, 2, derivative_step),
        central_mixed_second(surface, point, 1, 2, derivative_step),
        central_mixed_third(surface, point, derivative_step)};
}

PolynomialSample evaluate_tricubic(
    const double* jets,
    const Aabb& box,
    Vec3 point)
{
    const Vec3 size = box.extent();
    const Vec3 local{
        (point.x - box.minimum.x) / size.x,
        (point.y - box.minimum.y) / size.y,
        (point.z - box.minimum.z) / size.z};
    PolynomialSample out;
    for (std::size_t c = 0; c < 8; ++c)
    for (std::size_t derivative_mask = 0; derivative_mask < 8; ++derivative_mask)
    {
        const Basis bx = hermite_basis(
            local.x, size.x, (c & 1u) != 0, (derivative_mask & 1u) != 0);
        const Basis by = hermite_basis(
            local.y, size.y, (c & 2u) != 0, (derivative_mask & 2u) != 0);
        const Basis bz = hermite_basis(
            local.z, size.z, (c & 4u) != 0, (derivative_mask & 4u) != 0);
        const double coefficient = jets[8 * c + derivative_mask];
        out.value += coefficient * bx.value * by.value * bz.value;
        out.gradient.x += coefficient * bx.first * by.value * bz.value;
        out.gradient.y += coefficient * bx.value * by.first * bz.value;
        out.gradient.z += coefficient * bx.value * by.value * bz.first;
        out.hessian[0] += coefficient * bx.second * by.value * bz.value;
        out.hessian[4] += coefficient * bx.value * by.second * bz.value;
        out.hessian[8] += coefficient * bx.value * by.value * bz.second;
        out.hessian[1] += coefficient * bx.first * by.first * bz.value;
        out.hessian[2] += coefficient * bx.first * by.value * bz.first;
        out.hessian[5] += coefficient * bx.value * by.first * bz.first;
    }
    out.hessian[3] = out.hessian[1];
    out.hessian[6] = out.hessian[2];
    out.hessian[7] = out.hessian[5];
    return out;
}

std::array<double, 8> evaluate_tricubic_jet(
    const double* jets,
    const Aabb& box,
    Vec3 point)
{
    return tricubic_jet(jets, box, point);
}

std::array<double, 4> gradient_taylor_sample(
    const ExactSurface& surface,
    Vec3 center)
{
    const QueryResult exact = surface.query(center);
    return {
        exact.phi,
        exact.unit_normal.x,
        exact.unit_normal.y,
        exact.unit_normal.z};
}

PolynomialSample evaluate_gradient_taylor(
    const double* values,
    Vec3 center,
    Vec3 point)
{
    PolynomialSample out;
    out.gradient = {values[1], values[2], values[3]};
    out.value = values[0] + dot(point - center, out.gradient);
    return out;
}

} // namespace nexsdf::detail
