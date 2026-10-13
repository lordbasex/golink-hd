/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The 3D proof of concept (golinkhd_load_demo(e, 2)): a lived-in flat on
 * the fifth floor of a city block, seen from above without its ceiling like
 * a scale model, built in code from boxes, cylinders and balls with the
 * textures of tex3d.c, lit by r3d_bake (the sun's shadows, the sky, the
 * lamps). A person stands at the desk to show what moves. The first player
 * turns the view (left, right), tilts it (up, down), zooms (L, R) and walks
 * the view's center (A, B, X, Y).
 *
 * Its state is the showcase's (hd_show, `on` 2): kx and ky are the view's
 * center, ka its turn, kv its tilt, rpos its distance in centimeters.
 *
 * The plan, x to the east and z to the north, in centimeters (the flat is
 * 1200 x 900):
 *   north:  bedroom 0..450 | bathroom 450..700 | office 700..1200   (z 500..900)
 *   middle: hall 0..1200, the front door at its east end            (z 400..500)
 *   south:  living and dining 0..750 | kitchen 750..1200             (z 0..400)
 *   outside the living room, a balcony (z -160..0)
 */
#include <stdio.h>
#include <string.h>
#include "hd.h"
#include "gfx.h"
#include "text.h"
#include "r3d.h"
#include "actor3d.h"

#define CM(n) FX_FRAC(n, 100)
#define M(n) FX(n)
/* the flat sits away from 0 so the view's center stays positive (hd_show clamps it to 0..511) */
#define BASE CM(4000)

static r3d_mesh scene;
static actor3d_kit people[2];
static const int32_t *T;
static int32_t ready, poster[8];
static r3d_light_cfg sunlight;

/* ---- small helpers ---- */

/* Pieces at (x, y, z) centimeters of the flat, turned yaw. */
static void at(int32_t x, int32_t y, int32_t z, int32_t yaw)
{
   r3d_place(BASE + CM(x), CM(y), BASE + CM(z), yaw & (ANGLE_FULL - 1));
}

static void mat(int32_t tex, int32_t texels_per_m)
{
   r3d_material(tex >= 0 ? T[tex] : -1, texels_per_m);
}

static void plain(void)
{
   r3d_material(-1, 0);
}

/* A box in centimeters. */
static void bx(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1, uint32_t c)
{
   r3d_box(CM(x0), CM(y0), CM(z0), CM(x1), CM(y1), CM(z1), c, 0);
}

static void bxf(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1, uint32_t c, int32_t flags)
{
   r3d_box(CM(x0), CM(y0), CM(z0), CM(x1), CM(y1), CM(z1), c, flags);
}

static void cyl(int32_t x, int32_t y, int32_t z, int32_t r, int32_t rt, int32_t h, int32_t sides, uint32_t c)
{
   r3d_cylinder(CM(x), CM(y), CM(z), CM(r), CM(rt), CM(h), sides, c);
}

static void ball(int32_t x, int32_t y, int32_t z, int32_t rx, int32_t ry, int32_t rz, int32_t rings, uint32_t c)
{
   r3d_ball(CM(x), CM(y), CM(z), CM(rx), CM(ry), CM(rz), rings, c);
}

/* A picture stretched over a box's front (toward -z) and back. */
static void pane(int32_t tex, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1, uint32_t c)
{
   r3d_material(tex, 0);
   bxf(x0, y0, z0, x1, y1, z1, c, R3D_PANE);
   plain();
}

static uint32_t darker(uint32_t c)
{
   return gfx_mix(c, 0x000000, 40);
}

/* ---- the shell: floors, walls with doors and windows ---- */

enum { OPEN_DOOR = 0, OPEN_WINDOW, OPEN_BIG };
typedef struct
{
   int32_t from, to, kind;
} opening;

/* One stretch of wall, both of its faces, from s0 to s1 along it and y0 to y1 up. */
static void wall_piece(int32_t dir, int32_t pos, int32_t s0, int32_t s1, int32_t y0, int32_t y1, uint32_t c1, uint32_t c2)
{
   if (s1 <= s0 || y1 <= y0)
      return;
   mat(TEX3D_PLASTER, 64);
   if (dir == 0)
   {
      bxf(s0, y0, pos - 6, s1, y1, pos, c1, R3D_SKIP_BOTTOM);
      bxf(s0, y0, pos, s1, y1, pos + 6, c2, R3D_SKIP_BOTTOM);
   }
   else
   {
      bxf(pos - 6, y0, s0, pos, y1, s1, c1, R3D_SKIP_BOTTOM);
      bxf(pos, y0, s0, pos + 6, y1, s1, c2, R3D_SKIP_BOTTOM);
   }
   plain();
}

/* A window's frame, sill and glass (the glass lets light in), across an opening. */
static void window_in(int32_t dir, int32_t pos, int32_t s0, int32_t s1, int32_t bot, int32_t top)
{
   int32_t mid = (s0 + s1) / 2;
   plain();
   if (dir == 0)
   {
      bx(s0, bot, pos - 7, s0 + 6, top, pos + 7, 0xf4f2ee);
      bx(s1 - 6, bot, pos - 7, s1, top, pos + 7, 0xf4f2ee);
      bx(s0, top - 6, pos - 7, s1, top, pos + 7, 0xf4f2ee);
      bx(mid - 3, bot, pos - 4, mid + 3, top, pos + 4, 0xf4f2ee);
      if (bot)
         bx(s0 - 6, bot - 4, pos - 16, s1 + 6, bot, pos + 10, 0xf7f5f0);
      r3d_occlude(0);
      r3d_glass(90);
      bxf(s0 + 6, bot + (bot ? 0 : 2), pos - 1, s1 - 6, top - 6, pos + 1, 0xa8d4ee, R3D_PANE);
   }
   else
   {
      bx(pos - 7, bot, s0, pos + 7, top, s0 + 6, 0xf4f2ee);
      bx(pos - 7, bot, s1 - 6, pos + 7, top, s1, 0xf4f2ee);
      bx(pos - 7, top - 6, s0, pos + 7, top, s1, 0xf4f2ee);
      bx(pos - 4, bot, mid - 3, pos + 4, top, mid + 3, 0xf4f2ee);
      if (bot)
         bx(pos - 10, bot - 4, s0 - 6, pos + 16, bot, s1 + 6, 0xf7f5f0);
      r3d_occlude(0);
      r3d_glass(90);
      bxf(pos - 1, bot + (bot ? 0 : 2), s0 + 6, pos + 1, top - 6, s1 - 6, 0xa8d4ee, R3D_SKIP_BOTTOM | R3D_SKIP_TOP | R3D_SKIP_Z);
   }
   r3d_glass(0);
   r3d_occlude(1);
}

/*
 * A wall along x (dir 0, at z = pos from x = a to b) or along z (dir 1, at
 * x = pos from z = a to b), painted c1 on its lower side and c2 on its
 * upper one, with its openings (centimeters along it): a door's gap up to
 * 210, a window from 95 to 215, a big window (a balcony's door) up to 230.
 */
static void wall(int32_t dir, int32_t pos, int32_t a, int32_t b, uint32_t c1, uint32_t c2, const opening *o, int32_t n)
{
   int32_t i, cur = a;
   at(0, 0, 0, 0);
   for (i = 0; i < n; i++)
   {
      int32_t bot = o[i].kind == OPEN_WINDOW ? 95 : 0, top = o[i].kind == OPEN_DOOR ? 210 : o[i].kind == OPEN_BIG ? 230 : 215;
      wall_piece(dir, pos, cur, o[i].from, 0, 265, c1, c2);
      wall_piece(dir, pos, o[i].from, o[i].to, top, 265, c1, c2);
      wall_piece(dir, pos, o[i].from, o[i].to, 0, bot, c1, c2);
      if (o[i].kind != OPEN_DOOR)
         window_in(dir, pos, o[i].from, o[i].to, bot, top);
      cur = o[i].to;
   }
   wall_piece(dir, pos, cur, b, 0, 265, c1, c2);
}

/* A floor of the flat, from (x0, z0) to (x1, z1), its texture at `tpm` texels a meter. */
static void floor_of(int32_t x0, int32_t z0, int32_t x1, int32_t z1, int32_t tex, int32_t tpm, uint32_t c)
{
   at(0, 0, 0, 0);
   mat(tex, tpm);
   bxf(x0, -4, z0, x1, 0, z1, c, R3D_SKIP_BOTTOM | R3D_SKIP_SIDES);
   plain();
}

/* A skirting board along x or z, on the side `side` (+1 or -1) of the wall. */
static void skirting(int32_t dir, int32_t pos, int32_t a, int32_t b, int32_t side)
{
   at(0, 0, 0, 0);
   r3d_occlude(0);
   if (dir == 0)
      bx(a, 0, side > 0 ? pos + 6 : pos - 7, b, 8, side > 0 ? pos + 7 : pos - 6, 0xfaf8f4);
   else
      bx(side > 0 ? pos + 6 : pos - 7, 0, a, side > 0 ? pos + 7 : pos - 6, 8, b, 0xfaf8f4);
   r3d_occlude(1);
}

/* ---- furniture: each built at the place set by at() ---- */

static void books(int32_t x0, int32_t y, int32_t z0, int32_t width, int32_t depth, uint32_t seed)
{
   static const uint32_t col[] = { 0xb03a2e, 0x2e6f9e, 0x2f8f5a, 0xe0a030, 0x7d4a9e, 0x34495e, 0xd35400, 0xecf0f1, 0x16a085, 0x8d6e63 };
   int32_t x = x0;
   uint32_t r = seed * 2654435761u + 1;
   while (x < x0 + width - 3)
   {
      int32_t w = 2 + (int32_t)(rng_next(&r) % 4), h = 16 + (int32_t)(rng_next(&r) % 12);
      uint32_t c = col[rng_next(&r) % 10];
      if (rng_next(&r) % 11 == 0 && x + 22 < x0 + width)
      {
         /* a few lying flat */
         int32_t k;
         for (k = 0; k < 3; k++)
            bx(x, y + k * 3, z0 + 2, x + 18, y + k * 3 + 3, z0 + depth - 3, col[(k + (int32_t)seed) % 10]);
         x += 20;
         continue;
      }
      if (rng_next(&r) % 13 == 0 && x + 12 < x0 + width)
      {
         /* a small thing among them */
         cyl(x + 5, y, z0 + depth / 2, 4, 3, 9, 6, rng_next(&r) & 1 ? 0xe9e2d0 : 0x6e8b74);
         x += 12;
         continue;
      }
      bx(x, y, z0 + 2, x + w, y + h, z0 + depth - 2, c);
      x += w + (rng_next(&r) % 5 == 0 ? 3 : 0);
   }
}

static void bookshelf(int32_t w, int32_t h, int32_t rows, uint32_t wood, uint32_t seed)
{
   int32_t r;
   bx(0, 0, 0, 3, h, 32, wood);
   bx(w - 3, 0, 0, w, h, 32, wood);
   bx(0, 0, 30, w, h, 32, darker(wood));
   for (r = 0; r <= rows; r++)
   {
      int32_t y = r * (h - 4) / rows;
      bx(3, y, 0, w - 3, y + 3, 30, wood);
      if (r < rows)
         books(5, y + 3, 0, w - 10, 28, seed + (uint32_t)r * 7);
   }
}

static void plant_pot(int32_t size, uint32_t pot, uint32_t seed)
{
   int32_t i, n = 5 + (int32_t)(seed % 4);
   cyl(0, 0, 0, size * 4 / 10, size / 2, size * 7 / 10, 10, pot);
   cyl(0, size * 7 / 10, 0, size / 2 - 2, size / 2 - 2, 2, 10, 0x3b2a1a);
   mat(TEX3D_LEAVES, 64);
   for (i = 0; i < n; i++)
   {
      int32_t a = (int32_t)((i * 4096 / n + (int32_t)seed * 300) & 4095), rr = size * (3 + i % 3) / 10;
      int32_t x = (int32_t)(((int64_t)rr * hd_cos(a)) >> 14), z = (int32_t)(((int64_t)rr * hd_sin(a)) >> 14);
      ball(x, size + size * (i % 4) / 3, z, size / 2, size / 3, size / 2, 3, i & 1 ? 0x3f8f45 : 0x5aaa52);
   }
   ball(0, size * 2, 0, size / 2, size / 2, size / 2, 3, 0x4f9a4a);
   plain();
}

static void tall_plant(void)
{
   int32_t i;
   cyl(0, 0, 0, 20, 24, 44, 12, 0xd8d2c4);
   cyl(0, 44, 0, 22, 22, 2, 12, 0x3b2a1a);
   cyl(0, 44, 0, 2, 1, 110, 5, 0x6b4f2a);
   mat(TEX3D_LEAVES, 64);
   for (i = 0; i < 9; i++)
   {
      int32_t a = (i * 1500) & 4095, rr = 14 + (i % 3) * 8;
      ball((int32_t)(((int64_t)rr * hd_cos(a)) >> 14), 90 + (i % 5) * 16, (int32_t)(((int64_t)rr * hd_sin(a)) >> 14), 18, 9, 14, 3,
           i & 1 ? 0x2e7d32 : 0x43a047);
   }
   plain();
}

static void mug(int32_t x, int32_t y, int32_t z, uint32_t c)
{
   cyl(x, y, z, 4, 4, 9, 8, c);
   cyl(x, y + 8, z, 3, 3, 1, 8, 0x4a2c1a);
   bx(x + 4, y + 2, z - 1, x + 6, y + 7, z + 1, c);
}

static void chair_wood(uint32_t wood, uint32_t seat)
{
   bx(0, 44, 0, 42, 48, 42, wood);
   mat(TEX3D_FABRIC, 96);
   bx(2, 48, 2, 40, 51, 40, seat);
   plain();
   bx(2, 0, 2, 6, 44, 6, wood);
   bx(36, 0, 2, 40, 44, 6, wood);
   bx(2, 0, 36, 6, 92, 40, wood);
   bx(36, 0, 36, 40, 92, 40, wood);
   bx(2, 70, 37, 40, 90, 40, wood);
}

static void desk_chair(void)
{
   int32_t i;
   for (i = 0; i < 5; i++)
   {
      int32_t a = i * ANGLE_FULL / 5, x = (int32_t)((26LL * hd_cos(a)) >> 14), z = (int32_t)((26LL * hd_sin(a)) >> 14);
      bx(hd_min(0, x) - 2, 4, hd_min(0, z) - 2, hd_max(0, x) + 2, 7, hd_max(0, z) + 2, 0x26262a);
      ball(x, 3, z, 3, 3, 3, 2, 0x111114);
   }
   cyl(0, 6, 0, 3, 3, 38, 6, 0x9a9aa0);
   mat(TEX3D_FABRIC, 128);
   bx(-26, 44, -26, 26, 54, 26, 0x2b3f6b);
   bx(-24, 56, 18, 24, 120, 28, 0x2b3f6b);
   bx(-12, 100, 16, 12, 118, 19, 0xe8a13a); /* a cushion for the neck */
   plain();
   bx(-30, 54, -14, -26, 70, 14, 0x26262a);
   bx(26, 54, -14, 30, 70, 14, 0x26262a);
}

/* A screen at its place, its picture a texture that glows. */
static void monitor(int32_t x, int32_t w, int32_t h, int32_t tex)
{
   bx(x + w / 2 - 10, 0, 6, x + w / 2 + 10, 2, 22, 0x2a2a2e);
   bx(x + w / 2 - 2, 2, 16, x + w / 2 + 2, 16, 20, 0x2a2a2e);
   bx(x, 12, 10, x + w, 12 + h, 14, 0x18181b);
   r3d_glow(300);
   pane(tex, x + 1, 13, 9, x + w - 1, 11 + h, 10, 0xffffff);
   r3d_glow(0);
}

static void rubber_duck(int32_t x, int32_t y, int32_t z)
{
   ball(x, y + 4, z, 5, 4, 4, 3, 0xffd23f);
   ball(x + 3, y + 9, z, 3, 3, 3, 3, 0xffd23f);
   bx(x + 5, y + 8, z - 1, x + 8, y + 9, z + 1, 0xf28c28);
}

static void can(int32_t x, int32_t y, int32_t z, uint32_t c)
{
   cyl(x, y, z, 3, 3, 12, 8, c);
}

static void cardboard_box(int32_t w, int32_t h, int32_t d)
{
   bx(0, 0, 0, w, h, d, 0xc49a6c);
   bx(-1, h - 1, d / 2 - 3, w + 1, h + 1, d / 2 + 3, 0xd9c9a8); /* tape */
}

static void floor_lamp(uint32_t shade_c)
{
   cyl(0, 0, 0, 14, 14, 3, 12, 0x2c2c2c);
   cyl(0, 3, 0, 1, 1, 150, 5, 0x2c2c2c);
   r3d_glow(320);
   cyl(0, 140, 0, 20, 12, 26, 12, shade_c);
   r3d_glow(0);
}

static void pendant(int32_t drop)
{
   r3d_occlude(0);
   cyl(0, 265 - drop, 0, 1, 1, drop, 4, 0x222222);
   r3d_glow(360);
   cyl(0, 265 - drop - 18, 0, 22, 4, 18, 12, 0x2f6f6a);
   ball(0, 265 - drop - 18, 0, 5, 4, 5, 3, 0xfff1c8);
   r3d_glow(0);
   r3d_occlude(1);
}

static void frame_pic(int32_t w, int32_t h, int32_t tex)
{
   bx(0, 0, 0, w, h, 3, 0x2b2b2b);
   pane(tex, 3, 3, -1, w - 3, h - 3, 0, 0xffffff);
}

/* ---- the rooms ---- */

static void living(void)
{
   int32_t i;
   /* the sofa, an L facing the TV wall (west), with cushions and a blanket */
   at(320, 0, 120, 0);
   mat(TEX3D_FABRIC, 96);
   bx(0, 10, 0, 220, 44, 95, 0x51637a);
   bx(0, 44, 75, 220, 92, 95, 0x48586d);
   bx(0, 44, 0, 18, 64, 75, 0x48586d);
   bx(202, 10, -90, 220, 64, 0, 0x48586d);
   bx(140, 10, -90, 202, 44, 0, 0x51637a);
   for (i = 0; i < 3; i++)
      bx(22 + i * 58, 44, 6, 76 + i * 58, 52, 72, 0x5e7189);
   bx(150, 44, -84, 200, 52, 0, 0x5e7189);
   bx(40, 52, 58, 80, 86, 72, 0xe08e45);
   bx(150, 52, 60, 186, 82, 72, 0xe9c46a);
   bx(100, 52, 62, 132, 80, 72, 0x2a9d8f);
   mat(TEX3D_KNIT, 96);
   bx(60, 52, 10, 120, 55, 60, 0xd9d2c5);
   plain();
   for (i = 0; i < 4; i++)
      cyl(8 + (i & 1) * 204, 0, 8 + (i >> 1) * 80, 3, 2, 10, 6, 0x2c2c2c);
   /* the coffee table and what is on it */
   at(370, 0, 30, 0);
   mat(TEX3D_WOOD, 128);
   bx(0, 36, 0, 110, 40, 55, 0x9b6a3c);
   bx(0, 6, 0, 110, 9, 55, 0x9b6a3c);
   plain();
   for (i = 0; i < 4; i++)
      bx((i & 1) * 104, 0, (i >> 1) * 49, (i & 1) * 104 + 6, 36, (i >> 1) * 49 + 6, 0x2c2c2c);
   bx(12, 40, 10, 44, 42, 32, 0x2a2a30);         /* a laptop, closed */
   mug(60, 40, 20, 0xe76f51);
   mug(72, 40, 38, 0xf4f1de);
   bx(80, 40, 6, 104, 41, 24, 0xe63946);          /* a magazine */
   bx(82, 41, 8, 102, 42, 22, 0xf1faee);
   bx(20, 40, 40, 34, 42, 46, 0x111111);          /* the remote */
   cyl(94, 40, 42, 8, 10, 6, 10, 0xd4a373);       /* a bowl of crisps */
   ball(94, 46, 42, 7, 2, 7, 3, 0xf2cc8f);
   bx(10, 9, 10, 60, 14, 40, 0x6d597a);           /* magazines below */
   /* the TV unit on the west wall */
   at(48, 0, 90, 3072);
   mat(TEX3D_WOOD, 128);
   bx(0, 0, 0, 220, 50, 42, 0xc8a27a);
   plain();
   bx(4, 6, -1, 70, 44, 0, 0xb08e6c);
   bx(76, 6, -1, 144, 44, 0, 0xb08e6c);
   bx(150, 6, -1, 216, 44, 0, 0xb08e6c);
   bx(60, 50, 8, 160, 54, 30, 0x222226);            /* TV stand */
   bx(103, 54, 18, 117, 70, 24, 0x222226);
   bx(40, 64, 16, 180, 146, 20, 0x111114);          /* the TV */
   r3d_glow(260);
   pane(poster[0], 42, 66, 15, 178, 144, 16, 0xffffff);
   r3d_glow(0);
   bx(8, 50, 10, 30, 82, 32, 0x2d2d33);             /* speakers */
   bx(190, 50, 10, 212, 82, 32, 0x2d2d33);
   cyl(19, 72, 9, 6, 6, 1, 10, 0x555560);
   cyl(201, 72, 9, 6, 6, 1, 10, 0x555560);
   bx(70, 50, 4, 100, 55, 34, 0x1b1b1f);            /* a game console */
   bx(120, 50, 10, 150, 53, 30, 0xf1f1f1);
   at(26, 50, 104, 0);
   plant_pot(18, 0xe9e2d0, 3);
   /* shelves on the north wall of the living room, records and plants */
   at(20, 0, 360, 0);
   bookshelf(120, 190, 5, 0xe6dccb, 11);
   at(150, 0, 360, 0);
   bookshelf(80, 110, 3, 0xe6dccb, 12);
   at(166, 110, 364, 0);
   bx(0, 0, 0, 30, 10, 26, 0x2b2b2b);              /* a turntable */
   cyl(15, 10, 13, 11, 11, 1, 16, 0x111111);
   at(212, 110, 376, 0);
   plant_pot(14, 0x8d99ae, 7);
   /* the armchair and a floor lamp */
   at(560, 0, 70, 3072 + 400);
   mat(TEX3D_FABRIC, 96);
   bx(0, 10, 0, 80, 44, 80, 0xb5654a);
   bx(0, 44, 64, 80, 90, 80, 0xa65a40);
   bx(0, 44, 0, 14, 66, 64, 0xa65a40);
   bx(66, 44, 0, 80, 66, 64, 0xa65a40);
   plain();
   at(660, 0, 160, 0);
   floor_lamp(0xf6e7c1);
   r3d_light(0, CM(150), 0, M(4), 0xffd59a, 300);
   at(40, 0, 360, 0);
   tall_plant();
   /* the rug */
   at(300, 0, 10, 0);
   mat(TEX3D_RUG, 0);
   r3d_occlude(0);
   bxf(0, 0, 0, 240, 1, 170, 0xffffff, R3D_TOP_ONLY);
   r3d_occlude(1);
   plain();
   /* the dining table and its four chairs, a bowl of fruit, a pendant lamp */
   at(560, 0, 260, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 72, 0, 150, 76, 85, 0xb07a48);
   plain();
   bx(6, 0, 6, 12, 72, 12, 0x5b3d22);
   bx(138, 0, 6, 144, 72, 12, 0x5b3d22);
   bx(6, 0, 73, 12, 72, 79, 0x5b3d22);
   bx(138, 0, 73, 144, 72, 79, 0x5b3d22);
   cyl(75, 76, 42, 16, 20, 7, 12, 0xe9edc9);
   ball(70, 85, 40, 5, 5, 5, 3, 0xe63946);
   ball(80, 85, 45, 5, 5, 5, 3, 0xf4a261);
   ball(75, 86, 36, 4, 4, 4, 3, 0x90be6d);
   bx(20, 76, 20, 50, 77, 60, 0xf8f9fa);       /* a placemat with papers */
   mug(110, 76, 60, 0x264653);
   at(570, 0, 220, 0);
   chair_wood(0x6b4423, 0xd9c7a7);
   at(660, 0, 220, 0);
   chair_wood(0x6b4423, 0xd9c7a7);
   at(612, 0, 380, 2048);
   chair_wood(0x6b4423, 0xd9c7a7);
   at(702, 0, 380, 2048);
   chair_wood(0x6b4423, 0xd9c7a7);
   at(635, 0, 302, 0);
   pendant(80);
   r3d_light(0, CM(160), 0, M(3), 0xffe2b0, 260);
   /* pictures and a clock */
   at(300, 140, 391, 0);
   frame_pic(70, 50, poster[1]);
   at(390, 150, 391, 0);
   frame_pic(40, 56, poster[2]);
   at(9, 160, 330, 3072);
   frame_pic(60, 44, poster[3]);
   at(720, 180, 392, 2048);
   cyl(0, 0, 0, 14, 14, 3, 16, 0xf8f8f8);
   /* a guitar on its stand, a pair of trainers, a radiator */
   at(470, 0, 360, 0);
   bx(-8, 0, -6, 8, 30, 6, 0x222222);
   ball(0, 40, 0, 16, 18, 5, 3, 0x9c5a2b);
   ball(0, 66, 0, 12, 12, 5, 3, 0x9c5a2b);
   bx(-2, 76, -2, 2, 120, 2, 0x3b2a1a);
   at(520, 0, 340, 300);
   bx(0, 0, 0, 12, 8, 28, 0xf1f1f1);
   bx(14, 0, 4, 26, 8, 32, 0xf1f1f1);
   bx(0, 0, 18, 12, 6, 28, 0xe63946);
   at(640, 10, 8, 0);
   for (i = 0; i < 10; i++)
      bx(i * 9, 0, 0, i * 9 + 6, 60, 8, 0xf2f2f2);
}

static void kitchen(void)
{
   int32_t i;
   /* the counter along the south wall: cupboards, a sink, a hob, a kettle */
   at(760, 0, 8, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 0, 0, 380, 86, 62, 0x7c9a92);
   plain();
   for (i = 0; i < 6; i++)
   {
      bx(4 + i * 63, 8, 62, 62 + i * 63, 82, 63, 0x86a59c);
      bx(28 + i * 63, 60, 63, 38 + i * 63, 62, 66, 0xc0c0c0);
   }
   mat(TEX3D_MARBLE, 100);
   bx(-2, 86, -2, 382, 90, 64, 0xffffff);
   plain();
   bx(60, 82, 10, 120, 90, 50, 0xc9ccd1);        /* the sink */
   bx(64, 84, 14, 116, 90, 46, 0x9fa4ab);
   cyl(90, 90, 8, 2, 2, 28, 6, 0xd0d0d0);
   bx(88, 114, 8, 92, 118, 24, 0xd0d0d0);
   bx(200, 90, 8, 270, 91, 54, 0x1c1c1c);         /* the hob */
   for (i = 0; i < 4; i++)
      cyl(215 + (i & 1) * 40, 91, 20 + (i >> 1) * 24, 8, 8, 1, 12, 0x3a3a3a);
   cyl(215, 91, 20, 12, 12, 14, 12, 0xb0b5bb);    /* a pot */
   cyl(255, 91, 44, 13, 13, 4, 12, 0x2e2e2e);     /* a pan */
   bx(268, 93, 42, 290, 95, 46, 0x2e2e2e);
   cyl(320, 90, 30, 9, 7, 22, 10, 0xd62828);      /* the kettle */
   bx(150, 90, 10, 190, 91, 40, 0xe0c39a);        /* a cutting board with bread */
   ball(170, 96, 25, 14, 5, 8, 3, 0xc98b4a);
   bx(350, 90, 20, 372, 120, 46, 0x2b2b2b);       /* the coffee machine */
   mug(358, 90, 8, 0xffffff);
   for (i = 0; i < 3; i++)
      cyl(20 + i * 12, 90, 50, 4, 4, 14 + i * 4, 6, i == 0 ? 0x6a994e : i == 1 ? 0xbc4749 : 0xf2e8cf); /* bottles and jars */
   /* tiles behind it and the cupboards above */
   at(760, 90, 0, 0);
   mat(TEX3D_SUBWAY, 128);
   bxf(0, 0, 6, 380, 60, 7, 0xffffff, R3D_PANE);
   plain();
   at(760, 150, 0, 0);
   bx(0, 0, 6, 330, 75, 40, 0xf1eee8);
   for (i = 0; i < 5; i++)
      bx(4 + i * 65, 4, 40, 64 + i * 65, 71, 41, 0xf1ede4);
   r3d_light(CM(160), CM(-20), CM(30), M(2), 0xfff0d0, 200);
   /* the fridge in the corner */
   at(1140, 0, 8, 0);
   bx(0, 0, 0, 52, 195, 62, 0xe8eaec);
   bx(0, 120, 62, 52, 121, 63, 0xb7bcc2);
   bx(44, 130, 62, 47, 170, 66, 0xb7bcc2);
   bx(44, 60, 62, 47, 110, 66, 0xb7bcc2);
   bx(10, 150, 63, 18, 158, 64, 0xe63946);        /* magnets and a note */
   bx(22, 140, 63, 34, 155, 64, 0xffe66d);
   bx(8, 90, 63, 30, 115, 64, 0xf8f9fa);
   /* the island with two stools and a fruit bowl */
   at(880, 0, 190, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 0, 0, 160, 88, 70, 0x3d5a80);
   plain();
   mat(TEX3D_MARBLE, 100);
   bx(-6, 88, -6, 166, 92, 76, 0xffffff);
   plain();
   cyl(40, 92, 35, 14, 16, 6, 12, 0x2a2a2a);
   ball(36, 100, 32, 5, 5, 5, 3, 0xffb703);
   ball(46, 100, 38, 5, 5, 5, 3, 0x8ac926);
   bx(100, 92, 20, 140, 94, 50, 0xd0d0d0);       /* a laptop open: work from the kitchen */
   bx(100, 94, 48, 140, 120, 50, 0x2a2a30);
   for (i = 0; i < 2; i++)
   {
      at(905 + i * 80, 0, 290, 0);
      cyl(0, 0, 0, 12, 12, 3, 10, 0x333333);
      cyl(0, 3, 0, 2, 2, 62, 6, 0x333333);
      cyl(0, 62, 0, 16, 16, 6, 12, 0xc0703d);
   }
   /* the bins: three colors */
   at(1150, 0, 330, 1024);
   bx(0, 0, 0, 30, 60, 36, 0xf2c14e);
   bx(34, 0, 0, 64, 60, 36, 0x3a86ff);
   bx(68, 0, 0, 98, 60, 36, 0x8ac926);
   /* a pendant over the island */
   at(960, 0, 225, 0);
   pendant(70);
   r3d_light(0, CM(170), 0, M(3), 0xffe2b0, 240);
   /* a calendar */
   at(1191, 150, 390, 1024);
   frame_pic(36, 46, poster[4]);
}

static void office(void)
{
   int32_t i;
   /* the desk against the north wall, two screens of code, the work's whole kit */
   at(870, 0, 818, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 72, 0, 180, 76, 72, 0x2b2b2b);
   plain();
   bx(4, 0, 4, 8, 72, 68, 0x1c1c1c);
   bx(172, 0, 4, 176, 72, 68, 0x1c1c1c);
   bx(4, 66, 64, 176, 72, 68, 0x1c1c1c);
   at(870, 76, 818, 0);
   monitor(30, 60, 34, poster[5]);
   monitor(96, 60, 34, poster[6]);
   bx(50, 0, 14, 110, 2, 30, 0x222226);           /* keyboard, glowing a little */
   r3d_glow(140);
   bx(52, 2, 16, 108, 3, 28, 0x7b2cbf);
   r3d_glow(0);
   bx(118, 0, 16, 126, 3, 26, 0x222226);          /* mouse */
   bx(112, 0, 10, 140, 1, 32, 0x3c3c46);
   mug(14, 0, 20, 0xe9c46a);
   can(150, 0, 18, 0x06d6a0);
   can(160, 0, 26, 0xef476f);
   rubber_duck(150, 0, 50);
   bx(4, 0, 40, 28, 18, 62, 0xd9d9d9);            /* a NAS that blinks */
   r3d_glow(400);
   bx(8, 12, 39, 10, 14, 40, 0x38b000);
   r3d_glow(0);
   cyl(166, 0, 56, 5, 3, 20, 6, 0x333333);        /* a desk lamp */
   r3d_glow(300);
   ball(166, 22, 50, 6, 4, 6, 3, 0xfff1c0);
   r3d_glow(0);
   r3d_light(CM(166), CM(30), CM(40), M(2), 0xffe9c0, 260);
   cyl(-8, 0, 40, 6, 6, 2, 10, 0x333333);         /* headphones on their stand */
   bx(-9, 2, 39, -7, 30, 41, 0x333333);
   ball(-8, 30, 40, 9, 6, 3, 3, 0x1d1d1d);
   /* the gaming chair on its round mat */
   at(960, 0, 760, 2048 + 200);
   desk_chair();
   at(960, 0, 760, 0);
   r3d_occlude(0);
   cyl(0, 0, 0, 70, 70, 1, 24, 0x274c77);
   cyl(0, 1, 0, 55, 55, 1, 24, 0x6096ba);
   r3d_occlude(1);
   /* a ring light with its tripod */
   at(1110, 0, 830, 0);
   cyl(0, 0, 0, 2, 2, 150, 4, 0x222222);
   r3d_glow(330);
   for (i = 0; i < 12; i++)
   {
      int32_t a = i * 4096 / 12, x = (int32_t)((22LL * hd_cos(a)) >> 14), y = (int32_t)((22LL * hd_sin(a)) >> 14);
      bx(x - 3, 160 + y - 3, -2, x + 3, 160 + y + 3, 2, 0xfdfcf0);
   }
   r3d_glow(0);
   r3d_light(0, CM(160), 0, M(2), 0xf4f7ff, 200);
   /* a whiteboard full of arrows, a cork board, a terminal poster */
   at(709, 100, 600, 3072);
   bx(0, 0, 0, 160, 100, 3, 0xbdbdbd);
   pane(poster[7], 3, 3, -1, 157, 97, 0, 0xffffff);
   at(1000, 120, 891, 0);
   bx(0, 0, 0, 90, 60, 3, 0x5c4033);
   mat(TEX3D_CORK, 128);
   bxf(3, 3, -1, 87, 57, 0, 0xffffff, R3D_PANE);
   plain();
   for (i = 0; i < 6; i++)
      bx(10 + (i % 3) * 26, 10 + (i / 3) * 24, -2, 28 + (i % 3) * 26, 28 + (i / 3) * 24, -1, i & 1 ? 0xffe66d : 0xff8fab);
   at(1191, 130, 700, 1024);
   frame_pic(50, 70, poster[5]);
   /* a bookshelf of tech books, boxes from deliveries, plants */
   at(1156, 0, 640, 1024);
   bookshelf(100, 180, 5, 0x3d3d3d, 31);
   at(720, 0, 520, 0);
   cardboard_box(40, 30, 34);
   at(724, 30, 524, 200);
   cardboard_box(30, 20, 26);
   at(1150, 0, 860, 0);
   tall_plant();
   at(760, 0, 860, 0);
   plant_pot(16, 0x264653, 5);
}

static void bedroom(void)
{
   int32_t i;
   /* the bed against the west wall: a frame, the mattress, a knitted duvet, pillows */
   at(10, 0, 580, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 0, 0, 200, 30, 160, 0x9c6b43);
   bx(0, 30, 0, 8, 100, 160, 0x9c6b43);
   plain();
   bx(8, 30, 4, 196, 52, 156, 0xf4f1ea);
   mat(TEX3D_KNIT, 80);
   bx(70, 50, 2, 198, 60, 158, 0x8fb8a8);
   bx(196, 30, 2, 198, 52, 158, 0x8fb8a8);
   plain();
   mat(TEX3D_FABRIC, 128);
   bx(14, 52, 14, 54, 66, 74, 0xffffff);
   bx(14, 52, 86, 54, 66, 146, 0xffffff);
   bx(90, 60, 30, 140, 63, 100, 0xe9c46a);    /* a jumper left on the bed */
   plain();
   /* nightstands with lamps, a book and a phone charging */
   for (i = 0; i < 2; i++)
   {
      at(10, 0, i ? 745 : 530, 0);
      mat(TEX3D_WOOD, 100);
      bx(0, 0, 0, 45, 50, 40, 0xd4b48c);
      plain();
      cyl(20, 50, 20, 6, 6, 2, 8, 0x333333);
      cyl(20, 52, 20, 1, 1, 24, 4, 0x333333);
      r3d_glow(280);
      cyl(20, 70, 20, 12, 8, 14, 10, 0xf6e7c1);
      r3d_glow(0);
      r3d_light(CM(20), CM(76), CM(20), CM(220), 0xffd59a, 200);
   }
   at(30, 50, 535, 0);
   bx(0, 0, 0, 7, 1, 14, 0x111111);
   at(20, 50, 750, 0);
   bx(0, 0, 0, 16, 3, 22, 0x6d597a);
   /* the wardrobe on the north wall, a pile of clothes */
   at(250, 0, 830, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 0, 0, 180, 220, 60, 0xefe9df);
   plain();
   for (i = 0; i < 3; i++)
   {
      bx(i * 60 + 2, 4, -1, i * 60 + 58, 216, 0, 0xf6f1e8);
      bx(i * 60 + 52, 100, -3, i * 60 + 54, 130, -1, 0x999999);
   }
   at(330, 0, 560, 0);
   mat(TEX3D_FABRIC, 128);
   for (i = 0; i < 6; i++)
      bx(i * 5, i * 6, i * 3 + (i & 1) * 4, 50 - i * 3, i * 6 + 7, 40 - i * 2, i % 3 == 0 ? 0x3d5a80 : i % 3 == 1 ? 0x98c1d9 : 0xee6c4d);
   plain();
   /* a laundry basket, a mirror, a rug, plants, pictures, shoes */
   at(400, 0, 680, 0);
   cyl(0, 0, 0, 22, 25, 50, 12, 0xd6c3a1);
   mat(TEX3D_FABRIC, 128);
   ball(0, 50, 0, 18, 8, 18, 3, 0xffffff);
   plain();
   at(441, 0, 800, 1024);
   bx(0, 0, 0, 50, 170, 4, 0xc9a66b);
   r3d_glow(170);
   bxf(4, 4, -1, 46, 166, 0, 0xbcd4e6, R3D_PANE);
   r3d_glow(0);
   at(230, 0, 600, 0);
   mat(TEX3D_RUG2, 0);
   r3d_occlude(0);
   bxf(0, 0, 0, 140, 1, 200, 0xffffff, R3D_TOP_ONLY);
   r3d_occlude(1);
   plain();
   at(410, 0, 860, 0);
   plant_pot(20, 0xbc6c25, 9);
   at(60, 140, 891, 0);
   frame_pic(80, 56, poster[1]);
   at(9, 150, 790, 3072);
   frame_pic(100, 60, poster[3]);
   at(300, 0, 540, 900);
   bx(0, 0, 0, 12, 10, 30, 0x2b2d42);
   bx(16, 0, 4, 28, 10, 34, 0x2b2d42);
}

static void bathroom(void)
{
   int32_t i;
   /* the tub with a glass screen, the toilet, the sink with its mirror, towels, the washing machine */
   at(458, 0, 760, 0);
   bx(0, 0, 0, 170, 56, 130, 0xf8f9fa);
   bx(8, 30, 8, 162, 56, 122, 0xdfe7ec);
   r3d_occlude(0);
   r3d_glass(70);
   bxf(0, 56, 0, 4, 190, 70, 0xdcefff, R3D_SKIP_TOP | R3D_SKIP_BOTTOM | R3D_SKIP_Z);
   r3d_glass(0);
   r3d_occlude(1);
   cyl(150, 56, 120, 2, 2, 120, 4, 0xc0c0c0);
   cyl(150, 176, 112, 8, 8, 2, 10, 0xc0c0c0);
   bx(60, 56, 118, 80, 64, 124, 0xff8fab);       /* shampoo */
   bx(84, 56, 118, 96, 66, 124, 0x4cc9f0);
   rubber_duck(110, 50, 60);
   at(470, 0, 540, 0);
   bx(0, 0, 0, 40, 40, 50, 0xffffff);            /* the toilet */
   bx(2, 40, 0, 38, 44, 50, 0xf1f1f1);
   bx(0, 40, -16, 40, 80, 0, 0xffffff);
   cyl(52, 0, 30, 6, 6, 30, 8, 0xffffff);        /* a toilet brush */
   at(570, 0, 512, 0);
   bx(0, 0, 0, 80, 80, 45, 0x6c757d);            /* the sink's cupboard */
   bx(-4, 80, -4, 84, 86, 49, 0xf8f9fa);
   bx(16, 84, 10, 64, 86, 38, 0xdfe7ec);
   cyl(40, 86, 6, 2, 2, 18, 6, 0xc0c0c0);
   cyl(10, 86, 30, 3, 3, 12, 6, 0x90e0ef);       /* a toothbrush cup */
   at(570, 0, 506, 0);
   bx(0, 110, 0, 80, 190, 2, 0x9a9a9a);           /* the mirror */
   r3d_glow(180);
   bxf(3, 113, 2, 77, 187, 3, 0xd8e6ee, R3D_SKIP_BOTTOM | R3D_SKIP_TOP | R3D_SKIP_X);
   r3d_glow(0);
   at(690, 0, 660, 3072);
   bx(0, 0, 0, 60, 85, 60, 0xf8f9fa);             /* the washing machine */
   cyl(30, 40, -1, 18, 18, 2, 16, 0x9aa7b0);
   bx(6, 70, -1, 30, 80, 0, 0x3a3a3a);
   at(660, 0, 590, 0);
   cyl(0, 0, 0, 18, 22, 46, 10, 0x8d99ae);        /* the laundry basket */
   for (i = 0; i < 3; i++)
   {
      at(470 + i * 30, 120, 892, 2048);
      bx(0, -50, 0, 24, 0, 6, i == 1 ? 0xffb4a2 : 0x9bc1bc);
   }
}

static void hall(void)
{
   int32_t i;
   /* the front door at the east end, a coat rack, shoes, a console with keys, an umbrella stand, a mirror */
   at(1192, 0, 410, 3072);
   bx(0, 0, 0, 80, 210, 6, 0x6d4c41);
   bx(64, 100, -4, 70, 104, 0, 0xc0a060);
   at(1100, 0, 488, 0);
   bx(0, 150, 0, 70, 156, 6, 0x5d4037);
   for (i = 0; i < 4; i++)
   {
      bx(6 + i * 18, 140, -4, 9 + i * 18, 150, 0, 0x333333);
      if (i != 2)
      {
         mat(TEX3D_FABRIC, 96);
         bx(i * 18, 70, -16, 16 + i * 18, 145, -2, i == 0 ? 0x1d3557 : i == 1 ? 0xa8dadc : 0x7f5539);
         plain();
      }
   }
   at(1000, 0, 466, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 0, 0, 90, 12, 28, 0x8d6e63);
   bx(0, 30, 0, 90, 34, 28, 0x8d6e63);
   plain();
   for (i = 0; i < 4; i++)
      bx(6 + i * 21, 12, 4, 20 + i * 21, 20, 26, i & 1 ? 0x222222 : 0xe9ecef);
   at(100, 0, 468, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 0, 0, 100, 80, 26, 0x4e342e);
   plain();
   cyl(30, 80, 13, 10, 12, 4, 10, 0xd4a373);
   bx(26, 84, 10, 34, 86, 16, 0xffd166);           /* keys */
   at(80, 80, 483, 0);
   plant_pot(12, 0xffffff, 2);
   at(100, 120, 492, 0);
   bx(0, 0, 0, 100, 70, 3, 0xc9a66b);
   r3d_glow(170);
   bxf(4, 4, -1, 96, 66, 0, 0xbcd4e6, R3D_PANE);
   r3d_glow(0);
   at(1160, 0, 425, 0);
   cyl(0, 0, 0, 12, 12, 50, 10, 0x2a9d8f);
   cyl(-4, 30, -2, 2, 2, 50, 4, 0x222222);
   cyl(5, 30, 3, 2, 2, 46, 4, 0xe63946);
   /* a parcel nobody opened */
   at(1060, 0, 415, 300);
   cardboard_box(44, 28, 32);
   /* a runner rug along the hall */
   at(300, 0, 415, 0);
   mat(TEX3D_RUG2, 128);
   r3d_occlude(0);
   bxf(0, 0, 0, 500, 1, 70, 0xffffff, R3D_TOP_ONLY);
   r3d_occlude(1);
   plain();
}

static void balcony(void)
{
   int32_t i;
   at(0, 0, 0, 0);
   mat(TEX3D_TILES, 160);
   bxf(40, -10, -160, 620, 0, 0, 0xd8cfc4, R3D_SKIP_BOTTOM);
   plain();
   /* the railing */
   bx(40, 0, -162, 620, 4, -156, 0x2d2d2d);
   bx(40, 100, -162, 620, 104, -156, 0x2d2d2d);
   for (i = 0; i <= 58; i++)
      bx(40 + i * 10, 4, -161, 42 + i * 10, 100, -158, 0x2d2d2d);
   for (i = 0; i <= 15; i++)
   {
      bx(40, 4, -160 + i * 10, 42, 100, -158 + i * 10, 0x2d2d2d);
      bx(618, 4, -160 + i * 10, 620, 100, -158 + i * 10, 0x2d2d2d);
   }
   bx(40, 100, -160, 44, 104, 0, 0x2d2d2d);
   bx(616, 100, -160, 620, 104, 0, 0x2d2d2d);
   /* two folding chairs, a small table with a drink, plants in pots, a drying rack, the AC */
   at(120, 0, -110, 300);
   chair_wood(0x2a9d8f, 0x2a9d8f);
   at(230, 0, -120, 3800);
   chair_wood(0x2a9d8f, 0x2a9d8f);
   at(190, 0, -80, 0);
   cyl(0, 0, 0, 3, 3, 60, 6, 0x2d2d2d);
   cyl(0, 60, 0, 26, 26, 3, 16, 0x2d2d2d);
   cyl(-6, 63, 4, 4, 3, 12, 8, 0xffb703);
   for (i = 0; i < 5; i++)
   {
      at(60 + i * 22, 0, -140, 0);
      plant_pot(10 + (i % 3) * 3, i & 1 ? 0xbc6c25 : 0xe9edc9, (uint32_t)i);
   }
   at(380, 0, -130, 0);
   for (i = 0; i < 5; i++)
      bx(0, 80 + (i & 1) * 4, i * 18, 140, 82 + (i & 1) * 4, i * 18 + 2, 0xcccccc);
   bx(0, 0, 0, 3, 82, 3, 0xcccccc);
   bx(137, 0, 0, 140, 82, 3, 0xcccccc);
   bx(0, 0, 72, 3, 82, 75, 0xcccccc);
   bx(137, 0, 72, 140, 82, 75, 0xcccccc);
   mat(TEX3D_FABRIC, 128);
   bx(10, 40, 17, 50, 84, 19, 0xe63946);
   bx(60, 50, 35, 90, 84, 37, 0xffffff);
   bx(100, 30, 53, 130, 84, 55, 0x457b9d);
   plain();
   at(560, 0, -50, 0);
   bx(0, 0, 0, 50, 60, 40, 0xf1f1f1);
   cyl(25, 30, -1, 18, 18, 2, 16, 0x9a9a9a);
}

/* The building under the flat, the street and the city around it. */
static void city(void)
{
   int32_t i;
   /* the blocks around: lower than this one, so the view sees over their roofs */
   static const int32_t blocks[][5] = {
      { 1500, -800, 2600, 1200, 1100 }, { 1500, 1400, 2600, 2900, 1300 }, { -2400, -600, -500, 1600, 1000 },
      { -400, 1300, 1300, 2800, 1200 }, { -2600, -2600, -1600, -900, 900 }, { 1600, -2800, 3000, -1200, 1400 },
   };
   r3d_detail(0);
   at(0, 0, 0, 0);
   /* this building: five floors down to the street */
   mat(TEX3D_FACADE, 80);
   bxf(-40, -1500, -240, 1240, -10, 930, 0xffffff, R3D_SKIP_TOP | R3D_SKIP_BOTTOM);
   plain();
   mat(TEX3D_CONCRETE, 64);
   bxf(-40, -10, -240, 1240, 0, -160, 0xd9d4cc, R3D_SKIP_BOTTOM);
   bxf(-40, -10, -160, 40, 0, 0, 0xd9d4cc, R3D_SKIP_BOTTOM);
   bxf(620, -10, -160, 1240, 0, 0, 0xd9d4cc, R3D_SKIP_BOTTOM);
   bxf(-40, -10, 900, 1240, 0, 930, 0xd9d4cc, R3D_SKIP_BOTTOM);
   bxf(-40, -10, 0, 0, 0, 900, 0xd9d4cc, R3D_SKIP_BOTTOM);
   bxf(1200, -10, 0, 1240, 0, 900, 0xd9d4cc, R3D_SKIP_BOTTOM);
   plain();
   /* the street, the pavement and a park */
   mat(TEX3D_CONCRETE, 32);
   bxf(-3000, -1520, -3000, 4200, -1500, 4000, 0x6f747a, R3D_TOP_ONLY);
   bxf(-200, -1500, -700, 1400, -1488, -240, 0xc8c3ba, R3D_TOP_ONLY);
   plain();
   mat(TEX3D_GRASS, 48);
   bxf(-1400, -1500, -2200, -300, -1490, -800, 0xffffff, R3D_TOP_ONLY);
   plain();
   for (i = 0; i < 6; i++)
   {
      at(-1200 + (i % 3) * 330, -1490, -2000 + (i / 3) * 700, 0);
      cyl(0, 0, 0, 12, 10, 260, 6, 0x6b4f2a);
      mat(TEX3D_LEAVES, 32);
      ball(0, 320, 0, 160, 140, 160, 4, 0x4f9a4a);
      plain();
   }
   at(0, 0, 0, 0);
   for (i = 0; i < 8; i++)
      bxf(300 + i * 60, -1499, -1100, 330 + i * 60, -1498, -800, 0xf1f1f1, R3D_TOP_ONLY);
   /* other blocks around, of several heights */
   for (i = 0; i < 6; i++)
   {
      mat(TEX3D_FACADE, 80);
      bxf(blocks[i][0], -1500, blocks[i][1], blocks[i][2], -1500 + blocks[i][4], blocks[i][3], i & 1 ? 0xfff3e0 : 0xe3f2fd, R3D_SKIP_BOTTOM | R3D_SKIP_TOP);
      mat(TEX3D_CONCRETE, 32);
      bxf(blocks[i][0], -1500 + blocks[i][4], blocks[i][1], blocks[i][2], -1490 + blocks[i][4], blocks[i][3], 0xb0aaa0, R3D_SKIP_BOTTOM);
      plain();
   }
}

/* The two people who live here, with their soft shadows: one works at the desk, one walks around the living room. */
static void build_people(void)
{
   static const actor3d_look worker = { 0xe0ac69, 0x3d2817, 0xf4a261, 0xffffff, 0x2b2d42, 0x1d3557, 0x222222 };
   static const actor3d_look flatmate = { 0x8d5524, 0x111111, 0x2a9d8f, 0xe9c46a, 0x6c757d, 0xe63946, 0x8d5524 };
   r3d_mesh shadow;
   r3d_place(0, 0, 0, 0);
   r3d_detail(0);
   r3d_begin();
   r3d_decal(1);
   r3d_occlude(0);
   r3d_material(T[TEX3D_ROUND_SHADOW], 0);
   r3d_box(-CM(40), CM(1), -CM(40), CM(40), CM(1) + 1, CM(40), 0x3a3a50, R3D_TOP_ONLY);
   r3d_decal(0);
   r3d_occlude(1);
   plain();
   shadow = r3d_end();
   actor3d_build(&people[0], &worker, T[TEX3D_FABRIC]);
   actor3d_build(&people[1], &flatmate, T[TEX3D_FABRIC]);
   people[0].shadow = shadow;
   people[1].shadow = shadow;
}

void hd_r3d_build(void)
{
   static const opening liv_s[] = { { 60, 290, OPEN_BIG }, { 320, 610, OPEN_BIG } };
   static const opening kit_s[] = { { 900, 1080, OPEN_WINDOW } };
   static const opening liv_w[] = { { 140, 260, OPEN_WINDOW } };
   static const opening bed_w[] = { { 640, 820, OPEN_WINDOW } };
   static const opening north_bed[] = { { 60, 200, OPEN_WINDOW } };
   static const opening north_off[] = { { 760, 860, OPEN_WINDOW } };
   static const opening east_off[] = { { 620, 780, OPEN_WINDOW } };
   static const opening east_hall[] = { { 410, 490, OPEN_DOOR } };
   static const opening east_kit[] = { { 140, 330, OPEN_WINDOW } };
   static const opening hall_s[] = { { 620, 720, OPEN_DOOR }, { 900, 990, OPEN_DOOR } };
   static const opening hall_n[] = { { 340, 430, OPEN_DOOR }, { 560, 650, OPEN_DOOR }, { 760, 850, OPEN_DOOR } };
   if (ready)
      return;
   T = tex3d_build();
   poster[0] = tex3d_poster(0, 4);
   poster[1] = tex3d_poster(0, 1);
   poster[2] = tex3d_poster(1, 2);
   poster[3] = tex3d_poster(3, 3);
   poster[4] = tex3d_poster(1, 5);
   poster[5] = tex3d_poster(2, 5);
   poster[6] = tex3d_poster(2, 3);
   poster[7] = tex3d_poster(3, 0);
   r3d_clear();
   r3d_detail(CM(60));
   r3d_begin();
   /* floors */
   floor_of(0, 0, 750, 400, TEX3D_PARQUET, 160, 0xffffff);
   floor_of(750, 0, 1200, 400, TEX3D_PATTERN, 128, 0xffffff);
   floor_of(0, 400, 1200, 500, TEX3D_PLANKS, 96, 0xffffff);
   floor_of(0, 500, 450, 900, TEX3D_PLANKS, 96, 0xf0e6dc);
   floor_of(450, 500, 700, 900, TEX3D_TILES, 160, 0xffffff);
   floor_of(700, 500, 1200, 900, TEX3D_PARQUET, 160, 0xd8cbbd);
   /* outer walls (light outside, each room's color inside) */
   wall(0, 0, 0, 750, 0xf2efe9, 0xf3e3d3, liv_s, 2);
   wall(0, 0, 750, 1200, 0xf2efe9, 0xf6f3ea, kit_s, 1);
   wall(1, 0, 0, 400, 0xf2efe9, 0xc8553d, liv_w, 1);
   wall(1, 0, 400, 900, 0xf2efe9, 0xdfe7dc, bed_w, 1);
   wall(0, 900, 0, 450, 0xdfe7dc, 0xf2efe9, north_bed, 1);
   wall(0, 900, 450, 700, 0xe9f1f5, 0xf2efe9, NULL, 0);
   wall(0, 900, 700, 1200, 0x2f3e46, 0xf2efe9, north_off, 1);
   wall(1, 1200, 0, 400, 0xf6f3ea, 0xf2efe9, east_kit, 1);
   wall(1, 1200, 400, 500, 0xefe8de, 0xf2efe9, east_hall, 1);
   wall(1, 1200, 500, 900, 0x2f3e46, 0xf2efe9, east_off, 1);
   /* inner walls */
   wall(0, 400, 0, 1200, 0xf3e3d3, 0xefe8de, hall_s, 2);
   wall(0, 500, 0, 1200, 0xefe8de, 0xdfe7dc, hall_n, 3);
   wall(1, 750, 0, 400, 0xf3e3d3, 0xf6f3ea, NULL, 0);
   wall(1, 450, 500, 900, 0xdfe7dc, 0xe9f1f5, NULL, 0);
   wall(1, 700, 500, 900, 0xe9f1f5, 0x2f3e46, NULL, 0);
   /* tiles in the bathroom */
   at(0, 0, 0, 0);
   mat(TEX3D_SUBWAY, 128);
   bxf(456, 0, 506, 694, 140, 507, 0xffffff, R3D_PANE);
   bxf(456, 0, 893, 694, 140, 894, 0xffffff, R3D_PANE);
   plain();
   skirting(0, 400, 0, 750, -1);
   skirting(0, 500, 0, 1200, 1);
   skirting(1, 0, 0, 900, 1);
   living();
   kitchen();
   office();
   bedroom();
   bathroom();
   hall();
   balcony();
   city();
   scene = r3d_end();
   memset(&sunlight, 0, sizeof sunlight);
   /* shadows over the flat and its balcony; the city around is lit without them */
   sunlight.lo.x = BASE - CM(60), sunlight.lo.y = -CM(20), sunlight.lo.z = BASE - CM(260);
   sunlight.hi.x = BASE + CM(1260), sunlight.hi.y = CM(280), sunlight.hi.z = BASE + CM(950);
   sunlight.sun.x = -FX(3);
   sunlight.sun.y = FX(6);
   sunlight.sun.z = -FX(4);
   sunlight.sun_color = 0xfff1dc;
   sunlight.sun_strength = 340;
   sunlight.sun_soft = 200;
   sunlight.sky_color = 0xc8dcf0;
   sunlight.sky_strength = 170;
   sunlight.ground_color = 0xd8c8b0;
   sunlight.ground_strength = 80;
   sunlight.ao_reach = CM(70);
   r3d_bake(&sunlight);
   build_people();
   ready = 1;
}

void hd_r3d_start(hd_state *s)
{
   memset(&s->show, 0, sizeof s->show);
   s->show.on = 2;
   s->show.kx = BASE + CM(620);
   s->show.ky = BASE + CM(420);
   s->show.ka = 3600;
   s->show.kv = 640;
   s->show.rpos = 1500;
}

void hd_r3d_step(hd_state *s, const hd_input in[MAX_PLAYERS])
{
   hd_show *w = &s->show;
   uint32_t pad = in[0].buttons;
   int32_t c = hd_cos(w->ka), sn = hd_sin(w->ka), mx = 0, mz = 0;
   if ((pad & PAD_LEFT) || in[0].lx < -STICK_DEAD)
      w->ka = (w->ka - 12) & (ANGLE_FULL - 1);
   if ((pad & PAD_RIGHT) || in[0].lx > STICK_DEAD)
      w->ka = (w->ka + 12) & (ANGLE_FULL - 1);
   if ((pad & PAD_UP) || in[0].ly < -STICK_DEAD)
      w->kv = hd_clamp(w->kv + 6, 200, 1000);
   if ((pad & PAD_DOWN) || in[0].ly > STICK_DEAD)
      w->kv = hd_clamp(w->kv - 6, 200, 1000);
   if (pad & PAD_L)
      w->rpos = hd_clamp(w->rpos - 12, 300, 3000);
   if (pad & PAD_R)
      w->rpos = hd_clamp(w->rpos + 12, 300, 3000);
   /* walk the center on the floor, as the view sees it */
   if (pad & PAD_Y)
      mz += 1;
   if (pad & PAD_A)
      mz -= 1;
   if (pad & PAD_B)
      mx += 1;
   if (pad & PAD_X)
      mx -= 1;
   w->kx = hd_clamp(w->kx + (int32_t)(((int64_t)CM(4) * (mz * sn + mx * c)) >> 14), 0, FX(511));
   w->ky = hd_clamp(w->ky + (int32_t)(((int64_t)CM(4) * (mz * c - mx * sn)) >> 14), 0, FX(511));
   w->t++;
}

void hd_r3d_draw(const hd_state *s, void *screen)
{
   hd_surface *sf = (hd_surface *)screen;
   const hd_show *w = &s->show;
   r3d_view v;
   r3d_instance inst[ACT_INSTANCES * 2];
   actor3d_pose pose;
   int32_t n, leg;
   char line[96];
   int32_t scale = hd_res, dist = CM(w->rpos), sp = hd_sin(w->kv), cp = hd_cos(w->kv);
   hd_r3d_build();
   memset(&v, 0, sizeof v);
   /* the eye on a sphere around the center, looking at it */
   v.yaw = w->ka;
   v.pitch = w->kv;
   v.eye.x = w->kx - (int32_t)((((int64_t)dist * cp) >> 14) * hd_sin(w->ka) >> 14);
   v.eye.z = w->ky - (int32_t)((((int64_t)dist * cp) >> 14) * hd_cos(w->ka) >> 14);
   v.eye.y = CM(80) + (int32_t)(((int64_t)dist * sp) >> 14);
   v.fov = 520;
   v.near_z = CM(20);
   v.sky_top = 0x6f9fd0;
   v.sky_bottom = 0xd6e6f2;
   v.exposure = 256;
   v.smooth = 1;
   v.light = &sunlight;
   /* at the desk: sitting and typing, now and then a look at the other screen */
   memset(&pose, 0, sizeof pose);
   pose.at.x = BASE + CM(960);
   pose.at.z = BASE + CM(770);
   pose.yaw = 2048 + 200;
   pose.sit = 1;
   pose.type = 1;
   pose.phase = (w->t * 40) & 4095;
   pose.look = ((w->t / 240) & 1) ? 180 : 0;
   pose.nod = -80;
   n = actor3d_place(&people[0], &pose, inst);
   /* in the living room: walking up and down between the TV and the sofa, turning at each end */
   memset(&pose, 0, sizeof pose);
   leg = w->t % 600; /* 300 frames each way */
   pose.at.x = BASE + CM(200);
   pose.at.z = BASE + CM(70) + (int32_t)((int64_t)CM(250) * (leg < 300 ? leg : 600 - leg) / 300);
   pose.yaw = leg < 300 ? 2048 : 0;
   pose.walk = 16384;
   pose.phase = (w->t * 64) & 4095;
   pose.wave = ((w->t / 600) % 3) == 1 && leg < 60;
   n += actor3d_place(&people[1], &pose, inst + n);
   r3d_draw(sf, &v, &scene, inst, n);
   snprintf(line, sizeof line, hd_lang == 1 ? "3D  %d TRI\xc3\x81NGULOS, %d EN PANTALLA" : hd_lang == 2 ? "3D  %d TRI\xc3\x82NGULOS, %d NA TELA" : "3D  %d TRIANGLES, %d ON SCREEN",
            (int)r3d_triangles(), (int)r3d_drawn());
   text_draw(sf, line, 8 * scale, 8 * scale, scale, 0xffffff, 1, -1);
}

void hd_r3d_sanitize(hd_show *w)
{
   w->kx = hd_clamp(w->kx, 0, FX(511));
   w->ky = hd_clamp(w->ky, 0, FX(511));
   w->ka &= ANGLE_FULL - 1;
   w->kv = hd_clamp(w->kv, 200, 1000);
   w->rpos = hd_clamp(w->rpos, 300, 3000);
   w->t = hd_clamp(w->t, 0, 1 << 30);
}
