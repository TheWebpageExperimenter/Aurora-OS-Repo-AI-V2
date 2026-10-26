/* Freestanding mem* functions (the compiler may emit calls to these). */
#include "kernel.h"

void *memcpy(void *restrict dest, const void *restrict src, size_t n) {
    void *ret = dest;
    __asm__ volatile ("rep movsb" : "+D"(dest), "+S"(src), "+c"(n) : : "memory");
    return ret;
}

void *memset(void *s, int c, size_t n) {
    void *ret = s;
    __asm__ volatile ("rep stosb" : "+D"(s), "+c"(n) : "a"(c) : "memory");
    return ret;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest; const uint8_t *s = src;
    if (d == s || n == 0) return dest;
    if (d < s || d >= s + n) return memcpy(dest, src, n);
    for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    return dest;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p = a, *q = b;
    for (size_t i = 0; i < n; i++)
        if (p[i] != q[i]) return p[i] < q[i] ? -1 : 1;
    return 0;
}
