#include "browser_api.h"

bool aurora_browser_get_capabilities(struct aurora_browser_capabilities *out) {
    if (!out) return false;
    out->api_version = AURORA_BROWSER_API_VERSION;
    out->available_features = 0;
    out->pending_features = AURORA_BROWSER_FEATURE_HTML5 |
        AURORA_BROWSER_FEATURE_CSS | AURORA_BROWSER_FEATURE_JAVASCRIPT |
        AURORA_BROWSER_FEATURE_HTTP | AURORA_BROWSER_FEATURE_HTTPS |
        AURORA_BROWSER_FEATURE_VIDEO;
    return true;
}
