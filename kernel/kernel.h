#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#define PAGE_SIZE 4096ULL

/* serial.c */
void serial_init(void);
void serial_putc(char c);

/* console.c  (framebuffer text console) */
struct limine_framebuffer;
bool console_init(struct limine_framebuffer *fb);
void console_putc(char c);
void console_set_colors(uint32_t fg, uint32_t bg);
void console_clear(void);
uint32_t console_rgb(uint8_t r, uint8_t g, uint8_t b);

/* printf.c */
void kputc(char c);
void kvprintf(const char *fmt, va_list ap);
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  ksnprintf(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* panic.c */
__attribute__((noreturn)) void kpanic(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
void panic_begin(const char *title);
__attribute__((noreturn)) void halt_forever(void);

/* gdt.c / idt.c */
void gdt_init(void);
void idt_init(void);

/* port I/O */
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline void outw(uint16_t port, uint16_t v) {
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline void outl(uint16_t port, uint32_t v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* string.c */
void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
void gui_run(struct limine_framebuffer *fb);
