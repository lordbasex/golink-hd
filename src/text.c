/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/* Text and dialog boxes (text.h). */
#include <string.h>
#include "text.h"

/* 5 x 7 letters, one byte per row, bit 4 is the left column. */
static const char font_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ!-:/.x?,'()+=%\"&";
static const uint8_t font[][7] = {
   { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
   { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
   { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
   { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
   { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
   { 0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11 }, { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
   { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C },
   { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
   { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
   { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },
   { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
   { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 },
   { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },
   { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
   { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
   { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },
   { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }, { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
   { 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
   { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 },
   { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 }, { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00 },
   { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C }, { 0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11 },
   /* ? , ' ( ) + = % " & */
   { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 }, { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 },
   { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 }, { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 },
   { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 }, { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 },
   { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 }, { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 },
   { 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D },
};
static const uint8_t inverted_question[7] = { 0x04, 0x00, 0x04, 0x08, 0x10, 0x11, 0x0E };
static const uint8_t inverted_bang[7] = { 0x04, 0x00, 0x04, 0x04, 0x04, 0x04, 0x04 };

/* Marks drawn over a capital: two rows above it (cedilla: two rows under it). */
enum { MARK_NONE = 0, MARK_ACUTE, MARK_GRAVE, MARK_CIRCUMFLEX, MARK_TILDE, MARK_DIAERESIS, MARK_CEDILLA };
static const uint8_t marks[][2] = {
   { 0x00, 0x00 }, { 0x02, 0x04 }, { 0x08, 0x04 }, { 0x04, 0x0A }, { 0x0D, 0x16 }, { 0x00, 0x0A }, { 0x04, 0x08 },
};

/* Latin-1 letters with accents: base capital and mark. */
static const struct { uint16_t cp; char base; uint8_t mark; } accented[] = {
   { 0xC0, 'A', MARK_GRAVE }, { 0xC1, 'A', MARK_ACUTE }, { 0xC2, 'A', MARK_CIRCUMFLEX }, { 0xC3, 'A', MARK_TILDE }, { 0xC4, 'A', MARK_DIAERESIS },
   { 0xC7, 'C', MARK_CEDILLA },
   { 0xC8, 'E', MARK_GRAVE }, { 0xC9, 'E', MARK_ACUTE }, { 0xCA, 'E', MARK_CIRCUMFLEX }, { 0xCB, 'E', MARK_DIAERESIS },
   { 0xCC, 'I', MARK_GRAVE }, { 0xCD, 'I', MARK_ACUTE }, { 0xCE, 'I', MARK_CIRCUMFLEX }, { 0xCF, 'I', MARK_DIAERESIS },
   { 0xD1, 'N', MARK_TILDE },
   { 0xD2, 'O', MARK_GRAVE }, { 0xD3, 'O', MARK_ACUTE }, { 0xD4, 'O', MARK_CIRCUMFLEX }, { 0xD5, 'O', MARK_TILDE }, { 0xD6, 'O', MARK_DIAERESIS },
   { 0xD9, 'U', MARK_GRAVE }, { 0xDA, 'U', MARK_ACUTE }, { 0xDB, 'U', MARK_CIRCUMFLEX }, { 0xDC, 'U', MARK_DIAERESIS },
};

/* The next code point of a UTF-8 text; bad bytes read as '?'. */
static uint32_t next_cp(const char **p)
{
   const unsigned char *s = (const unsigned char *)*p;
   uint32_t c = s[0];
   if (c < 0x80)
   {
      *p += 1;
      return c;
   }
   if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80)
   {
      *p += 2;
      return (c & 0x1F) << 6 | (s[1] & 0x3F);
   }
   if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80)
   {
      *p += 3;
      return (c & 0x0F) << 12 | (uint32_t)(s[1] & 0x3F) << 6 | (s[2] & 0x3F);
   }
   *p += 1;
   while ((**p & 0xC0) == 0x80)
      *p += 1;
   return '?';
}

int32_t text_glyphs(const char *utf8)
{
   int32_t n = 0;
   while (*utf8)
   {
      next_cp(&utf8);
      n++;
   }
   return n;
}

int32_t text_width(const char *utf8, int32_t scale)
{
   return text_glyphs(utf8) * 6 * scale - scale;
}

static void bits(hd_surface *s, const uint8_t *rows, int32_t nrows, int32_t x, int32_t y, int32_t scale, uint32_t c)
{
   int32_t row, col;
   for (row = 0; row < nrows; row++)
      for (col = 0; col < 5; col++)
         if (rows[row] & (0x10 >> col))
            gfx_fill(s, x + col * scale, y + row * scale, scale, scale, c);
}

static void glyph(hd_surface *s, uint32_t cp, int32_t x, int32_t y, int32_t scale, uint32_t c)
{
   const char *at;
   uint32_t i;
   char ch;
   uint8_t mark = MARK_NONE;
   if (cp == 0xBF)
   {
      bits(s, inverted_question, 7, x, y, scale, c);
      return;
   }
   if (cp == 0xA1)
   {
      bits(s, inverted_bang, 7, x, y, scale, c);
      return;
   }
   if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)
      cp -= 0x20; /* small accented letters are drawn as capitals */
   for (i = 0; i < sizeof accented / sizeof accented[0]; i++)
      if (accented[i].cp == cp)
      {
         cp = (uint32_t)accented[i].base;
         mark = accented[i].mark;
         break;
      }
   if (cp >= 0x80 || cp == 0)
      return;
   ch = (char)cp;
   if (ch >= 'a' && ch <= 'z' && ch != 'x')
      ch = (char)(ch - 'a' + 'A');
   at = strchr(font_chars, ch);
   if (!at)
      return;
   bits(s, font[at - font_chars], 7, x, y, scale, c);
   if (mark == MARK_CEDILLA)
      bits(s, marks[mark], 2, x, y + 7 * scale, scale, c);
   else if (mark != MARK_NONE)
      bits(s, marks[mark], 2, x, y - 2 * scale, scale, c);
}

void text_draw(hd_surface *s, const char *utf8, int32_t x, int32_t y, int32_t scale, uint32_t color, int32_t shadow, int32_t max)
{
   int32_t pass, i;
   for (pass = shadow ? 0 : 1; pass < 2; pass++)
   {
      const char *p = utf8;
      for (i = 0; *p && (max < 0 || i < max); i++)
      {
         uint32_t cp = next_cp(&p);
         if (pass == 0)
            glyph(s, cp, x + i * 6 * scale + scale, y + scale, scale, 0x1a1020u);
         else
            glyph(s, cp, x + i * 6 * scale, y, scale, color);
      }
   }
}

void text_center(hd_surface *s, const char *utf8, int32_t y, int32_t scale, uint32_t color)
{
   text_draw(s, utf8, (s->w - text_width(utf8, scale)) / 2, y, scale, color, 1, -1);
}

int32_t text_dialog(hd_surface *s, const hd_image *portrait, const char *name, const char *utf8, int32_t shown, int32_t blink)
{
   int32_t bh = 92, by = s->h - bh - 8, bx = 8, bw = s->w - 16, tx = bx + 12, x, y;
   int32_t per_line, total = text_glyphs(utf8), line = 0, col = 0, drawn = 0;
   const char *p = utf8;
   /* the box: see-through dark with a gold border */
   for (y = by; y < by + bh; y++)
      for (x = bx; x < bx + bw; x++)
      {
         int edge = y < by + 2 || y >= by + bh - 2 || x < bx + 2 || x >= bx + bw - 2;
         s->px[y * s->w + x] = edge ? 0xf8c838u : gfx_mix(s->px[y * s->w + x], 0x1a1020u, 220);
      }
   if (portrait)
   {
      int32_t ps = hd_max(1, 72 / hd_max(1, hd_max(portrait->w, portrait->h)));
      gfx_blit_rot(s, portrait, bx + 10, by + 10, 0, 0, 0, FX(ps), FX(ps), NULL);
      tx = bx + 10 + portrait->w * ps + 12;
   }
   if (name && *name)
   {
      text_draw(s, name, tx, by + 8, 2, 0xf8c838u, 1, -1);
   }
   per_line = (bx + bw - 12 - tx) / 12; /* letters of scale 2 */
   /* word wrap: a word that does not fit the line starts the next one */
   while (*p && line < 4)
   {
      const char *word = p, *q = p;
      int32_t len = 0, i;
      while (*q && *q != ' ')
      {
         next_cp(&q);
         len++;
      }
      if (col > 0 && col + len > per_line)
      {
         line++;
         col = 0;
         if (line >= 4)
            break;
      }
      for (i = 0; i < len && drawn < shown; i++, drawn++)
      {
         uint32_t cp = next_cp(&word);
         glyph(s, cp, tx + (col + i) * 12 + 2, by + 30 + line * 16 + 2, 2, 0x1a1020u);
         glyph(s, cp, tx + (col + i) * 12, by + 30 + line * 16, 2, 0xffffffu);
      }
      if (drawn < shown && i < len)
         break;
      col += len;
      p = q;
      if (*p == ' ')
      {
         p++;
         col++;
         if (drawn < shown)
            drawn++;
      }
      if (drawn >= shown)
         break;
   }
   if (shown >= total && blink)
      gfx_fill(s, bx + bw - 20, by + bh - 16, 8, 6, 0xf8c838u); /* "press a button" */
   return total;
}
