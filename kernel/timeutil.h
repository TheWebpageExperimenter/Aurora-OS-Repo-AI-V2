#pragma once
#include "kernel.h"
struct rtc_time { int year, month, day, hour, minute, second; };
void     time_init(void);              /* calibrates the TSC against the PIT */
uint64_t uptime_ms(void);
uint64_t tsc_mhz(void);
void     rtc_read(struct rtc_time *t); /* CMOS real-time clock (UTC under QEMU) */
