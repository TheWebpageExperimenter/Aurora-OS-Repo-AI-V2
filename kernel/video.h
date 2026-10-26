#pragma once
#include <stdbool.h>
#include <stdint.h>

struct limine_framebuffer;

/* Capture the maximum framebuffer aperture mapped by the bootloader. Call this
 * before changing the adapter mode so higher resolutions cannot access unmapped VRAM. */
void video_init(struct limine_framebuffer *fb);

/* Attempt a live 32-bit mode change through the Bochs/QEMU VBE DISPI interface.
 * Returns false when the adapter does not expose it, rejects a mode, or the mode
 * would exceed the framebuffer aperture mapped at boot. */
bool video_set_mode(struct limine_framebuffer *fb, int width, int height);
