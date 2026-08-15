#include "nexsdf/c_api.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    nexsdf_asset* asset = NULL;
    nexsdf_asset_info info;
    nexsdf_query_result result;
    const double point[3] = {0.0, 0.0, 0.0};

    if (strcmp(nexsdf_status_message(NEXSDF_STATUS_OK), "ok") != 0)
    {
        fprintf(stderr, "unexpected status message\n");
        return 1;
    }
    if (nexsdf_asset_get_info(asset, &info) != NEXSDF_STATUS_INVALID_ARGUMENT)
    {
        fprintf(stderr, "null asset was not rejected\n");
        return 1;
    }
    {
        nexsdf_asset_provenance provenance;
        memset(&provenance, 0, sizeof(provenance));
        provenance.struct_size = (uint32_t)sizeof(provenance);
        if (nexsdf_asset_get_provenance(asset, &provenance) !=
            NEXSDF_STATUS_INVALID_ARGUMENT)
        {
            fprintf(stderr, "null provenance asset was not rejected\n");
            return 1;
        }
    }
    if (nexsdf_query(asset, point, &result) != NEXSDF_STATUS_INVALID_ARGUMENT)
    {
        fprintf(stderr, "null query asset was not rejected\n");
        return 1;
    }
    {
        double clearance = 0.0;
        if (nexsdf_query_certified(asset, point, &result, &clearance) !=
            NEXSDF_STATUS_INVALID_ARGUMENT)
        {
            fprintf(stderr, "null certified query asset was not rejected\n");
            return 1;
        }
        if (nexsdf_query_certified_batch(
                asset, 1, point, 3 * sizeof(double), &result, &clearance) !=
            NEXSDF_STATUS_INVALID_ARGUMENT)
        {
            fprintf(stderr, "null certified batch query asset was not rejected\n");
            return 1;
        }
    }
    if (nexsdf_last_error() == NULL || nexsdf_last_error()[0] == '\0')
    {
        fprintf(stderr, "C API did not retain an error message\n");
        return 1;
    }
    nexsdf_asset_close(NULL);
    puts("pure C API checks passed");
    return 0;
}
