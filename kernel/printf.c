#include "kernel.h"

void kputc(char c) {
    serial_putc(c);
    console_putc(c);
}

typedef void (*out_fn)(char c, void *ctx);
struct sink { out_fn fn; void *ctx; };

static void put_num(const struct sink *o, unsigned long long v, unsigned base, bool upper,
                    int width, char pad, bool neg) {
    char buf[24]; int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) buf[i++] = '0';
    while (v) { buf[i++] = digits[v % base]; v /= base; }
    int len = i + (neg ? 1 : 0);
    if (neg && pad == '0') o->fn('-', o->ctx);
    for (; len < width; len++) o->fn(pad, o->ctx);
    if (neg && pad != '0') o->fn('-', o->ctx);
    while (i) o->fn(buf[--i], o->ctx);
}

static void vformat(const struct sink *o, const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') { o->fn(*f, o->ctx); continue; }
        f++;
        char pad = ' ';
        int width = 0, lng = 0;
        if (*f == '0') { pad = '0'; f++; }
        while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
        while (*f == 'l') { lng++; f++; }
        switch (*f) {
        case 's': { const char *s = va_arg(ap, const char *);
                    if (!s) s = "(null)";
                    while (*s) { o->fn(*s, o->ctx); s++; }
                    break; }
        case 'c': o->fn((char)va_arg(ap, int), o->ctx); break;
        case 'd': { long long v = lng ? va_arg(ap, long long) : va_arg(ap, int);
                    bool neg = v < 0;
                    put_num(o, neg ? -(unsigned long long)v : (unsigned long long)v,
                            10, false, width, pad, neg); break; }
        case 'u': put_num(o, lng ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned),
                          10, false, width, pad, false); break;
        case 'x': case 'X':
                  put_num(o, lng ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned),
                          16, *f == 'X', width, pad, false); break;
        case 'p': o->fn('0', o->ctx); o->fn('x', o->ctx);
                  put_num(o, (uintptr_t)va_arg(ap, void *), 16, false, 16, '0', false); break;
        case '%': o->fn('%', o->ctx); break;
        case 0:   return;
        default:  o->fn('%', o->ctx); o->fn(*f, o->ctx); break;
        }
    }
}

static void out_kputc(char c, void *ctx) { (void)ctx; kputc(c); }

void kvprintf(const char *fmt, va_list ap) {
    struct sink o = { out_kputc, NULL };
    vformat(&o, fmt, ap);
}

void kprintf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); kvprintf(fmt, ap); va_end(ap);
}

struct sbuf { char *p; size_t n, i; };
static void out_sbuf(char c, void *ctx) {
    struct sbuf *s = ctx;
    if (s->i + 1 < s->n) s->p[s->i] = c;
    s->i++;
}

int ksnprintf(char *buf, size_t n, const char *fmt, ...) {
    struct sbuf s = { buf, n, 0 };
    struct sink o = { out_sbuf, &s };
    va_list ap; va_start(ap, fmt); vformat(&o, fmt, ap); va_end(ap);
    if (n) buf[s.i < n ? s.i : n - 1] = 0;
    return (int)s.i;
}
