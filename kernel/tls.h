#pragma once
#include <stdint.h>
#include <stdbool.h>
#define AURORA_TLS_API_VERSION 2u
#define AURORA_TLS_MAX_HOSTNAME 253u
#define AURORA_TLS_CA_BUNDLE_PATH "/boot/assets/ca-certificates.crt"

enum aurora_tls_state { AURORA_TLS_UNAVAILABLE = 0, AURORA_TLS_TRUST_BUNDLE_PACKAGED = 1, AURORA_TLS_READY = 2 };
struct aurora_tls_status_info {
    uint32_t api_version;
    uint16_t max_hostname;
    enum aurora_tls_state state;
    bool ca_bundle_packaged;
    bool certificate_validation_ready;
    bool tls_handshake_ready;
};
/* API v2 distinguishes a packaged trust bundle from a functioning TLS stack. */
bool aurora_tls_get_status(struct aurora_tls_status_info *out);
bool aurora_tls_verify_peer_certificate(const uint8_t *der, uint32_t length, const char *hostname);
const char *aurora_tls_status_text(void);
