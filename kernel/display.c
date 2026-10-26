/*
 * Aurora Display API v1.
 * Safely identifies common virtual display adapters through PCI config space.
 * Rendering uses Limine's boot framebuffer and Aurora's software renderer;
 * this module does NOT claim native SVGA/VMSVGA/virtio-gpu acceleration.
 */
#include "kernel.h"
#include "display.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_CLASS_DISPLAY  0x03

static struct aurora_display_info display_info;

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t fn, uint8_t off) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) | ((uint32_t)fn << 8) |
                       ((uint32_t)off & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

const char *aurora_display_kind_name(enum aurora_display_kind kind) {
    switch (kind) {
    case AURORA_DISPLAY_QEMU_STD_VGA: return "QEMU/Bochs-compatible VGA";
    case AURORA_DISPLAY_VBOX_VGA: return "VirtualBox VGA/VMSVGA";
    case AURORA_DISPLAY_VMWARE_SVGA: return "VMware SVGA";
    case AURORA_DISPLAY_VIRTIO_GPU: return "VirtIO GPU";
    case AURORA_DISPLAY_GENERIC_VGA: return "Generic PCI VGA";
    default: return "No supported PCI display adapter";
    }
}

static enum aurora_display_kind classify(uint16_t vendor, uint16_t device) {
    if (vendor == 0x1234 && device == 0x1111) return AURORA_DISPLAY_QEMU_STD_VGA;
    if (vendor == 0x80EE && (device == 0xBEEF || device == 0xCAFE)) return AURORA_DISPLAY_VBOX_VGA;
    if (vendor == 0x15AD && (device == 0x0405 || device == 0x0406 || device == 0x0407))
        return AURORA_DISPLAY_VMWARE_SVGA;
    if (vendor == 0x1AF4 && (device == 0x1050 || device == 0x1051 || device == 0x1052))
        return AURORA_DISPLAY_VIRTIO_GPU;
    if (vendor == 0x1234 || vendor == 0x80EE || vendor == 0x15AD)
        return AURORA_DISPLAY_GENERIC_VGA;
    return AURORA_DISPLAY_UNKNOWN;
}

void aurora_display_init(bool framebuffer_available) {
    memset(&display_info, 0, sizeof(display_info));
    display_info.api_version = AURORA_DISPLAY_API_VERSION;
    display_info.boot_framebuffer_available = framebuffer_available;
    display_info.hardware_2d_acceleration = false;

    /* Preserve the first recognized display controller. Probe bus 0 first/only:
       a full 256-bus scan can cause long boot stalls under virtual PCI emulation. */
    for (uint16_t bus = 0; bus < 1; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id0 = pci_read32((uint8_t)bus, slot, 0, 0x00);
            if ((uint16_t)id0 == 0xFFFF) continue;
            uint32_t hdr = pci_read32((uint8_t)bus, slot, 0, 0x0C);
            uint8_t functions = ((hdr >> 16) & 0x80u) ? 8 : 1;
            for (uint8_t fn = 0; fn < functions; fn++) {
                uint32_t id = fn ? pci_read32((uint8_t)bus, slot, fn, 0x00) : id0;
                if ((uint16_t)id == 0xFFFF) continue;
                uint32_t cls = pci_read32((uint8_t)bus, slot, fn, 0x08);
                if ((uint8_t)(cls >> 24) != PCI_CLASS_DISPLAY) continue;
                uint16_t vendor = (uint16_t)id;
                uint16_t device = (uint16_t)(id >> 16);
                enum aurora_display_kind kind = classify(vendor, device);
                if (kind == AURORA_DISPLAY_UNKNOWN) continue;
                display_info.vendor_id = vendor;
                display_info.device_id = device;
                display_info.bus = (uint8_t)bus;
                display_info.slot = slot;
                display_info.function = fn;
                display_info.kind = kind;
                return;
            }
        }
    }
}

bool aurora_display_get_info(struct aurora_display_info *out) {
    if (!out) return false;
    memcpy(out, &display_info, sizeof(*out));
    return true;
}

const char *aurora_display_status(void) {
    if (!display_info.boot_framebuffer_available) return "Boot framebuffer unavailable";
    if (display_info.kind == AURORA_DISPLAY_UNKNOWN)
        return "Using boot framebuffer; adapter not identified";
    return "Boot framebuffer active; software rendering (no 2D acceleration)";
}
