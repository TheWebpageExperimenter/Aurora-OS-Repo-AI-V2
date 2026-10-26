#pragma once
#include <stdint.h>
#include <stdbool.h>

#define AURORA_TASKBAR_API_VERSION 3u
#define AURORA_TASKBAR_SCHEME_AERO 0u
#define AURORA_TASKBAR_SCHEME_OCEAN 1u
#define AURORA_TASKBAR_SCHEME_GRAPHITE 2u
#define AURORA_TASKBAR_SCHEME_AMBER 3u
#define AURORA_TASKBAR_SCHEME_VIOLET 4u
#define AURORA_TASKBAR_SCHEME_COUNT 5u

struct aurora_taskbar_style {
    uint32_t api_version;
    bool color_enabled;
    uint32_t scheme;
};

uint32_t aurora_taskbar_api_version(void);
void aurora_taskbar_get_style(struct aurora_taskbar_style *out);
/* Returns false for an invalid scheme or null output; no partial update occurs. */
bool aurora_taskbar_set_style(bool color_enabled, uint32_t scheme);
