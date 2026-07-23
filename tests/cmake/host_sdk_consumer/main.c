#include "neoeng/dcore_host.h"

int main(void) {
    neoeng_dcore_host_config config;
    neoeng_dcore_host* host = 0;
    neoeng_dcore_body body = {1U, 0U, 0, 0, 0, 0};
    neoeng_dcore_state_summary state;
    if (neoeng_dcore_host_default_config(&config) != NEOENG_DCORE_STATUS_OK) {
        return 1;
    }
    if (neoeng_dcore_host_create(0U, &body, 1U, &config, &host)
        != NEOENG_DCORE_STATUS_OK) {
        return 2;
    }
    if (neoeng_dcore_host_advance(host, 0, 0U, 1U, 0U, &state)
        != NEOENG_DCORE_STATUS_OK || state.frame != 1U) {
        (void)neoeng_dcore_host_destroy(host);
        return 3;
    }
    return neoeng_dcore_host_destroy(host) == NEOENG_DCORE_STATUS_OK ? 0 : 4;
}
