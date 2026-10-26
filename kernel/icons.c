/* Procedural vector icons on a 24x24 design grid, scaled to any size. */
#include "icons.h"
#include "gfx.h"

static int ix, iy, isz;

/* 24-grid (in half units, so 0..48) -> 1/16 pixel */
static int F(int base, int v)   { return base * 16 + (v * isz * 16) / 48; }
static int FX(int v)            { return F(ix, v); }
static int FY(int v)            { return F(iy, v); }
static int TH(int tenths)       { int t = (tenths * isz * 16) / 240; return t < 21 ? 21 : t; }
static int LEN(int v)           { return (v * isz * 16) / 48; }

/* rounded rect in 24-grid whole units */
static void rr(int u0, int v0, int u1, int v1, int r, uint32_t c) {
    int x0 = ix + (u0 * isz + 12) / 24, y0 = iy + (v0 * isz + 12) / 24;
    int x1 = ix + (u1 * isz + 12) / 24, y1 = iy + (v1 * isz + 12) / 24;
    gfx_rrect(x0, y0, x1 - x0, y1 - y0, (r * isz + 12) / 24, c, 255);
}

static void seg(int a, int b, int c, int d, int tenths, uint32_t col) {   /* half-unit coords */
    gfx_line16(FX(a), FY(b), FX(c), FY(d), TH(tenths), col, 255);
}

void icon_draw(int id, int x, int y, int size, uint32_t col) {
    ix = x; iy = y; isz = size;
    switch (id) {
    case ICON_LOGO:
        gfx_rrect_grad(x, y, size, size, (6 * size + 12) / 24, RGB(70, 225, 190), RGB(150, 110, 255));
        seg(14, 36, 24, 12, 26, RGB(255, 255, 255));
        seg(24, 12, 34, 36, 26, RGB(255, 255, 255));
        seg(18, 28, 30, 28, 22, RGB(255, 255, 255));
        break;
    case ICON_SEARCH:
        gfx_ring16(FX(21), FY(21), LEN(13), TH(22), col, 255, false);
        seg(31, 31, 41, 41, 26, col);
        break;
    case ICON_FILES:
        rr(2, 4, 11, 10, 2, RGB(226, 150, 40));
        rr(2, 7, 22, 20, 3, RGB(255, 196, 80));
        rr(2, 11, 22, 20, 3, RGB(255, 214, 118));
        break;
    case ICON_NOTES:
        rr(4, 2, 20, 22, 3, RGB(255, 228, 120));
        seg(16, 18, 32, 18, 16, RGB(150, 122, 40));
        seg(16, 26, 32, 26, 16, RGB(150, 122, 40));
        seg(16, 34, 26, 34, 16, RGB(150, 122, 40));
        break;
    case ICON_MONITOR:
        rr(2, 3, 22, 21, 4, RGB(96, 108, 140));
        rr(3, 4, 21, 20, 3, RGB(30, 34, 50));
        rr(6, 12, 9, 18, 1, RGB(90, 220, 190));
        rr(10, 8, 13, 18, 1, RGB(140, 130, 255));
        rr(14, 10, 17, 18, 1, RGB(255, 190, 90));
        break;
    case ICON_SETTINGS: {
        static const int8_t dir[8][2] = {{10,0},{7,7},{0,10},{-7,7},{-10,0},{-7,-7},{0,-10},{7,-7}};
        uint32_t gc = RGB(196, 206, 222);
        gfx_ring16(FX(24), FY(24), LEN(13), TH(34), gc, 255, false);
        for (int i = 0; i < 8; i++) {
            int dx = dir[i][0], dy = dir[i][1];
            seg(24 + dx * 14 / 10, 24 + dy * 14 / 10, 24 + dx * 22 / 10, 24 + dy * 22 / 10, 30, gc);
        }
        break; }
    case ICON_POWER:
        gfx_ring16(FX(24), FY(27), LEN(14), TH(22), col, 255, true);
        seg(24, 6, 24, 24, 24, col);
        break;
    case ICON_DOC:
        rr(5, 2, 19, 22, 2, RGB(226, 232, 244));
        seg(14, 16, 34, 16, 14, RGB(120, 130, 156));
        seg(14, 24, 34, 24, 14, RGB(120, 130, 156));
        seg(14, 32, 26, 32, 14, RGB(120, 130, 156));
        break;
    case ICON_CLOSE:
        seg(14, 14, 34, 34, 18, col); seg(34, 14, 14, 34, 18, col); break;
    case ICON_MIN:
        seg(14, 24, 34, 24, 18, col); break;
    case ICON_CHEV_L:
        seg(28, 12, 18, 24, 20, col); seg(18, 24, 28, 36, 20, col); break;
    case ICON_CHEV_R:
        seg(20, 12, 30, 24, 20, col); seg(30, 24, 20, 36, 20, col); break;
    case ICON_CHEV_UP:
        seg(12, 30, 24, 18, 20, col); seg(24, 18, 36, 30, 20, col); break;
    case ICON_CHECK:
        seg(10, 24, 20, 34, 22, col); seg(20, 34, 38, 14, 22, col); break;
    case ICON_BROWSER:
        gfx_ring16(FX(24), FY(24), LEN(20), TH(22), col, 255, false);
        seg(4, 24, 44, 24, 12, col);
        seg(24, 4, 24, 44, 12, col);
        gfx_ring16(FX(24), FY(24), LEN(11), TH(10), col, 190, false);
        break;
    case ICON_LOCK:
        rr(6, 12, 18, 22, 2, col);
        gfx_ring16(FX(24), FY(17), LEN(8), TH(20), col, 255, true);
        seg(20, 21, 28, 21, 10, col);
        break;
    case ICON_RELOAD:
        gfx_ring16(FX(24), FY(24), LEN(15), TH(20), col, 255, true);
        seg(32, 5, 41, 8, 18, col);
        seg(41, 8, 39, 17, 18, col);
        break;
    case ICON_MESSAGES:
        rr(2, 4, 22, 18, 4, RGB(78, 174, 235));
        seg(8, 18, 5, 23, 18, RGB(78, 174, 235));
        seg(8, 10, 17, 10, 12, RGB(255, 255, 255));
        seg(8, 14, 14, 14, 12, RGB(255, 255, 255));
        break;
    case ICON_CLOCK:
        gfx_ring16(FX(24), FY(24), LEN(19), TH(24), RGB(113, 211, 174), 255, false);
        seg(24, 8, 24, 24, 24, RGB(255, 255, 255));
        seg(24, 24, 34, 29, 22, RGB(255, 255, 255));
        break;
    case ICON_TERMINAL:
        rr(2, 3, 22, 21, 3, RGB(38, 48, 65));
        seg(7, 12, 13, 18, 20, RGB(120, 230, 190));
        seg(13, 18, 7, 24, 20, RGB(120, 230, 190));
        seg(15, 20, 20, 20, 18, RGB(220, 230, 245));
        break;
    case ICON_MAPS:
        rr(3, 4, 10, 21, 2, RGB(83, 189, 146));
        rr(10, 2, 16, 18, 2, RGB(95, 154, 245));
        rr(16, 5, 22, 21, 2, RGB(245, 185, 77));
        gfx_circle16(FX(12), FY(10), LEN(4), RGB(255, 255, 255), 255);
        break;
    }
}
