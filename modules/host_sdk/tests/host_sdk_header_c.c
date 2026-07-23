#include "neoeng/dcore_host.h"

int main(void) {
    neoeng_dcore_host_config config;
    neoeng_dcore_version_info version;
    if (neoeng_dcore_host_default_config(&config) != NEOENG_DCORE_STATUS_OK) {
        return 1;
    }
    if (neoeng_dcore_host_get_version(&version) != NEOENG_DCORE_STATUS_OK) {
        return 2;
    }
    return (config.abi_major == version.abi_major
            && version.runtime_major == NEOENG_DCORE_RUNTIME_VERSION_MAJOR)
        ? 0 : 3;
}
