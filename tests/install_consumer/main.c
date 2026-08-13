#include "nexsdf/c_api.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    if (strcmp(nexsdf_status_message(NEXSDF_STATUS_OK), "ok") != 0)
    {
        fputs("installed NexDynSdf status API failed\n", stderr);
        return 1;
    }
    puts("installed NexDynSdf package is consumable");
    return 0;
}
