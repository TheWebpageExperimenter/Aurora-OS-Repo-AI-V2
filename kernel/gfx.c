/*
 * Software 2D renderer: 32-bit XRGB back buffer + cached wallpaper, anti-aliased
 * rounded rects / circles / lines (integer math only, the kernel has no FPU/SSE),
 * alpha blending and an embedded TrueType-derived antialiased glyph atlas.
 */
#include "gfx.h"
#include "limine.h"
#include "pmm.h"
#include "font_ttf.h"
#include "wallpapers.h"

static struct limine_framebuffer *fb;
static uint32_t *back, *wall;
static uint64_t back_phys, wall_phys, buffer_pages;
static int W, H;
static int wallpaper_preset = 0, wallpaper_mode = 0;
static int cx0, cy0, cx1, cy1;           /* clip rect, x1/y1 exclusive */
static bool fast_fmt;                    /* screen is plain XRGB8888 */
static uint32_t draw_opacity = 255;

void gfx_set_opacity(uint32_t opacity) { draw_opacity = opacity > 255 ? 255 : opacity; }

int gfx_w(void) { return W; }
int gfx_h(void) { return H; }

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int iabs(int a) { return a < 0 ? -a : a; }

static inline uint32_t mix(uint32_t d, uint32_t s, uint32_t a) {
    if (a >= 255) return s;
    if (a == 0) return d;
    uint32_t ia = 255 - a;
    uint32_t r = (((d >> 16) & 255) * ia + ((s >> 16) & 255) * a + 127) / 255;
    uint32_t g = (((d >> 8) & 255) * ia + ((s >> 8) & 255) * a + 127) / 255;
    uint32_t b = ((d & 255) * ia + (s & 255) * a + 127) / 255;
    return (r << 16) | (g << 8) | b;
}

static inline void blend(int x, int y, uint32_t c, uint32_t a) {
    if (x < cx0 || y < cy0 || x >= cx1 || y >= cy1) return;
    a = (a * draw_opacity + 127) / 255;
    if (!a) return;
    uint32_t *p = &back[y * W + x];
    *p = mix(*p, c, a);
}

void gfx_pixel(int x, int y, uint32_t c, uint32_t a) { blend(x, y, c, a); }

/* ---------- clip ---------- */

void gfx_clip_reset(void) { cx0 = 0; cy0 = 0; cx1 = W; cy1 = H; }
void gfx_clip(int x, int y, int w, int h) {
    /* Use wide intermediates: caller-supplied x+w/y+h must not signed-overflow. */
    int64_t rx1 = (int64_t)x + (int64_t)w;
    int64_t ry1 = (int64_t)y + (int64_t)h;
    if (w <= 0 || h <= 0) { cx0 = cy0 = cx1 = cy1 = 0; return; }
    cx0 = x < 0 ? 0 : (x > W ? W : x);
    cy0 = y < 0 ? 0 : (y > H ? H : y);
    cx1 = rx1 < 0 ? 0 : (rx1 > W ? W : (int)rx1);
    cy1 = ry1 < 0 ? 0 : (ry1 > H ? H : (int)ry1);
    if (cx1 < cx0) cx1 = cx0;
    if (cy1 < cy0) cy1 = cy0;
}
void gfx_clip_get(int *x, int *y, int *w, int *h) {
    *x = cx0; *y = cy0; *w = cx1 - cx0; *h = cy1 - cy0;
}

/* ---------- presenting ---------- */

static inline uint32_t pack(uint32_t c) {
    return (((c >> 16) & 255) << fb->red_mask_shift)
         | (((c >> 8) & 255) << fb->green_mask_shift)
         | ((c & 255) << fb->blue_mask_shift);
}

static void blit_rect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0 || !fb || !fb->address || !back || W <= 0 || H <= 0) return;
    int64_t rx1 = (int64_t)x + (int64_t)w, ry1 = (int64_t)y + (int64_t)h;
    int x0 = x < 0 ? 0 : (x > W ? W : x);
    int y0 = y < 0 ? 0 : (y > H ? H : y);
    int x1 = rx1 < 0 ? 0 : (rx1 > W ? W : (int)rx1);
    int y1 = ry1 < 0 ? 0 : (ry1 > H ? H : (int)ry1);
    if (x1 <= x0 || y1 <= y0) return;
    for (int j = y0; j < y1; j++) {
        uint32_t *src = back + j * W + x0;
        uint32_t *dst = (uint32_t *)((uint8_t *)fb->address + (uint64_t)j * fb->pitch) + x0;
        if (fast_fmt) memcpy(dst, src, (size_t)(x1 - x0) * 4);
        else for (int i = 0; i < x1 - x0; i++) dst[i] = pack(src[i]);
    }
}

void gfx_present(void) { blit_rect(0, 0, W, H); }
void gfx_restore(int x, int y, int w, int h) { blit_rect(x, y, w, h); }
void gfx_frame_begin(void) { draw_opacity = 255; memcpy(back, wall, (size_t)W * H * 4); gfx_clip_reset(); }

static const char *cursor_art[CURSOR_H] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ",
    "X....X      ", "X.....X     ", "X......X    ", "X.......X   ", "X........X  ",
    "X.....XXXXX ", "X..X..X     ", "X.X X..X    ", "XX  X..X    ", "X    X..X   ",
    "     X..X   ", "      X..X  ", "      X..X  ", "       XX   "
};

void gfx_draw_cursor(int x, int y) {
    uint32_t black = pack(RGB(10, 10, 14)), white = pack(RGB(255, 255, 255));
    for (int j = 0; j < CURSOR_H; j++) {
        if (y + j < 0 || y + j >= H) continue;
        uint32_t *row = (uint32_t *)((uint8_t *)fb->address + (uint64_t)(y + j) * fb->pitch);
        for (int i = 0; i < CURSOR_W; i++) {
            char ch = cursor_art[j][i];
            if (ch == ' ' || x + i < 0 || x + i >= W) continue;
            row[x + i] = ch == 'X' ? black : white;
        }
    }
}

/* ---------- filled shapes ---------- */

static void fill_span(int y, int xa, int xb, uint32_t c, uint32_t a) {
    if (y < cy0 || y >= cy1) return;
    xa = imax(xa, cx0); xb = imin(xb, cx1);
    uint32_t *p = back + y * W;
    if (a >= 255) { for (int x = xa; x < xb; x++) p[x] = c; }
    else if (a) { for (int x = xa; x < xb; x++) p[x] = mix(p[x], c, a); }
}

void gfx_rect(int x, int y, int w, int h, uint32_t c, uint32_t a) {
    if (w <= 0 || h <= 0) return;
    /* Clip before iterating, so off-screen or malformed rectangles stay bounded. */
    int64_t rx1 = (int64_t)x + (int64_t)w, ry1 = (int64_t)y + (int64_t)h;
    int x0 = x < cx0 ? cx0 : x;
    int y0 = y < cy0 ? cy0 : y;
    int x1 = rx1 > cx1 ? cx1 : (rx1 < INT32_MIN ? cx1 : (int)rx1);
    int y1 = ry1 > cy1 ? cy1 : (ry1 < INT32_MIN ? cy1 : (int)ry1);
    if (x1 <= x0 || y1 <= y0) return;
    for (int j = y0; j < y1; j++) fill_span(j, x0, x1, c, a);
}

/* coverage (0..255) of pixel (i,j) inside a w*h rounded rect of radius r */
static int rrect_cov(int i, int j, int w, int h, int r) {
    if (i < 0 || j < 0 || i >= w || j >= h) return 0;
    int dx = 0, dy = 0;
    if (j < r) dy = r - j; else if (j >= h - r) dy = j - (h - r) + 1;
    if (i < r) dx = r - i; else if (i >= w - r) dx = i - (w - r) + 1;
    if (!dx || !dy) return 255;
    int ddx = 2 * dx - 1, ddy = 2 * dy - 1, R = 2 * r;
    int cv = 128 + ((R * R - (ddx * ddx + ddy * ddy)) * 255) / (4 * R);
    return cv < 0 ? 0 : cv > 255 ? 255 : cv;
}

static uint32_t lerp_col(uint32_t c0, uint32_t c1, int t) {      /* t 0..255 */
    return mix(c0, c1, (uint32_t)t);
}

static void rrect_core(int x, int y, int w, int h, int r, uint32_t c0, uint32_t c1,
                       uint32_t alpha, bool grad) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < cy0 || py >= cy1) continue;
        bool edge_row = (j < r || j >= h - r);
        int ia = imax(0, cx0 - x), ib = imin(w, cx1 - x);
        for (int i = ia; i < ib; i++) {
            if (!edge_row && !grad) {            /* bulk fast path */
                int run_end = ib;
                fill_span(py, x + i, x + run_end, c0, alpha);
                break;
            }
            int cov = edge_row ? rrect_cov(i, j, w, h, r) : 255;
            if (i < r || i >= w - r) cov = rrect_cov(i, j, w, h, r);
            if (!cov) continue;
            uint32_t col = c0;
            if (grad) col = lerp_col(c0, c1, ((i * 255) / w + (j * 255) / h) / 2);
            blend(x + i, py, col, (uint32_t)cov * alpha / 255);
        }
    }
}

void gfx_rrect(int x, int y, int w, int h, int r, uint32_t c, uint32_t a) {
    rrect_core(x, y, w, h, r, c, c, a, false);
}
void gfx_rrect_grad(int x, int y, int w, int h, int r, uint32_t c0, uint32_t c1) {
    rrect_core(x, y, w, h, r, c0, c1, 255, true);
}

void gfx_rrect_stroke(int x, int y, int w, int h, int r, uint32_t c, uint32_t a) {
    if (w < 3 || h < 3) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < cy0 || py >= cy1) continue;
        if (j <= r || j >= h - 1 - r) {
            for (int i = 0; i < w; i++) {
                int co = rrect_cov(i, j, w, h, r);
                int ci = rrect_cov(i - 1, j - 1, w - 2, h - 2, r > 0 ? r - 1 : 0);
                int cv = co - ci;
                if (cv > 0) blend(x + i, py, c, (uint32_t)cv * a / 255);
            }
        } else {
            blend(x, py, c, a);
            blend(x + w - 1, py, c, a);
        }
    }
}

void gfx_shadow(int x, int y, int w, int h, int r) {
    for (int k = 1; k <= 9; k++) {
        int d = 10 - k;
        uint32_t a = (uint32_t)(d * d * 26) / 100;       /* quadratic falloff */
        if (!a) continue;
        gfx_rrect_stroke(x - k, y - k + 5, w + 2 * k, h + 2 * k, r + k, RGB(0, 0, 0), a);
    }
}

/* ---------- sub-pixel circles / rings / lines ---------- */

static inline int cov_from(int64_t r2, int64_t d2, int64_t r16) {
    int64_t v = 128 + ((r2 - d2) * 255) / (2 * r16 * 16);
    return v < 0 ? 0 : v > 255 ? 255 : (int)v;
}

void gfx_circle16(int cx, int cy, int r, uint32_t c, uint32_t a) {
    if (r <= 0) return;
    int x0 = (cx - r) / 16 - 1, x1 = (cx + r) / 16 + 2;
    int y0 = (cy - r) / 16 - 1, y1 = (cy + r) / 16 + 2;
    int64_t r2 = (int64_t)r * r;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            int64_t dx = i * 16 + 8 - cx, dy = j * 16 + 8 - cy;
            int cv = cov_from(r2, dx * dx + dy * dy, r);
            if (cv) blend(i, j, c, (uint32_t)cv * a / 255);
        }
}

void gfx_ring16(int cx, int cy, int r, int thick, uint32_t c, uint32_t a, bool gap_top) {
    int ro = r + thick / 2, ri = r - thick / 2;
    if (ri < 1) ri = 1;
    int x0 = (cx - ro) / 16 - 1, x1 = (cx + ro) / 16 + 2;
    int y0 = (cy - ro) / 16 - 1, y1 = (cy + ro) / 16 + 2;
    int64_t ro2 = (int64_t)ro * ro, ri2 = (int64_t)ri * ri;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            int64_t dx = i * 16 + 8 - cx, dy = j * 16 + 8 - cy;
            int64_t d2 = dx * dx + dy * dy;
            if (gap_top && dy < 0 && iabs((int)dx) < thick) continue;
            int cv = cov_from(ro2, d2, ro) - cov_from(ri2, d2, ri);
            if (cv > 0) blend(i, j, c, (uint32_t)cv * a / 255);
        }
}

void gfx_line16(int ax, int ay, int bx, int by, int thick, uint32_t c, uint32_t a) {
    int64_t rr = thick / 2;
    if (rr < 1) rr = 1;
    int pad = (int)rr + 16;
    int x0 = (imin(ax, bx) - pad) / 16 - 1, x1 = (imax(ax, bx) + pad) / 16 + 2;
    int y0 = (imin(ay, by) - pad) / 16 - 1, y1 = (imax(ay, by) + pad) / 16 + 2;
    int64_t abx = bx - ax, aby = by - ay, den = abx * abx + aby * aby, r2 = rr * rr;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            int64_t px = i * 16 + 8, py = j * 16 + 8;
            int64_t num = (px - ax) * abx + (py - ay) * aby, qx, qy;
            if (den == 0 || num <= 0) { qx = ax; qy = ay; }
            else if (num >= den)      { qx = bx; qy = by; }
            else { qx = ax + abx * num / den; qy = ay + aby * num / den; }
            int64_t dx = px - qx, dy = py - qy;
            int cv = cov_from(r2, dx * dx + dy * dy, rr);
            if (cv) blend(i, j, c, (uint32_t)cv * a / 255);
        }
}

/* ---------- TrueType-derived antialiased text ----------
 * The kernel has no VFS yet, so the bundled Go Medium TTF is rasterized during
 * development into a compact 2-bit atlas. The original .ttf files ship in ISO
 * assets for future runtime font loading; no bitmap/Spleen font is used here.
 */
struct font_atlas {
    int max_w, h, avg_w, bytes_per_glyph;
    const uint8_t *advance;
    const uint8_t *bitmap;
};
static const struct font_atlas fonts[3] = {
    { TTF_S_W, TTF_S_LINE_H, 7, TTF_S_BYTES_PER_GLYPH, ttf_s_adv, &ttf_s_bitmap[0][0] },
    { TTF_M_W, TTF_M_LINE_H, 9, TTF_M_BYTES_PER_GLYPH, ttf_m_adv, &ttf_m_bitmap[0][0] },
    { TTF_L_W, TTF_L_LINE_H, 14, TTF_L_BYTES_PER_GLYPH, ttf_l_adv, &ttf_l_bitmap[0][0] },
};

static unsigned int font_index(char ch) {
    unsigned char u = (unsigned char)ch;
    return (u >= 32 && u <= 126) ? (unsigned int)(u - 32) : (unsigned int)('?' - 32);
}
static unsigned int font_advance(const char *s, int f) {
    unsigned int w = 0;
    while (*s) w += fonts[f].advance[font_index(*s++)];
    return w;
}
int gfx_font_h(int f) { if (f < 0 || f > 2) f = FONT_M; return fonts[f].h; }
int gfx_font_w(int f) { if (f < 0 || f > 2) f = FONT_M; return fonts[f].avg_w; }
int gfx_text_w(const char *s, int f) { if (f < 0 || f > 2) f = FONT_M; return (int)font_advance(s, f); }

static void glyph_a(int x, int y, char ch, uint32_t c, uint32_t a, int f) {
    if (f < 0 || f > 2) f = FONT_M;
    unsigned int gi = font_index(ch);
    const struct font_atlas *ft = &fonts[f];
    if (x + ft->max_w <= cx0 || x >= cx1 || y + ft->h <= cy0 || y >= cy1) return;
    const uint8_t *g = ft->bitmap + (size_t)gi * ft->bytes_per_glyph;
    for (int j = 0; j < ft->h; j++) {
        if (y + j < cy0 || y + j >= cy1) continue;
        for (int i = 0; i < ft->max_w; i++) {
            int px = x + i;
            if (px < cx0 || px >= cx1) continue;
            size_t pos = (size_t)j * ft->max_w + (size_t)i;
            uint8_t packed = g[pos >> 2];
            unsigned int shift = 6U - (unsigned int)(pos & 3U) * 2U;
            unsigned int cov = (packed >> shift) & 3U;
            if (cov) blend(px, y + j, c, a * cov / 3U);
        }
    }
}

void gfx_glyph(int x, int y, char ch, uint32_t c, int f) { glyph_a(x, y, ch, c, 255, f); }

int gfx_text_a(int x, int y, const char *s, uint32_t c, uint32_t a, int f) {
    if (f < 0 || f > 2) f = FONT_M;
    int x0 = x;
    for (; *s; s++) {
        unsigned int gi = font_index(*s);
        glyph_a(x, y, *s, c, a, f);
        x += fonts[f].advance[gi];
    }
    return x - x0;
}
int gfx_text(int x, int y, const char *s, uint32_t c, int f) { return gfx_text_a(x, y, s, c, 255, f); }

/* Nearest-neighbour glyph enlargement for high-impact UI text (e.g. lock clock).
   Clamp scale and use 64-bit coordinate checks to keep malformed values harmless. */
int gfx_text_scaled(int x, int y, const char *s, uint32_t c, int f, int scale, bool bold) {
    if (f < 0 || f > 2) f = FONT_M;
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    int start_x = x;
    const struct font_atlas *ft = &fonts[f];
    for (; *s; ++s) {
        unsigned int gi = font_index(*s);
        const uint8_t *g = ft->bitmap + (size_t)gi * ft->bytes_per_glyph;
        for (int sy = 0; sy < ft->h; ++sy) {
            int64_t dy0 = (int64_t)y + (int64_t)sy * scale;
            if (dy0 >= cy1 || dy0 + scale <= cy0) continue;
            for (int sx = 0; sx < ft->max_w; ++sx) {
                size_t pos = (size_t)sy * ft->max_w + (size_t)sx;
                unsigned int shift = 6U - (unsigned int)(pos & 3U) * 2U;
                unsigned int cov = (g[pos >> 2] >> shift) & 3U;
                if (!cov) continue;
                int64_t dx0 = (int64_t)x + (int64_t)sx * scale;
                int extra = bold ? 1 : 0;
                for (int yy = 0; yy < scale; ++yy) {
                    int64_t py = dy0 + yy;
                    if (py < cy0 || py >= cy1 || py < 0 || py >= H) continue;
                    for (int xx = 0; xx < scale + extra; ++xx) {
                        int64_t px = dx0 + xx;
                        if (px < cx0 || px >= cx1 || px < 0 || px >= W) continue;
                        blend((int)px, (int)py, c, cov * 255U / 3U);
                    }
                }
            }
        }
        x += (int)ft->advance[gi] * scale;
    }
    return x - start_x;
}

/* ---------- wallpaper ---------- */

static uint32_t rng = 0x2545F491;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int32_t dxt[4096];

static void blob(int cxp, int cyp, int rx, int ry, uint32_t col, int maxa) {
    int x0 = imax(0, cxp - rx), x1 = imin(W, cxp + rx);
    int y0 = imax(0, cyp - ry), y1 = imin(H, cyp + ry);
    for (int x = x0; x < x1; x++) {
        int64_t dx = x - cxp;
        dxt[x] = (int32_t)((dx * dx * 1024) / ((int64_t)rx * rx));
    }
    for (int y = y0; y < y1; y++) {
        int64_t dy = y - cyp;
        int dyt = (int)((dy * dy * 1024) / ((int64_t)ry * ry));
        for (int x = x0; x < x1; x++) {
            int d = dxt[x] + dyt;
            if (d >= 1024) continue;
            int t = 1024 - d;
            int f = (t * t) >> 10;
            uint32_t a = (uint32_t)(f * maxa) >> 10;
            wall[y * W + x] = mix(wall[y * W + x], col, a);
        }
    }
}

static void make_wallpaper(void) {
    if (wallpaper_preset == 0) {
        /* Aurora is the default full-screen landscape wallpaper. */
        for (int y = 0; y < H; y++) {
            int sy = (int)((uint64_t)y * AURORA_WALLPAPER_H / (uint32_t)H);
            for (int x = 0; x < W; x++) {
                int sx = (int)((uint64_t)x * AURORA_WALLPAPER_W / (uint32_t)W);
                uint16_t v = wallpaper_aurora[sy * AURORA_WALLPAPER_W + sx];
                uint32_t r = ((v >> 11) & 31) * 255 / 31;
                uint32_t g = ((v >> 5) & 63) * 255 / 63;
                uint32_t b = (v & 31) * 255 / 31;
                wall[y * W + x] = RGB(r, g, b);
            }
        }
        return;
    }
    uint32_t topc = wallpaper_preset == 1 ? RGB(8, 24, 48) : wallpaper_preset == 2 ? RGB(30, 8, 38) : wallpaper_preset == 3 ? RGB(8, 30, 28) : RGB(35, 19, 8);
    uint32_t botc = wallpaper_preset == 1 ? RGB(15, 70, 100) : wallpaper_preset == 2 ? RGB(92, 28, 76) : wallpaper_preset == 3 ? RGB(20, 92, 70) : RGB(115, 58, 18);
    for (int y = 0; y < H; y++) {
        uint32_t t = (uint32_t)(y * 255 / H);
        uint32_t c = mix(topc, botc, t);
        for (int x = 0; x < W; x++) wall[y * W + x] = c;
    }
    blob(W * 30 / 100, H * 30 / 100, W * 45 / 100, H * 20 / 100, wallpaper_preset == 1 ? RGB(60, 190, 255) : wallpaper_preset == 2 ? RGB(255, 110, 190) : wallpaper_preset == 3 ? RGB(40, 240, 170) : RGB(255, 178, 70), 120);
    blob(W * 62 / 100, H * 22 / 100, W * 40 / 100, H * 17 / 100, wallpaper_preset == 1 ? RGB(90, 220, 240) : wallpaper_preset == 2 ? RGB(255, 160, 80) : wallpaper_preset == 3 ? RGB(110, 255, 190) : RGB(255, 220, 100), 110);
    blob(W * 85 / 100, H * 38 / 100, W * 30 / 100, H * 24 / 100, wallpaper_preset == 1 ? RGB(30, 150, 190) : wallpaper_preset == 2 ? RGB(160, 70, 240) : wallpaper_preset == 3 ? RGB(20, 160, 120) : RGB(220, 100, 30), 90);
    blob(W * 18 / 100, H * 85 / 100, W * 38 / 100, H * 20 / 100, wallpaper_preset == 1 ? RGB(35, 110, 190) : wallpaper_preset == 2 ? RGB(210, 55, 120) : wallpaper_preset == 3 ? RGB(35, 180, 120) : RGB(210, 100, 35), 70);
    for (int i = 0; i < 220; i++) {
        int x = (int)(rnd() % (uint32_t)W), y = (int)(rnd() % (uint32_t)(H * 6 / 10));
        uint32_t a = 70 + rnd() % 150;
        wall[y * W + x] = mix(wall[y * W + x], RGB(255, 255, 255), a);
    }
}

void gfx_set_wallpaper(int preset, int mode) {
    if (preset < 0 || preset > 4 || mode < 0 || mode > 4) return;
    wallpaper_preset = preset; wallpaper_mode = mode;
    if (wall) make_wallpaper();
}
int gfx_wallpaper_preset(void) { return wallpaper_preset; }
int gfx_wallpaper_mode(void) { return wallpaper_mode; }

static void free_buffer_run(uint64_t phys, uint64_t pages) {
    if (!phys) return;
    for (uint64_t i = 0; i < pages; i++) pmm_free(phys + i * PAGE_SIZE);
}

bool gfx_init(struct limine_framebuffer *f) {
    if (!f || !f->address || f->bpp != 32 || f->width < 640 || f->width > 4096 ||
        f->height < 400 || f->height > 2160 || f->width > UINT64_MAX / 4u ||
        f->pitch < f->width * 4u || f->pitch > UINT64_MAX / f->height ||
        f->red_mask_size == 0 || f->green_mask_size == 0 || f->blue_mask_size == 0 ||
        f->red_mask_shift >= 32 || f->green_mask_shift >= 32 || f->blue_mask_shift >= 32 ||
        (uint64_t)f->red_mask_shift + f->red_mask_size > 32 ||
        (uint64_t)f->green_mask_shift + f->green_mask_size > 32 ||
        (uint64_t)f->blue_mask_shift + f->blue_mask_size > 32)
        return false;

    int new_w = (int)f->width, new_h = (int)f->height;
    uint64_t pixels = (uint64_t)new_w * (uint64_t)new_h;
    if (pixels > (UINT64_MAX - (PAGE_SIZE - 1)) / 4u) return false;
    uint64_t bytes = pixels * 4u;
    uint64_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (!pages || pages > UINT64_MAX / PAGE_SIZE) return false;
    uint64_t pb = pmm_alloc_contiguous(pages);
    if (!pb) return false;
    uint64_t pw = pmm_alloc_contiguous(pages);
    if (!pw) { free_buffer_run(pb, pages); return false; }

    /* Allocate both replacements first, so a failed resize preserves the old UI. */
    free_buffer_run(back_phys, buffer_pages);
    free_buffer_run(wall_phys, buffer_pages);
    fb = f; W = new_w; H = new_h;
    back_phys = pb; wall_phys = pw; buffer_pages = pages;
    back = phys_to_virt(pb); wall = phys_to_virt(pw);
    fast_fmt = f->red_mask_shift == 16 && f->green_mask_shift == 8 && f->blue_mask_shift == 0;
    gfx_clip_reset();
    make_wallpaper();
    return true;
}
