/*
 * Aurora Network API v2
 * Defensive PCI enumeration for common VM network adapters. This is deliberately
 * discovery only: there is no packet driver or TCP/IP stack in this source tree.
 */
#include "kernel.h"
#include "net.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_CLASS_NETWORK  0x02

static struct aurora_net_info net_info;

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t fn, uint8_t off) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) | ((uint32_t)fn << 8) |
                       ((uint32_t)off & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

const char *aurora_net_kind_name(enum aurora_nic_kind kind) {
    switch (kind) {
    case AURORA_NIC_INTEL_E1000: return "Intel E1000 / PRO-1000";
    case AURORA_NIC_VIRTIO_NET: return "VirtIO network";
    case AURORA_NIC_VMWARE_VMXNET3: return "VMware vmxnet3";
    case AURORA_NIC_REALTEK_8139: return "Realtek RTL8139";
    case AURORA_NIC_AMD_PCNET: return "AMD PCnet";
    default: return "Unknown network adapter";
    }
}

static enum aurora_nic_kind classify(uint16_t vendor, uint16_t device) {
    if (vendor == 0x8086 && (device == 0x100E || device == 0x100F ||
        device == 0x10D3 || device == 0x153A || device == 0x15F3))
        return AURORA_NIC_INTEL_E1000;
    if (vendor == 0x1AF4 && (device == 0x1000 || device == 0x1041 ||
        device == 0x1042)) return AURORA_NIC_VIRTIO_NET;
    if (vendor == 0x15AD && (device == 0x07B0 || device == 0x07B1))
        return AURORA_NIC_VMWARE_VMXNET3;
    if (vendor == 0x10EC && device == 0x8139) return AURORA_NIC_REALTEK_8139;
    if (vendor == 0x1022 && device == 0x2000) return AURORA_NIC_AMD_PCNET;
    return AURORA_NIC_UNKNOWN;
}

static void add_nic(uint8_t bus, uint8_t slot, uint8_t fn, uint32_t id, uint32_t classreg) {
    uint16_t vendor = (uint16_t)(id & 0xFFFFu);
    uint16_t device = (uint16_t)(id >> 16);
    uint8_t class_code = (uint8_t)(classreg >> 24);
    uint8_t subclass = (uint8_t)(classreg >> 16);
    enum aurora_nic_kind kind;
    if (vendor == 0xFFFF || class_code != PCI_CLASS_NETWORK || subclass != 0x00)
        return;
    kind = classify(vendor, device);
    if (kind == AURORA_NIC_UNKNOWN || net_info.nic_count >= AURORA_NET_MAX_NICS)
        return;
    struct aurora_nic_info *n = &net_info.nics[net_info.nic_count++];
    n->vendor_id = vendor; n->device_id = device;
    n->bus = bus; n->slot = slot; n->function = fn; n->kind = kind;
}

void aurora_net_init(void) {
    memset(&net_info, 0, sizeof(net_info));
    net_info.api_version = AURORA_NET_API_VERSION;
    /* PCI config mechanism #1; probe bus 0 only in this early kernel.
       The previous 256-bus sweep performed tens of thousands of slow port I/Os
       on every boot and could look like a kernel deadlock in virtual machines.
       Current VMware/QEMU/VirtualBox virtual NICs and display adapters are on bus 0. */
    for (uint16_t bus = 0; bus < 1; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id0 = pci_read32((uint8_t)bus, slot, 0, 0x00);
            if ((uint16_t)id0 == 0xFFFF) continue;
            uint32_t hdr = pci_read32((uint8_t)bus, slot, 0, 0x0C);
            uint8_t functions = ((hdr >> 16) & 0x80u) ? 8 : 1;
            for (uint8_t fn = 0; fn < functions; fn++) {
                uint32_t id = fn ? pci_read32((uint8_t)bus, slot, fn, 0x00) : id0;
                if ((uint16_t)id == 0xFFFF) continue;
                uint32_t classreg = pci_read32((uint8_t)bus, slot, fn, 0x08);
                add_nic((uint8_t)bus, slot, fn, id, classreg);
            }
        }
    }
    net_info.packet_io_ready = false;
}

bool aurora_net_get_info_sized(void *out, size_t out_size) {
    /* Reject null, undersized, and oversized buffers rather than guessing the
       caller's ABI. This prevents a structure copy from overrunning a caller's
       buffer when headers and kernel objects are built from mismatched versions. */
    if (!out || out_size < sizeof(struct aurora_net_info)) return false;
    struct aurora_net_info snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.api_version = AURORA_NET_API_VERSION;
    snapshot.nic_count = net_info.nic_count;
    if (snapshot.nic_count > AURORA_NET_MAX_NICS)
        snapshot.nic_count = AURORA_NET_MAX_NICS;
    snapshot.packet_io_ready = false;
    for (uint32_t i = 0; i < snapshot.nic_count; ++i)
        snapshot.nics[i] = net_info.nics[i];
    memcpy(out, &snapshot, sizeof(snapshot));
    return true;
}

bool aurora_net_get_info(struct aurora_net_info *out) {
    return aurora_net_get_info_sized(out, out ? sizeof(*out) : 0);
}

const char *aurora_net_status(void) {
    return net_info.nic_count ? "Network adapter detected; packet stack unavailable" :
                                "No supported network adapter detected";
}
