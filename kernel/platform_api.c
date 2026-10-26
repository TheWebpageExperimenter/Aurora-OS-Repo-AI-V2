#include "kernel.h"
#include "platform_api.h"
#include <stddef.h>

bool aurora_platform_get_info_sized(void *out, size_t out_size) {
    if (!out || out_size < sizeof(struct aurora_platform_info)) return false;

    /* Zero all reserved fields and report only facilities actually present. */
    struct aurora_platform_info info = {0};
    info.api_version = AURORA_PLATFORM_API_VERSION;
    info.struct_size = (uint32_t)sizeof(info);
    info.capabilities = AURORA_PLATFORM_CAP_FRAMEBUFFER |
                        AURORA_PLATFORM_CAP_SOFTWARE_RENDERER |
                        AURORA_PLATFORM_CAP_PCI_DISCOVERY;
    info.graphics_backend = 1; /* current GUI uses software framebuffer drawing */
    memcpy(out, &info, sizeof(info));
    return true;
}

bool aurora_platform_get_info(struct aurora_platform_info *out) {
    return aurora_platform_get_info_sized(out, out ? sizeof(*out) : 0);
}

const char *aurora_platform_web_readiness(void) {
    return "Web browsing unavailable: network packet I/O, TCP/IP, DNS, TLS certificate validation, "
           "userspace ABI, and a modern browser engine are not implemented";
}
