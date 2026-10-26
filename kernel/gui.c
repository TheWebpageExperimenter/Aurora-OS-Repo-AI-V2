/*
 * Aurora desktop shell (runs inside the kernel until user space exists).
 *  - Aurora boot splash, classic glass login, desktop with icons and clock
 *  - Taskbar docked bottom / left / right, medium icons, clear Aero styling and running-app indicators
 *  - Start menu with pinned apps and live search
 *  - Window manager: drag, minimise, close, focus/raise
 *  - Apps: AuroraFM, AuroraNotes, AuroraMonitor, AuroraSettings
 * Rendering is "redraw on change": every frame is composed into a back buffer,
 * and every visible control registers a hit-rectangle so input needs no extra layout code.
 */
#include "kernel.h"
#include "gfx.h"
#include "icons.h"
#include "input.h"
#include "timeutil.h"
#include "pmm.h"
#include "limine.h"
#include "video.h"
#include "net.h"
#include "display.h"
#include "security_monitor.h"
#include "tls.h"
#include "taskbar_api.h"
#include "shell_api.h"

#define TITLE_H 32

#define C_TEXT   RGB(236, 240, 247)
#define C_DIM    RGB(142, 152, 172)
#define C_WIN    RGB(30, 33, 44)
#define C_TITLE  RGB(38, 42, 56)
#define C_CARD   RGB(40, 44, 58)
#define C_FIELD  RGB(22, 25, 36)
#define C_BORDER RGB(70, 76, 96)
#define C_DARKTX RGB(14, 18, 28)

enum { APP_FILES, APP_NOTES, APP_MONITOR, APP_SETTINGS, APP_BROWSER, APP_MESSAGES, APP_CLOCK, APP_MAPS, APP_TERMINAL, APP_COUNT };
enum { TB_BOTTOM, TB_LEFT, TB_RIGHT };
enum { SCREEN_BOOT, SCREEN_LOGIN, SCREEN_DESKTOP };
enum { LAYER_DESKTOP = -1, LAYER_OVERLAY = -2 };

enum {
    ACT_NONE = 0,
    ACT_WIN_BODY = 0x100, ACT_WIN_TITLE = 0x110, ACT_WIN_CLOSE = 0x120, ACT_WIN_MIN = 0x130, ACT_WIN_MAX = 0x140,
    ACT_TB_START = 0x200, ACT_TB_SEARCH = 0x201, ACT_TB_BG = 0x202, ACT_TB_SHOWDESK = 0x203,
    ACT_TB_APP = 0x210,
    ACT_SM_APP = 0x300, ACT_SM_POWER = 0x310, ACT_SM_SEARCH = 0x311, ACT_SM_BODY = 0x312,
    ACT_DESK_BG = 0x400, ACT_DESK_ICON = 0x410,
    ACT_SET_PAGE = 0x500, ACT_SET_TBPOS = 0x510, ACT_SET_ICON = 0x520, ACT_SET_ACCENT = 0x530,
    ACT_SET_SCROLL_UP = 0x5A0, ACT_SET_SCROLL_DOWN = 0x5A1,
    ACT_SET_PINTB = 0x540, ACT_SET_PINST = 0x550, ACT_SET_TBCOLOR = 0x5D0, ACT_SET_TBSCHEME = 0x5E0,
    ACT_FILES_NAV = 0x600, ACT_FILES_ROW = 0x620,
    ACT_BROWSER_ADDRESS = 0x700, ACT_BROWSER_GO = 0x701, ACT_BROWSER_RELOAD = 0x702,
    ACT_LOGIN_SIGNIN = 0x710, ACT_SET_RESOLUTION = 0x580, ACT_SET_WALLPAPER = 0x590, ACT_SET_WALLMODE = 0x5A0, ACT_TB_NOTIFICATIONS = 0x5B0, ACT_NTF_CLEAR = 0x5B1, ACT_NTF_ITEM = 0x5C0,
    ACT_NOTES_FILE = 0x800, ACT_NOTES_VIEW = 0x801, ACT_NOTES_NEW = 0x810, ACT_NOTES_CLEAR = 0x811, ACT_NOTES_STATUS = 0x812, ACT_SET_RGB = 0x850, ACT_TERM_INPUT = 0x860, ACT_FILES_EDIT = 0x870, ACT_FILES_DELETE = 0x871, ACT_FILES_RENAME = 0x872
};

static const char *app_label[APP_COUNT] = { "Files", "Notes", "Monitor", "Settings", "Browser", "Messages", "Clock", "Maps", "Terminal" };
static const char *app_title[APP_COUNT] = { "AuroraFM", "AuroraNotes", "AuroraMonitor", "AuroraSettings", "Aurora Browser UI 1.0.8", "Aurora Messages", "Aurora Clock", "Aurora Maps", "Aurora Terminal" };
static const int   app_icon[APP_COUNT]  = { ICON_FILES, ICON_NOTES, ICON_MONITOR, ICON_SETTINGS, ICON_BROWSER, ICON_MESSAGES, ICON_CLOCK, ICON_MAPS, ICON_TERMINAL };
static const int   app_w[APP_COUNT]     = { 660, 520, 560, 660, 900, 600, 480, 760, 700 };
static const int   app_h[APP_COUNT]     = { 420, 360, 390, 470, 590, 430, 380, 500, 430 };

static const uint32_t accents[5] = {
    RGB(90, 220, 190), RGB(150, 122, 255), RGB(84, 154, 255), RGB(255, 192, 92), RGB(255, 112, 152)
};
static uint8_t custom_r = 84, custom_g = 154, custom_b = 255;
static bool custom_accent_enabled;
static uint32_t custom_accent(void) { return RGB(custom_r, custom_g, custom_b); }
static const int icon_sizes[3] = { 24, 32, 40 };
static const int resolution_w[7] = { 800, 1280, 1366, 1600, 1823, 1920, 800 };
static const int resolution_h[7] = { 600, 720, 768, 720, 918, 1080, 600 };
static const char *resolution_label[7] = {
    "800 x 600", "1280 x 720", "1366 x 768", "1600 x 720", "1823 x 918", "1920 x 1080", "800 x 600"
};

static struct {
    int  tb_pos, icon, accent;
    bool pin_tb[APP_COUNT], pin_start[APP_COUNT];
} cfg = { TB_BOTTOM, 32, 0, { true, true, false, false, true, true, true, true, true }, { true, true, true, true, true, true, true, true, true } };
#define ACCENT (custom_accent_enabled ? custom_accent() : accents[cfg.accent])

/* ---------- state ---------- */

static int SW, SH;
static struct limine_framebuffer active_fb;
static struct limine_framebuffer *original_fb; /* keep panic console metadata in sync */
static const char *resolution_status = "Choose a mode to apply it immediately.";
static bool resolution_status_ok, resolution_status_error;
static int shell_screen = SCREEN_BOOT;
static uint64_t screen_started;
static bool login_transition;
static uint64_t login_transition_ms;
struct win { bool open, min, closing, maximized; int x, y, w, h, restore_x, restore_y, restore_w, restore_h; uint64_t anim_ms; };
static struct win wins[APP_COUNT];
static int zorder[APP_COUNT], zcount;

static bool start_open;
static uint64_t start_anim_ms;
static bool notifications_open;
static bool notifications_animating;
static bool notifications_anim_closing;
static uint64_t notification_opened_ms;
#define NOTIFY_CAP 12
#define NOTIFY_TITLE_MAX 40
#define NOTIFY_BODY_MAX 88
struct shell_notification { char title[NOTIFY_TITLE_MAX]; char body[NOTIFY_BODY_MAX]; uint64_t when_ms; uint8_t level; };
static struct shell_notification notification_log[NOTIFY_CAP];
static unsigned notification_count, notification_next;

/* Bounded static notification storage: never allocates from the kernel heap. */
static void notification_push(const char *title, const char *body, uint8_t level) {
    unsigned i = notification_next;
    /* Copy event text into owned bounded storage: callers may pass temporary buffers. */
    ksnprintf(notification_log[i].title, sizeof notification_log[i].title, "%s",
              title ? title : "Aurora notification");
    ksnprintf(notification_log[i].body, sizeof notification_log[i].body, "%s",
              body ? body : "No details available.");
    notification_log[i].when_ms = uptime_ms();
    notification_log[i].level = level;
    notification_next = (notification_next + 1) % NOTIFY_CAP;
    if (notification_count < NOTIFY_CAP) notification_count++;
}
static struct shell_notification *notification_at(unsigned oldest_index) {
    if (oldest_index >= notification_count) return 0;
    unsigned first = (notification_next + NOTIFY_CAP - notification_count) % NOTIFY_CAP;
    return &notification_log[(first + oldest_index) % NOTIFY_CAP];
}
static char search[32];
static int  search_len;
static int  desk_sel = -1;
static int  drag_app = -1, drag_dx, drag_dy;
static int  last_click_id = -1;
static uint64_t last_click_ms;
static int  settings_page = 0;
static int  settings_scroll = 0;
static int files_scroll = 0, notes_scroll = 0, browser_scroll = 0;
static int  files_nav = 1, files_sel = -1;
static char notes[2048];
static int  notes_len;
static bool notes_file_flyout, notes_view_flyout, notes_status_visible = true;
static char terminal_line[128];
static int terminal_len;
static bool terminal_about_open;
static const char *terminal_output = "Aurora Terminal 1.0.5.0  |  Type help to get started.";
static char browser_url[512] = "https://www.google.com/";
static int browser_url_len = 23;
static bool browser_url_focus, browser_attempted, browser_uri_valid;
static const char *browser_result = "Network services are not implemented in this alpha.";

static struct rtc_time now;
static int fps_hist[60], fps_idx, frames;
static bool dirty = true, blink_on;
static int hover_id;

static int cur_layer, top_at_mouse;
static bool over_overlay;

static int tb_x, tb_y, tb_w, tb_h, tb_slot;
static int work_x, work_y, work_w, work_h;
struct tb_item { int id, app, icon, x, y; };
static struct tb_item tbi[16];
static int ntbi;

/* ---------- hit testing ---------- */

struct hit { int x, y, w, h, id; };
static struct hit hits[512];
static int nhits;

static bool in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

static void hit_add(int x, int y, int w, int h, int id) {
    int cx, cy, cw, ch;
    gfx_clip_get(&cx, &cy, &cw, &ch);
    int x0 = imax(x, cx), y0 = imax(y, cy), x1 = imin(x + w, cx + cw), y1 = imin(y + h, cy + ch);
    if (x1 <= x0 || y1 <= y0 || nhits >= 512) return;
    hits[nhits++] = (struct hit){ x0, y0, x1 - x0, y1 - y0, id };
}

static int hit_find(int px, int py) {
    for (int i = nhits - 1; i >= 0; i--)
        if (in_rect(px, py, hits[i].x, hits[i].y, hits[i].w, hits[i].h)) return hits[i].id;
    return ACT_NONE;
}

static bool hov(int x, int y, int w, int h) {
    if (!in_rect(mouse_x, mouse_y, x, y, w, h)) return false;
    int cx, cy, cw, ch;
    gfx_clip_get(&cx, &cy, &cw, &ch);
    if (!in_rect(mouse_x, mouse_y, cx, cy, cw, ch)) return false;
    if (cur_layer == LAYER_OVERLAY) return true;
    return !over_overlay && cur_layer == top_at_mouse;
}

/* registers a clickable area and reports hover */
static bool ui_btn(int x, int y, int w, int h, int id) {
    hit_add(x, y, w, h, id);
    return hov(x, y, w, h);
}

/* ---------- small widgets ---------- */

static void text_c(int cx, int y, const char *s, uint32_t c, int font) {
    gfx_text(cx - gfx_text_w(s, font) / 2, y, s, c, font);
}

static void seg_btn(int x, int y, int w, int h, const char *label, bool sel, int id) {
    bool hv = ui_btn(x, y, w, h, id);
    gfx_rrect(x, y, w, h, 6, sel ? ACCENT : (hv ? RGB(68, 75, 96) : RGB(50, 55, 72)), 255);
    text_c(x + w / 2, y + (h - 12) / 2, label, sel ? C_DARKTX : C_TEXT, FONT_S);
}

static void toggle(int x, int y, bool on, int id) {
    bool hv = ui_btn(x, y, 38, 20, id);
    gfx_rrect(x, y, 38, 20, 10, on ? ACCENT : (hv ? RGB(84, 92, 114) : RGB(62, 68, 88)), 255);
    gfx_circle16((x + (on ? 28 : 10)) * 16, (y + 10) * 16, 7 * 16, on ? C_DARKTX : RGB(235, 238, 245), 255);
}

static void fmt_uptime(char *buf, size_t n) {
    uint64_t s = uptime_ms() / 1000;
    ksnprintf(buf, n, "%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60),
              (unsigned long)(s % 60));
}

/* ---------- window helpers ---------- */

static void z_remove(int a) {
    int j = 0;
    for (int i = 0; i < zcount; i++) if (zorder[i] != a) zorder[j++] = zorder[i];
    zcount = j;
}
static void z_push(int a) { z_remove(a); zorder[zcount++] = a; }

static int top_app(void) {
    for (int i = zcount - 1; i >= 0; i--) {
        struct win *w = &wins[zorder[i]];
        if (w->open && !w->min) return zorder[i];
    }
    return -1;
}

static int win_at(int px, int py) {
    for (int i = zcount - 1; i >= 0; i--) {
        struct win *w = &wins[zorder[i]];
        if (w->open && !w->min && in_rect(px, py, w->x, w->y, w->w, w->h)) return zorder[i];
    }
    return LAYER_DESKTOP;
}

static void maximize_win(struct win *w) {
    /* Maximize inside the current work area, not the full screen. This keeps
       windows visible when the taskbar is docked to either vertical edge. */
    int inset = 8;
    int avail_w = work_w - inset * 2;
    int avail_h = work_h - inset * 2;
    if (avail_w < 120) { inset = 0; avail_w = work_w; }
    if (avail_h < TITLE_H + 40) { inset = 0; avail_h = work_h; }
    w->x = work_x + inset;
    w->y = work_y + inset;
    w->w = imax(120, avail_w);
    w->h = imax(TITLE_H + 40, avail_h);
    w->maximized = true;
}

static void clamp_win(struct win *w) {
    if (w->maximized) {
        maximize_win(w);
        return;
    }
    int min_x = work_x - w->w + 90;
    int max_x = work_x + work_w - 90;
    int max_y = work_y + work_h - TITLE_H;
    w->x = imax(min_x, imin(w->x, max_x));
    w->y = imax(work_y, imin(w->y, max_y));
}

static void open_app(int a) {
    struct win *w = &wins[a];
    if (!w->open) {
        w->open = true;
        w->closing = false;
        w->anim_ms = uptime_ms();
        w->w = imin(app_w[a], work_w - 20);
        w->h = imin(app_h[a], work_h - 20);
        w->x = work_x + (work_w - w->w) / 2 - 50 + a * 30;
        w->y = work_y + (work_h - w->h) / 2 - 30 + a * 22;
        clamp_win(w);
    }
    w->min = false;
    w->closing = false;
    w->anim_ms = uptime_ms();
    z_push(a);
    start_open = false;
}

static void close_app(int a) {
    if (!wins[a].open || wins[a].closing) return;
    wins[a].closing = true;
    wins[a].anim_ms = uptime_ms();
    dirty = true;
}

/* ---------- taskbar layout ---------- */

static void layout_taskbar(void) {
    /* Alpha 1.0.8: full-span, edge-to-edge taskbar; no floating dock. */
    tb_slot = cfg.icon + 12;
    int thick = cfg.icon + 24, gap = 4;
    ntbi = 0;
    tbi[ntbi++] = (struct tb_item){ ACT_TB_START, -1, ICON_LOGO, 0, 0 };
    tbi[ntbi++] = (struct tb_item){ ACT_TB_SEARCH, -1, ICON_SEARCH, 0, 0 };
    for (int a = 0; a < APP_COUNT; a++)
        if (cfg.pin_tb[a] || wins[a].open)
            tbi[ntbi++] = (struct tb_item){ ACT_TB_APP + a, a, app_icon[a], 0, 0 };
    int total = ntbi * tb_slot + (ntbi - 1) * gap;

    switch (cfg.tb_pos) {
    case TB_LEFT:
        tb_x = 0; tb_y = 0; tb_w = thick; tb_h = SH;
        work_x = thick; work_y = 0; work_w = SW - thick; work_h = SH;
        break;
    case TB_RIGHT:
        tb_x = SW - thick; tb_y = 0; tb_w = thick; tb_h = SH;
        work_x = 0; work_y = 0; work_w = SW - thick; work_h = SH;
        break;
    default:
        tb_x = 0; tb_y = SH - thick; tb_w = SW; tb_h = thick;
        work_x = 0; work_y = 0; work_w = SW; work_h = SH - thick;
        break;
    }
    for (int i = 0; i < ntbi; i++) {
        if (cfg.tb_pos == TB_BOTTOM) {
            tbi[i].x = (SW - total) / 2 + i * (tb_slot + gap);
            tbi[i].y = tb_y + (thick - tb_slot) / 2;
        } else {
            tbi[i].x = tb_x + (thick - tb_slot) / 2;
            tbi[i].y = 12 + i * (tb_slot + gap);
        }
    }
}

static void start_geom(int *x, int *y, int *w, int *h) {
    *w = 500; *h = 500;
    if (*h > work_h - 20) *h = work_h - 20;
    if (cfg.tb_pos == TB_BOTTOM) { *x = (SW - *w) / 2; *y = tb_y - 10 - *h; }
    else if (cfg.tb_pos == TB_LEFT) { *x = tb_x + tb_w + 8; *y = 10; }
    else { *x = tb_x - 8 - *w; *y = 10; }
    /* Slide the menu upward from the taskbar while it opens. */
    uint64_t elapsed = uptime_ms() - start_anim_ms;
    if (start_open && elapsed < 220 && cfg.tb_pos == TB_BOTTOM)
        *y += (int)((220 - elapsed) * 24 / 220);
}

/* ======================================================================
 *  Applications
 * ====================================================================== */

/* ---- AuroraFM ---- */
struct fent { const char *name, *type, *size; bool dir; };
static const struct fent f_desktop[] = {
    { "This PC", "Shortcut", "", false }, { "Aurora Notes", "Shortcut", "", false },
    { "Settings", "Shortcut", "", false } };
static const struct fent f_docs[] = {
    { "Projects", "Folder", "", true }, { "Budget.xlsx", "Spreadsheet", "48 KB", false },
    { "Resume.docx", "Document", "112 KB", false }, { "notes.txt", "Text file", "2 KB", false },
    { "aurora-spec.md", "Markdown", "9 KB", false } };
static const struct fent f_down[] = {
    { "aurora.iso", "Disc image", "3.7 MB", false }, { "limine.zip", "Archive", "1.4 MB", false },
    { "wallpaper.png", "Image", "822 KB", false } };
static const struct fent f_pics[] = {
    { "Screenshots", "Folder", "", true }, { "aurora-boot.png", "Image", "16 KB", false },
    { "panic-test.png", "Image", "11 KB", false } };
#define NENT(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const struct { const char *name; const struct fent *e; int n; } fnav[4] = {
    { "Desktop", f_desktop, NENT(f_desktop) }, { "Documents", f_docs, NENT(f_docs) },
    { "Downloads", f_down, NENT(f_down) },     { "Pictures", f_pics, NENT(f_pics) } };

static const char *files_status = "Ready";
static void app_files(int cx, int cy, int cw, int ch) {
    /* Separate ribbon tabs, actions, and breadcrumb into rows so controls do
       not overlap as they did in the previous compact Explorer layout. */
    gfx_rect(cx, cy, cw, 27, RGB(35, 39, 53), 255);
    gfx_text(cx + 12, cy + 8, "File", C_TEXT, FONT_S);
    gfx_text(cx + 54, cy + 8, "Home", ACCENT, FONT_S);
    gfx_text(cx + 104, cy + 8, "Share", C_DIM, FONT_S);
    gfx_text(cx + 158, cy + 8, "View", C_DIM, FONT_S);
    gfx_rect(cx + 48, cy + 24, 38, 2, ACCENT, 255);
    int ry = cy + 29;
    gfx_rect(cx, ry, cw, 31, RGB(42, 46, 61), 245);
    seg_btn(cx + 8, ry + 2, 58, 26, "Edit", false, ACT_FILES_EDIT);
    seg_btn(cx + 70, ry + 2, 76, 26, "Rename", false, ACT_FILES_RENAME);
    seg_btn(cx + 150, ry + 2, 70, 26, "Delete", false, ACT_FILES_DELETE);
    gfx_rect(cx + 226, ry + 5, 1, 20, C_BORDER, 160);
    gfx_text(cx + 238, ry + 10, "Organize   |   New item", C_DIM, FONT_S);
    int by = cy + 63;
    icon_draw(ICON_CHEV_L, cx + 10, by + 5, 18, C_DIM);
    icon_draw(ICON_CHEV_R, cx + 32, by + 5, 18, C_DIM);
    icon_draw(ICON_CHEV_UP, cx + 54, by + 5, 18, C_DIM);
    int px = cx + 82, pw = cw - 94;
    if (pw < 80) pw = 80;
    gfx_rrect(px, by, pw, 28, 7, C_FIELD, 255);
    char path[64];
    ksnprintf(path, sizeof path, "This PC  >  %s", fnav[files_nav].name);
    gfx_text(px + 12, by + 8, path, C_TEXT, FONT_S);

    int nx = cx + 10, ny = cy + 100;
    gfx_text(nx + 6, ny + 2, "Quick access", C_DIM, FONT_S);
    for (int i = 0; i < 4; i++) {
        int y = ny + 22 + i * 27;
        bool hv = ui_btn(nx, y, 138, 25, ACT_FILES_NAV + i);
        if (i == files_nav) {
            gfx_rrect(nx, y, 138, 25, 6, ACCENT, 46);
            gfx_rrect(nx, y + 5, 3, 15, 1, ACCENT, 255);
        } else if (hv) gfx_rrect(nx, y, 138, 25, 6, RGB(255, 255, 255), 16);
        icon_draw(ICON_FILES, nx + 12, y + 5, 16, 0);
        gfx_text(nx + 36, y + 7, fnav[i].name, C_TEXT, FONT_S);
    }
    int content_bottom = cy + ch - 38;
    if (content_bottom > ny + 22) gfx_rect(cx + 156, ny, 1, content_bottom - ny, RGB(255, 255, 255), 18);
    gfx_text(cx + 168, cy + ch - 22, files_status, C_DIM, FONT_S);

    int lx = cx + 166, lw = cw - 180, hy = cy + 101;
    if (lw < 80) lw = 80;
    int col_t = lx + lw * 54 / 100, col_s = lx + lw * 82 / 100;
    gfx_text(lx + 34, hy, "Name", C_DIM, FONT_S);
    gfx_text(col_t, hy, "Type", C_DIM, FONT_S);
    gfx_text(col_s, hy, "Size", C_DIM, FONT_S);
    gfx_rect(lx, hy + 18, lw, 1, RGB(255, 255, 255), 18);
    const struct fent *e = fnav[files_nav].e;
    for (int i = files_scroll; i < fnav[files_nav].n; i++) {
        int iy = hy + 24 + (i - files_scroll) * 27;
        if (iy + 25 >= content_bottom) break;
        bool hv = ui_btn(lx, iy, lw, 25, ACT_FILES_ROW + i);
        if (i == files_sel) gfx_rrect(lx, iy, lw, 25, 6, ACCENT, 40);
        else if (hv) gfx_rrect(lx, iy, lw, 25, 6, RGB(255, 255, 255), 14);
        icon_draw(e[i].dir ? ICON_FILES : ICON_DOC, lx + 10, iy + 4, 16, 0);
        gfx_text(lx + 34, iy + 6, e[i].name, C_TEXT, FONT_S);
        gfx_text(col_t, iy + 6, e[i].type, C_DIM, FONT_S);
    }
}

static void app_notes(int cx, int cy, int cw, int ch) {
    /* Compact editor toolbar with anchored flyout menus. */
    gfx_rect(cx, cy, cw, 38, RGB(35, 39, 53), 255);
    bool file_hot = ui_btn(cx + 10, cy + 6, 66, 26, ACT_NOTES_FILE);
    bool view_hot = ui_btn(cx + 82, cy + 6, 70, 26, ACT_NOTES_VIEW);
    gfx_rrect(cx + 10, cy + 6, 66, 26, 5, (file_hot || notes_file_flyout) ? RGB(65, 74, 96) : RGB(45, 50, 67), 255);
    gfx_rrect(cx + 82, cy + 6, 70, 26, 5, (view_hot || notes_view_flyout) ? RGB(65, 74, 96) : RGB(45, 50, 67), 255);
    gfx_text(cx + 24, cy + 13, "File  v", C_TEXT, FONT_S);
    gfx_text(cx + 96, cy + 13, "View  v", C_TEXT, FONT_S);
    int tx = cx + 16, ty = cy + 48, lineh = gfx_font_h(FONT_M), row = 0, pen_x = tx;
    int right = cx + cw - 16;
    for (int i = 0; i < notes_len; i++) {
        char c = notes[i];
        if (c == '\n') { row++; pen_x = tx; continue; }
        int adv = gfx_text_w((char[2]){ c, 0 }, FONT_M);
        if (pen_x + adv > right) { row++; pen_x = tx; }
        if (row >= notes_scroll) gfx_text((int)pen_x, ty + (row - notes_scroll) * lineh, (char[2]){ c, 0 }, C_TEXT, FONT_M);
        pen_x += adv;
    }
    if (blink_on && top_app() == APP_NOTES && row >= notes_scroll)
        gfx_rect((int)pen_x, ty + (row - notes_scroll) * lineh, 2, lineh - 2, ACCENT, 255);
    if (notes_len == 0)
        gfx_text(tx + 6, ty + 1, "Click here and start typing...", C_DIM, FONT_M);
    if (notes_status_visible) {
        gfx_rect(cx, cy + ch - 34, cw, 34, RGB(35, 39, 53), 255);
        char st[64];
        ksnprintf(st, sizeof st, "%d characters   |   Unsaved - storage service pending", notes_len);
        gfx_text(cx + 16, cy + ch - 22, st, C_DIM, FONT_S);
    }
    if (notes_file_flyout || notes_view_flyout) {
        int fx = cx + (notes_file_flyout ? 10 : 82), fy = cy + 34, fw = notes_file_flyout ? 158 : 170;
        gfx_shadow(fx, fy, fw, notes_file_flyout ? 82 : 82, 6);
        gfx_rrect(fx, fy, fw, 82, 7, RGB(29, 33, 46), 255);
        gfx_rrect_stroke(fx, fy, fw, 82, 7, RGB(117, 132, 158), 180);
        if (notes_file_flyout) {
            bool n = ui_btn(fx + 6, fy + 6, fw - 12, 30, ACT_NOTES_NEW);
            bool c = ui_btn(fx + 6, fy + 42, fw - 12, 30, ACT_NOTES_CLEAR);
            if (n) gfx_rrect(fx + 6, fy + 6, fw - 12, 30, 4, RGB(68, 78, 101), 255);
            if (c) gfx_rrect(fx + 6, fy + 42, fw - 12, 30, 4, RGB(68, 78, 101), 255);
            gfx_text(fx + 16, fy + 15, "New note", C_TEXT, FONT_S);
            gfx_text(fx + 16, fy + 51, "Clear note", C_TEXT, FONT_S);
        } else {
            bool st = ui_btn(fx + 6, fy + 6, fw - 12, 30, ACT_NOTES_STATUS);
            if (st) gfx_rrect(fx + 6, fy + 6, fw - 12, 30, 4, RGB(68, 78, 101), 255);
            gfx_text(fx + 16, fy + 15, notes_status_visible ? "Hide status bar" : "Show status bar", C_TEXT, FONT_S);
            gfx_text(fx + 16, fy + 49, "Editor layout", C_DIM, FONT_S);
        }
    }
}

/* ---- Aurora Browser ---- */
static bool browser_uri_is_supported(const char *s) {
    if (!s) return false;
    size_t n = 0;
    while (n < sizeof(browser_url) && s[n]) {
        unsigned char c = (unsigned char)s[n];
        if (c < 32 || c == 127 || c == '\\') return false;
        n++;
    }
    if (n == 0 || n >= sizeof(browser_url)) return false;
    size_t first = 0;
    while (first < n && (s[first] == ' ' || s[first] == '\t')) first++;
    size_t last = n;
    while (last > first && (s[last - 1] == ' ' || s[last - 1] == '\t')) last--;
    if (first == last) return false;
    for (size_t i = first; i < last; i++) if (s[i] == ' ') return false;

    size_t colon = last;
    for (size_t i = first; i < last; i++) {
        if (s[i] == ':') { colon = i; break; }
        if (s[i] == '/' || s[i] == '?' || s[i] == '#') break;
    }
    if (colon == last) return false; /* Require explicit http:// or https://; no implicit scheme. */
    size_t scheme_len = colon - first;
    bool http = scheme_len == 4 && !memcmp(s + first, "http", 4);
    bool https = scheme_len == 5 && !memcmp(s + first, "https", 5);
    if (!http && !https) return false;
    /* Check the remaining length before reading delimiter bytes. */
    if (last - colon < 3 || s[colon + 1] != '/' || s[colon + 2] != '/') return false;
    size_t host = colon + 3;
    if (host >= last || s[host] == '/' || s[host] == '?' || s[host] == '#' || s[host] == '@') return false;
    size_t authority_end = host;
    while (authority_end < last && s[authority_end] != '/' && s[authority_end] != '?' && s[authority_end] != '#') {
        unsigned char c = (unsigned char)s[authority_end];
        if (c <= 32 || c == 127 || c == '\\' || c == '@') return false;
        authority_end++;
    }
    if (authority_end == host) return false;
    /* Reject empty host labels and obvious malformed authorities. */
    if (s[host] == '.' || s[authority_end - 1] == '.') return false;
    for (size_t i = host; i < authority_end; i++) {
        if (s[i] == '.' && (i + 1 == authority_end || s[i + 1] == '.')) return false;
    }
    return true;
}

static void browser_attempt(void) {
    /* Prepare a browser-style navigation target. This UI has no TCP/IP or HTTP
       transport yet, so it must never imply that a network request succeeded. */
    size_t n = 0;
    while (n < sizeof(browser_url) && browser_url[n]) n++;
    if (n >= sizeof(browser_url)) { browser_url[sizeof(browser_url) - 1] = 0; n = sizeof(browser_url) - 1; }
    while (n && (browser_url[n - 1] == ' ' || browser_url[n - 1] == '\t')) browser_url[--n] = 0;
    size_t first = 0;
    while (first < n && (browser_url[first] == ' ' || browser_url[first] == '\t')) first++;
    if (first) { memmove(browser_url, browser_url + first, n - first + 1); n -= first; }
    browser_url_len = (int)n;
    browser_url_focus = false;
    browser_uri_valid = false;
    if (!n) {
        browser_attempted = true;
        browser_result = "Enter a web address or search phrase.";
    } else {
        bool has_scheme = false;
        for (size_t i = 0; i < n && i < 8; i++) if (browser_url[i] == ':') { has_scheme = true; break; }
        bool has_space = false, has_dot = false, has_path = false;
        for (size_t i = 0; i < n; i++) { if (browser_url[i] == ' ') has_space = true; if (browser_url[i] == '.') has_dot = true; if (browser_url[i] == '/' || browser_url[i] == '?') has_path = true; }
        if (!has_scheme && (has_space || (!has_dot && !has_path))) {
            /* Encode spaces for a Google search URL, with strict bounds. */
            char query[512]; size_t q = 0;
            const char prefix[] = "https://www.google.com/search?q=";
            memcpy(query, prefix, sizeof(prefix) - 1); q = sizeof(prefix) - 1;
            for (size_t i = 0; i < n && q + 4 < sizeof(query); i++) {
                if (browser_url[i] == ' ') { query[q++] = '%'; query[q++] = '2'; query[q++] = '0'; }
                else query[q++] = browser_url[i];
            }
            query[q] = 0; memcpy(browser_url, query, q + 1); n = q; browser_url_len = (int)n;
        } else if (!has_scheme && n + 8 < sizeof(browser_url)) {
            memmove(browser_url + 8, browser_url, n + 1);
            memcpy(browser_url, "https://", 8); n += 8; browser_url_len = (int)n;
        }
        browser_uri_valid = browser_uri_is_supported(browser_url);
        browser_attempted = true;
        if (!browser_uri_valid) {
            browser_result = "Invalid address. Use a hostname, http://, or https:// URL.";
            (void)aurora_security_audit(AURORA_SEC_BAD_URI, AURORA_SEC_WARNING, "browser rejected malformed or unsupported URI");
        } else {
            browser_result = "Navigation target prepared; network transport is not available in this kernel.";
            (void)aurora_security_audit(AURORA_SEC_NOTICE, AURORA_SEC_INFO, "browser URL prepared; no network request issued");
        }
    }
    notification_push("Aurora Browser", browser_uri_valid ? "Address prepared; live web transport is not implemented." : "Check the address and try again.", browser_uri_valid ? 0 : 2);
    dirty = true;
}

static void app_browser(int cx, int cy, int cw, int ch) {
    const int pad = 12;
    int tx = cx + pad, ty = cy + 8, tw = cw - pad * 2;
    if (tw < 220 || ch < 170) {
        gfx_rrect(cx + 8, cy + 8, imax(cw - 16, 8), imax(ch - 16, 8), 8, RGB(248, 250, 253), 255);
        gfx_text(cx + 20, cy + 24, "Aurora Browser: window too small", RGB(62, 78, 96), FONT_M);
        gfx_text(cx + 20, cy + 48, "Resize the window to restore the browser view.", RGB(88, 105, 122), FONT_S);
        return;
    }

    /* Tabs and navigation controls, styled after a modern desktop browser. */
    gfx_rrect(tx, ty, tw, 34, 8, RGB(33, 42, 58), 255);
    gfx_rect(tx, ty + 32, tw, 2, RGB(255, 255, 255), 16);
    gfx_rrect(tx + 10, ty + 3, 170, 27, 6, RGB(56, 70, 91), 255);
    icon_draw(ICON_BROWSER, tx + 18, ty + 8, 16, RGB(152, 224, 239));
    gfx_text(tx + 42, ty + 9, "Google", C_TEXT, FONT_S);
    gfx_text(tx + 153, ty + 9, "x", C_DIM, FONT_S);

    int ay = ty + 42;
    icon_draw(ICON_CHEV_L, tx + 4, ay + 8, 18, C_DIM);
    icon_draw(ICON_CHEV_R, tx + 26, ay + 8, 18, C_DIM);
    bool rh = ui_btn(tx + 47, ay, 25, 36, ACT_BROWSER_RELOAD);
    if (rh) gfx_rrect(tx + 46, ay + 2, 25, 32, 8, RGB(255, 255, 255), 24);
    icon_draw(ICON_RELOAD, tx + 50, ay + 9, 16, C_DIM);
    int ax = tx + 77, aw = tw - 77 - 74;
    bool url_hov = ui_btn(ax, ay, aw, 36, ACT_BROWSER_ADDRESS);
    gfx_rrect(ax, ay, aw, 36, 18, RGB(34, 45, 61), 255);
    gfx_rrect_stroke(ax, ay, aw, 36, 18,
                     browser_url_focus ? ACCENT : (url_hov ? RGB(124, 176, 207) : RGB(75, 94, 118)),
                     browser_url_focus ? 240 : 155);
    if (!browser_url_focus) icon_draw(ICON_LOCK, ax + 10, ay + 9, 17, RGB(224, 164, 93));
    else icon_draw(ICON_SEARCH, ax + 10, ay + 9, 17, C_DIM);
    /* Clip long/malformed input to the address field so it cannot paint over controls. */
    int clipx, clipy, clipw, cliph;
    gfx_clip_get(&clipx, &clipy, &clipw, &cliph);
    gfx_clip(ax + 30, ay + 2, aw - 36, 32);
    if (browser_url_len) {
        gfx_text(ax + 34, ay + 10, browser_url, C_TEXT, FONT_S);
        if (browser_url_focus && blink_on) {
            int cxcur = ax + 34 + gfx_text_w(browser_url, FONT_S) + 1;
            if (cxcur > ax + aw - 4) cxcur = ax + aw - 4;
            if (cxcur < ax + 34) cxcur = ax + 34;
            gfx_rect(cxcur, ay + 9, 1, 18, ACCENT, 255);
        }
    } else {
        gfx_text(ax + 34, ay + 10, "Enter URL or search terms", C_DIM, FONT_S);
    }
    gfx_clip(clipx, clipy, clipw, cliph);
    int gx = ax + aw + 8;
    bool gh = ui_btn(gx, ay, 60, 36, ACT_BROWSER_GO);
    gfx_rrect(gx, ay, 60, 36, 18, gh ? RGB(108, 211, 238) : RGB(62, 139, 177), 255);
    text_c(gx + 30, ay + 10, "Go", RGB(7, 40, 59), FONT_S);

    int cy0 = ay + 48, bodyh = ch - (cy0 - cy) - 10;
    if (bodyh < 72) bodyh = 72;
    gfx_rrect(tx, cy0, tw, bodyh, 10, RGB(246, 249, 252), 255);
    gfx_rrect(tx + 1, cy0 + 1, tw - 2, bodyh - 2, 9, RGB(255, 255, 255), 245);
    int mid = tx + tw / 2;
    int logo_y = cy0 + (bodyh > 280 ? 58 : 22);
    const char *letters = "Google";
    const uint32_t cols[6] = { RGB(66,133,244), RGB(234,67,53), RGB(251,188,5),
                               RGB(66,133,244), RGB(52,168,83), RGB(234,67,53) };
    int total = gfx_text_w(letters, FONT_L), lx = mid - total / 2;
    for (int i = 0; i < 6; i++) {
        char one[2] = { letters[i], 0 };
        int ww = gfx_text_w(one, FONT_L);
        gfx_text(lx, logo_y, one, cols[i], FONT_L);
        lx += ww;
    }
    text_c(mid, logo_y + 42, "Aurora Browser UI 1.0.8", RGB(58, 71, 89), FONT_M);

    int cardw = imin(590, tw - 34);
    if (cardw < 120) cardw = 120;
    int cardx = mid - cardw / 2;
    int cardy = logo_y + 84 - browser_scroll;
    gfx_rrect(cardx, cardy, cardw, browser_attempted ? 106 : 92, 9,
              browser_attempted ? RGB(255, 242, 229) : RGB(237, 246, 251), 255);
    gfx_rrect_stroke(cardx, cardy, cardw, browser_attempted ? 106 : 92, 9,
                     browser_attempted ? RGB(207, 126, 63) : RGB(173, 210, 226), 150);
    if (browser_attempted) {
        gfx_circle16((cardx + 24) * 16, (cardy + 24) * 16, 9 * 16, RGB(205, 116, 47), 255);
        text_c(cardx + 24, cardy + 15, "!", RGB(255, 255, 255), FONT_S);
        gfx_text(cardx + 44, cardy + 12, browser_uri_valid ? "Network services unavailable" : "Address rejected safely", RGB(122, 67, 27), FONT_M);
        gfx_text(cardx + 18, cardy + 44, browser_result, RGB(93, 67, 48), FONT_S);
        gfx_text(cardx + 18, cardy + 66, aurora_net_status(), RGB(122, 67, 27), FONT_S);
        gfx_text(cardx + 18, cardy + 86, "No request sent: TCP/IP, DNS, TLS and HTML engine are pending.", RGB(122, 67, 27), FONT_S);
    } else {
        gfx_circle16((cardx + 24) * 16, (cardy + 24) * 16, 9 * 16, RGB(41, 130, 174), 255);
        icon_draw(ICON_LOCK, cardx + 17, cardy + 17, 14, RGB(255, 255, 255));
        gfx_text(cardx + 44, cardy + 12, "Secure browsing not available yet", RGB(36, 78, 100), FONT_M);
        gfx_text(cardx + 18, cardy + 44, aurora_net_status(), RGB(68, 91, 109), FONT_S);
        gfx_text(cardx + 18, cardy + 65, "CA bundle included; network, TLS and HTML engine still pending.", RGB(68, 91, 109), FONT_S);
    }
    const char *foot = "Aurora Browser UI 1.0.8 | Network transport pending";
    text_c(mid, cy0 + bodyh - 22, foot, RGB(111, 126, 141), FONT_S);
}

/* ---- AuroraMonitor ---- */
static void app_messages(int cx, int cy, int cw, int ch) {
    gfx_rect(cx, cy, cw, ch, RGB(246, 248, 252), 255);
    gfx_text(cx + 18, cy + 16, "Messages", RGB(35, 48, 68), FONT_L);
    gfx_text(cx + 18, cy + 48, "Your conversations", RGB(92, 108, 130), FONT_S);
    gfx_rrect(cx + 16, cy + 78, cw - 32, 58, 10, RGB(225, 239, 250), 255);
    gfx_circle16((cx + 42) * 16, (cy + 106) * 16, 15 * 16, RGB(74, 145, 204), 255);
    gfx_text(cx + 66, cy + 90, "Aurora Team", RGB(32, 49, 70), FONT_M);
    gfx_text(cx + 66, cy + 111, "Welcome to Aurora Alpha!", RGB(62, 78, 99), FONT_S);
    gfx_rrect(cx + 16, cy + 150, cw - 32, 58, 10, RGB(235, 239, 245), 255);
    gfx_text(cx + 30, cy + 164, "System", RGB(32, 49, 70), FONT_M);
    gfx_text(cx + 30, cy + 185, "Messages is running in local demo mode.", RGB(62, 78, 99), FONT_S);
    gfx_rrect(cx + 16, cy + ch - 54, cw - 32, 36, 18, RGB(255,255,255), 255);
    gfx_rrect_stroke(cx + 16, cy + ch - 54, cw - 32, 36, 18, RGB(175,190,207), 255);
    gfx_text(cx + 30, cy + ch - 42, "Messaging service not connected", RGB(112, 124, 142), FONT_S);
}
static void app_clock(int cx, int cy, int cw, int ch) {
    gfx_rect(cx, cy, cw, ch, RGB(24, 31, 48), 255);
    gfx_text(cx + 20, cy + 18, "Clock", RGB(228, 237, 250), FONT_L);
    int d = (ch < cw ? ch : cw) - 110; if (d > 220) d = 220; if (d < 120) d = 120;
    int ox = cx + (cw - d) / 2, oy = cy + 62;
    gfx_circle16((ox + d/2) * 16, (oy + d/2) * 16, (d/2) * 16, RGB(54, 70, 96), 255);
    gfx_circle16((ox + d/2) * 16, (oy + d/2) * 16, (d/2 - 5) * 16, RGB(28, 38, 58), 255);
    gfx_line16(ox + d/2, oy + d/2, ox + d/2, oy + d/2 - d/4, 2, RGB(119, 211, 205), 255);
    gfx_line16(ox + d/2, oy + d/2, ox + d/2 + d/5, oy + d/2 + d/8, 2, RGB(119, 211, 205), 255);
    gfx_circle16((ox + d/2) * 16, (oy + d/2) * 16, 4 * 16, RGB(238, 245, 255), 255);
    gfx_text(cx + 20, cy + ch - 32, "System uptime clock  |  Local time service", RGB(166, 184, 209), FONT_S);
}
static void app_maps(int cx, int cy, int cw, int ch) {
    gfx_rect(cx, cy, cw, ch, RGB(232, 240, 235), 255);
    for (int i = 0; i < cw; i += 72) gfx_line16(cx + i, cy, cx + i - 70, cy + ch, 1, RGB(208, 222, 214), 255);
    for (int j = 0; j < ch; j += 58) gfx_line16(cx, cy + j, cx + cw, cy + j + 20, 1, RGB(208, 222, 214), 255);
    gfx_line16(cx + 30, cy + ch - 30, cx + cw/3, cy + ch/2, 4, RGB(255,255,255), 255);
    gfx_line16(cx + cw/3, cy + ch/2, cx + cw/2, cy + ch/3, 4, RGB(255,255,255), 255);
    gfx_line16(cx + cw/2, cy + ch/3, cx + cw - 45, cy + 45, 4, RGB(255,255,255), 255);
    gfx_rrect(cx + 16, cy + 16, cw - 32, 38, 8, RGB(255,255,255), 245);
    gfx_text(cx + 28, cy + 28, "Search places (offline demo)", RGB(91, 108, 103), FONT_S);
    gfx_circle16((cx + cw/2) * 16, (cy + ch/2) * 16, 12 * 16, RGB(56, 137, 211), 255);
    gfx_circle16((cx + cw/2) * 16, (cy + ch/2) * 16, 4 * 16, RGB(255,255,255), 255);
    gfx_rrect(cx + 16, cy + ch - 44, cw - 32, 28, 8, RGB(255,255,255), 240);
    gfx_text(cx + 28, cy + ch - 35, "Maps needs a network service for live maps.", RGB(77, 93, 88), FONT_S);
}

static void app_monitor(int cx, int cy, int cw, int ch) {
    int x0 = cx + 16, w = cw - 32;
    uint64_t total = pmm_total_bytes(), freeb = pmm_free_bytes(), used = total - freeb;
    char b[96];

    gfx_rrect(x0, cy + 14, w, 84, 8, C_CARD, 255);
    gfx_text(x0 + 16, cy + 24, "Memory", C_TEXT, FONT_M);
    ksnprintf(b, sizeof b, "%lu MiB used of %lu MiB  (%lu%%)", (unsigned long)(used >> 20),
              (unsigned long)(total >> 20), (unsigned long)(total ? used * 100 / total : 0));
    gfx_text(x0 + 16, cy + 46, b, C_DIM, FONT_S);
    gfx_rrect(x0 + 16, cy + 68, w - 32, 12, 6, C_FIELD, 255);
    int fw = total ? (int)((uint64_t)(w - 32) * used / total) : 0;
    if (fw > 0) gfx_rrect(x0 + 16, cy + 68, imax(fw, 12), 12, 6, ACCENT, 255);

    int hw = (w - 12) / 2;
    struct aurora_display_info di;
    aurora_display_get_info(&di);
    gfx_rrect(x0, cy + 110, hw, 90, 8, C_CARD, 255);
    gfx_text(x0 + 16, cy + 120, "Display", C_TEXT, FONT_M);
    ksnprintf(b, sizeof b, "%d x %d, 32-bit", SW, SH);
    gfx_text(x0 + 16, cy + 146, b, C_DIM, FONT_S);
    gfx_text(x0 + 16, cy + 162, aurora_display_kind_name(di.kind), C_DIM, FONT_S);
    gfx_text(x0 + 16, cy + 180, "Framebuffer / software renderer", C_DIM, FONT_S);

    gfx_rrect(x0 + hw + 12, cy + 110, hw, 74, 8, C_CARD, 255);
    gfx_text(x0 + hw + 28, cy + 120, "Uptime", C_TEXT, FONT_M);
    char up[16]; fmt_uptime(up, sizeof up);
    gfx_text(x0 + hw + 28, cy + 146, up, C_DIM, FONT_S);
    ksnprintf(b, sizeof b, "CPU timestamp: %lu MHz", (unsigned long)tsc_mhz());
    gfx_text(x0 + hw + 28, cy + 162, b, C_DIM, FONT_S);

    int gy = cy + 206, gh = ch - 206 - 16;
    gfx_rrect(x0, gy, w, gh, 8, C_CARD, 255);
    gfx_text(x0 + 16, gy + 10, "UI frames per second (last 60 s)", C_TEXT, FONT_S);
    int gx = x0 + 16, gw = w - 32, top = gy + 32, bh = gh - 46;
    gfx_rrect(gx, top, gw, bh, 4, C_FIELD, 255);
    int mx = 10;
    for (int i = 0; i < 60; i++) mx = imax(mx, fps_hist[i]);
    int bw = gw / 60;
    for (int i = 0; i < 60; i++) {
        int v = fps_hist[(fps_idx + i) % 60];
        int hh = v * (bh - 4) / mx;
        if (hh > 0) gfx_rect(gx + 2 + i * bw, top + bh - 2 - hh, imax(bw - 1, 1), hh, ACCENT, 255);
    }
}

/* ---- AuroraSettings ---- */
static const char *set_pages[3] = { "System", "Personalization", "About" };
static const char *wallpaper_names[5] = { "Aurora", "Blue Horizon", "Neon Bloom", "Emerald", "Solar Flare" };
static const int wallpaper_ids[5] = { 0, 1, 2, 3, 4 };
static const char *wallmode_names[5] = { "Full screen", "Fit", "Stretch", "Center", "Tile" };

static void kv(int x, int y, const char *k, const char *v) {
    gfx_text(x, y, k, C_DIM, FONT_S);
    gfx_text(x + 130, y, v, C_TEXT, FONT_S);
}

static void app_settings(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    for (int i = 0; i < 3; i++) {
        int y = cy + 14 + i * 34;
        bool hv = ui_btn(cx + 10, y, 168, 30, ACT_SET_PAGE + i);
        if (i == settings_page) {
            gfx_rrect(cx + 10, y, 168, 30, 6, ACCENT, 46);
            gfx_rrect(cx + 10, y + 7, 3, 16, 1, ACCENT, 255);
        } else if (hv) gfx_rrect(cx + 10, y, 168, 30, 6, RGB(255, 255, 255), 16);
        gfx_text(cx + 26, y + 9, set_pages[i], C_TEXT, FONT_S);
    }
    gfx_rect(cx + 188, cy + 12, 1, ch - 24, RGB(255, 255, 255), 18);

    int px = cx + 208, py = cy + 16;
    char b[96];
    gfx_text(px, py, set_pages[settings_page], C_TEXT, FONT_L);
    /* Scroll buttons remain fixed while the settings content moves. */
    seg_btn(cx + cw - 88, cy + 12, 32, 26, "^", settings_scroll == 0, ACT_SET_SCROLL_UP);
    seg_btn(cx + cw - 50, cy + 12, 32, 26, "v", false, ACT_SET_SCROLL_DOWN);
    int clipx, clipy, clipw, cliph;
    gfx_clip_get(&clipx, &clipy, &clipw, &cliph);
    gfx_clip(px, cy + 48, cx + cw - px - 8, ch - 60);
    py += 40 - settings_scroll;

    if (settings_page == 0) {
        ksnprintf(b, sizeof b, "%d x %d  (32-bit colour)", SW, SH);
        kv(px, py, "Current", b); py += 27;
        gfx_text(px, py, "Resolution", C_DIM, FONT_S); py += 20;
        for (int i = 0; i < 6; i++) {
            int col = i % 2, row = i / 2;
            bool active = SW == resolution_w[i] && SH == resolution_h[i];
            seg_btn(px + col * 116, py + row * 32, 108, 27, resolution_label[i], active,
                    ACT_SET_RESOLUTION + i);
        }
        py += 104;
        uint32_t status_colour = resolution_status_ok ? RGB(131, 239, 202) :
                                 (resolution_status_error ? RGB(255, 178, 160) : C_DIM);
        gfx_text(px, py, resolution_status, status_colour, FONT_S);
        py += 23;
        ksnprintf(b, sizeof b, "%lu MiB usable, %lu MiB free", (unsigned long)(pmm_total_bytes() >> 20),
                  (unsigned long)(pmm_free_bytes() >> 20));
        kv(px, py, "Memory", b); py += 24;
        ksnprintf(b, sizeof b, "x86-64, %lu MHz", (unsigned long)tsc_mhz()); kv(px, py, "Processor", b); py += 24;
        fmt_uptime(b, sizeof b); kv(px, py, "Uptime", b); py += 24;
        kv(px, py, "Input", "PS/2 keyboard and mouse");
    } else if (settings_page == 1) {
        gfx_text(px, py, "Taskbar position", C_DIM, FONT_S);
        static const char *pos[3] = { "Bottom", "Left", "Right" };
        for (int i = 0; i < 3; i++) seg_btn(px + i * 94, py + 20, 88, 28, pos[i], cfg.tb_pos == i, ACT_SET_TBPOS + i);
        py += 62;
        gfx_text(px, py, "Taskbar icon size", C_DIM, FONT_S);
        static const char *isz[3] = { "Small  24", "Medium  32", "Large  40" };
        for (int i = 0; i < 3; i++) seg_btn(px + i * 94, py + 20, 88, 28, isz[i], cfg.icon == icon_sizes[i], ACT_SET_ICON + i);
        py += 62;
        gfx_text(px, py, "Wallpaper", C_DIM, FONT_S);
        for (int i = 0; i < 5; i++) { int col = i % 3, row = i / 3; seg_btn(px + col * 94, py + row * 30 + 20, 88, 26, wallpaper_names[i], gfx_wallpaper_preset() == wallpaper_ids[i], ACT_SET_WALLPAPER + i); }
        py += 82;
        gfx_text(px, py, "Wallpaper mode", C_DIM, FONT_S);
        for (int i = 0; i < 5; i++) { int col = i % 3, row = i / 3; seg_btn(px + col * 94, py + row * 30 + 20, 88, 26, wallmode_names[i], gfx_wallpaper_mode() == i, ACT_SET_WALLMODE + i); }
        py += 82;
        gfx_text(px, py, "Accent colour", C_DIM, FONT_S);
        for (int i = 0; i < 5; i++) {
            int sx = px + i * 40, sy = py + 20;
            bool hv = ui_btn(sx, sy, 32, 32, ACT_SET_ACCENT + i);
            gfx_circle16((sx + 16) * 16, (sy + 16) * 16, (hv ? 15 : 13) * 16, accents[i], 255);
            if (cfg.accent == i) gfx_ring16((sx + 16) * 16, (sy + 16) * 16, 15 * 16, 24, C_TEXT, 255, false);
        }
        py += 56;
        gfx_text(px, py, "Custom RGB accent", C_DIM, FONT_S);
        seg_btn(px, py + 18, 64, 28, "R -", false, ACT_SET_RGB + 0);
        seg_btn(px + 68, py + 18, 64, 28, "G -", false, ACT_SET_RGB + 1);
        seg_btn(px + 136, py + 18, 64, 28, "B -", false, ACT_SET_RGB + 2);
        seg_btn(px + 204, py + 18, 64, 28, "R +", false, ACT_SET_RGB + 3);
        seg_btn(px + 272, py + 18, 64, 28, "G +", false, ACT_SET_RGB + 4);
        seg_btn(px + 340, py + 18, 64, 28, "B +", false, ACT_SET_RGB + 5);
        gfx_rrect(px + 410, py + 18, 38, 28, 5, custom_accent(), 255);
        py += 54;
        struct aurora_taskbar_style tbstyle;
        aurora_taskbar_get_style(&tbstyle);
        gfx_text(px, py, "Taskbar colour", C_DIM, FONT_S);
        toggle(px + 150, py - 3, tbstyle.color_enabled, ACT_SET_TBCOLOR);
        gfx_text(px + 178, py, tbstyle.color_enabled ? "Enabled" : "Disabled", C_TEXT, FONT_S);
        py += 27;
        static const char *tb_scheme_names[AURORA_TASKBAR_SCHEME_COUNT] = {
            "Aero", "Ocean", "Graphite", "Amber", "Violet"
        };
        for (int i = 0; i < (int)AURORA_TASKBAR_SCHEME_COUNT; i++) {
            int col = i % 3, row = i / 3;
            seg_btn(px + col * 94, py + row * 30, 88, 26, tb_scheme_names[i],
                    tbstyle.scheme == (uint32_t)i, ACT_SET_TBSCHEME + i);
        }
        py += 66;
        gfx_text(px, py, "Pinned apps", C_DIM, FONT_S);
        gfx_text(px + 190, py, "Taskbar", C_DIM, FONT_S);
        gfx_text(px + 270, py, "Start", C_DIM, FONT_S);
        for (int a = 0; a < APP_COUNT; a++) {
            int y = py + 20 + a * 28;
            icon_draw(app_icon[a], px, y, 16, 0);
            gfx_text(px + 26, y + 2, app_label[a], C_TEXT, FONT_S);
            toggle(px + 184, y - 2, cfg.pin_tb[a], ACT_SET_PINTB + a);
            toggle(px + 262, y - 2, cfg.pin_start[a], ACT_SET_PINST + a);
        }
    } else {
        icon_draw(ICON_LOGO, px, py, 40, 0);
        gfx_text(px + 56, py + 2, "Aurora OS", C_TEXT, FONT_M);
        gfx_text(px + 56, py + 22, "Alpha 1.0.8 | Kernel API 2.2 | Shell API 4.0 | Build 102620262000", C_DIM, FONT_S);
        py += 66;
        gfx_text(px, py, "A from-scratch x86-64 operating system.", C_TEXT, FONT_S); py += 20;
        gfx_text(px, py, "Kernel, drivers and desktop shell written in C.", C_DIM, FONT_S); py += 20;
        gfx_text(px, py, "Licensed under the MIT License.", C_DIM, FONT_S); py += 34;
        gfx_text(px, py, "Bootloader: Limine (BSD-2-Clause)", C_DIM, FONT_S); py += 20;
        gfx_text(px, py, "Font: Go Medium TrueType (embedded antialiased atlas; BSD-3-Clause)", C_DIM, FONT_S);
    }
    gfx_clip(clipx, clipy, clipw, cliph);
}

/* ======================================================================
 *  Window chrome
 * ====================================================================== */

static void draw_window(int a) {
    struct win *w = &wins[a];
    bool focused = (top_app() == a);
    int original_y = w->y;
    uint64_t elapsed = uptime_ms() - w->anim_ms;
    uint32_t opacity = 255;
    if (elapsed < 180) {
        int offset = (int)((180 - elapsed) * 12 / 180);
        if (w->closing) {
            offset = -offset;
            opacity = 255 - (uint32_t)(elapsed * 255 / 180);
        } else {
            opacity = (uint32_t)(elapsed * 255 / 180);
        }
        w->y += offset;
    }
    /* Fade the complete window (chrome, controls, text and app contents) as one layer. */
    gfx_set_opacity(opacity);
    cur_layer = a;
    gfx_clip_reset();
    gfx_shadow(w->x, w->y, w->w, w->h, 8);
    gfx_rrect(w->x, w->y, w->w, w->h, 8, C_WIN, 255);
    gfx_rrect(w->x, w->y, w->w, TITLE_H + 8, 8, C_TITLE, 255);
    gfx_rect(w->x, w->y + TITLE_H, w->w, 8, C_WIN, 255);
    gfx_rect(w->x + 1, w->y + TITLE_H - 1, w->w - 2, 1, RGB(255, 255, 255), 14);
    gfx_rrect_stroke(w->x, w->y, w->w, w->h, 8, focused ? ACCENT : C_BORDER, focused ? 210 : 255);

    hit_add(w->x, w->y, w->w, w->h, ACT_WIN_BODY + a);
    hit_add(w->x, w->y, w->w - 120, TITLE_H, ACT_WIN_TITLE + a);

    icon_draw(app_icon[a], w->x + 12, w->y + 8, 16, 0);
    gfx_text(w->x + 36, w->y + (TITLE_H - 12) / 2, app_title[a], focused ? C_TEXT : C_DIM, FONT_S);

    int bx = w->x + w->w - 40, by = w->y + 4;
    bool hc = ui_btn(bx, by, 32, 24, ACT_WIN_CLOSE + a);
    if (hc) gfx_rrect(bx, by, 32, 24, 6, RGB(232, 60, 72), 235);
    icon_draw(ICON_CLOSE, bx + 8, by + 4, 16, C_TEXT);
    bool hx = ui_btn(bx - 36, by, 32, 24, ACT_WIN_MAX + a);
    if (hx) gfx_rrect(bx - 36, by, 32, 24, 6, RGB(255, 255, 255), 42);
    gfx_rrect_stroke(bx - 27, by + 7, 14, 11, 2, C_TEXT, 220);
    bool hm = ui_btn(bx - 72, by, 32, 24, ACT_WIN_MIN + a);
    if (hm) gfx_rrect(bx - 72, by, 32, 24, 6, RGB(255, 255, 255), 42);
    icon_draw(ICON_MIN, bx - 72 + 8, by + 4, 16, C_TEXT);

    int cx = w->x + 1, cy = w->y + TITLE_H, cw = w->w - 2, ch = w->h - TITLE_H - 1;
    gfx_clip(cx, cy, cw, ch);
    switch (a) {
    case APP_FILES:    app_files(cx, cy, cw, ch); break;
    case APP_NOTES:    app_notes(cx, cy, cw, ch); break;
    case APP_MONITOR:  app_monitor(cx, cy, cw, ch); break;
    case APP_SETTINGS: app_settings(cx, cy, cw, ch); break;
    case APP_BROWSER: app_browser(cx, cy, cw, ch); break;
    case APP_MESSAGES: app_messages(cx, cy, cw, ch); break;
    case APP_CLOCK: app_clock(cx, cy, cw, ch); break;
    case APP_MAPS: app_maps(cx, cy, cw, ch); break;
    case APP_TERMINAL: {
        /* Terminal 2.0: modern tabbed chrome, distinct output area and prompt strip. */
        gfx_rect(cx, cy, cw, ch, RGB(12, 15, 22), 255);
        gfx_rrect(cx + 10, cy + 9, cw - 20, 30, 7, RGB(29, 35, 48), 255);
        gfx_circle16((cx + 25) * 16, (cy + 24) * 16, 4 * 16, RGB(255, 105, 105), 255);
        gfx_circle16((cx + 39) * 16, (cy + 24) * 16, 4 * 16, RGB(255, 195, 86), 255);
        gfx_circle16((cx + 53) * 16, (cy + 24) * 16, 4 * 16, RGB(93, 220, 145), 255);
        gfx_text(cx + 70, cy + 18, "Aurora Terminal", C_TEXT, FONT_S);
        gfx_text(cx + cw - 96, cy + 18, "SESSION 01", C_DIM, FONT_S);
        gfx_rrect(cx + 12, cy + 48, cw - 24, ch - 100, 7, RGB(17, 22, 31), 255);
        gfx_text(cx + 25, cy + 63, "AURORA TERMINAL  /  ALPHA DEVELOPER STAGE", RGB(115, 190, 255), FONT_S);
        gfx_text(cx + 25, cy + 88, "Kernel shell ready. Type 'help' for commands.", RGB(151, 163, 181), FONT_S);
        gfx_rect(cx + 25, cy + 111, cw - 50, 1, RGB(255, 255, 255), 25);
        gfx_text(cx + 25, cy + 124, terminal_output, RGB(142, 232, 184), FONT_M);
        gfx_rrect(cx + 12, cy + ch - 43, cw - 24, 31, 6, RGB(29, 35, 48), 255);
        gfx_text(cx + 24, cy + ch - 33, "aurora@alpha  ~  $", RGB(115, 220, 170), FONT_S);
        gfx_text(cx + 151, cy + ch - 33, terminal_line, C_TEXT, FONT_S);
        gfx_text(cx + cw - 90, cy + ch - 33, "READY", RGB(115, 220, 170), FONT_S);
        hit_add(cx + 10, cy + ch - 47, cw - 20, 38, ACT_TERM_INPUT);
        if (terminal_about_open) {
            int ax = cx + 35, ay = cy + 65, aw = cw - 70, ah = ch - 125;
            gfx_shadow(ax, ay, aw, ah, 10); gfx_rrect(ax, ay, aw, ah, 10, RGB(34, 39, 53), 255);
            gfx_rrect(ax + 1, ay + 1, aw - 2, 4, 2, RGB(82, 174, 255), 255);
            icon_draw(ICON_LOGO, ax + 18, ay + 20, 48, 0);
            gfx_text(ax + 78, ay + 24, "Aurora OS", C_TEXT, FONT_L);
            gfx_text(ax + 78, ay + 52, "Alpha 1.0.8 | Kernel API 2.2 | Shell API 4.0", C_DIM, FONT_S);
            gfx_text(ax + 18, ay + 92, "Kernel: Aurora Alpha 1.0.8", C_TEXT, FONT_S);
            gfx_text(ax + 18, ay + 114, "Build: 102620261500", C_TEXT, FONT_S);
            gfx_text(ax + 18, ay + 136, "Date: 2026-10-26", C_TEXT, FONT_S);
            gfx_text(ax + 18, ay + 158, "Profile: Aurora Default", C_TEXT, FONT_S);
        }
        break;
    }
    }
    gfx_clip_reset();
    w->y = original_y;
    gfx_set_opacity(255);
}

/* ======================================================================
 *  Desktop, taskbar, start menu
 * ====================================================================== */

/* Keep each desktop shortcut paired with its own label and launch target. */
static const int desk_apps[3] = { APP_FILES, APP_NOTES, APP_SETTINGS };
static const char *desk_labels[3] = { "This PC", "Notes", "Settings" };

static void draw_desktop(void) {
    cur_layer = LAYER_DESKTOP;
    hit_add(0, 0, SW, SH, ACT_DESK_BG);
    for (int i = 0; i < 3; i++) {
        int x = work_x + 12, y = work_y + 12 + i * 84;
        bool sel = desk_sel == i, hv = ui_btn(x, y, 76, 76, ACT_DESK_ICON + i);
        if (sel || hv) gfx_rrect(x, y, 76, 76, 6, RGB(255, 255, 255), sel ? 44 : 20);
        if (sel) gfx_rrect_stroke(x, y, 76, 76, 6, RGB(255, 255, 255), 70);
        icon_draw(app_icon[desk_apps[i]], x + 22, y + 8, 32, C_TEXT);
        int tw = gfx_text_w(desk_labels[i], FONT_S);
        gfx_text_a(x + (76 - tw) / 2 + 1, y + 49, desk_labels[i], RGB(0, 0, 0), 170, FONT_S);
        gfx_text(x + (76 - tw) / 2, y + 48, desk_labels[i], RGB(255, 255, 255), FONT_S);
    }
}

/* Frutiger Aero Start orb: layered blue glass with a four-pane light-blue mark. */
static void draw_start_orb(int x, int y, int size, bool active, bool hovered) {
    int cx = x + size / 2, cy = y + size / 2;
    int r = size / 2 - 1;
    gfx_circle16(cx * 16, cy * 16, r * 16, RGB(10, 86, 176), 255);
    gfx_circle16(cx * 16, cy * 16, (r - 2) * 16, RGB(23, 151, 231), 255);
    /* Glossy upper crescent/highlight. */
    gfx_rrect_grad(x + 4, y + 3, size - 8, size / 2, size / 3,
                   RGB(189, 239, 255), RGB(39, 166, 246));
    /* Four luminous panes evoke the Aurora/Windows-style start mark. */
    int lx = x + size / 4, ly = y + size / 4, pane = size / 5, gap = 2;
    gfx_rect(lx, ly, pane, pane, RGB(235, 252, 255), 255);
    gfx_rect(lx + pane + gap, ly - 1, pane, pane + 1, RGB(203, 244, 255), 255);
    gfx_rect(lx, ly + pane + gap, pane, pane, RGB(192, 238, 255), 255);
    gfx_rect(lx + pane + gap, ly + pane + gap - 1, pane, pane, RGB(232, 251, 255), 255);
    gfx_ring16(cx * 16, cy * 16, r * 16, 1 * 16,
               hovered || active ? RGB(232, 251, 255) : RGB(132, 213, 255), 245, false);
    if (hovered || active)
        gfx_circle16(cx * 16, cy * 16, (r + 2) * 16, RGB(164, 229, 255), 75);
}

static void draw_taskbar(void) {
    cur_layer = LAYER_OVERLAY;
    gfx_clip_reset();

    /* Theme colours are routed through the versioned taskbar kernel API. */
    struct aurora_taskbar_style tbstyle;
    aurora_taskbar_get_style(&tbstyle);
    uint32_t glass = RGB(74, 112, 142), edge = RGB(150, 184, 207), shine = RGB(190, 215, 232);
    if (tbstyle.scheme == AURORA_TASKBAR_SCHEME_OCEAN) { glass = RGB(42, 137, 194); edge = RGB(175, 231, 255); shine = RGB(226, 249, 255); }
    else if (tbstyle.scheme == AURORA_TASKBAR_SCHEME_GRAPHITE) { glass = RGB(68, 76, 91); edge = RGB(195, 205, 220); shine = RGB(239, 244, 250); }
    else if (tbstyle.scheme == AURORA_TASKBAR_SCHEME_AMBER) { glass = RGB(218, 151, 62); edge = RGB(255, 225, 171); shine = RGB(255, 248, 225); }
    else if (tbstyle.scheme == AURORA_TASKBAR_SCHEME_VIOLET) { glass = RGB(126, 91, 184); edge = RGB(224, 201, 255); shine = RGB(249, 239, 255); }
    /* Alpha 1.0.8_02: liquid-glass taskbar. Keep the desktop wallpaper
       visibly readable through the panel; the disabled-color fallback is
       also translucent rather than an opaque grey slab. */
    if (tbstyle.color_enabled) {
        gfx_rect(tb_x, tb_y, tb_w, tb_h, glass, 72);
        gfx_rect(tb_x, tb_y, tb_w, 1, edge, 115);
        if (tb_w > 28) gfx_rect(tb_x + 1, tb_y + 1, tb_w - 2, 1, shine, 32);
        /* Very faint lower edge adds a glass pane boundary without filling it. */
        if (tb_h > 2) gfx_rect(tb_x, tb_y + tb_h - 1, tb_w, 1, RGB(225, 244, 255), 28);
    } else {
        gfx_rect(tb_x, tb_y, tb_w, tb_h, RGB(45, 51, 62), 58);
        gfx_rect(tb_x, tb_y, tb_w, 1, RGB(184, 199, 214), 80);
    }
    hit_add(tb_x, tb_y, tb_w, tb_h, ACT_TB_BG);

    int top = top_app();
    for (int i = 0; i < ntbi; i++) {
        struct tb_item *it = &tbi[i];
        bool hv = ui_btn(it->x, it->y, tb_slot, tb_slot, it->id);
        bool active = (it->id == ACT_TB_START && start_open) || (it->app >= 0 && it->app == top);
        if (active) {
            gfx_rect(it->x, it->y, tb_slot, tb_slot, RGB(221, 250, 255), 68);
        } else if (hv) {
            gfx_rect(it->x, it->y, tb_slot, tb_slot, RGB(231, 252, 255), 42);
        }
        if (it->id == ACT_TB_START) {
            int orb_size = cfg.icon > 34 ? 34 : cfg.icon;
            draw_start_orb(it->x + (tb_slot - orb_size) / 2,
                           it->y + (tb_slot - orb_size) / 2,
                           orb_size, active, hv);
        } else {
            icon_draw(it->icon, it->x + (tb_slot - cfg.icon) / 2, it->y + (tb_slot - cfg.icon) / 2,
                      cfg.icon, active ? RGB(12, 66, 91) : C_TEXT);
        }
        if (it->app >= 0 && wins[it->app].open) { /* running indicator */
            int len = active ? 16 : 6;
            uint32_t col = active ? RGB(199, 252, 255) : RGB(182, 238, 249);
            if (cfg.tb_pos == TB_BOTTOM)
                gfx_rect(it->x + (tb_slot - len) / 2, it->y + tb_slot, len, 3, col, 255);
            else if (cfg.tb_pos == TB_LEFT)
                gfx_rect(tb_x + 1, it->y + (tb_slot - len) / 2, 3, len, col, 255);
            else
                gfx_rect(tb_x + tb_w - 4, it->y + (tb_slot - len) / 2, 3, len, col, 255);
        }
    }

    /* The clock/date and show-desktop affordance are also transparent by
       default. A square, subtle highlight appears only while hovered. */
    char t[16], d[16];
    ksnprintf(t, sizeof t, "%02d:%02d", now.hour, now.minute);
    if (cfg.tb_pos == TB_BOTTOM) {
        ksnprintf(d, sizeof d, "%04d-%02d-%02d", now.year, now.month, now.day);
        /* Compact tray: status/battery glyph on the left, time and date
           stacked and right-aligned like the supplied reference image. */
        const int tray_w = 130, tray_h = 44;
        int x = SW - 42 - tray_w;
        int y = tb_y + (tb_h - tray_h) / 2;
        bool hv = ui_btn(x, y, tray_w, tray_h, ACT_TB_BG);
        if (hv) gfx_rect(x, y, tray_w, tray_h, RGB(255, 255, 255), 18);
        int bx = x + 10, by = y + 13;
        gfx_rrect_stroke(bx, by, 17, 10, 2, RGB(222, 232, 241), 235);
        gfx_rect(bx + 17, by + 3, 2, 4, RGB(222, 232, 241), 235);
        gfx_rrect(bx + 2, by + 2, 10, 6, 1, RGB(211, 239, 248), 225);
        int tx = x + tray_w - 8;
        gfx_text(tx - gfx_text_w(t, FONT_S), y + 5, t, C_TEXT, FONT_S);
        gfx_text(tx - gfx_text_w(d, FONT_S), y + 22, d, RGB(211, 239, 248), FONT_S);
        int nx = SW - 38, ny = tb_y + (tb_h - 32) / 2;
        bool nh = ui_btn(nx, ny, 30, 32, ACT_TB_NOTIFICATIONS);
        if (nh || notifications_open) gfx_rect(nx, ny, 30, 32, RGB(255, 255, 255), notifications_open ? 54 : 28);
        /* Simple bell glyph, drawn in primitives so no extra font/icon asset is required. */
        gfx_rrect(nx + 9, ny + 8, 12, 13, 5, C_TEXT, 220);
        gfx_rect(nx + 7, ny + 18, 16, 3, C_TEXT, 220);
        gfx_circle16((nx + 15) * 16, (ny + 23) * 16, 2 * 16, C_TEXT, 220);
        bool sd = ui_btn(SW - 6, tb_y, 6, tb_h, ACT_TB_SHOWDESK);
        if (sd) gfx_rect(SW - 6, tb_y + 6, 2, tb_h - 12, RGB(255, 255, 255), 80);
    } else {
        ksnprintf(d, sizeof d, "%02d-%02d", now.month, now.day);
        int y = SH - 48;
        text_c(tb_x + tb_w / 2, y, t, C_TEXT, FONT_M);
        text_c(tb_x + tb_w / 2, y + 18, d, RGB(211, 239, 248), FONT_S);
        bool sd = ui_btn(tb_x, SH - 6, tb_w, 6, ACT_TB_SHOWDESK);
        if (sd) gfx_rect(tb_x + 6, SH - 5, tb_w - 12, 1, RGB(255, 255, 255), 80);
    }
}

static void draw_notifications(void) {
    uint64_t elapsed = uptime_ms() - notification_opened_ms;
    bool anim_live = notifications_animating && elapsed < 220;
    if (!notifications_open && !anim_live) return;
    if (cfg.tb_pos != TB_BOTTOM) return;
    if (notifications_animating && elapsed >= 220) notifications_animating = false;
    cur_layer = LAYER_OVERLAY;
    int w = imin(360, SW - 16), h = imin(360, SH - 90), x = SW - w - 14, y = tb_y - h - 10;
    if (notifications_animating) {
        int offset = (int)((220 - elapsed) * 24 / 220);
        y += notifications_anim_closing ? -offset : offset;
    }
    if (notifications_animating) dirty = true;
    if (x < 8) x = 8;
    if (y < 8) y = 8;
    gfx_shadow(x, y, w, h, 10);
    uint32_t panel_alpha = notifications_animating ? (uint32_t)(notifications_anim_closing ? (220 - elapsed) * 250 / 220 : elapsed * 250 / 220) : 250;
    gfx_rrect(x, y, w, h, 10, RGB(31, 35, 49), panel_alpha);
    gfx_rrect_stroke(x, y, w, h, 10, ACCENT, panel_alpha * 150 / 255);
    if (notifications_open) hit_add(x, y, w, h, ACT_TB_NOTIFICATIONS);
    char ntf_heading[48];
    ksnprintf(ntf_heading, sizeof ntf_heading, "Notifications  (%u)", notification_count);
    gfx_text(x + 18, y + 14, ntf_heading, C_TEXT, FONT_L);
    int clear_x = x + w - 94;
    bool hc = ui_btn(clear_x, y + 12, 78, 26, ACT_NTF_CLEAR);
    gfx_rrect(clear_x, y + 12, 78, 26, 6, hc ? RGB(73, 81, 103) : RGB(48, 53, 70), 255);
    text_c(clear_x + 39, y + 19, "Clear all", C_TEXT, FONT_S);
    gfx_rect(x + 14, y + 47, w - 28, 1, C_BORDER, 180);

    int cy = y + 57;
    if (!notification_count) {
        gfx_circle16((x + w / 2) * 16, (cy + 36) * 16, 14 * 16, RGB(64, 78, 99), 210);
        icon_draw(ICON_CHECK, x + w / 2 - 8, cy + 28, 16, C_TEXT);
        text_c(x + w / 2, cy + 62, "You're all caught up", C_TEXT, FONT_M);
        text_c(x + w / 2, cy + 84, "New system notices appear here.", C_DIM, FONT_S);
        cy += 108;
    } else {
        for (unsigned i = notification_count; i > 0; i--) {
            struct shell_notification *n = notification_at(i - 1);
            if (!n || cy + 54 > y + h - 82) break;
            bool hv = ui_btn(x + 12, cy, w - 24, 50, ACT_NTF_ITEM + (int)(i - 1));
            gfx_rrect(x + 12, cy, w - 24, 50, 7, hv ? RGB(56, 62, 82) : RGB(39, 44, 60), 255);
            uint32_t dot = n->level >= 2 ? RGB(255, 154, 112) : ACCENT;
            gfx_circle16((x + 27) * 16, (cy + 17) * 16, 4 * 16, dot, 255);
            gfx_text(x + 39, cy + 7, n->title, C_TEXT, FONT_S);
            gfx_text(x + 39, cy + 25, n->body, C_DIM, FONT_S);
            cy += 56;
        }
    }
    int sy = y + h - 70;
    gfx_rect(x + 14, sy, w - 28, 1, C_BORDER, 170);
    gfx_text(x + 18, sy + 12, "Quick status", C_TEXT, FONT_S);
    char status[80];
    ksnprintf(status, sizeof status, "Free memory: %lu MiB   |   Software renderer",
              (unsigned long)(pmm_free_bytes() >> 20));
    gfx_text(x + 18, sy + 31, status, C_DIM, FONT_S);
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static bool contains_ci(const char *hay, const char *needle) {
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && hay[i + j] && lower(hay[i + j]) == lower(needle[j])) j++;
        if (!needle[j]) return true;
    }
    return !needle[0];
}

static int search_matches(int *out) {
    int n = 0;
    for (int a = 0; a < APP_COUNT; a++)
        if (contains_ci(app_label[a], search) || contains_ci(app_title[a], search)) out[n++] = a;
    return n;
}

static void draw_start_menu(void) {
    cur_layer = LAYER_OVERLAY;
    gfx_clip_reset();
    int x, y, w, h;
    start_geom(&x, &y, &w, &h);
    gfx_shadow(x, y, w, h, 10);
    uint64_t start_elapsed = uptime_ms() - start_anim_ms;
    uint32_t start_alpha = start_elapsed < 220 ? (uint32_t)(start_elapsed * 204 / 220) : 204;
    gfx_rrect(x, y, w, h, 10, RGB(33, 36, 50), start_alpha);
    gfx_rrect_stroke(x, y, w, h, 10, RGB(255, 255, 255), start_alpha * 34 / 204);
    if (start_elapsed < 220) dirty = true;
    hit_add(x, y, w, h, ACT_SM_BODY);

    /* search box */
    bool sh = ui_btn(x + 20, y + 20, w - 40, 36, ACT_SM_SEARCH);
    gfx_rrect(x + 20, y + 20, w - 40, 36, 18, C_FIELD, 218);
    gfx_rrect_stroke(x + 20, y + 20, w - 40, 36, 18, ACCENT, sh || search_len ? 220 : 90);
    icon_draw(ICON_SEARCH, x + 34, y + 29, 18, C_DIM);
    if (search_len == 0) gfx_text(x + 62, y + 32, "Search apps", C_DIM, FONT_S);
    else {
        int tw = gfx_text(x + 62, y + 32, search, C_TEXT, FONT_S);
        if (blink_on) gfx_rect(x + 62 + tw + 1, y + 30, 1, 16, ACCENT, 255);
    }
    if (search_len == 0 && blink_on) gfx_rect(x + 61, y + 30, 1, 16, ACCENT, 255);

    int body_bottom = y + h - 60;
    if (search_len == 0) {
        gfx_rrect(x + 20, y + 66, w - 40, 4, 2, ACCENT, 235);
        gfx_text(x + 24, y + 78, "PINNED APPS", C_DIM, FONT_S);
        gfx_text(x + w - 82, y + 78, "ALL APPS", ACCENT, FONT_S);
        int shown = 0;
        for (int a = 0; a < APP_COUNT; a++) {
            if (!cfg.pin_start[a]) continue;
            int tx = x + 20 + (shown % 5) * 92, ty = y + 96 + (shown / 5) * 84;
            bool hv = ui_btn(tx, ty, 88, 84, ACT_SM_APP + a);
            if (hv) gfx_rrect(tx, ty, 88, 84, 8, RGB(255, 255, 255), 20);
            icon_draw(app_icon[a], tx + 32, ty + 10, 24, C_TEXT);
            text_c(tx + 44, ty + 52, app_label[a], C_TEXT, FONT_S);
            shown++;
        }
        if (!shown) gfx_text(x + 28, y + 100, "Nothing pinned. Use Settings > Personalization.", C_DIM, FONT_S);
        char info[64];
        ksnprintf(info, sizeof info, "Memory free: %lu MiB", (unsigned long)(pmm_free_bytes() >> 20));
        gfx_text(x + 28, body_bottom - 22, info, C_DIM, FONT_S);
    } else {
        int res[APP_COUNT], n = search_matches(res);
        gfx_text(x + 28, y + 76, "Best match", C_TEXT, FONT_M);
        for (int i = 0; i < n; i++) {
            int ry = y + 104 + i * 46;
            bool hv = ui_btn(x + 20, ry, w - 40, 42, ACT_SM_APP + res[i]);
            if (i == 0) gfx_rrect(x + 20, ry, w - 40, 42, 8, ACCENT, 40);
            else if (hv) gfx_rrect(x + 20, ry, w - 40, 42, 8, RGB(255, 255, 255), 20);
            icon_draw(app_icon[res[i]], x + 34, ry + 9, 24, C_TEXT);
            gfx_text(x + 72, ry + 7, app_label[res[i]], C_TEXT, FONT_S);
            gfx_text(x + 72, ry + 22, app_title[res[i]], C_DIM, FONT_S);
        }
        if (!n) gfx_text(x + 28, y + 108, "No apps match your search.", C_DIM, FONT_S);
    }

    /* user bar */
    gfx_rrect(x, y + h - 56, w, 56, 10, RGB(26, 28, 40), 218);
    gfx_rect(x, y + h - 56, w, 12, RGB(26, 28, 40), 218);
    gfx_rect(x + 1, y + h - 56, w - 2, 1, RGB(255, 255, 255), 18);
    gfx_circle16((x + 44) * 16, (y + h - 28) * 16, 16 * 16, ACCENT, 255);
    text_c(x + 44, y + h - 34, "A", C_DARKTX, FONT_M);
    gfx_text(x + 70, y + h - 34, "Aurora User", C_TEXT, FONT_S);
    int px = x + w - 56, py = y + h - 44;
    bool ph = ui_btn(px, py, 32, 32, ACT_SM_POWER);
    if (ph) gfx_rrect(px, py, 32, 32, 8, RGB(255, 255, 255), 24);
    icon_draw(ICON_POWER, px + 6, py + 6, 20, C_TEXT);
}

/* ======================================================================
 *  Boot splash and classic 2010s-inspired login screen
 * ====================================================================== */

static void draw_boot_screen(uint64_t elapsed) {
    /* 5.1.0 boot animation: deterministic framebuffer clear and lightweight progress. */
    gfx_clip_reset();
    gfx_frame_begin();
    gfx_rect(0, 0, SW, SH, RGB(4,  9, 24), 255);
    gfx_rect(0, SH / 3, SW, SH / 3, RGB(7, 27, 48), 255);
    gfx_rect(0, SH * 2 / 3, SW, SH / 3, RGB(5, 18, 36), 255);

    /* Crisp aurora ribbons instead of broad translucent/blurred blobs. */
    int drift = (int)((elapsed / 18) % (SW / 5 + 1));
    gfx_rect(SW / 8 + drift / 3, SH / 3, SW * 3 / 5, 2, RGB(24, 116, 137), 150);
    gfx_rect(SW / 5 - drift / 4, SH / 3 + 5, SW / 2, 1, RGB(46, 196, 183), 180);
    gfx_rect(SW / 6, SH * 2 / 3, SW * 2 / 3, 1, RGB(33, 110, 142), 160);

    int cx = SW / 2, cy = SH / 2 - 24;
    /* Minimal emblem: sharp outer ring and a clear inner accent. */
    gfx_ring16(cx * 16, cy * 16, 37 * 16, 2 * 16, RGB(92, 222, 231), 220, false);
    gfx_ring16(cx * 16, cy * 16, 28 * 16, 1 * 16, RGB(52, 146, 186), 180, false);
    icon_draw(ICON_LOGO, cx - 18, cy - 18, 36, RGB(226, 250, 255));

    /* Twelve crisp dots orbit the emblem. Integer positions avoid libm. */
    static const int ox[12] = { 0, 13, 22, 26, 22, 13, 0, -13, -22, -26, -22, -13 };
    static const int oy[12] = { -26, -22, -13, 0, 13, 22, 26, 22, 13, 0, -13, -22 };
    int head = (int)((elapsed / 75) % 12);
    for (int i = 0; i < 12; i++) {
        int age = (i - head + 12) % 12;
        uint32_t col = age < 3 ? RGB(140, 250, 241) :
                       age < 6 ? RGB(61, 184, 202) : RGB(35, 91, 128);
        gfx_circle16((cx + ox[i]) * 16, (cy + oy[i]) * 16, (age < 3 ? 3 : 2) * 16, col, 255);
    }

    /* Slim progress line: grows steadily, no captions or brand text. */
    int bw = imin(240, SW - 64), bx = (SW - bw) / 2, by = cy + 74;
    gfx_rect(bx, by, bw, 2, RGB(20, 55, 77), 255);
    int progress = (int)(elapsed * 100 / 1700);
    if (progress > 100) progress = 100;
    if (progress < 2) progress = 2;
    gfx_rect(bx, by, (bw * progress) / 100, 2, RGB(100, 231, 224), 255);
}

static void draw_login_screen(void) {
    cur_layer = LAYER_OVERLAY;
    gfx_clip_reset();
    gfx_frame_begin();

    /* Keep the wallpaper visible through a single translucent grey veil.
       The previous three full-screen alpha passes made every login repaint
       needlessly expensive in the software renderer (notably at 1080p in
       VMware). One pass gives the same glass effect with one-third the work. */
    gfx_rect(0, 0, SW, SH, RGB(53, 60, 70), 104);

    struct rtc_time rt;
    rtc_read(&rt);
    static const char *months[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    char date[64], clockbuf[32], weekday[16];
    /* Gregorian weekday calculation (0=Sunday), used for the compact Sonoma-style date line. */
    static const char *weekdays[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    int yy = rt.year, mm = rt.month, dd = rt.day;
    static const int month_offset[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (mm >= 1 && mm <= 12) {
        if (mm < 3) yy--;
    } else mm = 1;
    int weekday_index = (yy + yy / 4 - yy / 100 + yy / 400 + month_offset[mm - 1] + dd) % 7;
    ksnprintf(weekday, sizeof weekday, "%s", weekdays[weekday_index]);
    int hour = rt.hour % 24;
    const char *ampm = hour >= 12 ? "PM" : "AM";
    int h12 = hour % 12; if (h12 == 0) h12 = 12;
    ksnprintf(clockbuf, sizeof clockbuf, "%02d:%02d %s", h12, rt.minute, ampm);
    ksnprintf(date, sizeof date, "%s, %s %d", weekday,
              (rt.month >= 1 && rt.month <= 12) ? months[rt.month - 1] : "Month", rt.day);
    /* Sonoma-inspired hierarchy: compact weekday/date above a prominent, very bold clock. */
    text_c(SW / 2, SH / 10, date, RGB(218, 231, 239), FONT_S);
    /* Oversized lock-screen clock: 3x atlas glyphs with a bold expansion pass.
       Center by measured scaled width and keep the date line compact above it. */
    int clock_scale = (SW >= 1000) ? 3 : 2;
    int clock_chars = 0;
    for (const char *cp = clockbuf; *cp; ++cp) clock_chars++;
    int clock_width = gfx_text_w(clockbuf, FONT_L) * clock_scale + clock_chars;
    int clock_y = SH / 10 + 28;
    gfx_text_scaled((SW - clock_width) / 2, clock_y, clockbuf,
                    RGB(245, 251, 255), FONT_L, clock_scale, true);

    int cx = SW / 2;
    int avatar_cx = cx, avatar_cy = SH * 72 / 100;
    gfx_circle16(avatar_cx * 16, avatar_cy * 16, 26 * 16, RGB(68, 111, 137), 255);
    gfx_circle16(avatar_cx * 16, (avatar_cy - 7) * 16, 7 * 16, RGB(229, 242, 248), 255);
    gfx_rrect(avatar_cx - 13, avatar_cy + 2, 26, 15, 7, RGB(229, 242, 248), 255);
    text_c(cx, avatar_cy + 38, "Aurora User", RGB(245, 250, 253), FONT_M);

    int bw = imin(240, SW - 48), bh = 34;
    int bx = cx - bw / 2, by = avatar_cy + 64;
    bool hov = ui_btn(bx, by, bw, bh, ACT_LOGIN_SIGNIN);
    gfx_rrect_grad(bx, by, bw, bh, 8,
                   hov ? RGB(215, 244, 250) : RGB(188, 222, 235),
                   hov ? RGB(112, 190, 218) : RGB(77, 151, 187));
    gfx_rrect_stroke(bx, by, bw, bh, 8, RGB(238, 250, 255), 190);
    text_c(cx, by + 9, "Touch or click to sign in", RGB(17, 47, 65), FONT_S);
}

/* ======================================================================
 *  Compose a frame
 * ====================================================================== */

static void compose(void) {
    nhits = 0;
    if (shell_screen == SCREEN_BOOT) {
        draw_boot_screen(uptime_ms() - screen_started);
        return;
    }
    if (shell_screen == SCREEN_LOGIN) {
        draw_login_screen();
        return;
    }
    layout_taskbar();
    int sx, sy, sw, sh;
    start_geom(&sx, &sy, &sw, &sh);
    over_overlay = in_rect(mouse_x, mouse_y, tb_x, tb_y, tb_w, tb_h) ||
                   (start_open && in_rect(mouse_x, mouse_y, sx, sy, sw, sh));
    top_at_mouse = win_at(mouse_x, mouse_y);

    gfx_frame_begin();
    draw_desktop();
    uint64_t now_ms = uptime_ms();
    for (int i = 0; i < zcount; ) {
        int a = zorder[i];
        struct win *w = &wins[a];
        if (w->closing && now_ms - w->anim_ms >= 180) {
            w->open = false; w->min = false; w->closing = false;
            z_remove(a);
            continue;
        }
        if (w->open && (!w->min || w->closing)) draw_window(a);
        if ((w->open && now_ms - w->anim_ms < 180) || w->closing) dirty = true;
        i++;
    }
    draw_taskbar();
    if (start_open) draw_start_menu();
    draw_notifications();
    if (notifications_animating) dirty = true;
    if (login_transition) {
        uint64_t elapsed = uptime_ms() - login_transition_ms;
        if (elapsed >= 320) login_transition = false;
        else {
            uint32_t alpha = (uint32_t)((320 - elapsed) * 210 / 320);
            gfx_rect(0, 0, SW, SH, RGB(8, 12, 22), alpha);
            dirty = true;
        }
    }
    frames++;
}

/* ======================================================================
 *  Actions and input
 * ====================================================================== */

static void close_start(void) { start_open = false; start_anim_ms = uptime_ms(); }

static void toggle_start(bool search_focus) {
    if (start_open && !search_focus) { close_start(); return; }
    start_open = true; start_anim_ms = uptime_ms(); search_len = 0; search[0] = 0;
}

static void shutdown(void) {
    outw(0x604, 0x2000);       /* QEMU */
    outw(0xB004, 0x2000);      /* Bochs / old QEMU */
    outw(0x4004, 0x3400);      /* VirtualBox */
    gfx_clip_reset();
    gfx_frame_begin();
    gfx_rect(0, 0, SW, SH, RGB(0, 0, 0), 255);
    text_c(SW / 2, SH / 2 - 8, "It is now safe to turn off your computer.", C_TEXT, FONT_M);
    gfx_present();
    halt_forever();
}

static void raise_win(int a) { wins[a].min = false; z_push(a); }

static void set_resolution(int index) {
    if (index < 0 || index >= 6) return;
    int width = resolution_w[index], height = resolution_h[index];
    if (SW == width && SH == height) {
        resolution_status = "That resolution is already active.";
        resolution_status_ok = true; resolution_status_error = false;
        return;
    }

    struct limine_framebuffer previous = active_fb;
    if (!video_set_mode(&active_fb, width, height)) {
        resolution_status = "Mode rejected or mapped framebuffer space is insufficient.";
        resolution_status_ok = false; resolution_status_error = true;
        return;
    }
    /* The panic/diagnostic console shares the Limine framebuffer descriptor.
       Sync it as soon as the hardware changes so exceptions cannot draw with
       the previous pitch while the new mode is active. */
    if (original_fb) *original_fb = active_fb;
    if (!gfx_init(&active_fb)) {
        /* Roll hardware, renderer metadata and panic-console metadata back. */
        active_fb = previous;
        (void)video_set_mode(&active_fb, SW, SH);
        if (original_fb) *original_fb = active_fb;
        resolution_status = "Not enough memory to apply this resolution.";
        resolution_status_ok = false; resolution_status_error = true;
        return;
    }

    SW = gfx_w(); SH = gfx_h();
    ps2_init(SW, SH);
    layout_taskbar();
    for (int a = 0; a < APP_COUNT; a++) if (wins[a].open) clamp_win(&wins[a]);
    resolution_status = "Display resolution changed successfully.";
    resolution_status_ok = true; resolution_status_error = false;
    dirty = true;
}

static void do_action(int id, int mx, int my) {
    if (id == ACT_NONE) return;
    int a;
    if (id == ACT_TB_NOTIFICATIONS) {
        uint64_t now = uptime_ms();
        if (notifications_open) {
            notifications_open = false; notifications_animating = true; notifications_anim_closing = true;
        } else {
            notifications_open = true; notifications_animating = true; notifications_anim_closing = false;
        }
        notification_opened_ms = now; dirty = true; return;
    }
    if (id == ACT_NTF_CLEAR) { notification_count = 0; notification_next = 0; dirty = true; return; }
    if (id >= ACT_NTF_ITEM && id < ACT_NTF_ITEM + NOTIFY_CAP) {
        unsigned idx = (unsigned)(id - ACT_NTF_ITEM);
        /* Dismiss selected record by compacting the bounded ring into oldest-first order. */
        if (idx < notification_count) {
            struct shell_notification tmp[NOTIFY_CAP]; unsigned n = 0;
            for (unsigned i = 0; i < notification_count; i++) if (i != idx) {
                struct shell_notification *entry = notification_at(i);
                if (entry) tmp[n++] = *entry;
            }
            for (unsigned i = 0; i < n; i++) notification_log[i] = tmp[i];
            notification_count = n; notification_next = n % NOTIFY_CAP;
        }
        dirty = true; return;
    }
    if (id >= ACT_WIN_BODY && id < ACT_WIN_BODY + APP_COUNT) raise_win(id - ACT_WIN_BODY);
    else if (id >= ACT_WIN_TITLE && id < ACT_WIN_TITLE + APP_COUNT) {
        a = id - ACT_WIN_TITLE; raise_win(a);
        drag_app = a; drag_dx = mx - wins[a].x; drag_dy = my - wins[a].y;
    }
    else if (id >= ACT_WIN_CLOSE && id < ACT_WIN_CLOSE + APP_COUNT) close_app(id - ACT_WIN_CLOSE);
    else if (id >= ACT_WIN_MIN && id < ACT_WIN_MIN + APP_COUNT) wins[id - ACT_WIN_MIN].min = true;
    else if (id >= ACT_WIN_MAX && id < ACT_WIN_MAX + APP_COUNT) {
        struct win *w = &wins[id - ACT_WIN_MAX];
        if (!w->maximized) {
            w->restore_x = w->x; w->restore_y = w->y; w->restore_w = w->w; w->restore_h = w->h;
            maximize_win(w);
        } else {
            w->x = w->restore_x; w->y = w->restore_y; w->w = w->restore_w; w->h = w->restore_h; w->maximized = false;
            clamp_win(w);
        }
        w->anim_ms = uptime_ms(); dirty = true;
    }
    else if (id == ACT_BROWSER_ADDRESS) {
        browser_url_focus = true; browser_attempted = false;
        /* Selecting the address bar selects its current text for replacement. */
        browser_url[0] = 0; browser_url_len = 0;
    }
    else if (id == ACT_BROWSER_GO || id == ACT_BROWSER_RELOAD) browser_attempt();
    else if (id == ACT_TB_START) toggle_start(false);
    else if (id == ACT_TB_SEARCH) toggle_start(true);
    else if (id >= ACT_TB_APP && id < ACT_TB_APP + APP_COUNT) {
        a = id - ACT_TB_APP;
        if (!wins[a].open) open_app(a);
        else if (wins[a].min) { raise_win(a); start_open = false; }
        else if (top_app() == a) wins[a].min = true;
        else raise_win(a);
    }
    else if (id == ACT_TB_SHOWDESK) { for (a = 0; a < APP_COUNT; a++) wins[a].min = true; }
    else if (id >= ACT_SM_APP && id < ACT_SM_APP + APP_COUNT) open_app(id - ACT_SM_APP);
    else if (id == ACT_SM_POWER) shutdown();
    else if (id == ACT_DESK_BG) desk_sel = -1;
    else if (id >= ACT_DESK_ICON && id < ACT_DESK_ICON + 3) {
        int i = id - ACT_DESK_ICON; uint64_t t = uptime_ms();
        if (last_click_id == id && t - last_click_ms < 500) { open_app(desk_apps[i]); last_click_id = -1; }
        else { desk_sel = i; last_click_id = id; last_click_ms = t; }
    }
    else if (id >= ACT_SET_RESOLUTION && id < ACT_SET_RESOLUTION + 6) set_resolution(id - ACT_SET_RESOLUTION);
    else if (id >= ACT_SET_RGB && id < ACT_SET_RGB + 6) { switch (id - ACT_SET_RGB) { case 0: custom_r = custom_r > 15 ? custom_r - 16 : 0; break; case 1: custom_g = custom_g > 15 ? custom_g - 16 : 0; break; case 2: custom_b = custom_b > 15 ? custom_b - 16 : 0; break; case 3: custom_r = custom_r < 240 ? custom_r + 16 : 255; break; case 4: custom_g = custom_g < 240 ? custom_g + 16 : 255; break; case 5: custom_b = custom_b < 240 ? custom_b + 16 : 255; break; } custom_accent_enabled = true; }
    else if (id >= ACT_SET_WALLPAPER && id < ACT_SET_WALLPAPER + 5) gfx_set_wallpaper(wallpaper_ids[id - ACT_SET_WALLPAPER], gfx_wallpaper_mode());
    else if (id >= ACT_SET_WALLMODE && id < ACT_SET_WALLMODE + 5) gfx_set_wallpaper(gfx_wallpaper_preset(), id - ACT_SET_WALLMODE);
    else if (id >= ACT_SET_PAGE && id < ACT_SET_PAGE + 3) { settings_page = id - ACT_SET_PAGE; settings_scroll = 0; }
    else if (id == ACT_SET_SCROLL_UP) settings_scroll = settings_scroll > 80 ? settings_scroll - 80 : 0;
    else if (id == ACT_SET_SCROLL_DOWN) settings_scroll = settings_scroll < 480 ? settings_scroll + 80 : 480;
    else if (id >= ACT_SET_TBPOS && id < ACT_SET_TBPOS + 3) {
        cfg.tb_pos = id - ACT_SET_TBPOS; layout_taskbar();
        for (a = 0; a < APP_COUNT; a++) if (wins[a].open) clamp_win(&wins[a]);
    }
    else if (id >= ACT_SET_ICON && id < ACT_SET_ICON + 3) cfg.icon = icon_sizes[id - ACT_SET_ICON];
    else if (id >= ACT_SET_ACCENT && id < ACT_SET_ACCENT + 5) {
        cfg.accent = id - ACT_SET_ACCENT;
        custom_accent_enabled = false;
        custom_r = (uint8_t)((accents[cfg.accent] >> 16) & 255);
        custom_g = (uint8_t)((accents[cfg.accent] >> 8) & 255);
        custom_b = (uint8_t)(accents[cfg.accent] & 255);
    }
    else if (id == ACT_SET_TBCOLOR) {
        struct aurora_taskbar_style tbstyle; aurora_taskbar_get_style(&tbstyle);
        (void)aurora_taskbar_set_style(!tbstyle.color_enabled, tbstyle.scheme);
    }
    else if (id >= ACT_SET_TBSCHEME && id < ACT_SET_TBSCHEME + (int)AURORA_TASKBAR_SCHEME_COUNT) {
        struct aurora_taskbar_style tbstyle; aurora_taskbar_get_style(&tbstyle);
        (void)aurora_taskbar_set_style(tbstyle.color_enabled, (uint32_t)(id - ACT_SET_TBSCHEME));
    }
    else if (id >= ACT_SET_PINTB && id < ACT_SET_PINTB + APP_COUNT) cfg.pin_tb[id - ACT_SET_PINTB] ^= 1;
    else if (id >= ACT_SET_PINST && id < ACT_SET_PINST + APP_COUNT) cfg.pin_start[id - ACT_SET_PINST] ^= 1;
    else if (id == ACT_NOTES_FILE) { notes_file_flyout = !notes_file_flyout; notes_view_flyout = false; }
    else if (id == ACT_NOTES_VIEW) { notes_view_flyout = !notes_view_flyout; notes_file_flyout = false; }
    else if (id == ACT_NOTES_NEW || id == ACT_NOTES_CLEAR) { notes_len = 0; notes[0] = 0; notes_file_flyout = false; }
    else if (id == ACT_NOTES_STATUS) { notes_status_visible = !notes_status_visible; notes_view_flyout = false; }
    else if (id >= ACT_FILES_NAV && id < ACT_FILES_NAV + 4) { files_nav = id - ACT_FILES_NAV; files_sel = -1; }
    else if (id >= ACT_FILES_ROW && id < ACT_FILES_ROW + 16) files_sel = id - ACT_FILES_ROW;
    else if (id == ACT_FILES_EDIT) files_status = files_sel >= 0 ? "Edit selected item (demo VFS)" : "Select an item first.";
    else if (id == ACT_FILES_RENAME) files_status = files_sel >= 0 ? "Rename requires writable AuroraFS." : "Select an item first.";
    else if (id == ACT_FILES_DELETE) files_status = files_sel >= 0 ? "Delete requires writable AuroraFS." : "Select an item first.";
}

static void on_mouse_down(int mx, int my) {
    int id = hit_find(mx, my);
    if (shell_screen == SCREEN_LOGIN) {
        if (id == ACT_LOGIN_SIGNIN) {
            shell_screen = SCREEN_DESKTOP;
            login_transition = true;
            login_transition_ms = uptime_ms();
            dirty = true;
        }
        return;
    }
    if (shell_screen != SCREEN_DESKTOP) return;
    bool in_start = (id >= ACT_SM_APP && id <= ACT_SM_BODY) || id == ACT_TB_START || id == ACT_TB_SEARCH;
    if (start_open && !in_start) close_start();
    if ((notes_file_flyout || notes_view_flyout) && id != ACT_NOTES_FILE && id != ACT_NOTES_VIEW && id != ACT_NOTES_NEW && id != ACT_NOTES_CLEAR && id != ACT_NOTES_STATUS) { notes_file_flyout = false; notes_view_flyout = false; }
    if (notifications_open && id != ACT_TB_NOTIFICATIONS && id != ACT_NTF_CLEAR && !(id >= ACT_NTF_ITEM && id < ACT_NTF_ITEM + NOTIFY_CAP)) {
        notifications_open = false; notifications_animating = true; notifications_anim_closing = true; notification_opened_ms = uptime_ms();
    }
    do_action(id, mx, my);
    dirty = true;
}

static void handle_mouse(void) {
    static int px = -1, py = -1;
    static uint8_t pb;
    int mx = mouse_x, my = mouse_y;
    uint8_t b = mouse_btn;
    int wheel = ps2_take_wheel();
    if (wheel) {
        int wa = win_at(mx, my);
        if (wa == APP_SETTINGS) {
            settings_scroll -= wheel * 32;
            if (settings_scroll < 0) settings_scroll = 0;
            if (settings_scroll > 480) settings_scroll = 480;
            dirty = true;
        } else if (wa == APP_FILES) {
            files_scroll -= wheel;
            if (files_scroll < 0) files_scroll = 0;
            int maxscroll = fnav[files_nav].n > 1 ? fnav[files_nav].n - 1 : 0;
            if (files_scroll > maxscroll) files_scroll = maxscroll;
            dirty = true;
        } else if (wa == APP_NOTES) {
            notes_scroll -= wheel;
            if (notes_scroll < 0) notes_scroll = 0;
            dirty = true;
        } else if (wa == APP_BROWSER) {
            browser_scroll -= wheel * 24;
            if (browser_scroll < 0) browser_scroll = 0;
            if (browser_scroll > 480) browser_scroll = 480;
            dirty = true;
        } else if (wa >= 0) {
            /* Other apps currently have fixed-height content; consume wheel safely. */
            dirty = true;
        }
    }
    if (mx != px || my != py) {
        if (drag_app >= 0 && (b & 1)) {
            struct win *w = &wins[drag_app];
            w->x = mx - drag_dx; w->y = my - drag_dy;
            clamp_win(w);
            dirty = true;
        } else {
            int id = hit_find(mx, my);
            if (id != hover_id) { hover_id = id; dirty = true; }
        }
    }
    if ((b & 1) && !(pb & 1)) on_mouse_down(mx, my);
    if (!(b & 1) && (pb & 1)) drag_app = -1;
    px = mx; py = my; pb = b;
}

static void handle_key(int k);

static bool shell_gui_event(const struct aurora_shell_event *event, void *context) {
    (void)context;
    if (event->type == AURORA_SHELL_EVENT_MOUSE) { handle_mouse(); return true; }
    if (event->type == AURORA_SHELL_EVENT_KEY) { handle_key((int)event->value); dirty = true; return true; }
    return false;
}

static bool term_is(const char *s) { int i = 0; while (s[i] && terminal_line[i] && s[i] == terminal_line[i]) i++; return s[i] == 0 && terminal_line[i] == 0; }
static void handle_key(int k) {
    if (shell_screen == SCREEN_BOOT) return;
    if (shell_screen == SCREEN_LOGIN) {
        if (k == '\n' || k == ' ') {
            shell_screen = SCREEN_DESKTOP;
            dirty = true;
        }
        return;
    }
    if (k == KEY_SUPER) { toggle_start(false); return; }
    if (start_open) {
        if (k == 27) close_start();
        else if (k == '\b') { if (search_len) search[--search_len] = 0; }
        else if (k == '\n') {
            int res[APP_COUNT], n = search_matches(res);
            if (search_len && n) open_app(res[0]);
        }
        else if (k >= 32 && k < 127 && search_len < 30) { search[search_len++] = (char)k; search[search_len] = 0; }
        return;
    }
    if (top_app() == APP_BROWSER && browser_url_focus) {
        if (k == 27) browser_url_focus = false;
        else if (k == '\b') { if (browser_url_len > 0) browser_url[--browser_url_len] = 0; }
        else if (k == '\n') browser_attempt();
        else if (k >= 32 && k < 127 && browser_url_len < (int)sizeof(browser_url) - 2) {
            browser_url[browser_url_len++] = (char)k; browser_url[browser_url_len] = 0;
        }
        return;
    }
    if (top_app() == APP_TERMINAL) {
        if (k == 27) { terminal_about_open = false; terminal_len = 0; terminal_line[0] = 0; }
        else if (k == '\b') { if (terminal_len) terminal_line[--terminal_len] = 0; }
        else if (k == '\n') {
            if (term_is("ver")) { terminal_about_open = true; terminal_output = "Aurora OS version window opened."; }
            else if (term_is("help")) terminal_output = "Commands: help, ver, clear, fastfetch, uname, ls";
            else if (term_is("clear")) terminal_output = "Aurora Terminal 1.0.5.0  |  Type help to get started.";
            else if (term_is("fastfetch")) terminal_output = "Aurora OS | x86_64 | Kernel API 2.2 | Shell API 4.0 | Alpha 1.0.8";
            else if (term_is("uname")) terminal_output = "Aurora alpha-1.0.5.0 x86_64 kernel-api-2.0";
            else if (term_is("ls")) terminal_output = "Desktop  Documents  Downloads  Notes (demo VFS)";
            else terminal_output = "Command unavailable in kernel shell. Type help.";
            terminal_len = 0; terminal_line[0] = 0;
        } else if (k >= 32 && k < 127 && terminal_len < (int)sizeof(terminal_line) - 2) { terminal_line[terminal_len++] = (char)k; terminal_line[terminal_len] = 0; }
        return;
    }
    if (top_app() == APP_NOTES) {
        if (k == '\b') { if (notes_len) notes[--notes_len] = 0; }
        else if ((k == '\n' || (k >= 32 && k < 127)) && notes_len < (int)sizeof(notes) - 1) { notes[notes_len++] = (char)k; notes[notes_len] = 0; }
    }
}

void gui_run(struct limine_framebuffer *f) {
    if (!f) return;
    original_fb = f;
    active_fb = *f;
    if (!gfx_init(&active_fb)) return;
    SW = gfx_w(); SH = gfx_h();
    resolution_status = "Choose a mode to apply it immediately.";
    resolution_status_ok = false; resolution_status_error = false;
    ps2_init(SW, SH);
    rtc_read(&now);
    notes_len = 0; notes[0] = 0;
    notification_count = 0; notification_next = 0;
    notification_push("Aurora shell ready", "Desktop services started successfully.", 0);
    notification_push("Memory manager", "Static UI state enabled; no heap allocation for notifications.", 0);
    shell_screen = SCREEN_BOOT;
    screen_started = uptime_ms();
    aurora_shell_reset_handlers();
    (void)aurora_shell_register_handler(AURORA_SHELL_LEVEL_SYSTEM, shell_gui_event, 0);

    int cur_x = -1000, cur_y = -1000;
    uint64_t last_sec = uptime_ms() / 1000, last_blink = uptime_ms() / 500;
    uint64_t last_integrity_check = uptime_ms();
    uint64_t last_boot_frame = uptime_ms() / 50;

    for (;;) {
        bool got = ps2_poll();
        struct aurora_shell_event mouse_event = { AURORA_SHELL_EVENT_MOUSE, 0, uptime_ms() };
        (void)aurora_shell_dispatch(&mouse_event);
        int k;
        while ((k = ps2_getkey())) {
            struct aurora_shell_event key_event = { AURORA_SHELL_EVENT_KEY, k, uptime_ms() };
            (void)aurora_shell_dispatch(&key_event);
        }

        uint64_t ms = uptime_ms();
        if (ms - last_integrity_check >= 30000) {
            last_integrity_check = ms;
            if (!aurora_security_check_integrity() || !aurora_security_check_invariants()) {
                (void)aurora_security_audit(AURORA_SEC_INTEGRITY_FAIL, AURORA_SEC_CRITICAL,
                    "periodic kernel security self-check failed");
            }
        }
        if (shell_screen == SCREEN_BOOT) {
            if (ms - screen_started >= 1800) {
                shell_screen = SCREEN_LOGIN;
                screen_started = ms;
                dirty = true;
            } else if (ms / 50 != last_boot_frame) {
                last_boot_frame = ms / 50;
                dirty = true;
            }
        }
        if (ms / 500 != last_blink) {
            last_blink = ms / 500; blink_on = last_blink & 1;
            if (start_open || top_app() == APP_NOTES) dirty = true;
        }
        if (ms / 1000 != last_sec) {
            last_sec = ms / 1000;
            int old_min = now.minute;
            rtc_read(&now);
            if (now.minute != old_min) dirty = true;
            fps_hist[fps_idx] = frames; fps_idx = (fps_idx + 1) % 60; frames = 0;
            if (wins[APP_MONITOR].open && !wins[APP_MONITOR].min) dirty = true;
            if (wins[APP_SETTINGS].open && !wins[APP_SETTINGS].min && settings_page == 0) dirty = true;
        }

        if (dirty) {
            dirty = false;
            compose();
            gfx_present();
            if (shell_screen != SCREEN_BOOT) gfx_draw_cursor(mouse_x, mouse_y);
            cur_x = mouse_x; cur_y = mouse_y;
        } else if (mouse_x != cur_x || mouse_y != cur_y) {
            gfx_restore(cur_x, cur_y, CURSOR_W, CURSOR_H);
            if (shell_screen != SCREEN_BOOT) gfx_draw_cursor(mouse_x, mouse_y);
            cur_x = mouse_x; cur_y = mouse_y;
        }
        if (!got) __asm__ volatile ("pause");
    }
}
