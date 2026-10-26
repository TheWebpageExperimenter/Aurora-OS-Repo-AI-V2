#pragma once
#include "kernel.h"
enum {
    ICON_LOGO, ICON_SEARCH, ICON_FILES, ICON_NOTES, ICON_MONITOR, ICON_SETTINGS,
    ICON_POWER, ICON_DOC, ICON_CLOSE, ICON_MIN, ICON_CHEV_L, ICON_CHEV_R, ICON_CHEV_UP,
    ICON_CHECK, ICON_BROWSER, ICON_LOCK, ICON_RELOAD, ICON_MESSAGES, ICON_CLOCK, ICON_MAPS, ICON_TERMINAL
};
/* Draws icon `id` into the box (x, y, size, size). `col` tints the monochrome glyph icons. */
void icon_draw(int id, int x, int y, int size, uint32_t col);
