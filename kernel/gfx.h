#pragma once
#include "kernel.h"

struct limine_framebuffer;

#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
enum { FONT_S, FONT_M, FONT_L };               /* Spleen 6x12, 8x16, 12x24 */
#define CURSOR_W 12
#define CURSOR_H 19

bool gfx_init(struct limine_framebuffer *fb);
int  gfx_w(void);
int  gfx_h(void);

/* frame handling: draw into the back buffer, then present */
void gfx_frame_begin(void);                    /* back buffer = wallpaper */
void gfx_set_opacity(uint32_t opacity);         /* global primitive opacity, 0..255 */
void gfx_present(void);                        /* whole back buffer -> screen */
void gfx_restore(int x, int y, int w, int h);  /* back buffer rect -> screen */
void gfx_draw_cursor(int x, int y);            /* straight onto the screen */

/* clipping (absolute screen rect, always intersected with the screen) */
void gfx_clip(int x, int y, int w, int h);
void gfx_clip_reset(void);
void gfx_clip_get(int *x, int *y, int *w, int *h);

/* primitives; colour is 0xRRGGBB, alpha 0..255 */
void gfx_pixel(int x, int y, uint32_t c, uint32_t a);
void gfx_rect(int x, int y, int w, int h, uint32_t c, uint32_t a);
void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c, uint32_t a);
void gfx_rrect_grad(int x, int y, int w, int h, int r, uint32_t c0, uint32_t c1);
void gfx_rrect_stroke(int x, int y, int w, int h, int r, uint32_t c, uint32_t a);
void gfx_shadow(int x, int y, int w, int h, int r);
/* sub-pixel shapes: coordinates in 1/16 pixel */
void gfx_circle16(int cx, int cy, int r, uint32_t c, uint32_t a);
void gfx_ring16(int cx, int cy, int r, int thick, uint32_t c, uint32_t a, bool gap_top);
void gfx_line16(int x0, int y0, int x1, int y1, int thick, uint32_t c, uint32_t a);

/* text */
int  gfx_text(int x, int y, const char *s, uint32_t c, int font);      /* returns width */
int  gfx_text_a(int x, int y, const char *s, uint32_t c, uint32_t a, int font);
int  gfx_text_scaled(int x, int y, const char *s, uint32_t c, int font, int scale, bool bold);
int  gfx_text_w(const char *s, int font);
int  gfx_font_h(int font);
int  gfx_font_w(int font);
void gfx_glyph(int x, int y, char ch, uint32_t c, int font);

/* Wallpaper presets: 0..8; modes: fullscreen, fit, stretch, center, tile. */
void gfx_set_wallpaper(int preset, int mode);
int gfx_wallpaper_preset(void);
int gfx_wallpaper_mode(void);
