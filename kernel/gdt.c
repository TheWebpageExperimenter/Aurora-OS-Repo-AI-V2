/*
 * GDT + TSS.
 * Selector layout (chosen so SYSCALL/SYSRET can be used later):
 *   0x00 null | 0x08 kernel code | 0x10 kernel data
 *   0x18 user data | 0x20 user code | 0x28 TSS (16 bytes)
 */
#include "kernel.h"

struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

struct gdt_table {
    uint64_t entries[5];       /* null, kcode, kdata, udata, ucode */
    uint64_t tss_low, tss_high;
} __attribute__((packed, aligned(16)));

struct gdtr { uint16_t limit; uint64_t base; } __attribute__((packed));

#define IST_STACK_SIZE (16 * 1024)

static struct tss tss;
static struct gdt_table gdt;
static uint8_t ist1_stack[IST_STACK_SIZE] __attribute__((aligned(16)));  /* double fault */
static uint8_t ist2_stack[IST_STACK_SIZE] __attribute__((aligned(16)));  /* NMI / machine check */
static uint8_t rsp0_stack[IST_STACK_SIZE] __attribute__((aligned(16)));  /* future ring3 -> ring0 */

void gdt_init(void) {
    gdt.entries[0] = 0;
    gdt.entries[1] = 0x00AF9A000000FFFFULL;  /* kernel code, 64-bit */
    gdt.entries[2] = 0x00CF92000000FFFFULL;  /* kernel data */
    gdt.entries[3] = 0x00CFF2000000FFFFULL;  /* user data   */
    gdt.entries[4] = 0x00AFFA000000FFFFULL;  /* user code, 64-bit */

    tss.rsp[0]      = (uint64_t)&rsp0_stack[IST_STACK_SIZE];
    tss.ist[0]      = (uint64_t)&ist1_stack[IST_STACK_SIZE];
    tss.ist[1]      = (uint64_t)&ist2_stack[IST_STACK_SIZE];
    tss.iopb_offset = sizeof(tss);

    uint64_t base = (uint64_t)&tss, limit = sizeof(tss) - 1;
    gdt.tss_low  = (limit & 0xFFFF)
                 | ((base & 0xFFFFFF) << 16)
                 | (0x89ULL << 40)                    /* present, 64-bit TSS (available) */
                 | (((limit >> 16) & 0xF) << 48)
                 | (((base >> 24) & 0xFF) << 56);
    gdt.tss_high = base >> 32;

    struct gdtr r = { sizeof(gdt) - 1, (uint64_t)&gdt };
    __asm__ volatile (
        "lgdt %0\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "xorw %%ax, %%ax\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        : : "m"(r) : "rax", "memory");
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)0x28));
}
