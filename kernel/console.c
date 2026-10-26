/* Framebuffer text console: 8x8 bitmap font drawn at 2x scale (16x16 cells). */
#include "kernel.h"
#include "limine.h"
#include "font8x8_basic.h"

#define SCALE  2
#define CELL_W (8 * SCALE)
#define CELL_H (8 * SCALE + 2)   /* 2px line gap */
#define MARGIN 16

static struct limine_framebuffer *fb;
static uint64_t cols, rows, cx, cy;
static uint32_t fg_color, bg_color;

uint32_t console_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!fb) return 0;
    return ((uint32_t)r << fb->red_mask_shift)
         | ((uint32_t)g << fb->green_mask_shift)
         | ((uint32_t)b << fb->blue_mask_shift);
}

static inline uint32_t *row_ptr(uint64_t y) {
    return (uint32_t *)((uint8_t *)fb->address + y * fb->pitch);
}

static void fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t c) {
    for (uint64_t j = 0; j < h; j++) {
        uint32_t *p = row_ptr(y + j) + x;
        for (uint64_t i = 0; i < w; i++) p[i] = c;
    }
}

bool console_init(struct limine_framebuffer *f) {
    if (!f || f->bpp != 32) return false;
    fb = f;
    cols = (fb->width  - 2 * MARGIN) / CELL_W;
    rows = (fb->height - 2 * MARGIN) / CELL_H;
    cx = cy = 0;
    fg_color = console_rgb(210, 220, 230);
    bg_color = console_rgb(14, 18, 34);
    console_clear();
    return true;
}

void console_set_colors(uint32_t fg, uint32_t bg) { fg_color = fg; bg_color = bg; }

void console_clear(void) {
    if (!fb) return;
    fill_rect(0, 0, fb->width, fb->height, bg_color);
    cx = cy = 0;
}

static void draw_glyph(uint64_t col, uint64_t row, unsigned char ch) {
    if (ch >= 128) ch = '?';
    const unsigned char *g = font8x8_basic[ch];
    uint64_t px = MARGIN + col * CELL_W, py = MARGIN + row * CELL_H;
    for (int gy = 0; gy < 8; gy++) {
        for (int sy = 0; sy < SCALE; sy++) {
            uint32_t *p = row_ptr(py + gy * SCALE + sy) + px;
            for (int gx = 0; gx < 8; gx++) {
                uint32_t c = (g[gy] >> gx) & 1 ? fg_color : bg_color;
                for (int sx = 0; sx < SCALE; sx++) p[gx * SCALE + sx] = c;
            }
        }
    }
}

static void scroll(void) {
    uint64_t line_bytes = CELL_H * fb->pitch;
    uint8_t *base = (uint8_t *)fb->address + MARGIN * fb->pitch;
    memmove(base, base + line_bytes, (rows - 1) * line_bytes);
    fill_rect(0, MARGIN + (rows - 1) * CELL_H, fb->width, CELL_H, bg_color);
}

void console_putc(char c) {
    if (!fb) return;
    switch (c) {
    case '\n': cx = 0; cy++; break;
    case '\r': cx = 0; break;
    case '\t': cx = (cx + 4) & ~3ULL; break;
    case '\b': if (cx) cx--; break;
    default:
        if (cx >= cols) { cx = 0; cy++; }
        if (cy >= rows) { scroll(); cy = rows - 1; }
        draw_glyph(cx, cy, (unsigned char)c);
        cx++;
        return;
    }
    if (cx >= cols) { cx = 0; cy++; }
    if (cy >= rows) { scroll(); cy = rows - 1; }
}
