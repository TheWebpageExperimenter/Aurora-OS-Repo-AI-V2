#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Aurora platform/runtime capability API v3.
 * Capabilities describe implemented kernel services, not promises that a
 * userspace ABI, accelerated GPU driver, or web engine exists. */
#define AURORA_PLATFORM_API_VERSION 4u
#define AURORA_PLATFORM_CAP_FRAMEBUFFER       (1u << 0)
#define AURORA_PLATFORM_CAP_PCI_DISCOVERY     (1u << 1)
#define AURORA_PLATFORM_CAP_SOFTWARE_RENDERER (1u << 2)
#define AURORA_PLATFORM_CAP_DISPLAY_MODE_SET  (1u << 3)
#define AURORA_PLATFORM_CAP_NETWORK_IO        (1u << 4)
#define AURORA_PLATFORM_CAP_TCP_IP            (1u << 5)
#define AURORA_PLATFORM_CAP_DNS               (1u << 6)
#define AURORA_PLATFORM_CAP_TLS13              (1u << 7)
#define AURORA_PLATFORM_CAP_USERSPACE_ABI      (1u << 8)
#define AURORA_PLATFORM_CAP_MODERN_BROWSER     (1u << 9)

struct aurora_platform_info {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t capabilities;
    uint32_t graphics_backend; /* 0=none, 1=software framebuffer */
    uint32_t reserved;
};

/* Returns false without writing if out is null or too small. */
bool aurora_platform_get_info_sized(void *out, size_t out_size);
bool aurora_platform_get_info(struct aurora_platform_info *out);
const char *aurora_platform_web_readiness(void);
