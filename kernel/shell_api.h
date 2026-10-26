#ifndef AURORA_SHELL_API_H
#define AURORA_SHELL_API_H
#include <stdbool.h>
#include <stdint.h>

#define AURORA_SHELL_API_VERSION 4u
#define AURORA_SHELL_API_LEVEL 2u
#define AURORA_SHELL_MAX_HANDLERS 8u

enum aurora_shell_event_type {
    AURORA_SHELL_EVENT_POLL = 1,
    AURORA_SHELL_EVENT_KEY = 2,
    AURORA_SHELL_EVENT_MOUSE = 3,
    AURORA_SHELL_EVENT_SHUTDOWN = 4
};

enum aurora_shell_handler_level {
    AURORA_SHELL_LEVEL_KERNEL = 0,
    AURORA_SHELL_LEVEL_SYSTEM = 1,
    AURORA_SHELL_LEVEL_APPLICATION = 2
};

struct aurora_shell_event {
    uint32_t type;
    int32_t value;
    uint64_t timestamp_ms;
};

typedef bool (*aurora_shell_event_handler)(const struct aurora_shell_event *event, void *context);

uint32_t aurora_shell_api_version(void);
uint32_t aurora_shell_api_level(void);
bool aurora_shell_register_handler(enum aurora_shell_handler_level level,
                                   aurora_shell_event_handler handler, void *context);
bool aurora_shell_dispatch(const struct aurora_shell_event *event);
void aurora_shell_reset_handlers(void);
#endif
