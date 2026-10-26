#include "shell_api.h"
#include "timeutil.h"

struct handler_slot {
    aurora_shell_event_handler fn;
    void *context;
    enum aurora_shell_handler_level level;
    uint32_t order;
};
static struct handler_slot slots[AURORA_SHELL_MAX_HANDLERS];
static uint32_t slot_count;
static uint32_t next_order;

uint32_t aurora_shell_api_version(void) { return AURORA_SHELL_API_VERSION; }
uint32_t aurora_shell_api_level(void) { return AURORA_SHELL_API_LEVEL; }

bool aurora_shell_register_handler(enum aurora_shell_handler_level level,
                                   aurora_shell_event_handler handler, void *context) {
    if (!handler || level < AURORA_SHELL_LEVEL_KERNEL || level > AURORA_SHELL_LEVEL_APPLICATION ||
        slot_count >= AURORA_SHELL_MAX_HANDLERS) return false;
    slots[slot_count++] = (struct handler_slot){ handler, context, level, next_order++ };
    return true;
}

bool aurora_shell_dispatch(const struct aurora_shell_event *event) {
    if (!event) return false;
    /* Dispatch in privilege/importance order: kernel, system, then application. */
    for (int level = AURORA_SHELL_LEVEL_KERNEL; level <= AURORA_SHELL_LEVEL_APPLICATION; ++level) {
        for (uint32_t i = 0; i < slot_count; ++i) {
            if ((int)slots[i].level != level) continue;
            if (slots[i].fn(event, slots[i].context)) return true;
        }
    }
    return false;
}

void aurora_shell_reset_handlers(void) {
    for (uint32_t i = 0; i < AURORA_SHELL_MAX_HANDLERS; ++i) slots[i] = (struct handler_slot){0};
    slot_count = 0;
    next_order = 0;
}
