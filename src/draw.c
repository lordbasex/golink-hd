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

/* BLIT_BIG: a picture made for the logical screen (the built-in art), drawn hd_res times bigger. */
enum { BLIT_FLIP = 1, BLIT_WHITE = 2, BLIT_BIG = 4 };

/* The drawing's scale (hd_res): logical pixels to the picture's. */
#define RES (hd_res)
#define S(v) ((v) * RES)

static void blit_style(const hd_image *im, int32_t x, int32_t y, int32_t flags, uint32_t outline)
{
   hd_style st;
   memset(&st, 0, sizeof st);
   st.flags = ((flags & BLIT_FLIP) ? DRAW_FLIP_X : 0) | ((flags & BLIT_WHITE) ? DRAW_WHITE : 0);
   st.outline = outline;
   if ((flags & BLIT_BIG) && RES > 1)
      gfx_blit_rot(&surf, im, x, y, 0, 0, 0, FX(RES), FX(RES), &st);
   else
      gfx_blit(&surf, im, x, y, &st);
}

/* Draws a picture at screen (x, y); alpha 255 copies, lower alpha blends. */
static void blit(const hd_image *im, int32_t x, int32_t y, int32_t flags)
{
   blit_style(im, x, y, flags, 0);
}

/* Text with a dark shadow one step down and right. */
static void text(const char *s, int32_t x, int32_t y, int32_t scale, uint32_t c)
{
   text_draw(&surf, s, x, y, scale, c, 1, -1);
}

/* A line in the middle of the screen, smaller when it would not fit (4:3 and 9:16 screens). */
static void center(const char *s, int32_t y, int32_t scale, uint32_t c)
{
   while (scale > 1 && text_width(s, scale) > surf.w - S(16))
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
      int32_t cx = S(((i * 211 + 40 - cam_x / 5) % 1280 + 1280) % 1280 - 160);
      int32_t cy = S(40 + (i * 37) % 90 - cam_y / 10);
      int32_t w = S(50 + (i * 29) % 50), h = S(14 + (i * 13) % 10), dx, dy;
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
      /* in logical pixels, then drawn hd_res times bigger */
      int32_t wx = x / RES, vh = surf.h / RES;
      int32_t fh = S(far_h[((wx + cam_x / 4) % 1024 + 1024) % 1024] - (cam_y - (MAP_H * TILE - vh)) / 4);
      int32_t nh = S(near_h[((wx + cam_x / 2) % 1024 + 1024) % 1024] - (cam_y - (MAP_H * TILE - vh)) / 2);
      for (y = hd_max(fh, 0); y < surf.h; y++)
         surf.px[y * surf.w + x] = mix(0x7a8ec8u, sky[y], 90 + (y - fh) / RES / 2 > 200 ? 200 : 90 + (y - fh) / RES / 2);
      for (y = hd_max(nh, 0); y < surf.h; y++)
         surf.px[y * surf.w + x] = y < nh + S(3) ? 0x3e8a48u : mix(0x5aa860u, 0x2e6e3cu, hd_min((y - nh) * 2 / RES, 256));
   }
}

/*
 * A cell's 16 x 16 piece (16 hd_res times) of textures laid over the level,
 * see-through pixels skipped: its left half from t[0], its right half from
 * t[1] (a run's end on each side; NULL draws nothing there). With cover[h],
 * a top band laid over the cell next, a half is only drawn below the band's
 * top in each column: what lies above a floor's surface stays empty. With
 * above[h], only above that band's bottom: a wall's side over a step's band.
 */
static void texture_halves(const hd_image *const t[2], const hd_image *const cover[2], const hd_image *const above[2],
                           int32_t tx, int32_t ty, int32_t sx, int32_t sy)
{
   int32_t ts = S(TILE), from[16 * HD_RES_MAX], until[16 * HD_RES_MAX], x, y, h;
   for (x = 0; x < ts; x++)
   {
      const hd_image *c = cover[x >= ts / 2], *u = above[x >= ts / 2];
      from[x] = 0;
      until[x] = ts;
      if (c)
      {
         /* the band's first half-solid pixel down this column */
         int32_t cx = ((tx * ts) % c->w + c->w) % c->w + x, cy = ((ty * ts) % c->h + c->h) % c->h;
         while (from[x] < ts && (c->px[(size_t)(cy + from[x]) * c->w + cx] >> 24) < 128)
            from[x]++;
      }
      if (u)
      {
         /* and its last, up this column */
         int32_t cx = ((tx * ts) % u->w + u->w) % u->w + x, cy = ((ty * ts) % u->h + u->h) % u->h;
         while (until[x] > 0 && (u->px[(size_t)(cy + until[x] - 1) * u->w + cx] >> 24) < 128)
            until[x]--;
      }
   }
   for (h = 0; h < 2; h++)
   {
      const hd_image *im = t[h];
      int32_t ox, oy, x0 = h ? ts / 2 : 0, x1 = h ? ts : ts / 2;
      if (!im)
         continue;
      ox = ((tx * ts) % im->w + im->w) % im->w;
      oy = ((ty * ts) % im->h + im->h) % im->h;
      for (y = hd_max(0, -sy); y < ts && sy + y < surf.h; y++)
      {
         const uint32_t *src = im->px + (size_t)(oy + y) * im->w + ox;
         uint32_t *dst = surf.px + (size_t)(sy + y) * surf.w + sx;
         for (x = hd_max(x0, -sx); x < x1 && sx + x < surf.w; x++)
         {
            uint32_t c = src[x], a = c >> 24;
            if (y < from[x] || y >= until[x])
               continue;
            if (a == 255)
               dst[x] = c & 0xffffffu;
            else if (a)
               dst[x] = mix(dst[x], c & 0xffffffu, (int32_t)a);
         }
      }
   }
}

/* A texture kind's picture for a cell, or its end where the run stops on that side (NULL: the package has none). */
static const hd_image *pick(int32_t kind, int open_left, int open_right)
{
   if (kind >= TL_COUNT && !hd_textures[kind].px)
      kind = TL_BRICK; /* a wall's own pictures are optional */
   if (open_left && hd_textures[TEX_LEFT(kind)].px)
      return &hd_textures[TEX_LEFT(kind)];
   if (open_right && hd_textures[TEX_RIGHT(kind)].px)
      return &hd_textures[TEX_RIGHT(kind)];
   return hd_textures[kind].px ? &hd_textures[kind] : NULL;
}

/* A kind's texture in both halves of a cell, each with its side's end when the run stops there (a 1 cell run gets both). */
static void texture_cell(int32_t kind, int32_t tx, int32_t ty, int32_t sx, int32_t sy, int open_left, int open_right,
                         const hd_image *const cover[2])
{
   const hd_image *t[2];
   static const hd_image *const none[2] = { NULL, NULL };
   t[0] = pick(kind, open_left, 0);
   t[1] = pick(kind, 0, open_right);
   texture_halves(t, cover ? cover : none, none, tx, ty, sx, sy);
}

/* A tile: the package's texture when it has one (its ends where the run of its kind stops), else the tile picture. */
static void tile(int32_t kind, int32_t tx, int32_t ty, int32_t sx, int32_t sy, int open_left, int open_right)
{
   if (pick(kind, 0, 0))
      texture_cell(kind, tx, ty, sx, sy, open_left, open_right, NULL);
   else
      blit(&hd_tiles[kind < TL_COUNT ? kind : TL_BRICK], sx, sy, BLIT_BIG);
}

/* Frame t of an animation (t in frames of 60 per second), looping or held on its last frame. */
static const hd_image *anim_frame(const hd_anim *an, int32_t t, int loop)
{
   /* 64 bits: the state's counters go up to 2^30, times up to 60 frames a second */
   int64_t f = (int64_t)(t < 0 ? 0 : t) * an->fps / 60;
   f = loop ? f % an->count : (f < an->count - 1 ? f : an->count - 1);
   return &an->frames[f];
}

/* An object's animation frame standing on (x, bottom) and centered on x. */
static void object(int32_t obj, int32_t t, int32_t x, int32_t bottom)
{
   const hd_image *im = anim_frame(&hd_objects[obj], t, 1);
   const hd_anim *an = &hd_objects[obj];
   blit(im, x - im->w / 2, bottom - (im->h - an->feet), 0);
}

/*
 * What a floor or wall cell is drawn as: a floor (its inside and band),
 * also a wall standing on a floor (a step of it: the same pictures, laid
 * the same way, so the two meet), or a wall that floats. 0 when not solid.
 */
enum { MAT_NONE = 0, MAT_FLOOR, MAT_WALL };

static int32_t material(int32_t tx, int32_t ty)
{
   int32_t c = hd_cell(tx, ty), y = ty;
   if (c == T_GROUND)
      return MAT_FLOOR;
   if (c != T_BRICK)
      return MAT_NONE;
   while (y < MAP_H - 1 && hd_cell(tx, y + 1) == T_BRICK)
      y++;
   return hd_cell(tx, y + 1) == T_GROUND || (y == MAP_H - 1 && hd_textures[TL_GROUND].px) ? MAT_FLOOR : MAT_WALL;
}

static int32_t band_of(int32_t mat)
{
   return mat == MAT_FLOOR ? TL_GROUND_TOP : TX_BRICK_TOP;
}

/* Whether (tx, ty) is a top cell drawn with a band: solid, nothing solid over it, and its material has a band picture. */
static int is_top(int32_t tx, int32_t ty)
{
   int32_t m = material(tx, ty);
   return m && !material(tx, ty - 1) && hd_textures[band_of(m)].px;
}

/*
 * Whether a top cell's band of material `mat` goes on into (tx, ty): a top
 * of the same material, or a higher wall (the band runs under its side).
 */
static int band_goes_on(int32_t mat, int32_t tx, int32_t ty)
{
   int32_t m = material(tx, ty);
   return m && (material(tx, ty - 1) || m == mat);
}

/*
 * A floor or wall cell. Without a top band of its own: one picture per
 * cell, as the built-in tiles. With one: the inside (its ends where the
 * cell is open, its bottom edge when nothing holds a wall up), then the
 * band laid over the top cells. Beside a lower step (a top cell next to a
 * cell that has more over it), the higher cell's side is an end above the
 * step's surface: under it the inside and the step's band go on, and the
 * side's ink is laid over them.
 */
static void solid_cell(int32_t t, int32_t tx, int32_t ty, int32_t sx, int32_t sy)
{
   int32_t mat = material(tx, ty), inner = mat == MAT_FLOOR ? TL_GROUND : TL_BRICK, band = band_of(mat), k;
   int open[2], step[2], top = !material(tx, ty - 1);
   const hd_image *cover[2] = { NULL, NULL };
   open[0] = tx > 0 && !material(tx - 1, ty);
   open[1] = tx < MAP_W - 1 && !material(tx + 1, ty);
   if (!hd_textures[band].px)
   {
      tile(t == T_GROUND && hd_cell(tx, ty - 1) != T_GROUND ? TL_GROUND_TOP : t == T_GROUND ? TL_GROUND : TL_BRICK, tx, ty, sx, sy, open[0], open[1]);
      return;
   }
   if (mat == MAT_WALL && ty < MAP_H - 1 && !material(tx, ty + 1))
      inner = TX_BRICK_BOTTOM;
   for (k = 0; k < 2; k++)
   {
      int32_t nx = tx + (k ? 1 : -1);
      step[k] = !top && nx >= 0 && nx < MAP_W && is_top(nx, ty);
   }
   if (top)
   {
      cover[0] = pick(band, open[0], 0);
      cover[1] = pick(band, 0, open[1]);
   }
   if (!pick(inner, 0, 0))
      return;
   {
      static const hd_image *const none[2] = { NULL, NULL };
      const hd_image *t2[2], *run[2] = { NULL, NULL };
      for (k = 0; k < 2; k++)
         if (step[k])
         {
            /*
             * Beside a lower step: under its surface the inside goes on and
             * its band runs in; the side (an end) is laid over them down to
             * the band's bottom, so the wall stands on the step.
             */
            const hd_image *plain[2] = { NULL, NULL }, *band_here[2] = { NULL, NULL };
            run[k] = band_here[k] = pick(band_of(material(tx + (k ? 1 : -1), ty)), 0, 0);
            plain[k] = pick(inner, 0, 0);
            texture_halves(plain, band_here, none, tx, ty, sx, sy);
            texture_halves(band_here, none, none, tx, ty, sx, sy);
         }
      t2[0] = pick(inner, open[0] || step[0], 0);
      t2[1] = pick(inner, 0, open[1] || step[1]);
      texture_halves(t2, cover, run, tx, ty, sx, sy);
   }
   if (top)
      texture_cell(band, tx, ty, sx, sy, tx > 0 && !band_goes_on(mat, tx - 1, ty), tx < MAP_W - 1 && !band_goes_on(mat, tx + 1, ty), NULL);
}

static void level(const hd_state *s)
{
   int32_t tx0 = cam_x >> 4, ty0 = cam_y >> 4, tx, ty;
   for (ty = ty0; ty <= ty0 + surf.h / S(TILE) + 1; ty++)
      for (tx = tx0; tx <= tx0 + surf.w / S(TILE) + 1; tx++)
      {
         int32_t t = hd_cell(tx, ty), sx = S(tx * TILE - cam_x), sy = S(ty * TILE - cam_y);
         int32_t i = ty * MAP_W + tx;
         int got = tx >= 0 && tx < MAP_W && ty >= 0 && ty < MAP_H && ((s->taken[i >> 5] >> (i & 31)) & 1);
         switch (t)
         {
         case T_GROUND:
         case T_BRICK:
            solid_cell(t, tx, ty, sx, sy);
            break;
         case T_PLATFORM:
            tile(TL_PLATFORM, tx, ty, sx, sy, hd_cell(tx - 1, ty) != T_PLATFORM, hd_cell(tx + 1, ty) != T_PLATFORM);
            break;
         case T_COIN:
            if (got)
               break;
            if (hd_objects[OBJ_COIN].frames)
               object(OBJ_COIN, s->frame + tx * 7, sx + S(TILE / 2), sy + S(TILE + ((((s->frame >> 4) + tx) & 1) ? 1 : 0)));
            else
               blit(&hd_coin[((s->frame + tx * 3) >> 3) & 3], sx, sy + S((((s->frame >> 4) + tx) & 1) ? 1 : 0), BLIT_BIG);
            break;
         case T_CHECK:
            if (hd_objects[got ? OBJ_CHECK_ON : OBJ_CHECK_OFF].frames)
               object(got ? OBJ_CHECK_ON : OBJ_CHECK_OFF, s->frame, sx + S(TILE / 2), sy + S(TILE));
            else
               blit(&hd_check[got ? 1 : 0], sx, sy - S(16), BLIT_BIG);
            break;
         case T_FLAG:
            if (!hd_goal_open(s))
               break; /* it shows up when the level's boss is beaten */
            if (hd_objects[OBJ_GOAL].frames)
               object(OBJ_GOAL, s->frame, sx + S(TILE / 2), sy + S(TILE));
            else
               blit(&hd_flag, sx, sy - S(48), BLIT_BIG);
            break;
         }
      }
}

/* A character: with the level's outline, and its shadow on the ground when the level has shadows. */
/* (all in the picture's pixels) */
static void actor(const hd_image *im, int32_t x, int32_t y, int32_t flags, int32_t feet_x, int32_t feet_y, int32_t shadow_w)
{
   if (hd_fx.shadows && shadow_w > 0)
      gfx_shadow(&surf, feet_x, feet_y, shadow_w, S(2), 120);
   blit_style(im, x, y, flags, hd_fx.outline);
}


/* A hero drawn from its package's sprites: its state picks the animation. */
static void hero_sprite(const hd_state *s, const hd_player *p, int32_t i)
{
   const hd_skin *sk = &hd_skins[hd_skin_of[i]];
   /* the feet: the hitbox's bottom middle, in the picture's pixels */
   const int32_t fx = S(FX_INT(p->x) + PW / 2 - cam_x), fy = S(FX_INT(p->y) + PH - cam_y);
   if (p->ko)
   {
      /* knocked out: the skin's "knockout" played once over it, else the hero blinking */
      const hd_anim *ko = &sk->anim[ANIM_KO];
      if (ko->frames)
      {
         int32_t done = hd_health.knockout - p->ko;
         const hd_image *im = &ko->frames[hd_clamp((int32_t)((int64_t)done * ko->count / hd_max(1, hd_health.knockout)), 0, ko->count - 1)];
         actor(im, fx - im->w / 2, fy - (im->h - ko->feet), p->facing < 0 ? BLIT_FLIP : 0, fx, fy, S(PW * 3 / 5));
         return;
      }
      if ((p->ko >> 2) & 1)
         return;
   }
   if (p->super_t && sk->anim[ANIM_SUPER].frames)
   {
      /* the super attack: the skin's "super" played once over it */
      const hd_anim *an = &sk->anim[ANIM_SUPER];
      const hd_image *im = &an->frames[hd_clamp((int32_t)((int64_t)p->super_t * an->count / hd_max(1, hd_weapon.super_frames)), 0, an->count - 1)];
      actor(im, fx - im->w / 2, fy - (im->h - an->feet), p->facing < 0 ? BLIT_FLIP : 0, fx, fy, S(PW * 3 / 5));
      return;
   }
   if (p->dash_t && sk->anim[ANIM_DASH].frames)
   {
      /* the dash: the skin's "dash" played once over it */
      const hd_anim *an = &sk->anim[ANIM_DASH];
      const hd_image *im = &an->frames[hd_clamp((int32_t)((int64_t)p->dash_t * an->count / hd_max(1, hd_dash.frames + 1)), 0, an->count - 1)];
      actor(im, fx - im->w / 2, fy - (im->h - an->feet), p->facing < 0 ? BLIT_FLIP : 0, fx, fy, S(PW * 3 / 5));
      return;
   }
   if (sk->has_rig)
   {
      if (p->hurt && p->hurt <= HURT_FRAMES - 30 && ((p->hurt >> 2) & 1))
         return; /* blinking */
      if (hd_fx.shadows)
         gfx_shadow(&surf, fx, fy, S(PW * 3 / 5), S(2), 120);
      hd_rig_draw(&surf, &sk->rig, s, p, i, fx, fy, p->hurt > HURT_FLASH);
      return;
   }
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
   else if (state == ANIM_RUN && an->stride > 0)
   {
      /* the steps follow the ground: p->anim grows by 4 a pixel walked */
      int64_t f = (int64_t)p->anim * an->count / ((int64_t)an->stride * 4);
      im = &an->frames[f % an->count];
   }
   else
      im = anim_frame(an, t, loop);
   actor(im, fx - im->w / 2, fy - (im->h - an->feet), (p->facing < 0 ? BLIT_FLIP : 0) | (p->hurt > HURT_FLASH ? BLIT_WHITE : 0),
         fx, fy, S(p->ground ? PW * 3 / 5 : PW * 2 / 5));
}

/*
 * An enemy of format 3's kinds: a spore (fly, pop), a spitter (idle, spit
 * over its attack, squashed), a boss (idle, windup, attack, hurt while it
 * flashes, down), a minion (the boss's "minion"). Pictures face left; a
 * missing one is a plain box of the hitbox's size.
 */
static void kind_actor(const hd_state *s, const hd_enemy *e)
{
   const hd_anim *an = NULL;
   const hd_image *im;
   int32_t w = hd_kinds[e->kind].w, h = hd_kinds[e->kind].h, t = e->anim, loop = 1, frame = -1;
   int32_t fx = S(FX_INT(e->x) + w / 2 - cam_x), fy = S(FX_INT(e->y) + h - cam_y);
   /* a boss is hit many times a second: its "hurt" picture shows it (white only without one) */
   int32_t white = e->flash > 0 && (e->kind != EK_BOSS || !hd_boss_anim[BOSS_HURT].frames);
   int32_t flags = (e->face > 0 ? BLIT_FLIP : 0) | (white ? BLIT_WHITE : 0), shadow = e->alive == 1 ? S(w * 3 / 5) : 0;
   switch (e->kind)
   {
   case EK_SPORE:
      shadow = 0; /* it flies */
      if (e->alive == 2)
      {
         an = &hd_objects[OBJ_SPORE_POP];
         t = 30 - e->squash;
         loop = 0;
      }
      else
         an = &hd_objects[OBJ_SPORE_FLY];
      break;
   case EK_SPITTER:
      if (e->alive == 2)
         an = &hd_objects[OBJ_SPITTER_SQUASHED];
      else if (e->act >= 0 && hd_objects[OBJ_SPITTER_SPIT].frames)
      {
         an = &hd_objects[OBJ_SPITTER_SPIT];
         frame = hd_min(e->act_t * an->count / SPIT_END, an->count - 1);
      }
      else
         an = &hd_objects[OBJ_SPITTER_IDLE];
      break;
   case EK_MINION:
      if (e->alive == 2)
         return; /* it bursts (the particles) */
      an = &hd_boss_anim[BOSS_MINION];
      break;
   case EK_ROLLER:
      an = &hd_objects[e->alive == 2 ? OBJ_ROLLER_SQUASHED : OBJ_ROLLER_ROLL];
      if (e->alive == 1 && e->act < 0)
         frame = an->count > 4 ? 4 : 0; /* curled up and waiting: its standing frame when it has one */
      else if (e->alive == 1 && an->count)
         frame = (int32_t)(((int64_t)hd_abs(FX_INT(e->x)) / 12) % hd_min(an->count, 4)); /* the ball turns as it goes */
      break;
   case EK_HOPPER:
      an = &hd_objects[e->alive == 2 ? OBJ_HOPPER_SQUASHED : !e->ground && hd_objects[OBJ_HOPPER_JUMP].frames ? OBJ_HOPPER_JUMP : OBJ_HOPPER_IDLE];
      if (e->alive == 1 && !e->ground && an->count)
         frame = hd_clamp((e->vy + FX(6)) * an->count / FX(12), 0, an->count - 1); /* up, top, down */
      break;
   case EK_PUFFER:
      if (e->alive == 2)
         an = &hd_objects[OBJ_PUFFER_SQUASHED];
      else if (e->act >= 0 && hd_objects[OBJ_PUFFER_PUFF].frames)
      {
         an = &hd_objects[OBJ_PUFFER_PUFF];
         frame = hd_min(e->act_t * an->count / SPIT_END, an->count - 1);
      }
      else
         an = &hd_objects[OBJ_PUFFER_IDLE];
      break;
   case EK_SPLITTER:
      if (e->alive == 2)
      {
         /* a whole one splits, a half bursts */
         an = &hd_objects[!e->seq && hd_objects[OBJ_SPLITTER_SPLIT].frames ? OBJ_SPLITTER_SPLIT : OBJ_SPLITTER_SQUASHED];
         t = 30 - e->squash;
         loop = 0;
      }
      else
         an = &hd_objects[OBJ_SPLITTER_CRAWL];
      break;
   default: /* EK_BOSS */
      if (e->alive == 2)
      {
         an = &hd_boss_anim[BOSS_DOWN];
         fx += ((e->squash >> 1) & 1) ? S(2) : -S(2); /* shaking */
         flags &= ~BLIT_WHITE;
      }
      else if (e->act < 0 && e->flash > 7 && hd_boss_anim[BOSS_HURT].frames)
         an = &hd_boss_anim[BOSS_HURT]; /* a flinch at each hit while it rests: its attacks always show */
      else if (e->act >= 0 && e->act_t <= BOSS_WINDUP(s))
         an = &hd_boss_anim[BOSS_WINDUP];
      else if (e->act >= 0)
         an = &hd_boss_anim[BOSS_ATTACK];
      else
         an = &hd_boss_anim[BOSS_IDLE];
      if (!an->frames)
         an = &hd_boss_anim[BOSS_IDLE];
      break;
   }
   if (!an || !an->frames)
   {
      int32_t x = S(FX_INT(e->x) - cam_x), y = S(FX_INT(e->y) - cam_y);
      if (e->alive != 1)
         return;
      rect(x, y, S(w), S(h), 0x2a1020u);
      rect(x + S(1), y + S(1), S(w - 2), S(h - 2), e->flash ? 0xffffffu : e->kind == EK_BOSS ? 0xb03050u : 0x70b040u);
      return;
   }
   im = frame >= 0 ? &an->frames[frame] : anim_frame(an, t, loop);
   if (e->kind == EK_SPLITTER && e->seq)
   {
      /* a splitter's half: its pictures two thirds of their size, feet on its hitbox's bottom */
      hd_style st;
      int32_t sw = im->w * 2 / 3, sh = (im->h - an->feet) * 2 / 3;
      memset(&st, 0, sizeof st);
      st.flags = ((flags & BLIT_FLIP) ? DRAW_FLIP_X : 0) | ((flags & BLIT_WHITE) ? DRAW_WHITE : 0);
      st.outline = hd_fx.outline;
      fx = S(FX_INT(e->x) - cam_x) + S(w * 2 / 3) / 2;
      fy = S(FX_INT(e->y) + h * 2 / 3 - cam_y);
      if (hd_fx.shadows && shadow)
         gfx_shadow(&surf, fx, fy, shadow * 2 / 3, S(2), 120);
      gfx_blit_rot(&surf, im, fx - sw / 2, fy - sh, 0, 0, 0, FX(2) / 3, FX(2) / 3, &st);
      return;
   }
   actor(im, fx - im->w / 2, fy - (im->h - an->feet), flags, fx, fy, shadow);
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
      if (e->kind != EK_WALKER)
      {
         kind_actor(s, e);
         continue;
      }
      if (hd_objects[OBJ_ENEMY_WALK].frames)
      {
         /* the package's enemy: feet on the hitbox's bottom, facing left like the built-in one */
         int32_t obj = e->alive == 2 && hd_objects[OBJ_ENEMY_SQUASHED].frames ? OBJ_ENEMY_SQUASHED : OBJ_ENEMY_WALK;
         const hd_anim *an = &hd_objects[obj];
         /* with a stride the frames follow where it is (it walks back and forth), else the time */
         const hd_image *im = an->stride > 0 ? &an->frames[((int64_t)hd_abs(FX_INT(e->x)) * an->count / an->stride) % an->count]
                                             : anim_frame(an, e->anim, 1);
         int32_t ex = S(FX_INT(e->x) + EW / 2 - cam_x), ey = S(FX_INT(e->y) + EH - cam_y);
         actor(im, ex - im->w / 2, ey - (im->h - an->feet), (e->vx > 0 ? BLIT_FLIP : 0) | (e->flash ? BLIT_WHITE : 0),
               ex, ey, e->alive == 1 ? S(EW * 3 / 5) : 0);
         continue;
      }
      frame = e->alive == 2 ? ENEMY_SQUASHED : ((e->anim >> 3) & 1);
      actor(&hd_enemy_img[frame], S(FX_INT(e->x) - 1 - cam_x), S(FX_INT(e->y) - 4 - cam_y),
            BLIT_BIG | (e->vx > 0 ? BLIT_FLIP : 0) | (e->flash ? BLIT_WHITE : 0),
            S(FX_INT(e->x) + EW / 2 - cam_x), S(FX_INT(e->y) + EH - cam_y), e->alive == 1 ? S(7) : 0);
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
      if ((p->hurt && ((p->hurt >> 2) & 1)) || (p->ko && ((p->ko >> 2) & 1)))
         continue; /* blinking */
      if (!p->ground)
         frame = HERO_JUMP;
      else if (hd_abs(p->vx) < FX_FRAC(1, 4))
         frame = HERO_IDLE;
      else
         frame = ((p->anim >> 5) & 1) ? HERO_WALK1 : HERO_WALK2;
      actor(&hd_hero[i][frame], S(FX_INT(p->x) - 3 - cam_x), S(FX_INT(p->y) - 2 - cam_y),
            BLIT_BIG | (p->facing < 0 ? BLIT_FLIP : 0) | (p->hurt > HURT_FLASH ? BLIT_WHITE : 0),
            S(FX_INT(p->x) + PW / 2 - cam_x), S(FX_INT(p->y) + PH - cam_y), S(p->ground ? 6 : 4));
   }
}

/* The weapon's shots: the shooter's skin's "shot" and "shot_hit" pictures, else a glowing pellet and a ring. */
static void shots(const hd_state *s)
{
   int32_t i;
   for (i = 0; i < MAX_SHOTS; i++)
   {
      const hd_shot *q = &s->shot[i];
      const hd_skin *sk = hd_skin_count ? &hd_skins[hd_skin_of[q->owner]] : NULL;
      int32_t x = S(FX_INT(q->x) - cam_x), y = S(FX_INT(q->y) - cam_y);
      uint32_t c = hd_player_color[q->owner] & 0xffffffu;
      if (q->hit)
      {
         int32_t done = SHOT_HIT_FRAMES - q->hit, r;
         if (sk && sk->anim[ANIM_SHOT_HIT].frames)
         {
            const hd_anim *an = &sk->anim[ANIM_SHOT_HIT];
            const hd_image *im = &an->frames[hd_min(done * an->count / SHOT_HIT_FRAMES, an->count - 1)];
            blit(im, x - im->w / 2, y - im->h / 2, q->vx < 0 ? BLIT_FLIP : 0);
            continue;
         }
         /* a ring growing and thinning */
         r = S(2 + done / 2);
         rect(x - r, y - r, 2 * r, RES, c);
         rect(x - r, y + r, 2 * r, RES, c);
         rect(x - r, y - r, RES, 2 * r, c);
         rect(x + r, y - r, RES, 2 * r + RES, c);
      }
      else if (q->life && q->granule)
      {
         if (sk && sk->anim[ANIM_GRANULE].frames)
         {
            const hd_image *im = anim_frame(&sk->anim[ANIM_GRANULE], q->age, 1);
            blit(im, x - im->w / 2, y - im->h / 2, 0);
            continue;
         }
         rect(x - S(2), y - S(2), S(4), S(4), c);
         rect(x - S(1), y - S(1), S(1), S(1), 0xffffffu);
      }
      else if (q->life)
      {
         if (sk && sk->anim[ANIM_SHOT].frames)
         {
            const hd_image *im = anim_frame(&sk->anim[ANIM_SHOT], q->age, 1);
            blit(im, x - im->w / 2, y - im->h / 2, q->vx < 0 ? BLIT_FLIP : 0);
            continue;
         }
         rect(x - S(3), y - S(2), S(6), S(4), c);
         rect(x - S(2), y - S(1), S(4), S(2), 0xffffffu);
      }
   }
}

/* The enemies' shots: a spitter's "spit", a boss's own "shot" (else the spit), else a green glob. */
static void bolts(const hd_state *s)
{
   int32_t i;
   for (i = 0; i < MAX_BOLTS; i++)
   {
      const hd_bolt *b = &s->bolt[i];
      const hd_anim *an;
      int32_t x, y, r;
      if (!b->life)
         continue;
      x = S(FX_INT(b->x) - cam_x);
      y = S(FX_INT(b->y) - cam_y);
      an = b->big && hd_boss_anim[BOSS_SHOT].frames ? &hd_boss_anim[BOSS_SHOT]
         : b->puff && hd_objects[OBJ_PUFFER_SHOT].frames ? &hd_objects[OBJ_PUFFER_SHOT]
         : hd_objects[OBJ_SPIT].frames ? &hd_objects[OBJ_SPIT] : NULL;
      if (an)
      {
         const hd_image *im = anim_frame(an, b->age, 1);
         blit(im, x - im->w / 2, y - im->h / 2, b->vx > 0 ? BLIT_FLIP : 0); /* drawn flying left */
         continue;
      }
      r = S(b->big ? 8 : 5);
      rect(x - r - RES, y - r - RES, 2 * r + 2 * RES, 2 * r + 2 * RES, 0x203010u);
      rect(x - r, y - r, 2 * r, 2 * r, 0x9ad040u);
      rect(x - r / 2, y - r / 2, r / 2, r / 2, 0xe0ffa0u);
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
      x = S(FX_INT(q->x) - cam_x);
      y = S(FX_INT(q->y) - cam_y);
      size = S(q->life * 3 / q->max + 1);
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

/* A player's continue, its lives and a game over, in the game's language. */
static const char *const cont_text[3] = { "CONTINUE?", "CONTINUAR?", "CONTINUAR?" };
static const char *const start_text[3] = { "PRESS START", "PULSA START", "APERTE START" };
static const char *const lives_text[3] = { "LIVES", "VIDAS", "VIDAS" };

/* Darkens a box of the screen to a third, so what is drawn on it reads over the game. */
static void dim(int32_t x, int32_t y, int32_t w, int32_t h)
{
   int32_t i, j;
   for (j = hd_max(y, 0); j < hd_min(y + h, surf.h); j++)
      for (i = hd_max(x, 0); i < hd_min(x + w, surf.w); i++)
      {
         uint32_t c = surf.px[j * surf.w + i];
         surf.px[j * surf.w + i] = (c & 0xff000000u) | ((c >> 2) & 0x3f3f3fu) | ((c >> 3) & 0x1f1f1fu);
      }
}

/* A text centered between x and x + w, smaller when it would not fit. */
static void center_in(const char *t, int32_t x, int32_t w, int32_t y, int32_t scale, uint32_t c)
{
   while (scale > 1 && text_width(t, scale) > w - S(8))
      scale--;
   text(t, x + (w - text_width(t, scale)) / 2, y, scale, c);
}

/*
 * A player out of lives: its countdown over its own part of the screen (the
 * whole of it alone, a half with two players, a quarter with more), so the
 * others play on around it.
 */
static void continue_panel(const hd_state *s, int32_t i)
{
   const hd_player *p = &s->p[i];
   int32_t cols = hd_min(hd_max(hd_players, 1), 4), rows = (hd_max(hd_players, 1) + 3) / 4;
   int32_t w = HD_OUT_W / cols, cell_h = HD_OUT_H / rows, ph = hd_min(S(150), cell_h - S(8));
   int32_t x = (i % cols) * w, y = (i / cols) * cell_h + (cell_h - ph) / 2;
   char buf[8] = "P1";
   buf[1] = (char)('1' + i);
   dim(x + S(6), y, w - S(12), ph);
   center_in(buf, x, w, y + S(10), S(2), hd_player_color[i] & 0xffffffu);
   center_in(cont_text[hd_lang], x, w, y + S(30), S(3), 0xffffffu);
   number(buf, (p->cont + 59) / 60);
   center_in(buf, x, w, y + S(60), S(7), 0xf8c838u);
   if ((s->frame >> 4) & 1)
      center_in(start_text[hd_lang], x, w, y + ph - S(24), S(2), 0xffffffu);
}

/* The credits rolling up over the ending picture darkened (else the sky's darkest color): headings gold and bigger. */
static void credits(const hd_state *s)
{
   int32_t i, y0 = S(HD_H + 10) - S(s->phase_t / 2);
   if (hd_screens[SCREEN_ENDING].px)
   {
      memcpy(surf.px, hd_screens[SCREEN_ENDING].px, (size_t)HD_OUT_W * HD_OUT_H * 4);
      dim(0, 0, HD_OUT_W, HD_OUT_H);
   }
   else
      rect(0, 0, HD_OUT_W, HD_OUT_H, 0x100810u);
   for (i = 0; i < hd_credit_count; i++)
   {
      int32_t y = y0 + S(i * CREDIT_LINE);
      const char *t = hd_credits[i];
      if (y < -S(CREDIT_LINE) || y > HD_OUT_H)
         continue;
      if (t[0] == '#' && t[1] == ' ')
         center(t + 2, y, S(3), 0xf8c838u);
      else if (t[0])
         center(t, y + S(3), S(2), 0xffffffu);
   }
}

/* "Hold A to skip" in the game's language. */
static const char *const skip_text[3] = { "HOLD A TO SKIP", "MANTÉN A PARA SALTAR", "SEGURE A PARA PULAR" };

/*
 * A package's whole-screen pictures (format 3's screens): the title (with
 * PRESS START blinking), the level's intro (with a ring filling while jump
 * is held) and the ending (the last part of the stage clear). Returns 1
 * when one was drawn: nothing else goes on top.
 */
static int screen_picture(const hd_state *s)
{
   const hd_image *im = NULL;
   if (s->phase == PH_TITLE && hd_screens[SCREEN_TITLE].px)
      im = &hd_screens[SCREEN_TITLE];
   else if (s->phase == PH_INTRO && hd_screens[SCREEN_INTRO].px)
      im = &hd_screens[SCREEN_INTRO];
   else if (s->phase == PH_CLEAR && s->phase_t >= 180 && hd_screens[SCREEN_ENDING].px && s->stage + 1 >= hd_stage_count)
      im = &hd_screens[SCREEN_ENDING];
   if (!im)
      return 0;
   memcpy(surf.px, im->px, (size_t)HD_OUT_W * HD_OUT_H * 4);
   if (s->phase == PH_TITLE && ((s->frame >> 5) & 1))
      center("PRESS START", S(HD_H - 40), S(2), 0xffffffu);
   if (s->phase == PH_INTRO)
   {
      /* a ring of 24 dots at the bottom right, lit as the hold goes on */
      int32_t lit = s->skip_hold * 24 / SKIP_HOLD_FRAMES, k, cx = S(HD_W - 36), cy = S(HD_H - 36);
      text(skip_text[hd_lang], S(HD_W - 64) - text_width(skip_text[hd_lang], S(1)), S(HD_H - 40), S(1), 0xffffffu);
      for (k = 0; k < 24; k++)
      {
         int32_t a = k * 4096 / 24;
         int32_t dx = S((int32_t)(((int64_t)hd_sin(a) * 18) >> 14)), dy = -S((int32_t)(((int64_t)hd_cos(a) * 18) >> 14));
         rect(cx + dx - S(2), cy + dy - S(2), S(4), S(4), k < lit ? 0xf8c838u : 0x404040u);
      }
   }
   return 1;
}

static void hud(const hd_state *s)
{
   int32_t i;
   char buf[16];
   if (s->phase == PH_TITLE)
   {
      if (hd_content_id[0] | hd_content_id[1] | hd_content_id[2] | hd_content_id[3])
         center(hd_title, S(ROW(110)), S(4), 0xf8c838u);
      else
      {
         center("GO-LINK HD", S(ROW(96)), S(6), 0xf8c838u);
         center("DEMO", S(ROW(150)), S(3), 0xffffffu);
      }
      if ((s->frame >> 5) & 1)
         center("PRESS START", S(ROW(230)), S(2), 0xffffffu);
      snprintf(buf, sizeof buf, "1 TO %d PLAYERS", (int)hd_players);
      if (hd_players == 1)
         strcpy(buf, "1 PLAYER");
      {
         char line[48];
         snprintf(line, sizeof line, "%s - B JUMP - Y RUN", buf);
         center(line, S(ROW(300)), S(1), 0xffffffu);
      }
      return;
   }
   for (i = 0; i < hd_players; i++)
   {
      const hd_player *p = &s->p[i];
      /* as many players a row as the screen's width takes: 4 on 16:9 */
      int32_t per = hd_max(1, HD_W / 156);
      int32_t x = S(12 + (i % per) * 156), y = S(12 + (i / per) * 22);
      buf[0] = 'P';
      buf[1] = (char)('1' + i);
      buf[2] = 0;
      if (!p->active)
      {
         if ((s->frame >> 5) & 1)
         {
            text(buf, x, y, S(2), 0xc0c8d8u);
            text("START", x + S(30), y, S(2), 0xc0c8d8u);
         }
         continue;
      }
      text(buf, x, y, S(2), hd_player_color[i] & 0xffffffu);
      blit(&hd_coin[0], x + S(28), y - S(2), BLIT_BIG);
      buf[0] = 'x';
      number(buf + 1, p->coins);
      text(buf, x + S(46), y, S(2), 0xffffffu);
      if (hd_health.on)
      {
         /* its health: HP and the hits left, red and blinking when worn; after the coins, however many */
         int32_t worn = p->hp <= hd_health.worn, hx = hd_max(x + S(90), x + S(46) + text_width(buf, S(2)) + S(6));
         strcpy(buf, "HP");
         number(buf + 2, p->hp);
         if (!worn || ((s->frame >> 4) & 1))
            text(buf, hx, y, S(2), worn ? 0xff4040u : 0xffffffu);
      }
      if (hd_health.lives)
      {
         /* its lives, small under the line, after the super's bar */
         char line[16];
         snprintf(line, sizeof line, "%s %d", lives_text[hd_lang], (int)p->lives);
         text(line, x + S(66), y + S(16), S(1), p->lives <= 1 ? 0xff4040u : 0xffffffu);
      }
      if (hd_weapon.super_on)
      {
         /* the super's charge: a bar under the line, gold and blinking when full */
         int32_t full = p->charge >= hd_weapon.super_charge, w = S(60);
         rect(x, y + S(17), w + S(2), S(4), 0x201018u);
         rect(x + S(1), y + S(18), w * p->charge / hd_max(1, hd_weapon.super_charge), S(2),
              full ? (((s->frame >> 3) & 1) ? 0xfff0a0u : 0xf8c838u) : hd_player_color[i] & 0xffffffu);
      }
   }
   if (s->boss && s->boss <= MAX_ENEMIES && s->e[s->boss - 1].alive == 1 && s->phase == PH_PLAY)
   {
      /* the boss's health: its name over a bar at the bottom, the bar blinking once it is angry */
      const hd_enemy *b = &s->e[s->boss - 1];
      int32_t w = S(HD_W / 2), x = (HD_OUT_W - w) / 2, y = S(HD_H - 22);
      uint32_t c = s->boss_angry && ((s->frame >> 3) & 1) ? 0xff9030u : 0xe02838u;
      if (hd_boss.name[0])
         center(hd_boss.name, y - S(16), S(2), 0xffffffu);
      rect(x - S(2), y - S(2), w + S(4), S(10), 0x1a1020u);
      rect(x, y, (int32_t)((int64_t)w * hd_max(0, b->hp) / hd_max(1, s->boss_max)), S(6), c);
   }
   for (i = 0; i < hd_players; i++)
      if (s->p[i].active && s->p[i].cont)
         continue_panel(s, i);
   if (s->phase == PH_OVER)
   {
      dim(0, S(HD_H / 2 - 40), HD_OUT_W, S(80));
      center("GAME OVER", S(HD_H / 2 - 21), S(6), 0xff4040u);
   }
   if (s->paused)
   {
      rect(0, S(HD_H / 2 - 30), HD_OUT_W, S(60), 0x1a1020u);
      center("PAUSE", S(HD_H / 2 - 14), S(4), 0xffffffu);
   }
   if (s->phase == PH_CLEAR)
   {
      center("STAGE CLEAR!", S(110), S(5), 0xf8c838u);
      for (i = 0; i < hd_players; i++)
         if (s->p[i].active)
         {
            char line[24] = "P1  x";
            line[1] = (char)('1' + i);
            number(line + 5, s->p[i].coins);
            center(line, S(180 + i * 24), S(2), hd_player_color[i] & 0xffffffu);
         }
   }
}

/* The world (backdrop, level, characters, particles) with its top-left at the camera, into a surface. */
void hd_draw_world(const hd_state *s, hd_surface *target, int32_t cx, int32_t cy)
{
   surf = *target;
   cam_x = cx;
   cam_y = cy;
   if (hd_layer_count)
   {
      int32_t x, y;
      /* the sky, unless a solid layer covers the whole surface anyway */
      for (y = 0; y < surf.h && !hd_layers_cover(surf.h, S(cam_y)); y++)
      {
         uint32_t c = sky_at(y), *row = surf.px + y * surf.w;
         for (x = 0; x < surf.w; x++)
            row[x] = c;
      }
      hd_layers_draw(surf.px, surf.w, surf.h, S(cam_x), S(cam_y), 0);
   }
   else
      backdrop();
   level(s);
   actors(s);
   shots(s);
   bolts(s);
   particles(s);
   if (hd_layer_count)
      hd_layers_draw(surf.px, surf.w, surf.h, S(cam_x), S(cam_y), 1);
}

/* The level's effects (package format 2), on the finished world. */
static void effects(const hd_state *s, hd_surface *screen, int32_t zoom)
{
   static hd_light lights[FX_LIGHTS_MAX + MAX_PLAYERS];
   int32_t n = 0, i;
   if (hd_fx.waves_amp)
   {
      int32_t y0 = S(hd_fx.waves_y - cam_y) * zoom / 256;
      fx_waves(screen, hd_max(y0, 0), screen->h - 1, S(hd_fx.waves_amp), S(hd_max(8, hd_fx.waves_len)), s->frame * 40);
   }
   if (hd_fx.darkness > 0 || hd_fx.lights > 0)
   {
      for (i = 0; i < hd_fx.lights; i++)
      {
         hd_light *l = &lights[n++];
         l->x = S(hd_fx.light_x[i] - cam_x) * zoom / 256;
         l->y = S(hd_fx.light_y[i] - cam_y) * zoom / 256;
         /* a flame's flicker: the radius breathes with two sines */
         l->radius = S(hd_fx.light_r[i] + hd_fx.light_flicker[i] * (hd_sin(s->frame * 97 + i * 911) + hd_sin(s->frame * 41 + i * 333)) / (2 * TRIG_ONE)) * zoom / 256;
         l->color = hd_fx.light_color[i];
         l->strength = 256;
      }
      if (hd_fx.player_light)
         for (i = 0; i < MAX_PLAYERS; i++)
            if (s->p[i].active && !s->p[i].respawn)
            {
               hd_light *l = &lights[n++];
               l->x = S(FX_INT(s->p[i].x) + PW / 2 - cam_x) * zoom / 256;
               l->y = S(FX_INT(s->p[i].y) + PH / 2 - cam_y) * zoom / 256;
               l->radius = S(hd_fx.player_light) * zoom / 256;
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
   hd_stage_select(s->stage);
   if (s->boss_form != hd_boss_form_now())
      hd_boss_form_use(s->boss_form);
   if (ready_gen != hd_content_gen)
      prepare();
   screen.px = out;
   screen.w = HD_OUT_W;
   screen.h = HD_OUT_H;
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
      big.w = hd_min(HD_OUT_W * 256 / zoom, ZOOM_MAX_W);
      big.h = hd_min(HD_OUT_H * 256 / zoom, ZOOM_MAX_H);
      hd_draw_world(s, &big, cx, cy);
      gfx_scale(&screen, &big);
   }
   effects(s, &screen, zoom);
   surf = screen;
   if (s->phase == PH_CREDITS)
   {
      credits(s);
      return;
   }
   if (screen_picture(s))
      return;
   hud(s);
   if (s->dlg > 0 && s->dlg <= hd_fx.dialogs)
      text_dialog(&screen, hd_portrait.px ? &hd_portrait : NULL, hd_fx.dialog_name[s->dlg - 1],
                  hd_fx.dialog_text[s->dlg - 1][hd_lang], s->dlg_chars, (s->frame >> 4) & 1);
}
