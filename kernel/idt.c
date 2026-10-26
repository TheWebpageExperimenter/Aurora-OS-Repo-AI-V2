#include "kernel.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idtr { uint16_t limit; uint64_t base; } __attribute__((packed));

/* Layout must match the push order in isr.S. */
struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};

extern uint64_t isr_stub_table[32];
static struct idt_entry idt[256] __attribute__((aligned(16)));

static const char *exception_names[32] = {
    "Divide Error (#DE)", "Debug (#DB)", "Non-Maskable Interrupt",
    "Breakpoint (#BP)", "Overflow (#OF)", "Bound Range Exceeded (#BR)",
    "Invalid Opcode (#UD)", "Device Not Available (#NM)", "Double Fault (#DF)",
    "Coprocessor Segment Overrun", "Invalid TSS (#TS)", "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)", "General Protection Fault (#GP)", "Page Fault (#PF)",
    "Reserved", "x87 Floating-Point (#MF)", "Alignment Check (#AC)",
    "Machine Check (#MC)", "SIMD Floating-Point (#XM)", "Virtualization (#VE)",
    "Control Protection (#CP)", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Hypervisor Injection (#HV)", "VMM Communication (#VC)",
    "Security (#SX)", "Reserved"
};

static void set_gate(int v, uint64_t handler, uint8_t ist) {
    idt[v].offset_low  = handler & 0xFFFF;
    idt[v].selector    = 0x08;
    idt[v].ist         = ist;
    idt[v].type_attr   = 0x8E;           /* present, ring 0, 64-bit interrupt gate */
    idt[v].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[v].offset_high = (uint32_t)(handler >> 32);
    idt[v].zero        = 0;
}

void idt_init(void) {
    for (int v = 0; v < 32; v++) {
        uint8_t ist = 0;
        if (v == 8)                ist = 1;   /* double fault   -> IST1 */
        if (v == 2 || v == 18)     ist = 2;   /* NMI, #MC       -> IST2 */
        set_gate(v, isr_stub_table[v], ist);
    }
    struct idtr r = { sizeof(idt) - 1, (uint64_t)&idt };
    __asm__ volatile ("lidt %0" : : "m"(r));
}

static void dump_regs(const struct regs *r) {
    kprintf("RIP=%016lx  CS=%04lx  RFLAGS=%016lx\n", r->rip, r->cs, r->rflags);
    kprintf("RSP=%016lx  SS=%04lx  ERR=%016lx\n\n", r->rsp, r->ss, r->error);
    kprintf("RAX=%016lx  RBX=%016lx\n", r->rax, r->rbx);
    kprintf("RCX=%016lx  RDX=%016lx\n", r->rcx, r->rdx);
    kprintf("RSI=%016lx  RDI=%016lx\n", r->rsi, r->rdi);
    kprintf("RBP=%016lx  R8 =%016lx\n", r->rbp, r->r8);
    kprintf("R9 =%016lx  R10=%016lx\n", r->r9, r->r10);
    kprintf("R11=%016lx  R12=%016lx\n", r->r11, r->r12);
    kprintf("R13=%016lx  R14=%016lx\n", r->r13, r->r14);
    kprintf("R15=%016lx\n", r->r15);
}

void exception_handler(struct regs *r) {
    /* A breakpoint is recoverable: log it and resume. */
    if (r->vector == 3) {
        kprintf("[ !! ] breakpoint at %p (resuming)\n", (void *)r->rip);
        return;
    }

    uint64_t cr2, cr3;
    __asm__ volatile ("movq %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("movq %%cr3, %0" : "=r"(cr3));

    __asm__ volatile ("cli");
    panic_begin(r->vector < 32 ? exception_names[r->vector] : "Unknown exception");
    kprintf("Vector %lu\n", r->vector);
    if (r->vector == 14) {
        kprintf("Faulting address (CR2): %016lx\n", cr2);
        kprintf("Cause: %s, %s, %s%s\n",
                (r->error & 1) ? "protection violation" : "page not present",
                (r->error & 2) ? "write" : "read",
                (r->error & 4) ? "user mode" : "kernel mode",
                (r->error & 16) ? ", instruction fetch" : "");
    }
    kprintf("\n");
    dump_regs(r);
    kprintf("\nCR2=%016lx  CR3=%016lx\n", cr2, cr3);
    kprintf("\nSystem halted.\n");
    halt_forever();
}
