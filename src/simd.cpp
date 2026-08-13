#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#if defined(NEXSDF_ENABLE_SIMD) && (defined(_M_X64) || defined(__SSE2__))
#include <immintrin.h>
#define NEXSDF_HAS_SSE2 1
#endif

namespace nexsdf::detail
{
namespace
{

std::size_t grid_index(
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

std::uint64_t hash_combine(std::uint64_t seed, std::uint64_t value) noexcept
{
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
    return seed;
}

QueryResult trilinear_simd_result(const AssetData& data, Vec3 point)
{
    const auto resolution = data.info.resolution;
    const Vec3 extent = data.info.domain.extent();
    std::array<std::uint32_t, 3> cell{};
    double local[3]{};
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const double coordinate = (point[axis] - data.info.domain.minimum[axis]) /
            extent[axis] * resolution[axis];
        cell[axis] = std::min(
            resolution[axis] - 1,
            static_cast<std::uint32_t>(std::max(0.0, std::floor(coordinate))));
        local[axis] = coordinate - cell[axis];
    }
    const std::array<std::uint32_t, 3> dimensions{
        resolution[0] + 1, resolution[1] + 1, resolution[2] + 1};
    double values[8]{};
    for (std::size_t corner = 0; corner < 8; ++corner)
    {
        values[corner] = data.coefficients[grid_index(
            cell[0] + ((corner & 1u) ? 1u : 0u),
            cell[1] + ((corner & 2u) ? 1u : 0u),
            cell[2] + ((corner & 4u) ? 1u : 0u), dimensions)];
    }

#if defined(NEXSDF_HAS_SSE2)
    const __m128d vx0 = _mm_loadu_pd(values);
    const __m128d vx1 = _mm_loadu_pd(values + 2);
    const __m128d vx2 = _mm_loadu_pd(values + 4);
    const __m128d vx3 = _mm_loadu_pd(values + 6);
    const __m128d wx = _mm_set_pd(local[0], 1.0 - local[0]);
    const auto horizontal = [](const __m128d value) {
        const __m128d sum = _mm_add_sd(value, _mm_unpackhi_pd(value, value));
        return _mm_cvtsd_f64(sum);
    };
    const double x00 = horizontal(_mm_mul_pd(vx0, wx));
    const double x10 = horizontal(_mm_mul_pd(vx1, wx));
    const double x01 = horizontal(_mm_mul_pd(vx2, wx));
    const double x11 = horizontal(_mm_mul_pd(vx3, wx));
#else
    const double x00 = values[0] * (1.0 - local[0]) + values[1] * local[0];
    const double x10 = values[2] * (1.0 - local[0]) + values[3] * local[0];
    const double x01 = values[4] * (1.0 - local[0]) + values[5] * local[0];
    const double x11 = values[6] * (1.0 - local[0]) + values[7] * local[0];
#endif
    QueryResult result;
    const Vec3 cell_size{
        extent.x / resolution[0], extent.y / resolution[1], extent.z / resolution[2]};
    const double y0 = x00 * (1.0 - local[1]) + x10 * local[1];
    const double y1 = x01 * (1.0 - local[1]) + x11 * local[1];
    result.phi = y0 * (1.0 - local[2]) + y1 * local[2];
    const double dx00 = values[1] - values[0];
    const double dx10 = values[3] - values[2];
    const double dx01 = values[5] - values[4];
    const double dx11 = values[7] - values[6];
    result.raw_gradient = {
        ((dx00 * (1.0 - local[1]) + dx10 * local[1]) * (1.0 - local[2]) +
         (dx01 * (1.0 - local[1]) + dx11 * local[1]) * local[2]) / cell_size.x,
        ((x10 - x00) * (1.0 - local[2]) + (x11 - x01) * local[2]) / cell_size.y,
        (y1 - y0) / cell_size.z};
    const double gradient_length = norm(result.raw_gradient);
    result.unit_normal = gradient_length > 1.0e-14
        ? result.raw_gradient / gradient_length : Vec3{};
    result.hessian[1] = result.hessian[3] =
        (((values[3] - values[2]) - (values[1] - values[0])) * (1.0 - local[2]) +
         ((values[7] - values[6]) - (values[5] - values[4])) * local[2]) /
        (cell_size.x * cell_size.y);
    result.hessian[2] = result.hessian[6] =
        (((values[5] - values[4]) - (values[1] - values[0])) * (1.0 - local[1]) +
         ((values[7] - values[6]) - (values[3] - values[2])) * local[1]) /
        (cell_size.x * cell_size.z);
    result.hessian[5] = result.hessian[7] =
        ((x11 - x01) - (x10 - x00)) / (cell_size.y * cell_size.z);
    const std::uint64_t cell_signature = grid_index(cell[0], cell[1], cell[2], resolution);
    result.branch_signature = hash_combine(0x44454e5345ull, cell_signature);
    result.valid = std::isfinite(result.phi) &&
        std::isfinite(result.raw_gradient.x) &&
        std::isfinite(result.raw_gradient.y) &&
        std::isfinite(result.raw_gradient.z);
    result.in_domain = true;
    result.has_hessian = true;
    return result;
}

} // namespace

bool query_batch_simd(
    const AssetData& data,
    const Vec3* points,
    std::size_t count,
    QueryResult* out)
{
#if !defined(NEXSDF_HAS_SSE2)
    (void)data;
    (void)points;
    (void)count;
    (void)out;
    return false;
#else
    if (data.info.representation != Representation::DenseGrid ||
        data.info.reconstruction != Reconstruction::Trilinear)
    {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        out[index] = data.info.domain.contains(points[index])
            ? trilinear_simd_result(data, points[index]) : QueryResult{};
    }
    return true;
#endif
}

} // namespace nexsdf::detail
