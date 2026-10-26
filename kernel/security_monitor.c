/* Aurora kernel security telemetry API v1.
 * Fixed-size, allocation-free audit ring. This is diagnostic hardening, not a
 * security boundary or tamper-proof log; no user/kernel isolation is implied.
 */
#include "kernel.h"
#include "security_monitor.h"

extern char __text_start[];
extern char __text_end[];

static struct aurora_security_record ring[AURORA_SECURITY_AUDIT_CAPACITY];
static struct aurora_security_stats stats;
static uint32_t next_slot;
static bool initialized;
static bool writing;

static uint64_t text_fingerprint(void) {
    const volatile uint8_t *p = (const volatile uint8_t *)__text_start;
    const volatile uint8_t *end = (const volatile uint8_t *)__text_end;
    uint64_t h = 1469598103934665603ULL;
    if (end <= p) return 0;
    while (p < end) { h ^= *p++; h *= 1099511628211ULL; }
    return h;
}

static void copy_message(char *dst, const char *src) {
    uint32_t i = 0;
    if (!src) src = "(no message)";
    for (; i + 1 < AURORA_SECURITY_MESSAGE_MAX && src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (c < 32 || c == 127) ? '?' : (char)c;
    }
    dst[i] = 0;
    for (++i; i < AURORA_SECURITY_MESSAGE_MAX; i++) dst[i] = 0;
}

bool aurora_security_audit(uint32_t event, enum aurora_security_severity severity, const char *message) {
    if (!initialized || event == 0 || severity < AURORA_SEC_INFO || severity > AURORA_SEC_CRITICAL) {
        return false;
    }
    /* Prevent interrupt-time reentrancy from corrupting the ring. This is not SMP locking. */
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    if (writing) { __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc"); return false; }
    writing = true;
    struct aurora_security_record *r = &ring[next_slot];
    r->sequence = ++stats.total_events;
    r->event = event;
    r->severity = (uint8_t)severity;
    copy_message(r->message, message);
    next_slot = (next_slot + 1) % AURORA_SECURITY_AUDIT_CAPACITY;
    if (stats.records_retained < AURORA_SECURITY_AUDIT_CAPACITY) stats.records_retained++;
    stats.severity_count[severity]++;
    writing = false;
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
    return true;
}

void aurora_security_init(void) {
    memset(ring, 0, sizeof(ring));
    memset(&stats, 0, sizeof(stats));
    next_slot = 0;
    writing = false;
    stats.api_version = AURORA_SECURITY_API_VERSION;
    stats.integrity_baseline = text_fingerprint();
    stats.integrity_available = stats.integrity_baseline != 0;
    stats.integrity_ok = stats.integrity_available;
    initialized = true;
    (void)aurora_security_audit(AURORA_SEC_BOOT, AURORA_SEC_NOTICE, "kernel security monitor initialized");
    (void)aurora_security_audit(stats.integrity_ok ? AURORA_SEC_INTEGRITY_OK : AURORA_SEC_INTEGRITY_FAIL,
        stats.integrity_ok ? AURORA_SEC_INFO : AURORA_SEC_CRITICAL,
        stats.integrity_ok ? "kernel text baseline recorded" : "kernel text baseline unavailable");
}

bool aurora_security_check_integrity(void) {
    if (!initialized || !stats.integrity_available) return false;
    bool ok = text_fingerprint() == stats.integrity_baseline;
    stats.integrity_ok = ok;
    (void)aurora_security_audit(ok ? AURORA_SEC_INTEGRITY_OK : AURORA_SEC_INTEGRITY_FAIL,
        ok ? AURORA_SEC_INFO : AURORA_SEC_CRITICAL,
        ok ? "kernel text fingerprint matches boot baseline" : "kernel text fingerprint mismatch");
    return ok;
}

bool aurora_security_get_stats(struct aurora_security_stats *out) {
    if (!out || !initialized) return false;
    memcpy(out, &stats, sizeof(*out));
    return true;
}

uint32_t aurora_security_snapshot(struct aurora_security_record *out, uint32_t capacity) {
    if (!out || !initialized || capacity == 0) return 0;
    uint32_t count = stats.records_retained < capacity ? stats.records_retained : capacity;
    uint32_t oldest = (next_slot + AURORA_SECURITY_AUDIT_CAPACITY - stats.records_retained) % AURORA_SECURITY_AUDIT_CAPACITY;
    uint32_t skip = stats.records_retained - count;
    oldest = (oldest + skip) % AURORA_SECURITY_AUDIT_CAPACITY;
    for (uint32_t i = 0; i < count; i++) out[i] = ring[(oldest + i) % AURORA_SECURITY_AUDIT_CAPACITY];
    return count;
}

bool aurora_security_check_invariants(void) {
    if (!initialized || stats.api_version != AURORA_SECURITY_API_VERSION ||
        stats.records_retained > AURORA_SECURITY_AUDIT_CAPACITY || next_slot >= AURORA_SECURITY_AUDIT_CAPACITY)
        return false;
    uint64_t sum = 0;
    for (unsigned i = 0; i < 4; i++) sum += stats.severity_count[i];
    if (sum != stats.total_events) return false;
    uint32_t oldest = (next_slot + AURORA_SECURITY_AUDIT_CAPACITY - stats.records_retained) % AURORA_SECURITY_AUDIT_CAPACITY;
    for (uint32_t i = 0; i < stats.records_retained; i++) {
        const struct aurora_security_record *r = &ring[(oldest + i) % AURORA_SECURITY_AUDIT_CAPACITY];
        if (!r->sequence || r->severity > AURORA_SEC_CRITICAL || r->event == 0) return false;
        bool terminated = false;
        for (uint32_t j = 0; j < AURORA_SECURITY_MESSAGE_MAX; j++) {
            if (r->message[j] == 0) { terminated = true; break; }
        }
        if (!terminated) return false;
    }
    return true;
}

uint32_t aurora_security_api_version(void) { return AURORA_SECURITY_API_VERSION; }
