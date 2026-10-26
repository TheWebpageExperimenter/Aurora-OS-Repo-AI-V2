/* PS/2 controller driver (polled): keyboard (scancode set 1) and mouse. */
#include "input.h"

int mouse_x, mouse_y;
uint8_t mouse_btn;
int mouse_wheel;

static int scr_w, scr_h;
static uint8_t pkt[4];
static int pkt_i, pkt_len = 3;
static bool shift, caps, e0;

#define KQ 64
static uint8_t kq[KQ];
static int kq_h, kq_t;

static const char map_n[58] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
};
static const char map_s[58] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b', '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' '
};

static bool wait_in(void)  { for (int i = 0; i < 200000; i++) if (!(inb(0x64) & 2)) return true; return false; }
static bool wait_out(void) { for (int i = 0; i < 200000; i++) if (inb(0x64) & 1)   return true; return false; }
static void ctl(uint8_t c) { wait_in(); outb(0x64, c); }

static uint8_t mouse_command(uint8_t b) {
    ctl(0xD4); wait_in(); outb(0x60, b);
    if (wait_out()) return inb(0x60);
    return 0;
}
static void mouse_write(uint8_t b) { (void)mouse_command(b); }
int ps2_take_wheel(void) { int v = mouse_wheel; mouse_wheel = 0; return v; }
static void mouse_enable_wheel(void) {
    /* IntelliMouse negotiation: failure leaves the standard 3-byte protocol. */
    mouse_write(0xF6);
    mouse_write(0xF3); mouse_write(200);
    mouse_write(0xF3); mouse_write(100);
    mouse_write(0xF3); mouse_write(80);
    uint8_t ack = mouse_command(0xF2);
    (void)ack;
    if (wait_out()) {
        uint8_t id = inb(0x60);
        if (id == 3 || id == 4) pkt_len = 4;
    }
    mouse_write(0xF4);
}

void ps2_init(int w, int h) {
    scr_w = w; scr_h = h; mouse_x = w / 2; mouse_y = h / 2;
    ctl(0xAD); ctl(0xA7);
    while (inb(0x64) & 1) (void)inb(0x60);
    ctl(0x20); wait_out();
    uint8_t cfg = inb(0x60);
    cfg |= 0x40;                            /* translate set 2 -> set 1 */
    cfg &= ~0x30;                           /* enable both port clocks */
    ctl(0x60); wait_in(); outb(0x60, cfg);
    ctl(0xAE); ctl(0xA8);
    wait_in(); outb(0x60, 0xF4);            /* keyboard: enable scanning */
    if (wait_out()) (void)inb(0x60);
    mouse_enable_wheel();                   /* wheel-capable mice, safe fallback */
}

static void kpush(uint8_t k) { int n = (kq_h + 1) % KQ; if (n != kq_t) { kq[kq_h] = k; kq_h = n; } }

int ps2_getkey(void) {
    if (kq_t == kq_h) return 0;
    int k = kq[kq_t]; kq_t = (kq_t + 1) % KQ; return k;
}

static void kbd_byte(uint8_t d) {
    if (d == 0xE0) { e0 = true; return; }
    if (e0) {
        e0 = false;
        if (d == 0x5B) kpush(KEY_SUPER);    /* left Windows/Super key (make) */
        return;
    }
    if (d & 0x80) {
        uint8_t c = d & 0x7F;
        if (c == 0x2A || c == 0x36) shift = false;
        return;
    }
    if (d == 0x2A || d == 0x36) { shift = true; return; }
    if (d == 0x3A) { caps = !caps; return; }
    if (d < sizeof(map_n)) {
        char c = shift ? map_s[d] : map_n[d];
        if (caps && c >= 'a' && c <= 'z') c -= 32;
        else if (caps && c >= 'A' && c <= 'Z' && shift) c += 32;
        if (c) kpush((uint8_t)c);
    }
}

static void mouse_byte(uint8_t d) {
    if (pkt_i == 0 && !(d & 0x08)) return;  /* resync */
    pkt[pkt_i++] = d;
    if (pkt_i < pkt_len) return;
    pkt_i = 0;
    if (pkt[0] & 0xC0) return;              /* overflow: drop packet */
    int dx = pkt[1] - ((pkt[0] & 0x10) ? 256 : 0);
    int dy = pkt[2] - ((pkt[0] & 0x20) ? 256 : 0);
    mouse_btn = pkt[0] & 7;
    if (pkt_len == 4) { int wheel = pkt[3] & 0x0F; if (wheel & 0x08) wheel -= 16; mouse_wheel += wheel; if (mouse_wheel > 8) mouse_wheel = 8; if (mouse_wheel < -8) mouse_wheel = -8; }
    mouse_x += dx; mouse_y -= dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= scr_w) mouse_x = scr_w - 1;
    if (mouse_y >= scr_h) mouse_y = scr_h - 1;
}

bool ps2_poll(void) {
    bool any = false;
    for (int n = 0; n < 128; n++) {
        uint8_t st = inb(0x64);
        if (!(st & 1)) break;
        uint8_t d = inb(0x60);
        any = true;
        if (st & 0x20) mouse_byte(d); else kbd_byte(d);
    }
    return any;
}
