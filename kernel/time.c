#include "timeutil.h"

static uint64_t tsc_base;
static uint64_t tsc_per_ms = 2000000;   /* fallback: 2 GHz */

void time_init(void) {
    /* PIT channel 2, mode 0, ~50 ms one-shot, polled through port 0x61. */
    const uint16_t count = 59659;       /* 1193182 Hz * 0.05 s */
    outb(0x61, (inb(0x61) & ~0x02) | 0x01);
    outb(0x43, 0xB0);
    outb(0x42, count & 0xFF);
    outb(0x42, count >> 8);
    uint8_t v = inb(0x61) & ~0x01;
    outb(0x61, v);                      /* gate low -> high restarts the count */
    outb(0x61, v | 0x01);
    uint64_t t0 = rdtsc();
    uint32_t guard = 50000000;
    while (!(inb(0x61) & 0x20) && --guard) { }
    uint64_t t1 = rdtsc();
    if (guard) {
        uint64_t per_ms = (t1 - t0) / 50;
        if (per_ms > 100000 && per_ms < 20000000) tsc_per_ms = per_ms;
    }
    tsc_base = rdtsc();
}

uint64_t uptime_ms(void) { return (rdtsc() - tsc_base) / tsc_per_ms; }
uint64_t tsc_mhz(void)   { return tsc_per_ms / 1000; }

static uint8_t cmos(uint8_t reg) { outb(0x70, reg); return inb(0x71); }
static int bcd(int v) { return (v & 0x0F) + ((v >> 4) * 10); }

void rtc_read(struct rtc_time *t) {
    for (int i = 0; i < 10000 && (cmos(0x0A) & 0x80); i++) { }
    int s = cmos(0x00), mi = cmos(0x02), h = cmos(0x04);
    int d = cmos(0x07), mo = cmos(0x08), y = cmos(0x09), b = cmos(0x0B);
    bool pm = h & 0x80; h &= 0x7F;
    if (!(b & 4)) { s = bcd(s); mi = bcd(mi); h = bcd(h); d = bcd(d); mo = bcd(mo); y = bcd(y); }
    if (!(b & 2)) { h %= 12; if (pm) h += 12; }
    t->second = s; t->minute = mi; t->hour = h; t->day = d; t->month = mo; t->year = 2000 + y;
}
