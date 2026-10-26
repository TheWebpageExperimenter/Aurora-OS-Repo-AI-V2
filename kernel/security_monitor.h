#pragma once
#include <stdint.h>
#include <stdbool.h>

#define AURORA_SECURITY_API_VERSION 1u
#define AURORA_SECURITY_AUDIT_CAPACITY 64u
#define AURORA_SECURITY_MESSAGE_MAX 64u

enum aurora_security_severity {
    AURORA_SEC_INFO = 0,
    AURORA_SEC_NOTICE = 1,
    AURORA_SEC_WARNING = 2,
    AURORA_SEC_CRITICAL = 3
};

enum aurora_security_event {
    AURORA_SEC_BOOT = 1,
    AURORA_SEC_INTEGRITY_OK = 2,
    AURORA_SEC_INTEGRITY_FAIL = 3,
    AURORA_SEC_BAD_URI = 4,
    AURORA_SEC_UNSUPPORTED_SCHEME = 5,
    AURORA_SEC_API_MISUSE = 6,
    AURORA_SEC_CERT_REJECTED = 7
};

struct aurora_security_record {
    uint64_t sequence;
    uint32_t event;
    uint8_t severity;
    char message[AURORA_SECURITY_MESSAGE_MAX];
};

struct aurora_security_stats {
    uint32_t api_version;
    uint32_t records_retained;
    uint64_t total_events;
    uint64_t severity_count[4];
    uint64_t integrity_baseline;
    bool integrity_available;
    bool integrity_ok;
};

void aurora_security_init(void);
bool aurora_security_audit(uint32_t event, enum aurora_security_severity severity, const char *message);
bool aurora_security_get_stats(struct aurora_security_stats *out);
uint32_t aurora_security_snapshot(struct aurora_security_record *out, uint32_t capacity);
bool aurora_security_check_integrity(void);
bool aurora_security_check_invariants(void);
uint32_t aurora_security_api_version(void);
