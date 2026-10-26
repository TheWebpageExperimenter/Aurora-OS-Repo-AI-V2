#pragma once
#include "kernel.h"
#define KEY_SUPER 0x81
extern int     mouse_x, mouse_y;
extern uint8_t mouse_btn;            /* bit0 left, bit1 right, bit2 middle */
extern int mouse_wheel;               /* signed wheel delta; positive is up */
int ps2_take_wheel(void);
void ps2_init(int screen_w, int screen_h);
bool ps2_poll(void);                 /* drain the controller; true if anything arrived */
int  ps2_getkey(void);               /* ASCII, '\b', '\n', 27, KEY_SUPER; 0 if none */
