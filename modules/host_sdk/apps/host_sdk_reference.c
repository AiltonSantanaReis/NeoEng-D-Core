#include "neoeng/dcore_host.h"

#include <inttypes.h>
#include <stdio.h>

int main(void) {
    neoeng_dcore_host_config config;
    neoeng_dcore_host* host = NULL;
    neoeng_dcore_body body = {1U, 0U, 0, 0, 0, 0};
    neoeng_dcore_input input = {1U, 0U, INT64_C(4294967296), 0};
    neoeng_dcore_state_summary state;
    uint64_t required = 0U;
    neoeng_dcore_body copied;

    if (neoeng_dcore_host_default_config(&config) != NEOENG_DCORE_STATUS_OK) {
        return 1;
    }
    if (neoeng_dcore_host_create(0U, &body, 1U, &config, &host)
        != NEOENG_DCORE_STATUS_OK) {
        return 2;
    }
    if (neoeng_dcore_host_advance(host, &input, 1U, 7U, 0U, &state)
        != NEOENG_DCORE_STATUS_OK) {
        (void)neoeng_dcore_host_destroy(host);
        return 3;
    }
    if (neoeng_dcore_host_copy_bodies(host, &copied, 1U, &required)
        != NEOENG_DCORE_STATUS_OK) {
        (void)neoeng_dcore_host_destroy(host);
        return 4;
    }
    printf("frame=%" PRIu64 " bodies=%" PRIu64 " hash=0x%016" PRIX64
           " position_x_raw=%" PRId64 "\n",
           state.frame, required, state.stable_hash, copied.position_x_raw);
    return neoeng_dcore_host_destroy(host) == NEOENG_DCORE_STATUS_OK ? 0 : 5;
}
