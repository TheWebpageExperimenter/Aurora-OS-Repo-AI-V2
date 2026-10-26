#include "kernel.h"

__attribute__((noreturn)) void halt_forever(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

/* Switch the console to the red panic look and print a banner. */
void panic_begin(const char *title) {
    console_set_colors(console_rgb(255, 255, 255), console_rgb(150, 20, 30));
    console_clear();
    kprintf("*** AURORA OS KERNEL PANIC ***\n\n%s\n\n", title);
}

__attribute__((noreturn)) void kpanic(const char *fmt, ...) {
    __asm__ volatile ("cli");
    panic_begin("Unrecoverable kernel error");
    va_list ap; va_start(ap, fmt); kvprintf(fmt, ap); va_end(ap);
    kprintf("\n\nSystem halted.\n");
    halt_forever();
}
