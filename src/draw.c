/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Draws the state into a 640 x 360 frame buffer (0x00RRGGBB, the API's
 * XRGB8888), in software and with integer math only, so every build draws
 * the same pixels. Back to front: sky, clouds, mountains and hills (each
 * scrolling at its own speed), the level, the actors, particles, the HUD.
 */
#include <stdio.h>
#include <string.h>
#include "hd.h"
#include "gfx.h"
#include "sprite.h"
#include "text.h"

static int32_t cam_x, cam_y;

/* Precomputed once: the hills' outlines. */
static int16_t far_h[1024], near_h[1024];
static int32_t ready_gen = -1;

static uint32_t mix(uint32_t a, uint32_t b, int32_t t) /* t in 0..256 */
{
   return gfx_mix(a, b, t);
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
   outline(far_h, 7, 250, 120);
   outline(near_h, 99, 310, 70);
   ready_gen = hd_content_gen;
}

static hd_surface surf;

/* The sky's color on a row of the surface drawn into. */
static uint32_t sky_at(int32_t y)
{
   return mix(hd_sky_top, hd_sky_bottom, y * 256 / surf.h);
}

static void rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c)
{
   gfx_fill(&surf, x, y, w, h, c);
}

enum { BLIT_FLIP = 1, BLIT_WHITE = 2 };

/* Draws a picture at screen (x, y); alpha 255 copies, lower alpha blends. */
static void blit(const hd_image *im, int32_t x, int32_t y, int32_t flags)
{
   hd_style st;
   memset(&st, 0, sizeof st);
   st.flags = ((flags & BLIT_FLIP) ? DRAW_FLIP_X : 0) | ((flags & BLIT_WHITE) ? DRAW_WHITE : 0);
   gfx_blit(&surf, im, x, y, &st);
}

/* Text with a dark shadow one step down and right. */
static void text(const char *s, int32_t x, int32_t y, int32_t scale, uint32_t c)
{
   text_draw(&surf, s, x, y, scale, c, 1, -1);
}

/* A line in the middle of the screen, smaller when it would not fit (4:3 and 9:16 screens). */
static void center(const char *s, int32_t y, int32_t scale, uint32_t c)
{
   while (scale > 1 && text_width(s, scale) > surf.w - 16)
      scale--;
   text_center(&surf, s, y, scale, c);
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
   static uint32_t sky[ZOOM_MAX_H];
   int32_t x, y, i;
   for (y = 0; y < surf.h; y++)
      sky[y] = sky_at(y); /* once a row, not once a pixel */
   /* sky */
   for (y = 0; y < surf.h; y++)
   {
      uint32_t *row = surf.px + y * surf.w;
      for (x = 0; x < surf.w; x++)
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
            if (dx * dx * h * h + dy * dy * w * w <= w * w * h * h && (uint32_t)(cx + dx) < (uint32_t)surf.w && (uint32_t)(cy + dy) < (uint32_t)surf.h)
            {
               uint32_t *px = surf.px + (cy + dy) * surf.w + cx + dx;
               *px = mix(*px, dy > h / 3 ? 0xd8e8f8u : 0xffffffu, 190);
            }
   }
   /* far mountains at a quarter of the speed, near hills at half */
   for (x = 0; x < surf.w; x++)
   {
      int32_t fh = far_h[((x + cam_x / 4) % 1024 + 1024) % 1024] - (cam_y - (MAP_H * TILE - surf.h)) / 4;
      int32_t nh = near_h[((x + cam_x / 2) % 1024 + 1024) % 1024] - (cam_y - (MAP_H * TILE - surf.h)) / 2;
      for (y = hd_max(fh, 0); y < surf.h; y++)
         surf.px[y * surf.w + x] = mix(0x7a8ec8u, sky[y], 90 + (y - fh) / 2 > 200 ? 200 : 90 + (y - fh) / 2);
      for (y = hd_max(nh, 0); y < surf.h; y++)
         surf.px[y * surf.w + x] = y < nh + 3 ? 0x3e8a48u : mix(0x5aa860u, 0x2e6e3cu, hd_min((y - nh) * 2, 256));
   }
}

static void level(const hd_state *s)
{
   int32_t tx0 = cam_x >> 4, ty0 = cam_y >> 4, tx, ty;
   for (ty = ty0; ty <= ty0 + surf.h / TILE + 1; ty++)
      for (tx = tx0; tx <= tx0 + surf.w / TILE + 1; tx++)
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

/* A character: with the level's outline, and its shadow on the ground when the level has shadows. */
static void actor(const hd_image *im, int32_t x, int32_t y, int32_t flags, int32_t feet_x, int32_t feet_y, int32_t shadow_w)
{
   hd_style st;
   if (hd_fx.shadows && shadow_w > 0)
      gfx_shadow(&surf, feet_x, feet_y, shadow_w, 2, 120);
   memset(&st, 0, sizeof st);
   st.flags = ((flags & BLIT_FLIP) ? DRAW_FLIP_X : 0) | ((flags & BLIT_WHITE) ? DRAW_WHITE : 0);
   st.outline = hd_fx.outline;
   gfx_blit(&surf, im, x, y, &st);
}

/* Frame t of an animation (t in frames of 60 per second), looping or held on its last frame. */
static const hd_image *anim_frame(const hd_anim *an, int32_t t, int loop)
{
   int32_t f = (t < 0 ? 0 : t) * an->fps / 60;
   f = loop ? f % an->count : hd_min(f, an->count - 1);
   return &an->frames[f];
}

/* A hero drawn from its package's sprites: its state picks the animation. */
static void hero_sprite(const hd_state *s, const hd_player *p, int32_t i)
{
   const hd_skin *sk = &hd_skins[hd_skin_of[i]];
   const hd_anim *an;
   const hd_image *im;
   int32_t t = s->frame, loop = 1, state;
   if (s->phase == PH_CLEAR)
   {
      state = ANIM_WIN;
      t = s->phase_t;
   }
   else if (p->hurt > HURT_FRAMES - 30 && sk->anim[ANIM_HURT].frames)
   {
      state = ANIM_HURT;
      t = HURT_FRAMES - p->hurt;
      loop = 0;
   }
   else if (!p->ground)
      state = ANIM_JUMP;
   else if (hd_abs(p->vx) >= FX_FRAC(1, 4))
      state = ANIM_RUN;
   else if (p->still >= BORED_AFTER && sk->anim[ANIM_BORED].frames)
   {
      /* the bored animation once, then idle a while, again and again */
      const hd_anim *b = &sk->anim[ANIM_BORED];
      int32_t len = b->count * 60 / b->fps;
      t = (p->still - BORED_AFTER) % (len + 240);
      state = t < len ? ANIM_BORED : ANIM_IDLE;
      if (state == ANIM_IDLE)
         t = s->frame;
   }
   else
      state = ANIM_IDLE;
   if (state != ANIM_HURT && p->hurt && ((p->hurt >> 2) & 1))
      return; /* blinking */
   an = hd_skin_anim(sk, state);
   if (!an)
      return;
   if (state == ANIM_JUMP && an == &sk->anim[ANIM_JUMP])
   {
      /* rising to falling across the frames, leaving out the first and last (take-off and landing) when there are 4 or more */
      int32_t first = an->count >= 4 ? 1 : 0, last = an->count >= 4 ? an->count - 2 : an->count - 1;
      int32_t span = hd_phys.jump_speed + hd_phys.fall_max, at = hd_clamp(p->vy + hd_phys.jump_speed, 0, span - 1);
      im = &an->frames[first + (int32_t)((int64_t)at * (last - first + 1) / span)];
   }
   else
      im = anim_frame(an, t, loop);
   actor(im, FX_INT(p->x) + PW / 2 - im->w / 2 - cam_x, FX_INT(p->y) + PH - (im->h - an->feet) - cam_y,
         (p->facing < 0 ? BLIT_FLIP : 0) | (p->hurt > HURT_FLASH ? BLIT_WHITE : 0),
         FX_INT(p->x) + PW / 2 - cam_x, FX_INT(p->y) + PH - cam_y, p->ground ? PW * 3 / 5 : PW * 2 / 5);
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
      actor(&hd_enemy_img[frame], FX_INT(e->x) - 1 - cam_x, FX_INT(e->y) - 4 - cam_y, e->vx > 0 ? BLIT_FLIP : 0,
            FX_INT(e->x) + EW / 2 - cam_x, FX_INT(e->y) + EH - cam_y, e->alive == 1 ? 7 : 0);
   }
   for (i = MAX_PLAYERS - 1; i >= 0; i--)
   {
      const hd_player *p = &s->p[i];
      int32_t frame;
      if (!p->active || p->respawn)
         continue;
      if (hd_skin_count)
      {
         hero_sprite(s, p, i);
         continue;
      }
      if (p->hurt && ((p->hurt >> 2) & 1))
         continue; /* blinking */
      if (!p->ground)
         frame = HERO_JUMP;
      else if (hd_abs(p->vx) < FX_FRAC(1, 4))
         frame = HERO_IDLE;
      else
         frame = ((p->anim >> 5) & 1) ? HERO_WALK1 : HERO_WALK2;
      actor(&hd_hero[i][frame], FX_INT(p->x) - 3 - cam_x, FX_INT(p->y) - 2 - cam_y,
            (p->facing < 0 ? BLIT_FLIP : 0) | (p->hurt > HURT_FLASH ? BLIT_WHITE : 0),
            FX_INT(p->x) + PW / 2 - cam_x, FX_INT(p->y) + PH - cam_y, p->ground ? 6 : 4);
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
               if ((uint32_t)(x + k) < (uint32_t)surf.w && (uint32_t)(y + j) < (uint32_t)surf.h)
                  surf.px[(y + j) * surf.w + x + k] = add(surf.px[(y + j) * surf.w + x + k], q->color & 0xffffffu, t);
      }
      else
         rect(x, y, size, size, q->color & 0xffffffu);
   }
}

/* A row of the 360 row screen, moved to the same place on a taller one. */
#define ROW(y) ((y) * HD_H / 360)

static void hud(const hd_state *s)
{
   int32_t i;
   char buf[16];
   if (s->phase == PH_TITLE)
   {
      if (hd_content_id[0] | hd_content_id[1] | hd_content_id[2] | hd_content_id[3])
         center(hd_title, ROW(110), 4, 0xf8c838u);
      else
      {
         center("GO-LINK HD", ROW(96), 6, 0xf8c838u);
         center("DEMO", ROW(150), 3, 0xffffffu);
      }
      if ((s->frame >> 5) & 1)
         center("PRESS START", ROW(230), 2, 0xffffffu);
      snprintf(buf, sizeof buf, "1 TO %d PLAYERS", (int)hd_players);
      if (hd_players == 1)
         strcpy(buf, "1 PLAYER");
      {
         char line[48];
         snprintf(line, sizeof line, "%s - B JUMP - Y RUN", buf);
         center(line, ROW(300), 1, 0xffffffu);
      }
      return;
   }
   for (i = 0; i < hd_players; i++)
   {
      const hd_player *p = &s->p[i];
      /* as many players a row as the screen's width takes: 4 on 16:9 */
      int32_t per = hd_max(1, HD_W / 156);
      int32_t x = 12 + (i % per) * 156, y = 12 + (i / per) * 22;
      buf[0] = 'P';
      buf[1] = (char)('1' + i);
      buf[2] = 0;
      if (!p->active)
      {
         if ((s->frame >> 5) & 1)
         {
            text(buf, x, y, 2, 0xc0c8d8u);
            text("START", x + 30, y, 2, 0xc0c8d8u);
         }
         continue;
      }
      text(buf, x, y, 2, hd_player_color[i] & 0xffffffu);
      blit(&hd_coin[0], x + 28, y - 2, 0);
      buf[0] = 'x';
      number(buf + 1, p->coins);
      text(buf, x + 46, y, 2, 0xffffffu);
   }
   if (s->paused)
   {
      rect(0, HD_H / 2 - 30, HD_W, 60, 0x1a1020u);
      center("PAUSE", HD_H / 2 - 14, 4, 0xffffffu);
   }
   if (s->phase == PH_CLEAR)
   {
      center("STAGE CLEAR!", 110, 5, 0xf8c838u);
      for (i = 0; i < hd_players; i++)
         if (s->p[i].active)
         {
            char line[24] = "P1  x";
            line[1] = (char)('1' + i);
            number(line + 5, s->p[i].coins);
            center(line, 180 + i * 24, 2, hd_player_color[i] & 0xffffffu);
         }
   }
}

/* The world (backdrop, level, characters, particles) with its top-left at the camera, into a surface. */
void hd_draw_world(const hd_state *s, hd_surface *target, int32_t cx, int32_t cy)
{
   surf = *target;
   cam_x = cx;
   cam_y = cy;
   backdrop();
   level(s);
   actors(s);
   particles(s);
}

/* The level's effects (package format 2), on the finished world. */
static void effects(const hd_state *s, hd_surface *screen, int32_t zoom)
{
   static hd_light lights[FX_LIGHTS_MAX + MAX_PLAYERS];
   int32_t n = 0, i;
   if (hd_fx.waves_amp)
   {
      int32_t y0 = (hd_fx.waves_y - cam_y) * zoom / 256;
      fx_waves(screen, hd_max(y0, 0), screen->h - 1, hd_fx.waves_amp, hd_max(8, hd_fx.waves_len), s->frame * 40);
   }
   if (hd_fx.darkness > 0 || hd_fx.lights > 0)
   {
      for (i = 0; i < hd_fx.lights; i++)
      {
         hd_light *l = &lights[n++];
         l->x = (hd_fx.light_x[i] - cam_x) * zoom / 256;
         l->y = (hd_fx.light_y[i] - cam_y) * zoom / 256;
         /* a flame's flicker: the radius breathes with two sines */
         l->radius = (hd_fx.light_r[i] + hd_fx.light_flicker[i] * (hd_sin(s->frame * 97 + i * 911) + hd_sin(s->frame * 41 + i * 333)) / (2 * TRIG_ONE)) * zoom / 256;
         l->color = hd_fx.light_color[i];
         l->strength = 256;
      }
      if (hd_fx.player_light)
         for (i = 0; i < MAX_PLAYERS; i++)
            if (s->p[i].active && !s->p[i].respawn)
            {
               hd_light *l = &lights[n++];
               l->x = (FX_INT(s->p[i].x) + PW / 2 - cam_x) * zoom / 256;
               l->y = (FX_INT(s->p[i].y) + PH / 2 - cam_y) * zoom / 256;
               l->radius = hd_fx.player_light * zoom / 256;
               l->color = hd_fx.player_light_color;
               l->strength = 256;
            }
      fx_lights(screen, hd_fx.darkness, lights, n);
   }
   if (hd_fx.bloom)
      fx_bloom(screen, hd_fx.bloom_threshold, hd_fx.bloom);
   if (hd_fx.grade)
      fx_grade(screen, hd_fx.grade_amount);
}

/* Zoomed out, the world is drawn bigger and scaled down; zoomed in, smaller and scaled up. */
static uint32_t zoom_px[ZOOM_MAX_W * ZOOM_MAX_H];

void hd_draw(const hd_state *s, uint32_t *out)
{
   hd_surface screen;
   int32_t zoom = s->zoom ? hd_clamp(s->zoom, 128, 512) : 256; /* 0 (an old or zeroed state) is 1x */
   int32_t cx = FX_INT(s->cam_x) + s->shake_x, cy = FX_INT(s->cam_y) + s->shake_y;
   if (ready_gen != hd_content_gen)
      prepare();
   screen.px = out;
   screen.w = HD_W;
   screen.h = HD_H;
   if (s->show.on)
   {
      hd_show_draw(s, &screen);
      return;
   }
   if (zoom == 256)
      hd_draw_world(s, &screen, cx, cy);
   else
   {
      hd_surface big;
      big.px = zoom_px;
      big.w = hd_min(HD_W * 256 / zoom, ZOOM_MAX_W);
      big.h = hd_min(HD_H * 256 / zoom, ZOOM_MAX_H);
      hd_draw_world(s, &big, cx, cy);
      gfx_scale(&screen, &big);
   }
   effects(s, &screen, zoom);
   surf = screen;
   hud(s);
   if (s->dlg > 0 && s->dlg <= hd_fx.dialogs)
      text_dialog(&screen, hd_portrait.px ? &hd_portrait : NULL, hd_fx.dialog_name[s->dlg - 1],
                  hd_fx.dialog_text[s->dlg - 1][hd_lang], s->dlg_chars, (s->frame >> 4) & 1);
}
