#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Aurora Network API v2: PCI NIC discovery only; no packet I/O is implied. */
#define AURORA_NET_API_VERSION 2u
#define AURORA_NET_MAX_NICS 8u

enum aurora_nic_kind {
    AURORA_NIC_UNKNOWN = 0,
    AURORA_NIC_INTEL_E1000,
    AURORA_NIC_VIRTIO_NET,
    AURORA_NIC_VMWARE_VMXNET3,
    AURORA_NIC_REALTEK_8139,
    AURORA_NIC_AMD_PCNET
};

struct aurora_nic_info {
    uint16_t vendor_id, device_id;
    uint8_t bus, slot, function;
    enum aurora_nic_kind kind;
};

struct aurora_net_info {
    uint32_t api_version;
    uint32_t nic_count;
    bool packet_io_ready; /* Always false until a driver and network stack are present. */
    struct aurora_nic_info nics[AURORA_NET_MAX_NICS];
};

void aurora_net_init(void);
/* Sized API prevents writing a full structure into a smaller caller buffer. */
bool aurora_net_get_info_sized(void *out, size_t out_size);
/* Compatibility wrapper; caller must provide sizeof(struct aurora_net_info). */
bool aurora_net_get_info(struct aurora_net_info *out);
const char *aurora_net_kind_name(enum aurora_nic_kind kind);
const char *aurora_net_status(void);
