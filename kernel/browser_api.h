#pragma once
#include <stdint.h>
#include <stdbool.h>

#define AURORA_BROWSER_API_VERSION 2u

enum aurora_browser_feature {
    AURORA_BROWSER_FEATURE_HTML5 = 1u << 0,
    AURORA_BROWSER_FEATURE_CSS = 1u << 1,
    AURORA_BROWSER_FEATURE_JAVASCRIPT = 1u << 2,
    AURORA_BROWSER_FEATURE_HTTP = 1u << 3,
    AURORA_BROWSER_FEATURE_HTTPS = 1u << 4,
    AURORA_BROWSER_FEATURE_VIDEO = 1u << 5
};

struct aurora_browser_capabilities {
    uint32_t api_version;
    uint32_t available_features;
    uint32_t pending_features;
};

/* Capability discovery only; this build has no HTML/JS rendering engine. */
bool aurora_browser_get_capabilities(struct aurora_browser_capabilities *out);
