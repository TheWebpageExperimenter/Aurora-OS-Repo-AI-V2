/*
 * Aurora OS Alpha 1.0.0
 * GUI shell on top of GDT/TSS, IDT + panic screen, physical memory manager, framebuffer console.
 */
#include "kernel.h"
#include "limine.h"
#include "pmm.h"
#include "timeutil.h"
#include "video.h"
#include "net.h"
#include "display.h"
#include "security_monitor.h"
#include "tls.h"
#include "platform_api.h"
#include "shell_api.h"

/* ---------- Limine requests ---------- */

/* Request bootloader metadata so startup diagnostics can identify the loader. */
__attribute__((used, section(".requests")))
static volatile struct limine_bootloader_info_request bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST, .revision = 0
};

__attribute__((used, section(".requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST, .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST, .revision = 0
};

__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

/* ---------- helpers ---------- */

static const char *memmap_type_name(uint64_t t) {
    switch (t) {
    case LIMINE_MEMMAP_USABLE:                 return "usable";
    case LIMINE_MEMMAP_RESERVED:               return "reserved";
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       return "ACPI reclaimable";
    case LIMINE_MEMMAP_ACPI_NVS:               return "ACPI NVS";
    case LIMINE_MEMMAP_BAD_MEMORY:             return "bad memory";
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "bootloader";
    case LIMINE_MEMMAP_KERNEL_AND_MODULES:     return "kernel";
    case LIMINE_MEMMAP_FRAMEBUFFER:            return "framebuffer";
    default:                                   return "unknown";
    }
}

static void ok(const char *msg) {
    uint32_t saved_fg = console_rgb(210, 220, 230);
    console_set_colors(console_rgb(90, 220, 190), console_rgb(14, 18, 34));
    kprintf("[ ok ] ");
    console_set_colors(saved_fg, console_rgb(14, 18, 34));
    kprintf("%s\n", msg);
}

#ifdef AURORA_FAULT_DIV
static void trigger_fault(void) {
    volatile int zero = 0;
    kprintf("Triggering divide-by-zero...\n");
    __asm__ volatile ("movl $1, %%eax; xorl %%edx, %%edx; divl %0" : : "r"(zero) : "eax", "edx");
}
#elif defined(AURORA_FAULT_PF)
static void trigger_fault(void) {
    kprintf("Triggering page fault (read from 0xdead0000)...\n");
    volatile uint64_t *p = (volatile uint64_t *)0xdead0000ULL;
    (void)*p;
}
#elif defined(AURORA_FAULT_UD)
static void trigger_fault(void) {
    kprintf("Triggering invalid opcode...\n");
    __asm__ volatile ("ud2");
}
#elif defined(AURORA_FAULT_DF)
static void trigger_fault(void) {
    kprintf("Triggering double fault (bad stack + int3)...\n");
    __asm__ volatile ("xorq %%rsp, %%rsp; int3" : : : "memory");
}
#else
static void trigger_fault(void) { }
#endif

/* ---------- kernel entry ---------- */

void kmain(void) {
    serial_init();

    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        serial_putc('!');
        halt_forever();
    }

    if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0) {
        struct limine_framebuffer *boot_fb = framebuffer_request.response->framebuffers[0];
        /* The bootloader maps only the original framebuffer extent. Ask Limine
           for 1920x1080 so that the full VRAM aperture is mapped, then switch to
           Aurora's 1280x720 default before the console or desktop starts. The
           video layer remembers the mapped capacity and guards every later mode. */
        video_init(boot_fb);
        if (boot_fb->width >= 1920 && boot_fb->height >= 1080)
            (void)video_set_mode(boot_fb, 1280, 720);
        console_init(boot_fb);
    }

    console_set_colors(console_rgb(110, 130, 255), console_rgb(14, 18, 34));
    kprintf("Aurora OS Alpha 1.0.11 - kernel API 2.2, shell API %u, level handler %u, TLS API 2.0, build 102720260630\n", aurora_shell_api_version(), aurora_shell_api_level());
    if (bootloader_info_request.response && bootloader_info_request.response->name &&
        bootloader_info_request.response->version) {
        kprintf("Bootloader: %s %s\n", bootloader_info_request.response->name,
                bootloader_info_request.response->version);
    } else {
        kprintf("Bootloader: Limine-compatible (metadata unavailable)\n");
    }
    console_set_colors(console_rgb(210, 220, 230), console_rgb(14, 18, 34));
    kprintf("-------------------------\n\n");

    gdt_init();
    ok("GDT and TSS loaded (IST1: double fault, IST2: NMI/#MC)");

    idt_init();
    ok("IDT loaded, exception handlers 0-31 installed");

    __asm__ volatile ("int3");          /* self-test: recoverable exception */
    ok("breakpoint exception returned cleanly");

    /* Initialize interrupt-state-preserving security telemetry only after IDT setup. */
    aurora_security_init();
    kprintf("Security API v%u: %s\n", aurora_security_api_version(),
            aurora_security_check_invariants() ? "audit ring invariants OK" : "audit ring invariant failure");
    if (!aurora_security_check_integrity())
        kprintf("[WARN] Kernel text integrity fingerprint unavailable or mismatched\n");
    else
        ok("kernel text fingerprint matches boot baseline (diagnostic, not signed secure boot)");
    struct aurora_tls_status_info tls_status;
    (void)aurora_tls_get_status(&tls_status);
    kprintf("TLS API v%u: %s\n", tls_status.api_version, aurora_tls_status_text());

    if (!memmap_request.response || !hhdm_request.response)
        kpanic("Bootloader did not provide required memory map / HHDM; check Limine protocol and kernel requests");

    struct limine_memmap_response *mm = memmap_request.response;
    kprintf("\nMemory map (%lu entries):\n", mm->entry_count);
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        kprintf("  %016lx - %016lx  %s\n", e->base, e->base + e->length,
                memmap_type_name(e->type));
    }
    kprintf("\n");

    pmm_init(mm, hhdm_request.response->offset);
    ok("physical memory manager initialised (reserved-page free guard enabled)");
    struct aurora_platform_info platform_info;
    if (aurora_platform_get_info(&platform_info)) {
        kprintf("Platform API v%u: capability mask 0x%016llx\n",
                platform_info.api_version, (unsigned long long)platform_info.capabilities);
        kprintf("       %s\n", aurora_platform_web_readiness());
    }
    aurora_net_init();
    kprintf("Network API v%u: %u supported virtual NIC(s) discovered\n",
            AURORA_NET_API_VERSION, (unsigned)({ struct aurora_net_info ni; aurora_net_get_info(&ni); ni.nic_count; }));
    kprintf("       %s\n", aurora_net_status());
    ok("PCI network-adapter discovery complete (packet transport not enabled)");
    bool have_framebuffer = framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0;
    aurora_display_init(have_framebuffer);
    struct aurora_display_info display_info;
    aurora_display_get_info(&display_info);
    kprintf("Display API v%u: %s (PCI %04x:%04x)\n", AURORA_DISPLAY_API_VERSION,
            aurora_display_kind_name(display_info.kind), display_info.vendor_id, display_info.device_id);
    kprintf("       %s\n", aurora_display_status());
    ok("virtual display detection complete (Limine framebuffer/software renderer active)");
    kprintf("       %lu MiB usable, %lu MiB free\n",
            pmm_total_bytes() >> 20, pmm_free_bytes() >> 20);

    /* PMM self-test: allocate, free, and check the page gets reused. */
    uint64_t a = pmm_alloc(), b = pmm_alloc(), c = pmm_alloc_zeroed();
    kprintf("       alloc: %lx %lx %lx\n", a, b, c);
    uint64_t before = pmm_free_bytes();
    pmm_free(b);
    uint64_t b2 = pmm_alloc();
    if (!a || !b || !c || b2 != b || pmm_free_bytes() != before)
        kpanic("PMM self-test failed (b=%lx b2=%lx)", b, b2);
    pmm_free(a); pmm_free(b2); pmm_free(c);
    ok("PMM self-test passed (alloc / free / reuse)");

    trigger_fault();

    ok("boot complete, starting desktop");
    time_init();
    if (framebuffer_request.response && framebuffer_request.response->framebuffer_count > 0)
        gui_run(framebuffer_request.response->framebuffers[0]);

    kprintf("\nGraphics unavailable, staying on the text console.\n");
    halt_forever();
}
