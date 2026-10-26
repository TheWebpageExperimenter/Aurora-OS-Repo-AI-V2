#pragma once
#include <stdint.h>
#include <stdbool.h>

#define AURORA_DISPLAY_API_VERSION 1u

enum aurora_display_kind {
    AURORA_DISPLAY_UNKNOWN = 0,
    AURORA_DISPLAY_QEMU_STD_VGA,
    AURORA_DISPLAY_VBOX_VGA,
    AURORA_DISPLAY_VMWARE_SVGA,
    AURORA_DISPLAY_VIRTIO_GPU,
    AURORA_DISPLAY_GENERIC_VGA
};

struct aurora_display_info {
    uint32_t api_version;
    uint16_t vendor_id, device_id;
    uint8_t bus, slot, function;
    enum aurora_display_kind kind;
    bool boot_framebuffer_available;
    bool hardware_2d_acceleration; /* false: renderer is software-only */
};

void aurora_display_init(bool framebuffer_available);
bool aurora_display_get_info(struct aurora_display_info *out);
const char *aurora_display_kind_name(enum aurora_display_kind kind);
const char *aurora_display_status(void);
