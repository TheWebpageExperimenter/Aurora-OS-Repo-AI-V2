#include "taskbar_api.h"

static struct aurora_taskbar_style current_style = {
    AURORA_TASKBAR_API_VERSION, true, AURORA_TASKBAR_SCHEME_AERO
};

uint32_t aurora_taskbar_api_version(void) { return AURORA_TASKBAR_API_VERSION; }

void aurora_taskbar_get_style(struct aurora_taskbar_style *out) {
    if (out) *out = current_style;
}

bool aurora_taskbar_set_style(bool color_enabled, uint32_t scheme) {
    if (scheme >= AURORA_TASKBAR_SCHEME_COUNT) return false;
    current_style.color_enabled = color_enabled;
    current_style.scheme = scheme;
    return true;
}
