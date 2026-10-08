/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Text: a 5 x 7 pixel font read as UTF-8, with the accents of Spanish and
 * Portuguese drawn over its capitals (á é í ó ú à â ã õ ê ô ü ñ ç, ¿ ¡),
 * scaled in whole pixels, and dialog boxes with a typing effect.
 */
#ifndef HD_TEXT_H
#define HD_TEXT_H

#include "gfx.h"

/* How many letters a text has (UTF-8 aware). */
int32_t text_glyphs(const char *utf8);
int32_t text_width(const char *utf8, int32_t scale);
/* At most max letters (-1 = all), with a dark shadow one step down and right when shadow. */
void text_draw(hd_surface *s, const char *utf8, int32_t x, int32_t y, int32_t scale, uint32_t color, int32_t shadow, int32_t max);
void text_center(hd_surface *s, const char *utf8, int32_t y, int32_t scale, uint32_t color);

/*
 * A dialog box at the bottom of the screen: a portrait (may be NULL) and the
 * text wrapped to the box, showing its first `shown` letters. Returns how
 * many letters the text has, so the game knows when it is all out.
 */
int32_t text_dialog(hd_surface *s, const hd_image *portrait, const char *name, const char *utf8, int32_t shown, int32_t blink);

#endif
