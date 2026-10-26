/* Runtime display mode switching for Bochs/QEMU-compatible VGA adapters.
 * Other adapters safely report that live mode changes are unavailable.
 */
#include "kernel.h"
#include "limine.h"
#include "video.h"

#define VBE_INDEX_PORT 0x01CE
#define VBE_DATA_PORT  0x01CF
#define VBE_ID         0x0000
#define VBE_XRES       0x0001
#define VBE_YRES       0x0002
#define VBE_BPP        0x0003
#define VBE_ENABLE     0x0004
#define VBE_VIRT_WIDTH 0x0006
#define VBE_X_OFFSET   0x0008
#define VBE_Y_OFFSET   0x0009
#define VBE_VIDEO_MEM  0x000A
#define VBE_LFB_ENABLE 0x0040
#define VBE_NOCLEARMEM 0x0080
#define VBE_ENABLED    0x0001
#define VBE_ID_MIN     0xB0C2
#define VBE_ID_MAX     0xB0C5

static inline void port_outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint16_t port_inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
/* Limine maps the framebuffer at boot for its initial pitch * height only.
 * Changing VBE registers does not enlarge that virtual mapping. Preserve the
 * original aperture size and never switch to a mode that would write beyond it. */
static uint64_t mapped_capacity_bytes;

void video_init(struct limine_framebuffer *fb) {
    if (!fb || mapped_capacity_bytes || !fb->pitch || !fb->height) return;
    if (fb->pitch > UINT64_MAX / fb->height) return;
    mapped_capacity_bytes = fb->pitch * fb->height;
}

static uint16_t vbe_read(uint16_t index) {
    port_outw(VBE_INDEX_PORT, index);
    return port_inw(VBE_DATA_PORT);
}
static void vbe_write(uint16_t index, uint16_t value) {
    port_outw(VBE_INDEX_PORT, index);
    port_outw(VBE_DATA_PORT, value);
}

static bool vbe_has_room(int width, int height) {
    uint16_t blocks = vbe_read(VBE_VIDEO_MEM); /* units of 64 KiB */
    if (!blocks) return true; /* older emulators may not implement this register */
    uint64_t need = (uint64_t)width * (uint64_t)height * 4u;
    return need <= (uint64_t)blocks * 65536u;
}

static bool raw_mode(int width, int height) {
    if (width < 640 || width > 4096 || height < 400 || height > 2160)
        return false;
    if (!vbe_has_room(width, height)) return false;

    vbe_write(VBE_ENABLE, 0);
    vbe_write(VBE_XRES, (uint16_t)width);
    vbe_write(VBE_YRES, (uint16_t)height);
    vbe_write(VBE_BPP, 32);
    vbe_write(VBE_VIRT_WIDTH, (uint16_t)width);
    vbe_write(VBE_X_OFFSET, 0);
    vbe_write(VBE_Y_OFFSET, 0);
    vbe_write(VBE_ENABLE, VBE_ENABLED | VBE_LFB_ENABLE | VBE_NOCLEARMEM);

    uint16_t got_w = vbe_read(VBE_XRES);
    uint16_t got_h = vbe_read(VBE_YRES);
    uint16_t got_bpp = vbe_read(VBE_BPP);
    uint16_t pitch_px = vbe_read(VBE_VIRT_WIDTH);
    return got_w == (uint16_t)width && got_h == (uint16_t)height &&
           got_bpp == 32 && pitch_px >= (uint16_t)width;
}

bool video_set_mode(struct limine_framebuffer *fb, int width, int height) {
    /* Only the Bochs/QEMU DISPI-compatible interface is implemented. Never
       poke these ports unless the adapter has advertised the expected ID. */
    if (!fb || fb->bpp != 32 || width <= 0 || height <= 0)
        return false;
    video_init(fb);

    /* Do the capacity check before touching hardware. Use a 64-bit product to
       avoid overflow from malformed dimensions. */
    uint64_t minimum_bytes = (uint64_t)width * (uint64_t)height * 4u;
    if (!mapped_capacity_bytes || minimum_bytes > mapped_capacity_bytes)
        return false;

    uint16_t id = vbe_read(VBE_ID);
    if (id < VBE_ID_MIN || id > VBE_ID_MAX)
        return false;

    uint64_t old_w = fb->width, old_h = fb->height, old_pitch = fb->pitch;
    uint16_t old_bpp = fb->bpp;
    uint8_t old_r = fb->red_mask_size, old_rs = fb->red_mask_shift;
    uint8_t old_g = fb->green_mask_size, old_gs = fb->green_mask_shift;
    uint8_t old_b = fb->blue_mask_size, old_bs = fb->blue_mask_shift;
    uint8_t old_mm = fb->memory_model;

    if (!raw_mode(width, height)) {
        /* Put the previous display mode back if the adapter rejected the request. */
        (void)raw_mode((int)old_w, (int)old_h);
        return false;
    }

    uint16_t pitch_px = vbe_read(VBE_VIRT_WIDTH);
    uint64_t actual_span = (uint64_t)pitch_px * 4u * (uint64_t)height;
    if (pitch_px < (uint16_t)width || actual_span > mapped_capacity_bytes) {
        /* Restore the actual old hardware mode before returning an error. */
        (void)raw_mode((int)old_w, (int)old_h);
        return false;
    }

    fb->width = (uint64_t)width;
    fb->height = (uint64_t)height;
    fb->pitch = (uint64_t)pitch_px * 4u;
    fb->bpp = 32;
    fb->memory_model = LIMINE_FRAMEBUFFER_RGB;
    fb->red_mask_size = 8;   fb->red_mask_shift = 16;
    fb->green_mask_size = 8; fb->green_mask_shift = 8;
    fb->blue_mask_size = 8;  fb->blue_mask_shift = 0;

    /* Defensive sanity check: retain a valid old description on malformed readback. */
    if (fb->pitch < fb->width * 4u || fb->pitch > fb->width * 8u) {
        (void)raw_mode((int)old_w, (int)old_h);
        fb->width = old_w; fb->height = old_h; fb->pitch = old_pitch; fb->bpp = old_bpp;
        fb->red_mask_size = old_r; fb->red_mask_shift = old_rs;
        fb->green_mask_size = old_g; fb->green_mask_shift = old_gs;
        fb->blue_mask_size = old_b; fb->blue_mask_shift = old_bs;
        fb->memory_model = old_mm;
        return false;
    }
    return true;
}
