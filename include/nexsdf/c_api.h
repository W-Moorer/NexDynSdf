#pragma once

#include "nexsdf/export.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nexsdf_asset nexsdf_asset;

typedef enum nexsdf_status
{
    NEXSDF_STATUS_OK = 0,
    NEXSDF_STATUS_INVALID_ARGUMENT = 1,
    NEXSDF_STATUS_IO_ERROR = 2,
    NEXSDF_STATUS_INVALID_FORMAT = 3,
    NEXSDF_STATUS_INVALID_MESH = 4,
    NEXSDF_STATUS_UNSUPPORTED = 5,
    NEXSDF_STATUS_OUT_OF_DOMAIN = 6,
    NEXSDF_STATUS_CORRUPT_ASSET = 7,
    NEXSDF_STATUS_INTERNAL_ERROR = 8
} nexsdf_status;

typedef struct nexsdf_asset_info
{
    uint32_t format_major;
    uint32_t format_minor;
    uint32_t representation;
    uint32_t reconstruction;
    double domain_minimum[3];
    double domain_maximum[3];
    uint32_t resolution[3];
    uint32_t maximum_depth;
    double requested_error_tolerance;
    double measured_maximum_error;
    uint64_t node_count;
    uint64_t coefficient_count;
    uint64_t triangle_count;
    uint32_t has_measured_error;
} nexsdf_asset_info;

typedef struct nexsdf_asset_provenance
{
    uint32_t struct_size;
    uint32_t influence_filter;
    uint64_t candidate_index_count;
    uint32_t composition;
    uint32_t component_count;
    uint32_t active_component_count;
} nexsdf_asset_provenance;

typedef struct nexsdf_query_result
{
    double phi;
    double raw_gradient[3];
    double unit_normal[3];
    double hessian[9];
    double witness[3];
    double measured_leaf_error;
    uint64_t branch_signature;
    uint32_t cell_depth;
    uint32_t face_id;
    uint32_t feature;
    uint32_t flags;
} nexsdf_query_result;

enum
{
    NEXSDF_QUERY_VALID = 1u << 0,
    NEXSDF_QUERY_EXACT = 1u << 1,
    NEXSDF_QUERY_IN_DOMAIN = 1u << 2,
    NEXSDF_QUERY_HAS_HESSIAN = 1u << 3,
    NEXSDF_QUERY_HAS_WITNESS = 1u << 4,
    NEXSDF_QUERY_HAS_MEASURED_ERROR = 1u << 5
};

NEXSDF_API nexsdf_status nexsdf_asset_open(
    const char* path,
    nexsdf_asset** out_asset);
NEXSDF_API void nexsdf_asset_close(nexsdf_asset* asset);
NEXSDF_API nexsdf_status nexsdf_asset_get_info(
    const nexsdf_asset* asset,
    nexsdf_asset_info* out_info);
NEXSDF_API nexsdf_status nexsdf_asset_get_provenance(
    const nexsdf_asset* asset,
    nexsdf_asset_provenance* out_provenance);
NEXSDF_API nexsdf_status nexsdf_query(
    const nexsdf_asset* asset,
    const double point[3],
    nexsdf_query_result* out_result);
NEXSDF_API nexsdf_status nexsdf_query_batch(
    const nexsdf_asset* asset,
    size_t count,
    const double* xyz,
    size_t xyz_stride_bytes,
    nexsdf_query_result* out_results);
NEXSDF_API const char* nexsdf_last_error(void);
NEXSDF_API const char* nexsdf_status_message(nexsdf_status status);

#ifdef __cplusplus
}
#endif
