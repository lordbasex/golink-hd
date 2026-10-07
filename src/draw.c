/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Draws the state into a 640 x 360 frame buffer (0x00RRGGBB, libretro's
 * XRGB8888), in software and with integer math only, so every build draws
 * the same pixels. Back to front: sky, clouds, mountains and hills (each
 * scrolling at its own speed), the level, the actors, particles, the HUD.
 */
#include <string.h>
#include "hd.h"

static uint32_t *fb;
static int32_t cam_x, cam_y;

/* Precomputed once: the sky's color per row and the hills' outlines. */
static uint32_t sky[HD_H];
static int16_t far_h[1024], near_h[1024];
static int32_t ready_gen = -1;

static uint32_t mix(uint32_t a, uint32_t b, int32_t t) /* t in 0..256 */
{
   uint32_t r = (((a >> 16) & 0xff) * (uint32_t)(256 - t) + ((b >> 16) & 0xff) * (uint32_t)t) >> 8;
   uint32_t g = (((a >> 8) & 0xff) * (uint32_t)(256 - t) + ((b >> 8) & 0xff) * (uint32_t)t) >> 8;
   uint32_t bl = ((a & 0xff) * (uint32_t)(256 - t) + (b & 0xff) * (uint32_t)t) >> 8;
   return r << 16 | g << 8 | bl;
}

static uint32_t add(uint32_t a, uint32_t b, int32_t t)
{
   uint32_t r = ((a >> 16) & 0xff) + ((((b >> 16) & 0xff) * (uint32_t)t) >> 8);
   uint32_t g = ((a >> 8) & 0xff) + ((((b >> 8) & 0xff) * (uint32_t)t) >> 8);
   uint32_t bl = (a & 0xff) + (((b & 0xff) * (uint32_t)t) >> 8);
   return (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (bl > 255 ? 255 : bl);
}

/* A smooth line through random heights every 64 px; it repeats every 1024 px. */
static void outline(int16_t *h, uint32_t seed, int32_t base, int32_t range)
{
   int32_t pts[17], i, x;
   for (i = 0; i < 16; i++)
   {
      seed = seed * 1664525u + 1013904223u;
      pts[i] = base - (int32_t)((seed >> 16) % (uint32_t)range);
   }
   pts[16] = pts[0];
   for (x = 0; x < 1024; x++)
   {
      int32_t k = x >> 6, t = (x & 63) * 4; /* t in 0..252 */
      int32_t s = (3 * t * t * 256 - 2 * t * t * t) >> 16; /* smoothstep, 0..256 */
      h[x] = (int16_t)(pts[k] + ((pts[k + 1] - pts[k]) * s >> 8));
   }
}

static void prepare(void)
{
   int32_t y;
   for (y = 0; y < HD_H; y++)
      sky[y] = mix(hd_sky_top, hd_sky_bottom, y * 256 / HD_H);
   outline(far_h, 7, 250, 120);
   outline(near_h, 99, 310, 70);
   ready_gen = hd_content_gen;
}

static void rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c)
{
   int32_t i, j;
   for (j = hd_max(y, 0); j < hd_min(y + h, HD_H); j++)
      for (i = hd_max(x, 0); i < hd_min(x + w, HD_W); i++)
         fb[j * HD_W + i] = c;
}

enum { BLIT_FLIP = 1, BLIT_WHITE = 2 };

/* Draws a picture at screen (x, y); alpha 255 copies, lower alpha blends. */
static void blit(const hd_image *im, int32_t x, int32_t y, int32_t flags)
{
   int32_t i, j;
   int32_t x0 = hd_max(0, -x), x1 = hd_min(im->w, HD_W - x);
   int32_t y0 = hd_max(0, -y), y1 = hd_min(im->h, HD_H - y);
   for (j = y0; j < y1; j++)
   {
      uint32_t *row = fb + (y + j) * HD_W + x;
      const uint32_t *src = im->px + j * im->w;
      for (i = x0; i < x1; i++)
      {
         uint32_t c = src[(flags & BLIT_FLIP) ? im->w - 1 - i : i];
         uint32_t a = c >> 24;
         if (!a)
            continue;
         if (flags & BLIT_WHITE)
            c = 0xffffffffu;
         row[i] = a == 255 ? (c & 0xffffffu) : mix(row[i], c & 0xffffffu, (int32_t)a);
      }
   }
}

/* 5 x 7 letters, one byte per row, bit 4 is the left column. */
static const char font_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ!-:/.x";
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
};

static void letter(char ch, int32_t x, int32_t y, int32_t scale, uint32_t c)
{
   const char *at;
   int32_t row, col;
   if (ch >= 'a' && ch <= 'z' && ch != 'x')
      ch = (char)(ch - 'a' + 'A');
   at = strchr(font_chars, ch);
   if (!at || ch == '\0')
      return;
   for (row = 0; row < 7; row++)
      for (col = 0; col < 5; col++)
         if (font[at - font_chars][row] & (0x10 >> col))
            rect(x + col * scale, y + row * scale, scale, scale, c);
}

static int32_t text_width(const char *s, int32_t scale)
{
   return (int32_t)strlen(s) * 6 * scale - scale;
}

/* Text with a dark shadow one step down and right. */
static void text(const char *s, int32_t x, int32_t y, int32_t scale, uint32_t c)
{
   int32_t i;
   for (i = 0; s[i]; i++)
      letter(s[i], x + i * 6 * scale + scale, y + scale, scale, 0x1a1020u);
   for (i = 0; s[i]; i++)
      letter(s[i], x + i * 6 * scale, y, scale, c);
}

static void text_center(const char *s, int32_t y, int32_t scale, uint32_t c)
{
   text(s, (HD_W - text_width(s, scale)) / 2, y, scale, c);
}

static void number(char *out, int32_t v)
{
   char tmp[12];
   int32_t n = 0, i = 0;
   if (v < 0)
      v = 0;
   do
   {
      tmp[n++] = (char)('0' + v % 10);
      v /= 10;
   } while (v && n < 11);
   while (n)
      out[i++] = tmp[--n];
   out[i] = 0;
}

static void backdrop(void)
{
   int32_t x, y, i;
   /* sky */
   for (y = 0; y < HD_H; y++)
   {
      uint32_t *row = fb + y * HD_W;
      for (x = 0; x < HD_W; x++)
         row[x] = sky[y];
   }
   /* clouds at a fifth of the speed, see-through */
   for (i = 0; i < 7; i++)
   {
      int32_t cx = ((i * 211 + 40 - cam_x / 5) % 1280 + 1280) % 1280 - 160;
      int32_t cy = 40 + (i * 37) % 90 - cam_y / 10;
      int32_t w = 50 + (i * 29) % 50, h = 14 + (i * 13) % 10, dx, dy;
      for (dy = -h; dy <= h; dy++)
         for (dx = -w; dx <= w; dx++)
            if (dx * dx * h * h + dy * dy * w * w <= w * w * h * h && (uint32_t)(cx + dx) < HD_W && (uint32_t)(cy + dy) < HD_H)
            {
               uint32_t *px = fb + (cy + dy) * HD_W + cx + dx;
               *px = mix(*px, dy > h / 3 ? 0xd8e8f8u : 0xffffffu, 190);
            }
   }
   /* far mountains at a quarter of the speed, near hills at half */
   for (x = 0; x < HD_W; x++)
   {
      int32_t fh = far_h[((x + cam_x / 4) % 1024 + 1024) % 1024] - (cam_y - (MAP_H * TILE - HD_H)) / 4;
      int32_t nh = near_h[((x + cam_x / 2) % 1024 + 1024) % 1024] - (cam_y - (MAP_H * TILE - HD_H)) / 2;
      for (y = hd_max(fh, 0); y < HD_H; y++)
         fb[y * HD_W + x] = mix(0x7a8ec8u, sky[y], 90 + (y - fh) / 2 > 200 ? 200 : 90 + (y - fh) / 2);
      for (y = hd_max(nh, 0); y < HD_H; y++)
         fb[y * HD_W + x] = y < nh + 3 ? 0x3e8a48u : mix(0x5aa860u, 0x2e6e3cu, hd_min((y - nh) * 2, 256));
   }
}

static void level(const hd_state *s)
{
   int32_t tx0 = cam_x >> 4, ty0 = cam_y >> 4, tx, ty;
   for (ty = ty0; ty <= ty0 + HD_H / TILE + 1; ty++)
      for (tx = tx0; tx <= tx0 + HD_W / TILE + 1; tx++)
      {
         int32_t t = hd_cell(tx, ty), sx = tx * TILE - cam_x, sy = ty * TILE - cam_y;
         int32_t i = ty * MAP_W + tx;
         int got = tx >= 0 && tx < MAP_W && ty >= 0 && ty < MAP_H && ((s->taken[i >> 5] >> (i & 31)) & 1);
         switch (t)
         {
         case T_GROUND:
            blit(&hd_tiles[hd_cell(tx, ty - 1) == T_GROUND ? TL_GROUND : TL_GROUND_TOP], sx, sy, 0);
            break;
         case T_BRICK:
            blit(&hd_tiles[TL_BRICK], sx, sy, 0);
            break;
         case T_PLATFORM:
            blit(&hd_tiles[TL_PLATFORM], sx, sy, 0);
            break;
         case T_COIN:
            if (!got)
               blit(&hd_coin[((s->frame + tx * 3) >> 3) & 3], sx, sy + ((((s->frame >> 4) + tx) & 1) ? 1 : 0), 0);
            break;
         case T_CHECK:
            blit(&hd_check[got ? 1 : 0], sx, sy - 16, 0);
            break;
         case T_FLAG:
            blit(&hd_flag, sx, sy - 48, 0);
            break;
         }
      }
}

static void actors(const hd_state *s)
{
   int32_t i;
   for (i = 0; i < MAX_ENEMIES; i++)
   {
      const hd_enemy *e = &s->e[i];
      int32_t frame;
      if (!e->alive)
         continue;
      frame = e->alive == 2 ? ENEMY_SQUASHED : ((e->anim >> 3) & 1);
      blit(&hd_enemy_img[frame], FX_INT(e->x) - 1 - cam_x, FX_INT(e->y) - 4 - cam_y, e->vx > 0 ? BLIT_FLIP : 0);
   }
   for (i = MAX_PLAYERS - 1; i >= 0; i--)
   {
      const hd_player *p = &s->p[i];
      int32_t frame;
      if (!p->active || p->respawn)
         continue;
      if (p->hurt && ((p->hurt >> 2) & 1))
         continue; /* blinking */
      if (!p->ground)
         frame = HERO_JUMP;
      else if (hd_abs(p->vx) < FX_FRAC(1, 4))
         frame = HERO_IDLE;
      else
         frame = ((p->anim >> 5) & 1) ? HERO_WALK1 : HERO_WALK2;
      blit(&hd_hero[i][frame], FX_INT(p->x) - 3 - cam_x, FX_INT(p->y) - 2 - cam_y,
           (p->facing < 0 ? BLIT_FLIP : 0) | (p->hurt > HURT_FLASH ? BLIT_WHITE : 0));
   }
}

static void particles(const hd_state *s)
{
   int32_t i;
   for (i = 0; i < MAX_PARTICLES; i++)
   {
      const hd_particle *q = &s->part[i];
      int32_t x, y, size, t;
      if (!q->life)
         continue;
      x = FX_INT(q->x) - cam_x;
      y = FX_INT(q->y) - cam_y;
      size = q->life * 3 / q->max + 1;
      t = q->life * 256 / q->max;
      if (q->flags & 2)
      {
         int32_t j, k;
         for (j = 0; j < size; j++)
            for (k = 0; k < size; k++)
               if ((uint32_t)(x + k) < HD_W && (uint32_t)(y + j) < HD_H)
                  fb[(y + j) * HD_W + x + k] = add(fb[(y + j) * HD_W + x + k], q->color & 0xffffffu, t);
      }
      else
         rect(x, y, size, size, q->color & 0xffffffu);
   }
}

static void hud(const hd_state *s)
{
   int32_t i;
   char buf[16];
   if (s->phase == PH_TITLE)
   {
      if (hd_content_id[0] | hd_content_id[1] | hd_content_id[2] | hd_content_id[3])
         text_center(hd_title, 110, 4, 0xf8c838u);
      else
      {
         text_center("GO-LINK HD", 96, 6, 0xf8c838u);
         text_center("DEMO", 150, 3, 0xffffffu);
      }
      if ((s->frame >> 5) & 1)
         text_center("PRESS START", 230, 2, 0xffffffu);
      text_center("1 TO 4 PLAYERS - B JUMP - Y RUN", 300, 1, 0xffffffu);
      return;
   }
   for (i = 0; i < MAX_PLAYERS; i++)
   {
      const hd_player *p = &s->p[i];
      int32_t x = 12 + i * 156;
      buf[0] = 'P';
      buf[1] = (char)('1' + i);
      buf[2] = 0;
      if (!p->active)
      {
         if ((s->frame >> 5) & 1)
         {
            text(buf, x, 12, 2, 0xc0c8d8u);
            text("START", x + 30, 12, 2, 0xc0c8d8u);
         }
         continue;
      }
      text(buf, x, 12, 2, hd_player_color[i] & 0xffffffu);
      blit(&hd_coin[0], x + 28, 10, 0);
      buf[0] = 'x';
      number(buf + 1, p->coins);
      text(buf, x + 46, 12, 2, 0xffffffu);
   }
   if (s->paused)
   {
      rect(0, HD_H / 2 - 30, HD_W, 60, 0x1a1020u);
      text_center("PAUSE", HD_H / 2 - 14, 4, 0xffffffu);
   }
   if (s->phase == PH_CLEAR)
   {
      text_center("STAGE CLEAR!", 110, 5, 0xf8c838u);
      for (i = 0; i < MAX_PLAYERS; i++)
         if (s->p[i].active)
         {
            char line[24] = "P1  x";
            line[1] = (char)('1' + i);
            number(line + 5, s->p[i].coins);
            text_center(line, 180 + i * 24, 2, hd_player_color[i] & 0xffffffu);
         }
   }
}

void hd_draw(const hd_state *s, uint32_t *out)
{
   if (ready_gen != hd_content_gen)
      prepare();
   fb = out;
   cam_x = FX_INT(s->cam_x) + s->shake_x;
   cam_y = FX_INT(s->cam_y) + s->shake_y;
   backdrop();
   level(s);
   actors(s);
   particles(s);
   hud(s);
}
