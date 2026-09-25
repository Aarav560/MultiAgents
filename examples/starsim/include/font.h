/* font.h - built-in 8x8 bitmap font (printable ASCII 32..126) for the HUD. */
#ifndef STARSIM_FONT_H
#define STARSIM_FONT_H

#include <stdint.h>

#include "render.h"

/* Row bits of glyph ch (8 bytes, top row first, bit 0 = leftmost pixel). Unknown chars -> '?'. */
const uint8_t *font_glyph(char ch);
/* Draws s at (x, y) = top-left, each glyph 8*scale px wide/tall, alpha-blended. '\n' starts a
   new line (8*scale + 2 px lower). Returns the width in px of the widest line. */
int font_draw(canvas *c, int x, int y, const char *s, uint32_t color, int scale);
/* Width in px of the widest line of s at this scale (no drawing). */
int font_width(const char *s, int scale);

#endif
