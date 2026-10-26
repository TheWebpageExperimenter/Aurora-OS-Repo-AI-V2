#include "tls.h"
#include "kernel.h"

bool aurora_tls_get_status(struct aurora_tls_status_info *out) {
    if (!out) return false;
    out->api_version = AURORA_TLS_API_VERSION;
    out->max_hostname = AURORA_TLS_MAX_HOSTNAME;
    /* The CA bundle is shipped on the ISO, but the current kernel has no
       filesystem-backed trust-store loader, DER chain builder, or TLS engine. */
    out->state = AURORA_TLS_TRUST_BUNDLE_PACKAGED;
    out->ca_bundle_packaged = true;
    out->certificate_validation_ready = false;
    out->tls_handshake_ready = false;
    return true;
}

bool aurora_tls_verify_peer_certificate(const uint8_t *der, uint32_t length, const char *hostname) {
    (void)der; (void)length; (void)hostname;
    /* Fail closed until DER parsing, chain building, hostname checks, validity
       dates, revocation policy, and a loaded trust store are implemented. */
    return false;
}

const char *aurora_tls_status_text(void) {
    return "CA bundle packaged; TLS handshake and certificate-chain validation are not implemented";
}
