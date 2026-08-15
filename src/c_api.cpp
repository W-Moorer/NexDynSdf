#include "nexsdf/c_api.h"

#include "nexsdf/nexsdf.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

struct nexsdf_asset
{
    explicit nexsdf_asset(nexsdf::Asset value) : asset(std::move(value)) {}
    nexsdf::Asset asset;
};

namespace
{

thread_local std::string last_error;

nexsdf_status fail(nexsdf_status status, const char* message) noexcept
{
    try
    {
        last_error = message ? message : "unknown error";
    }
    catch (...)
    {
    }
    return status;
}

nexsdf_status translate_exception() noexcept
{
    try
    {
        throw;
    }
    catch (const std::invalid_argument& error)
    {
        return fail(NEXSDF_STATUS_INVALID_ARGUMENT, error.what());
    }
    catch (const std::bad_alloc& error)
    {
        return fail(NEXSDF_STATUS_INTERNAL_ERROR, error.what());
    }
    catch (const std::exception& error)
    {
        const char* message = error.what();
        if (std::strstr(message, "cannot open") ||
            std::strstr(message, "failed to read") ||
            std::strstr(message, "failed to write"))
        {
            return fail(NEXSDF_STATUS_IO_ERROR, error.what());
        }
        if (std::strstr(message, "NSDF"))
        {
            return fail(NEXSDF_STATUS_CORRUPT_ASSET, error.what());
        }
        return fail(NEXSDF_STATUS_INVALID_FORMAT, error.what());
    }
    catch (...)
    {
        return fail(NEXSDF_STATUS_INTERNAL_ERROR, "non-standard C++ exception");
    }
}

void copy_result(const nexsdf::QueryResult& source, nexsdf_query_result& target) noexcept
{
    std::memset(&target, 0, sizeof(target));
    target.phi = source.phi;
    target.raw_gradient[0] = source.raw_gradient.x;
    target.raw_gradient[1] = source.raw_gradient.y;
    target.raw_gradient[2] = source.raw_gradient.z;
    target.unit_normal[0] = source.unit_normal.x;
    target.unit_normal[1] = source.unit_normal.y;
    target.unit_normal[2] = source.unit_normal.z;
    for (std::size_t i = 0; i < 9; ++i) target.hessian[i] = source.hessian[i];
    target.witness[0] = source.witness.x;
    target.witness[1] = source.witness.y;
    target.witness[2] = source.witness.z;
    target.measured_leaf_error = source.measured_leaf_error;
    target.branch_signature = source.branch_signature;
    target.cell_depth = source.cell_depth;
    target.face_id = source.face_id;
    target.feature = static_cast<std::uint32_t>(source.feature);
    target.flags = (source.valid ? NEXSDF_QUERY_VALID : 0u) |
        (source.exact ? NEXSDF_QUERY_EXACT : 0u) |
        (source.in_domain ? NEXSDF_QUERY_IN_DOMAIN : 0u) |
        (source.has_hessian ? NEXSDF_QUERY_HAS_HESSIAN : 0u) |
        (source.has_witness ? NEXSDF_QUERY_HAS_WITNESS : 0u) |
        (source.has_measured_error ? NEXSDF_QUERY_HAS_MEASURED_ERROR : 0u);
}

nexsdf::BatchQueryOptions c_api_batch_options(
    const nexsdf::Asset& asset,
    std::size_t count) noexcept
{
    (void)asset;
    nexsdf::BatchQueryOptions options;
    options.backend = nexsdf::BatchBackend::AutoSimd;
    // Thread creation is not profitable for the small Newton batches used by
    // branch-certificate refreshes.  Full-surface frozen residuals contain
    // thousands of independent samples, however, and deterministic contiguous
    // partitions preserve every per-point result while substantially reducing
    // latency.  Cap the stable C ABI policy so it cannot oversubscribe a host.
    constexpr std::size_t kParallelBatchThreshold = 256u;
    constexpr std::uint32_t kMaximumWorkers = 8u;
    const std::uint32_t hardware = std::thread::hardware_concurrency();
    options.worker_threads = count >= kParallelBatchThreshold
        ? std::max(
              1u,
              std::min(kMaximumWorkers, hardware == 0u ? 1u : hardware))
        : 1u;
    return options;
}

bool valid_batch_arguments(
    const nexsdf_asset* asset,
    size_t count,
    const double* xyz,
    size_t xyz_stride_bytes,
    const void* out_results) noexcept
{
    constexpr size_t point_bytes = 3 * sizeof(double);
    const size_t maximum_size = static_cast<size_t>(-1);
    return asset && (count == 0 || (xyz && out_results)) &&
        (count == 0 || xyz_stride_bytes >= point_bytes) &&
        count <= maximum_size / sizeof(nexsdf_query_result) &&
        (count <= 1 || xyz_stride_bytes <=
            ((maximum_size - point_bytes) / (count - 1)));
}

std::vector<nexsdf::Vec3> gather_points(
    size_t count,
    const double* xyz,
    size_t xyz_stride_bytes)
{
    std::vector<nexsdf::Vec3> points(count);
    const auto* bytes = reinterpret_cast<const unsigned char*>(xyz);
    for (std::size_t i = 0; i < count; ++i)
    {
        double point[3]{};
        std::memcpy(point, bytes + i * xyz_stride_bytes, sizeof(point));
        points[i] = {point[0], point[1], point[2]};
    }
    return points;
}

} // namespace

extern "C"
{

nexsdf_status nexsdf_asset_open(const char* path, nexsdf_asset** out_asset)
{
    if (!path || !out_asset)
    {
        return fail(NEXSDF_STATUS_INVALID_ARGUMENT, "path and out_asset are required");
    }
    *out_asset = nullptr;
    try
    {
        *out_asset = new nexsdf_asset(nexsdf::Asset::load(path));
        last_error.clear();
        return NEXSDF_STATUS_OK;
    }
    catch (...)
    {
        return translate_exception();
    }
}

void nexsdf_asset_close(nexsdf_asset* asset)
{
    delete asset;
}

nexsdf_status nexsdf_asset_get_info(
    const nexsdf_asset* asset,
    nexsdf_asset_info* out_info)
{
    if (!asset || !out_info)
    {
        return fail(NEXSDF_STATUS_INVALID_ARGUMENT, "asset and out_info are required");
    }
    const nexsdf::AssetInfo& source = asset->asset.info();
    std::memset(out_info, 0, sizeof(*out_info));
    out_info->format_major = source.format_major;
    out_info->format_minor = source.format_minor;
    out_info->representation = static_cast<std::uint32_t>(source.representation);
    out_info->reconstruction = static_cast<std::uint32_t>(source.reconstruction);
    out_info->domain_minimum[0] = source.domain.minimum.x;
    out_info->domain_minimum[1] = source.domain.minimum.y;
    out_info->domain_minimum[2] = source.domain.minimum.z;
    out_info->domain_maximum[0] = source.domain.maximum.x;
    out_info->domain_maximum[1] = source.domain.maximum.y;
    out_info->domain_maximum[2] = source.domain.maximum.z;
    for (std::size_t i = 0; i < 3; ++i) out_info->resolution[i] = source.resolution[i];
    out_info->maximum_depth = source.maximum_depth;
    out_info->requested_error_tolerance = source.requested_error_tolerance;
    out_info->measured_maximum_error = source.measured_maximum_error;
    out_info->node_count = source.node_count;
    out_info->coefficient_count = source.coefficient_count;
    out_info->triangle_count = source.triangle_count;
    out_info->has_measured_error = source.has_measured_error ? 1u : 0u;
    last_error.clear();
    return NEXSDF_STATUS_OK;
}

nexsdf_status nexsdf_asset_get_provenance(
    const nexsdf_asset* asset,
    nexsdf_asset_provenance* out_provenance)
{
    constexpr std::size_t minimum_size =
        offsetof(nexsdf_asset_provenance, build_backend);
    if (!asset || !out_provenance || out_provenance->struct_size < minimum_size)
    {
        return fail(NEXSDF_STATUS_INVALID_ARGUMENT,
            "asset and a size-initialized provenance structure are required");
    }
    const nexsdf::AssetInfo& source = asset->asset.info();
    const std::uint32_t requested_size = out_provenance->struct_size;
    nexsdf_asset_provenance result{};
    result.struct_size = requested_size;
    result.influence_filter =
        static_cast<std::uint32_t>(source.influence_filter);
    result.candidate_index_count = source.candidate_index_count;
    result.composition = static_cast<std::uint32_t>(source.composition);
    result.component_count = source.component_count;
    result.active_component_count = source.active_component_count;
    result.build_backend = static_cast<std::uint32_t>(source.build_backend);
    result.worker_threads = source.worker_threads;
    std::memcpy(out_provenance, &result,
        std::min<std::size_t>(requested_size, sizeof(result)));
    last_error.clear();
    return NEXSDF_STATUS_OK;
}

nexsdf_status nexsdf_query(
    const nexsdf_asset* asset,
    const double point[3],
    nexsdf_query_result* out_result)
{
    if (!asset || !point || !out_result)
    {
        return fail(NEXSDF_STATUS_INVALID_ARGUMENT, "asset, point, and result are required");
    }
    try
    {
        const nexsdf::QueryResult result = asset->asset.query({point[0], point[1], point[2]});
        copy_result(result, *out_result);
        if (!result.in_domain)
        {
            return fail(NEXSDF_STATUS_OUT_OF_DOMAIN, "query point is outside the asset domain");
        }
        if (!result.valid)
        {
            return fail(NEXSDF_STATUS_INTERNAL_ERROR, "query did not produce a finite result");
        }
        last_error.clear();
        return NEXSDF_STATUS_OK;
    }
    catch (...)
    {
        return translate_exception();
    }
}

nexsdf_status nexsdf_query_certified(
    const nexsdf_asset* asset,
    const double point[3],
    nexsdf_query_result* out_result,
    double* out_branch_motion_clearance)
{
    if (!asset || !point || !out_result || !out_branch_motion_clearance)
    {
        return fail(
            NEXSDF_STATUS_INVALID_ARGUMENT,
            "asset, point, result, and branch clearance are required");
    }
    *out_branch_motion_clearance = 0.0;
    try
    {
        const nexsdf::QueryResult result = asset->asset.query_certified(
            {point[0], point[1], point[2]});
        copy_result(result, *out_result);
        *out_branch_motion_clearance = result.branch_motion_clearance;
        if (!result.in_domain)
        {
            return fail(
                NEXSDF_STATUS_OUT_OF_DOMAIN,
                "query point is outside the asset domain");
        }
        if (!result.valid)
        {
            return fail(
                NEXSDF_STATUS_INTERNAL_ERROR,
                "certified query did not produce a finite result");
        }
        last_error.clear();
        return NEXSDF_STATUS_OK;
    }
    catch (...)
    {
        return translate_exception();
    }
}

nexsdf_status nexsdf_query_certified_batch(
    const nexsdf_asset* asset,
    size_t count,
    const double* xyz,
    size_t xyz_stride_bytes,
    nexsdf_query_result* out_results,
    double* out_branch_motion_clearances)
{
    if (!valid_batch_arguments(
            asset, count, xyz, xyz_stride_bytes, out_results) ||
        (count != 0 && !out_branch_motion_clearances))
    {
        return fail(
            NEXSDF_STATUS_INVALID_ARGUMENT,
            "invalid certified batch query arguments");
    }
    bool outside = false;
    try
    {
        const std::vector<nexsdf::Vec3> points =
            gather_points(count, xyz, xyz_stride_bytes);
        std::vector<nexsdf::QueryResult> results(count);
        asset->asset.query_certified_batch(
            points.data(),
            points.size(),
            results.data(),
            c_api_batch_options(asset->asset, count));
        for (std::size_t i = 0; i < count; ++i)
        {
            copy_result(results[i], out_results[i]);
            out_branch_motion_clearances[i] =
                results[i].branch_motion_clearance;
            outside = outside || !results[i].in_domain;
        }
        if (outside)
        {
            return fail(
                NEXSDF_STATUS_OUT_OF_DOMAIN,
                "one or more certified batch points are outside the asset domain");
        }
        last_error.clear();
        return NEXSDF_STATUS_OK;
    }
    catch (...)
    {
        return translate_exception();
    }
}

nexsdf_status nexsdf_query_batch(
    const nexsdf_asset* asset,
    size_t count,
    const double* xyz,
    size_t xyz_stride_bytes,
    nexsdf_query_result* out_results)
{
    if (!valid_batch_arguments(
            asset, count, xyz, xyz_stride_bytes, out_results))
    {
        return fail(NEXSDF_STATUS_INVALID_ARGUMENT, "invalid batch query arguments");
    }
    bool outside = false;
    try
    {
        const std::vector<nexsdf::Vec3> points =
            gather_points(count, xyz, xyz_stride_bytes);
        std::vector<nexsdf::QueryResult> results(count);
        asset->asset.query_batch(
            points.data(),
            points.size(),
            results.data(),
            c_api_batch_options(asset->asset, count));
        for (std::size_t i = 0; i < count; ++i)
        {
            copy_result(results[i], out_results[i]);
            outside = outside || !results[i].in_domain;
        }
        if (outside)
        {
            return fail(NEXSDF_STATUS_OUT_OF_DOMAIN, "one or more batch points are outside the asset domain");
        }
        last_error.clear();
        return NEXSDF_STATUS_OK;
    }
    catch (...)
    {
        return translate_exception();
    }
}

const char* nexsdf_last_error(void)
{
    return last_error.c_str();
}

const char* nexsdf_status_message(nexsdf_status status)
{
    return nexsdf::status_message(static_cast<nexsdf::Status>(status));
}

} // extern "C"
