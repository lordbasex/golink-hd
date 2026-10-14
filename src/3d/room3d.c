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
 * The plan, x to the east and z to the north, in centimeters (the house is
 * 1600 x 1250, 200 square meters):
 *   north:  bedroom 0..500 | bathroom 500..750 | office 750..1150 | game room 1150..1600   (z 800..1250)
 *   middle: laundry 0..250 | hall 250..1600, the front door at its east end                (z 650..800)
 *   south:  living 0..900 (dining by the west window, the lounge facing the TV on the east wall) | kitchen 900..1600 (z 0..650)
 *   outside the living room, a terrace (x 40..880, z -200..0)
 */
#include <stdio.h>
#include <string.h>
#include "hd.h"
#include "gfx.h"
#include "text.h"
#include "r3d.h"
#include "actor3d.h"
#include "room3d.h"

#define CM(n) FX_FRAC(n, 100)
#define M(n) FX(n)
/* the flat sits away from 0 so the view's center stays positive (hd_show clamps it to 0..511) */
#define BASE ROOM3D_BASE

static r3d_mesh scene;
static actor3d_kit people[ROOM3D_PEOPLE];
static pet3d_kit cat;
static r3d_mesh props[ROOM3D_PROPS];
static model3d_body bodies[ROOM3D_PEOPLE];
static int32_t built_for = -1, builds;
static int32_t bodies_gen = -1, bodies_first = -1;
static r3d_mesh person_shadow;
static const int32_t *T;
static int32_t ready, poster[10];
static r3d_light_cfg sunlight;

/* ---- small helpers ---- */

/* Pieces at (x, y, z) centimeters of the flat, turned yaw. */
static int32_t shift_x, shift_z; /* a room built where it was planned, moved whole to its place in the house */

static void at(int32_t x, int32_t y, int32_t z, int32_t yaw)
{
   r3d_place(BASE + CM(x + shift_x), CM(y), BASE + CM(z + shift_z), yaw & (ANGLE_FULL - 1));
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

/*
 * A piece of furniture made by an artist in place of the built one: the
 * package's model `name` (its front toward +z) fit into the built one's box
 * (centimeters, the place's axes), its front turned toward `face` (FACE_*:
 * the side of the box it looks out of), standing on the box's floor. Under
 * it a low solid a little inside the box: walking goes round it and it
 * darkens the floor under it, while the model's own faces (its seat, its
 * cushions) stay outside every solid, lit (a solid of the whole box made
 * them black). Returns 0 when the package has no such model: then the
 * built one is built.
 */
enum { FACE_MZ = 2048, FACE_PX = 1024, FACE_MX = 3072, FACE_PZ = 0, MIRROR = 1 /* added to a FACE_*: left and right swapped (an L sofa's other hand) */,
       NO_SOLID = 2 /* added: the caller gives the solids (an L does not fill its box) */ };
static int piece(const char *name, int32_t face, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1)
{
   int32_t solid = !(face & NO_SOLID), mirror = face & MIRROR;
   const model3d *m = model3d_find(name);
   r3d_vec lo, hi, scale, center;
   int32_t mw, md, mh, across, along;
   face &= ~(MIRROR | NO_SOLID);
   if (!m || model3d_bones(m))
      return 0;
   model3d_bounds(m, &lo, &hi);
   mw = hd_max(1, hi.x - lo.x), md = hd_max(1, hi.z - lo.z), mh = hd_max(1, hi.y - lo.y);
   /* a quarter turn swaps the model's width and depth in the box */
   across = (face == FACE_PX || face == FACE_MX) ? CM(z1 - z0) : CM(x1 - x0);
   along = (face == FACE_PX || face == FACE_MX) ? CM(x1 - x0) : CM(z1 - z0);
   scale.x = (int32_t)((int64_t)across * 65536 / mw);
   scale.y = (int32_t)((int64_t)CM(y1 - y0) * 65536 / mh);
   scale.z = (int32_t)((int64_t)along * 65536 / md);
   if (mirror)
      scale.x = -scale.x;
   center.x = CM(x0 + x1) / 2, center.y = CM(y0), center.z = CM(z0 + z1) / 2;
   plain();
   model3d_add(m, scale, face, center);
   if (solid)
      r3d_solid_box(CM(x0 + 4), CM(y0), CM(z0 + 4), CM(x1 - 4), CM(y0 + hd_min(25, y1 - y0)), CM(z1 - 4));
   return 1;
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
/*
 * `side` says which side of the wall dresses the opening: a door's open
 * leaf lies against the wall on the upper (z or x above the wall) or lower
 * side, after the opening (DOOR_UP_AFTER...) or before it; a window's
 * curtains hang on the upper or lower side (CURTAIN_UP, CURTAIN_DOWN).
 * 0: nothing.
 */
enum { DOOR_UP_AFTER = 1, DOOR_UP_BEFORE, DOOR_DOWN_AFTER, DOOR_DOWN_BEFORE };
enum { CURTAIN_UP = 1, CURTAIN_DOWN };
typedef struct
{
   int32_t from, to, kind, side;
} opening;

/* A box along the wall: s0..s1 along it, y0..y1 up, d0..d1 across it from the wall's middle (dir 0: z, dir 1: x). */
static void wall_box(int32_t dir, int32_t pos, int32_t s0, int32_t s1, int32_t y0, int32_t y1, int32_t d0, int32_t d1, uint32_t c)
{
   if (dir == 0)
      bx(s0, y0, pos + hd_min(d0, d1), s1, y1, pos + hd_max(d0, d1), c);
   else
      bx(pos + hd_min(d0, d1), y0, s0, pos + hd_max(d0, d1), y1, s1, c);
}

/* Skirting boards on both faces of a stretch of wall. */
static void skirt(int32_t dir, int32_t pos, int32_t s0, int32_t s1)
{
   if (s1 <= s0)
      return;
   r3d_occlude(0);
   wall_box(dir, pos, s0, s1, 0, 9, 6, 7, 0xfaf8f4);
   wall_box(dir, pos, s0, s1, 0, 9, -7, -6, 0xfaf8f4);
   r3d_occlude(1);
}

/* A door's dressing: the frame on both faces, the open leaf against the wall with its handle, a light switch. */
static void door_dress(int32_t dir, int32_t pos, int32_t from, int32_t to, int32_t top, int32_t side)
{
   int32_t sg, w = to - from - 6;
   r3d_occlude(0);
   for (sg = -1; sg <= 1; sg += 2)
   {
      wall_box(dir, pos, from - 7, from, 0, top + 7, sg * 6, sg * 8, 0xf7f5f0);
      wall_box(dir, pos, to, to + 7, 0, top + 7, sg * 6, sg * 8, 0xf7f5f0);
      wall_box(dir, pos, from - 7, to + 7, top, top + 7, sg * 6, sg * 8, 0xf7f5f0);
      wall_box(dir, pos, to + 12, to + 20, 104, 116, sg * 6, sg * 7, 0xf2f2f2); /* the switch */
      wall_box(dir, pos, to + 14, to + 18, 108, 112, sg * 7, sg * 8, 0xdddddd);
   }
   if (side)
   {
      int32_t up = side == DOOR_UP_AFTER || side == DOOR_UP_BEFORE ? 1 : -1;
      int32_t a = side == DOOR_UP_AFTER || side == DOOR_DOWN_AFTER ? to + 9 : from - 9 - w, b = a + w;
      int32_t hand = side == DOOR_UP_AFTER || side == DOOR_DOWN_AFTER ? b - 8 : a + 4;
      mat(TEX3D_WOOD, 100);
      wall_box(dir, pos, a, b, 1, 205, up * 9, up * 13, 0xf1ede6);
      plain();
      wall_box(dir, pos, a + 8, b - 8, 20, 90, up * 13, up * 14, 0xe9e4da);  /* two panels */
      wall_box(dir, pos, a + 8, b - 8, 110, 190, up * 13, up * 14, 0xe9e4da);
      wall_box(dir, pos, hand, hand + 4, 98, 102, up * 13, up * 19, 0xb0b0b0); /* the handle */
   }
   r3d_occlude(1);
}

/* Curtains on a window's inner side: a rod, two panels in soft folds down to the floor. */
static void curtains(int32_t dir, int32_t pos, int32_t s0, int32_t s1, int32_t top, int32_t up)
{
   int32_t p, i;
   wall_box(dir, pos, s0 - 26, s1 + 26, top + 14, top + 16, up * 12, up * 14, 0x3a3a3a);
   mat(TEX3D_FABRIC, 128);
   for (p = 0; p < 2; p++)
   {
      int32_t a = p ? s1 - 14 : s0 - 24;
      for (i = 0; i < 5; i++)
      {
         int32_t d = (i & 1) ? 4 : 0;
         wall_box(dir, pos, a + i * 8, a + i * 8 + 8, 3, top + 14, up * (10 + d), up * (13 + d), (i & 1) ? 0xe8dfcf : 0xf1eadc);
      }
   }
   plain();
}

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
      skirt(dir, pos, cur, o[i].from);
      if (o[i].kind != OPEN_DOOR)
         window_in(dir, pos, o[i].from, o[i].to, bot, top);
      if (o[i].kind == OPEN_WINDOW)
         skirt(dir, pos, o[i].from, o[i].to);
      if (o[i].kind == OPEN_DOOR)
         door_dress(dir, pos, o[i].from, o[i].to, top, o[i].side);
      if (o[i].kind == OPEN_WINDOW && o[i].side)
         curtains(dir, pos, o[i].from, o[i].to, top, o[i].side == CURTAIN_UP ? 1 : -1);
      cur = o[i].to;
   }
   wall_piece(dir, pos, cur, b, 0, 265, c1, c2);
   skirt(dir, pos, cur, b);
}

/* A floor of the flat, from (x0, z0) to (x1, z1), its texture at `tpm` texels a meter. */
static void floor_of(int32_t x0, int32_t z0, int32_t x1, int32_t z1, int32_t tex, int32_t tpm, uint32_t c)
{
   at(0, 0, 0, 0);
   mat(tex, tpm);
   bxf(x0, -4, z0, x1, 0, z1, c, R3D_SKIP_BOTTOM | R3D_SKIP_SIDES);
   plain();
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

/*
 * One leaf, both of its sides: a diamond from its base at (x, y, z)
 * centimeters, turned yaw, rising at `tilt` (of 4096 a turn), `len` long
 * and `wide` across, its tip drooping `droop`. Lit by its own slant, so
 * a plant made of them shows light and shade leaf by leaf.
 */
static void leaf(int32_t x, int32_t y, int32_t z, int32_t yaw, int32_t tilt, int32_t len, int32_t wide, int32_t droop, uint32_t c)
{
   int32_t ct = hd_cos(tilt), st = hd_sin(tilt), cy = hd_cos(yaw), sy = hd_sin(yaw);
   int32_t reach = (int32_t)(((int64_t)len * ct) >> 14), up = (int32_t)(((int64_t)len * st) >> 14);
   int32_t px = (int32_t)(((int64_t)wide / 2 * cy) >> 14), pz = (int32_t)(((int64_t)-wide / 2 * sy) >> 14);
   int32_t dx = (int32_t)(((int64_t)reach * sy) >> 14), dz = (int32_t)(((int64_t)reach * cy) >> 14);
   r3d_vec b = { CM(x), CM(y), CM(z) };
   r3d_vec t = { CM(x + dx), CM(y + up - droop), CM(z + dz) };
   r3d_vec m1 = { CM(x + dx * 45 / 100 + px), CM(y + up * 55 / 100), CM(z + dz * 45 / 100 + pz) };
   r3d_vec m2 = { CM(x + dx * 45 / 100 - px), CM(y + up * 55 / 100), CM(z + dz * 45 / 100 - pz) };
   r3d_quad(b, m1, t, m2, c);
   r3d_quad(b, m2, t, m1, gfx_mix(c, 0x1b3a1b, 60)); /* the underside, darker */
}

/* Greens for leaves, picked by a seed. */
static uint32_t leaf_green(uint32_t k)
{
   static const uint32_t g[6] = { 0x2e7d32, 0x43a047, 0x388e3c, 0x66bb6a, 0x558b2f, 0x7cb342 };
   return g[k % 6];
}

/* A pot with a leafy plant: leaves arching out of the soil in a spiral. */
static void plant_pot(int32_t size, uint32_t pot, uint32_t seed)
{
   int32_t i, n = 14 + (int32_t)(seed % 6);
   cyl(0, 0, 0, size * 4 / 10, size / 2, size * 7 / 10, 12, pot);
   cyl(0, size * 7 / 10, 0, size / 2 - 2, size / 2 - 2, 2, 12, 0x3b2a1a);
   for (i = 0; i < n; i++)
   {
      int32_t yaw = (int32_t)((i * 1583 + seed * 911) & 4095), tilt = 560 + (i * 197 + (int32_t)seed * 61) % 420;
      int32_t len = size * (110 + (i * 37) % 60) / 100;
      leaf(0, size * 7 / 10 + 1, 0, yaw, tilt, len, size * 6 / 10, size * (10 + i % 4 * 6) / 100, leaf_green(seed + (uint32_t)i));
   }
}

/* A tall plant in a big pot: three stems with broad leaves all the way up. */
static void tall_plant(void)
{
   int32_t i, st;
   cyl(0, 0, 0, 20, 24, 44, 14, 0xd8d2c4);
   cyl(0, 44, 0, 22, 22, 2, 14, 0x3b2a1a);
   for (st = 0; st < 3; st++)
   {
      int32_t sx = st == 0 ? 0 : st == 1 ? 6 : -5, sz = st == 0 ? 0 : st == 1 ? -4 : 5, top = 120 - st * 22;
      cyl(sx, 44, sz, 2, 1, top, 6, 0x6b4f2a);
      for (i = 0; i < 16; i++)
      {
         int32_t h = 64 + i * (top - 24) / 16, yaw = (i * 1583 + st * 1300) & 4095, tilt = 120 + (i * 131 + st * 70) % 460;
         leaf(sx, h, sz, yaw, tilt, 28 + (i % 4) * 5, 22 + (i % 3) * 4, 6 + (i % 3) * 4, leaf_green((uint32_t)(i + st * 5)));
      }
      for (i = 0; i < 4; i++)
         leaf(sx, 44 + top - 4, sz, i * 1024 + st * 300, 860, 20, 12, 2, leaf_green((uint32_t)(i + 3))); /* the new leaves on top */
   }
}

static void mug(int32_t x, int32_t y, int32_t z, uint32_t c)
{
   cyl(x, y, z, 4, 4, 9, 8, c);
   cyl(x, y + 8, z, 3, 3, 1, 8, 0x4a2c1a);
   bx(x + 4, y + 2, z - 1, x + 6, y + 7, z + 1, c);
}

static void chair_wood(uint32_t wood, uint32_t seat)
{
   if (piece("chair", FACE_MZ, 0, 0, 0, 42, 92, 42))
      return;
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
   if (piece("desk_chair", FACE_MZ, -32, 0, -30, 32, 120, 30))
      return;
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
static void monitor(int32_t x, int32_t z, int32_t w, int32_t h, int32_t tex)
{
   int32_t front = 9;
   if (piece("monitor", FACE_MZ | NO_SOLID, x, 0, z + 8, x + w, 12 + h, z + 20))
      front = z + 12; /* the package's monitor: its screen's face is a little in front of its box's middle */
   else
   {
      bx(x + w / 2 - 10, 0, 6, x + w / 2 + 10, 2, 22, 0x2a2a2e);
      bx(x + w / 2 - 2, 2, 16, x + w / 2 + 2, 16, 20, 0x2a2a2e);
      bx(x, 12, 10, x + w, 12 + h, 14, 0x18181b);
   }
   r3d_glow(300);
   pane(tex, x + 1, 13, front, x + w - 1, 11 + h, front + 1, 0xffffff);
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
   r3d_glow(320);
   if (piece("floor_lamp", FACE_MZ, -21, 0, -21, 21, 166, 21))
      cyl(0, 124, 0, 21, 15, 42, 12, shade_c); /* the package's lamp, its shade lit */
   else
   {
      r3d_glow(0);
      cyl(0, 0, 0, 14, 14, 3, 12, 0x2c2c2c);
      cyl(0, 3, 0, 1, 1, 150, 5, 0x2c2c2c);
      r3d_glow(320);
      cyl(0, 140, 0, 20, 12, 26, 12, shade_c);
   }
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

/* A low TV unit 220 wide with the TV, its speakers and a game console, its front toward -z. */
static void tv_unit(int32_t screen)
{
   if (!piece("tv_unit", FACE_MZ, 0, 0, 0, 220, 50, 42))
   {
      mat(TEX3D_WOOD, 128);
      bx(0, 0, 0, 220, 50, 42, 0xc8a27a);
      plain();
      bx(4, 6, -1, 70, 44, 0, 0xb08e6c);
      bx(76, 6, -1, 144, 44, 0, 0xb08e6c);
      bx(150, 6, -1, 216, 44, 0, 0xb08e6c);
   }
   bx(60, 50, 8, 160, 54, 30, 0x222226);            /* TV stand */
   bx(103, 54, 18, 117, 70, 24, 0x222226);
   bx(40, 64, 16, 180, 146, 20, 0x111114);          /* the TV */
   r3d_glow(260);
   pane(screen, 42, 66, 15, 178, 144, 16, 0xffffff);
   r3d_glow(0);
   bx(8, 50, 10, 30, 82, 32, 0x2d2d33);             /* speakers */
   bx(190, 50, 10, 212, 82, 32, 0x2d2d33);
   cyl(19, 72, 9, 6, 6, 1, 10, 0x555560);
   cyl(201, 72, 9, 6, 6, 1, 10, 0x555560);
   bx(70, 50, 4, 100, 55, 34, 0x1b1b1f);            /* a game console */
   bx(120, 50, 10, 150, 53, 30, 0xf1f1f1);
}

static void living(void)
{
   int32_t i;
   /*
    * Two areas: to the east the lounge (the TV against the kitchen's wall,
    * the L sofa facing it, the coffee table and the rug between them, the
    * armchair to the north), to the west the dining table by the window.
    * The strip along the balcony's doors and the way in from the hall's
    * door (the north-east corner) stay clear.
    *
    * The sofa, an L facing the TV (east), with cushions and a blanket.
    */
   at(560, 0, 60, 3072);
   if (piece("sofa", FACE_MZ + MIRROR + NO_SOLID, 0, 0, -90, 220, 92, 95))
   {
      /* its two arms' solids, the corner in front of the seat left free */
      r3d_solid_box(CM(4), 0, CM(4), CM(216), CM(25), CM(91));
      r3d_solid_box(CM(144), 0, -CM(86), CM(216), CM(25), CM(4));
   }
   else
   {
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
   }
   /* the coffee table and what is on it */
   at(710, 0, 90, 3072);
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
   /* the TV unit against the east wall (the kitchen's), the doorway to the kitchen north of it */
   at(852, 0, 280, 1024);
   tv_unit(poster[0]);
   at(874, 50, 266, 0);
   plant_pot(18, 0xe9e2d0, 3);
   /* shelves on the north wall of the living room, records and plants */
   at(250, 0, 610, 0);
   bookshelf(120, 190, 5, 0xe6dccb, 11);
   at(380, 0, 610, 0);
   bookshelf(80, 110, 3, 0xe6dccb, 12);
   at(396, 110, 614, 0);
   bx(0, 0, 0, 30, 10, 26, 0x2b2b2b);              /* a turntable */
   cyl(15, 10, 13, 11, 11, 1, 16, 0x111111);
   at(442, 110, 626, 0);
   plant_pot(14, 0x8d99ae, 7);
   /* the armchair, north of the coffee table, and a floor lamp behind the sofa */
   at(700, 0, 380, 0);
   if (!piece("armchair", FACE_MZ, 0, 0, 0, 80, 90, 80))
   {
      mat(TEX3D_FABRIC, 96);
      bx(0, 10, 0, 80, 44, 80, 0xb5654a);
      bx(0, 44, 64, 80, 90, 80, 0xa65a40);
      bx(0, 44, 0, 14, 66, 64, 0xa65a40);
      bx(66, 44, 0, 80, 66, 64, 0xa65a40);
      plain();
   }
   at(440, 0, 300, 0);
   floor_lamp(0xf6e7c1);
   r3d_light(0, CM(150), 0, M(4), 0xffd59a, 300);
   at(30, 0, 610, 0);
   tall_plant();
   /* the rug */
   at(800, 0, 40, 3072);
   mat(TEX3D_RUG, 0);
   r3d_occlude(0);
   bxf(0, 0, 0, 240, 1, 170, 0xffffff, R3D_TOP_ONLY);
   r3d_occlude(1);
   plain();
   /* the dining table and its four chairs, a bowl of fruit, a pendant lamp */
   at(130, 0, 210, 0);
   if (!piece("dining_table", FACE_MZ, 0, 0, 0, 150, 76, 85))
   {
      mat(TEX3D_WOOD, 100);
      bx(0, 72, 0, 150, 76, 85, 0xb07a48);
      plain();
      bx(6, 0, 6, 12, 72, 12, 0x5b3d22);
      bx(138, 0, 6, 144, 72, 12, 0x5b3d22);
      bx(6, 0, 73, 12, 72, 79, 0x5b3d22);
      bx(138, 0, 73, 144, 72, 79, 0x5b3d22);
   }
   cyl(75, 76, 42, 16, 20, 7, 12, 0xe9edc9);
   ball(70, 85, 40, 5, 5, 5, 3, 0xe63946);
   ball(80, 85, 45, 5, 5, 5, 3, 0xf4a261);
   ball(75, 86, 36, 4, 4, 4, 3, 0x90be6d);
   bx(20, 76, 20, 50, 77, 60, 0xf8f9fa);       /* a placemat with papers */
   mug(110, 76, 60, 0x264653);
   /* by the west window: two chairs on each side */
   at(182, 0, 210, 2048);
   chair_wood(0x6b4423, 0xd9c7a7);
   at(272, 0, 210, 2048);
   chair_wood(0x6b4423, 0xd9c7a7);
   at(140, 0, 298, 0);
   chair_wood(0x6b4423, 0xd9c7a7);
   at(230, 0, 298, 0);
   chair_wood(0x6b4423, 0xd9c7a7);
   at(205, 0, 252, 0);
   pendant(80);
   r3d_light(0, CM(160), 0, M(3), 0xffe2b0, 260);
   /* pictures and a clock */
   at(386, 140, 641, 0);
   frame_pic(70, 50, poster[1]);
   at(472, 150, 641, 0);
   frame_pic(40, 56, poster[2]);
   at(9, 160, 420, 3072);
   frame_pic(60, 44, poster[3]);
   at(160, 200, 642, 2048);
   cyl(0, 0, 0, 14, 14, 3, 16, 0xf8f8f8);
   /* a pair of trainers, a radiator between the windows */
   at(620, 0, 600, 300);
   bx(0, 0, 0, 12, 8, 28, 0xf1f1f1);
   bx(14, 0, 4, 26, 8, 32, 0xf1f1f1);
   bx(0, 0, 18, 12, 6, 28, 0xe63946);
   at(410, 10, 8, 0);
   for (i = 0; i < 7; i++)
      bx(i * 9, 0, 0, i * 9 + 6, 60, 8, 0xf2f2f2);
   /* a sideboard by the dining table, a side table and a pouf by the sofa, a big plant by the TV */
   at(50, 0, 430, 3072);
   if (!piece("sideboard", FACE_MZ, 0, 0, 0, 170, 80, 44))
   {
      mat(TEX3D_WOOD, 100);
      bx(0, 0, 0, 170, 80, 44, 0x6b4423);
      plain();
      for (i = 0; i < 3; i++)
         bx(4 + i * 55, 6, -1, 56 + i * 55, 76, 0, 0x7a5030);
   }
   at(28, 80, 550, 0);
   plant_pot(14, 0xe9edc9, 4);
   at(22, 80, 470, 0);
   bx(0, 0, 0, 14, 30, 10, 0xf4a261);           /* a vase */
   at(480, 0, 300, 0);
   cyl(0, 0, 0, 22, 22, 50, 12, 0xd4b48c);
   cyl(-4, 50, 6, 6, 5, 8, 8, 0x264653);
   at(470, 0, 400, 0); /* the pouf out of the way between the sofa and the armchair */
   mat(TEX3D_KNIT, 96);
   cyl(0, 0, 0, 28, 28, 40, 14, 0xe9c46a);
   plain();
   at(870, 0, 30, 0);
   tall_plant();
}

/* A run of base cabinets along x (its front toward +z at z1), its doors and handles, and the stone top. */
static void base_run_x(int32_t x0, int32_t x1, int32_t z0, int32_t z1, uint32_t c, uint32_t door)
{
   int32_t x;
   mat(TEX3D_WOOD, 100);
   bx(x0, 0, z0, x1, 86, z1, c);
   plain();
   for (x = x0; x + 58 <= x1; x += 60)
   {
      bx(x + 2, 8, z1, x + 58, 82, z1 + 1, door);
      bx(x + 24, 72, z1 + 1, x + 36, 74, z1 + 4, 0xc0c0c0);
   }
   mat(TEX3D_MARBLE, 100);
   bx(x0 - 2, 86, z0 - 2, x1 + 2, 90, z1 + 2, 0xffffff);
   plain();
}

/* The same along z, its front toward -x at x0. */
static void base_run_z(int32_t x0, int32_t x1, int32_t z0, int32_t z1, uint32_t c, uint32_t door)
{
   int32_t z;
   mat(TEX3D_WOOD, 100);
   bx(x0, 0, z0, x1, 86, z1, c);
   plain();
   for (z = z0; z + 58 <= z1; z += 60)
   {
      bx(x0 - 1, 8, z + 2, x0, 82, z + 58, door);
      bx(x0 - 4, 72, z + 24, x0 - 1, 74, z + 36, 0xc0c0c0);
   }
   mat(TEX3D_MARBLE, 100);
   bx(x0 - 2, 86, z0 - 2, x1 + 2, 90, z1 + 2, 0xffffff);
   plain();
}

static void stool(int32_t x, int32_t z, int32_t h, uint32_t seat)
{
   at(x, 0, z, 0);
   if (piece("stool", FACE_MZ, -17, 0, -17, 17, h, 17))
      return;
   cyl(0, 0, 0, 14, 14, 3, 10, 0x333333);
   cyl(0, 3, 0, 2, 2, h - 6, 6, 0x333333);
   cyl(0, h - 30, 0, 12, 12, 2, 10, 0x333333); /* the foot ring */
   cyl(0, h - 6, 0, 17, 17, 6, 12, seat);
}

/*
 * The kitchen (900..1600 x 0..650), a family's: an L of counters (the
 * south wall: the washing machine under the counter, the dishwasher, the
 * sink under the window, the hob with the oven and the hood; the east wall:
 * the coffee corner, the toaster, the kettle, the microwave), wall
 * cupboards, the two-door fridge and the pantry, an island with three
 * stools, a table for four, and the bar to the living room in the opening
 * of the west wall. Everything a kitchen in use has on it.
 */
static void kitchen(void)
{
   int32_t i;
   const uint32_t cab = 0x7c9a92, door = 0x86a59c;
   at(0, 0, 0, 0);
   /* the south run: 1040..1530 */
   base_run_x(1040, 1530, 6, 66, cab, door);
   /* the washing machine under the counter (1040..1100) */
   bx(1042, 0, 66, 1098, 84, 67, 0xf8f9fa);
   bx(1050, 24, 67, 1090, 64, 69, 0x9aa7b0);
   bx(1056, 30, 69, 1084, 58, 70, 0x3b4a57);
   r3d_glow(300);
   bx(1080, 74, 67, 1090, 78, 68, 0x38b000);
   r3d_glow(0);
   /* the dishwasher (1160..1220) */
   bx(1162, 4, 66, 1218, 84, 67, 0xd0d4d9);
   bx(1166, 76, 67, 1214, 79, 69, 0x9aa0a6);
   /* the sink under the window, its tap, a dish rack, the washing-up liquid */
   bx(1225, 82, 12, 1295, 90, 56, 0xc9ccd1);
   bx(1229, 84, 16, 1291, 90, 52, 0x9fa4ab);
   cyl(1260, 90, 10, 2, 2, 30, 6, 0xd0d0d0);
   bx(1258, 118, 10, 1262, 122, 30, 0xd0d0d0);
   bx(1300, 90, 14, 1350, 92, 54, 0xb0b5bb);
   for (i = 0; i < 5; i++)
      bx(1306 + i * 9, 92, 20, 1308 + i * 9, 116, 48, i & 1 ? 0xf1faee : 0xa8dadc);
   cyl(1214, 90, 20, 4, 4, 16, 8, 0x2ec4b6);
   /* herbs on the window sill */
   for (i = 0; i < 3; i++)
   {
      at(1140 + i * 110, 90, 10, 0);
      plant_pot(10, i == 1 ? 0xbc6c25 : 0xffffff, (uint32_t)(20 + i));
   }
   at(0, 0, 0, 0);
   /* the hob, the oven under it, the hood, a pot and a pan */
   bx(1410, 90, 10, 1490, 91, 58, 0x1c1c1c);
   for (i = 0; i < 4; i++)
      cyl(1428 + (i & 1) * 44, 91, 22 + (i >> 1) * 24, 9, 9, 1, 12, 0x3a3a3a);
   cyl(1428, 91, 22, 12, 12, 16, 12, 0xb0b5bb);
   cyl(1472, 91, 46, 13, 13, 4, 12, 0x2e2e2e);
   bx(1484, 93, 44, 1508, 95, 48, 0x2e2e2e);
   bx(1412, 10, 66, 1488, 70, 67, 0x1c1c1c);
   r3d_glow(160);
   bx(1420, 20, 67, 1480, 52, 68, 0xff9f1c);
   r3d_glow(0);
   bx(1415, 60, 67, 1485, 62, 70, 0xc0c0c0);
   bx(1405, 175, 6, 1495, 185, 56, 0xc0c4c8);
   bx(1430, 185, 6, 1470, 265, 30, 0xc0c4c8);
   r3d_light(CM(1450), CM(170), CM(30), M(2), 0xfff0d0, 140);
   /* a rail of pans and ladles on the wall between the window and the hood */
   bx(1300, 150, 6, 1400, 152, 9, 0x9a9aa0);
   for (i = 0; i < 4; i++)
      bx(1308 + i * 24, 112, 9, 1316 + i * 24, 150, 11, i & 1 ? 0x2e2e2e : 0xb0b5bb);
   /* a knife block, a jar of spoons, a cutting board with bread, a roll of paper, a fruit bowl */
   bx(1110, 90, 14, 1124, 112, 30, 0x6b4423);
   for (i = 0; i < 4; i++)
      bx(1112 + i * 3, 112, 18, 1113 + i * 3, 122, 26, 0x222222);
   cyl(1140, 90, 22, 6, 6, 14, 8, 0xe9edc9);
   for (i = 0; i < 3; i++)
      bx(1136 + i * 4, 104, 20, 1138 + i * 4, 122, 22, 0x6b4423);
   bx(1355, 90, 12, 1395, 91, 46, 0xe0c39a);
   ball(1375, 96, 28, 14, 5, 8, 3, 0xc98b4a);
   cyl(1060, 90, 40, 6, 6, 26, 10, 0xffffff);
   /* the wall cupboards: left of the window and over the hob's sides */
   bx(1040, 150, 6, 1095, 225, 40, 0xf1eee8);
   bx(1495, 150, 6, 1530, 225, 40, 0xf1eee8);
   bx(1040, 150, 40, 1095, 225, 41, 0xf1ede4);
   bx(1495, 150, 40, 1530, 225, 41, 0xf1ede4);
   /* tiles behind the counters */
   mat(TEX3D_SUBWAY, 128);
   bxf(1040, 90, 6, 1100, 150, 7, 0xffffff, R3D_PANE);
   bxf(1400, 90, 6, 1530, 150, 7, 0xffffff, R3D_PANE);
   plain();
   /* the east run: 68..470, the coffee corner */
   base_run_z(1532, 1594, 66, 470, cab, door);
   bx(1555, 90, 90, 1585, 130, 116, 0x2b2b2b);     /* the coffee machine */
   bx(1548, 104, 96, 1555, 108, 110, 0x9a9aa0);
   for (i = 0; i < 4; i++)
      mug(1570, 90, 130 + i * 12, i & 1 ? 0xffffff : 0xe76f51);
   bx(1560, 90, 190, 1588, 108, 222, 0xc0c0c0);    /* the toaster */
   bx(1566, 108, 196, 1582, 109, 216, 0x222222);
   cyl(1572, 90, 260, 9, 7, 22, 10, 0xd62828);    /* the kettle */
   bx(1548, 90, 300, 1590, 120, 360, 0x2b2b2b);   /* the microwave */
   bx(1546, 96, 306, 1548, 114, 340, 0x111111);
   for (i = 0; i < 5; i++)
      cyl(1580, 90, 390 + i * 12, 4, 4, 12 + (i % 3) * 3, 6, i % 3 == 0 ? 0x6a994e : i % 3 == 1 ? 0xbc4749 : 0xf2e8cf); /* spices */
   cyl(1565, 90, 440, 16, 18, 8, 12, 0x2a2a2a);   /* a fruit bowl */
   ball(1561, 99, 437, 5, 5, 5, 3, 0xffb703);
   ball(1570, 99, 444, 5, 5, 5, 3, 0x8ac926);
   ball(1566, 100, 434, 5, 5, 5, 3, 0xe63946);
   /* the fridge and the pantry */
   at(1532, 0, 564, 1024);
   if (!piece("fridge", FACE_MZ, 0, 0, 0, 88, 195, 62))
   {
      bx(0, 0, 0, 88, 195, 62, 0xe8eaec);
      bx(43, 4, -1, 45, 191, 0, 0xb7bcc2);
      bx(36, 90, -4, 39, 140, -1, 0xb7bcc2);
      bx(49, 90, -4, 52, 140, -1, 0xb7bcc2);
   }
   bx(10, 150, -2, 18, 158, -1, 0xe63946);        /* magnets and a drawing */
   bx(54, 140, -2, 76, 168, -1, 0xffe66d);
   at(0, 0, 0, 0);
   mat(TEX3D_WOOD, 100);
   bx(1534, 0, 566, 1594, 230, 644, 0xefe9df);
   plain();
   bx(1533, 6, 570, 1534, 224, 604, 0xf6f1e8);
   bx(1533, 6, 606, 1534, 224, 640, 0xf6f1e8);
   /* the island with three stools and two pendants */
   mat(TEX3D_WOOD, 100);
   bx(1180, 0, 250, 1420, 88, 330, 0x3d5a80);
   plain();
   mat(TEX3D_MARBLE, 100);
   bx(1172, 88, 244, 1428, 92, 356, 0xffffff);
   plain();
   bx(1220, 92, 270, 1260, 94, 300, 0xd0d0d0);    /* a laptop open: work from the kitchen */
   bx(1220, 94, 268, 1260, 120, 270, 0x2a2a30);
   cyl(1350, 92, 290, 14, 16, 6, 12, 0x2a2a2a);
   ball(1346, 100, 287, 5, 5, 5, 3, 0xffb703);
   for (i = 0; i < 3; i++)
      stool(1220 + i * 80, 385, 70, 0xc0703d);
   for (i = 0; i < 2; i++)
   {
      at(1240 + i * 120, 0, 300, 0);
      pendant(80);
   }
   at(0, 0, 0, 0);
   r3d_light(CM(1300), CM(170), CM(300), M(3), 0xffe2b0, 240);
   /* the family's table for four */
   at(940, 0, 120, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 72, 0, 140, 76, 90, 0xd4b48c);
   plain();
   bx(6, 0, 6, 12, 72, 12, 0x6b4423);
   bx(128, 0, 6, 134, 72, 12, 0x6b4423);
   bx(6, 0, 78, 12, 72, 84, 0x6b4423);
   bx(128, 0, 78, 134, 72, 84, 0x6b4423);
   mug(30, 76, 30, 0xe63946);
   mug(100, 76, 60, 0xffffff);
   bx(50, 76, 20, 90, 77, 50, 0xf1faee);          /* a newspaper */
   cyl(70, 76, 70, 10, 10, 2, 12, 0xf8f9fa);      /* a plate with toast */
   bx(64, 78, 66, 76, 80, 74, 0xe9c46a);
   at(992, 0, 120, 2048);
   chair_wood(0x3d5a80, 0xe0e1dd);
   at(1072, 0, 120, 2048);
   chair_wood(0x3d5a80, 0xe0e1dd);
   at(950, 0, 210, 0);
   chair_wood(0x3d5a80, 0xe0e1dd);
   at(1030, 0, 210, 0);
   chair_wood(0x3d5a80, 0xe0e1dd);
   at(1010, 0, 165, 0);
   pendant(90);
   r3d_light(0, CM(150), 0, M(3), 0xffe2b0, 200);
   /* the tall cupboards with the second oven and the microwave, against the north wall */
   at(1130, 0, 584, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 0, 0, 240, 220, 60, cab);
   plain();
   for (i = 0; i < 4; i++)
      bx(4 + i * 60, 4, -1, 56 + i * 60, 216, 0, door);
   bx(66, 80, -2, 114, 130, -1, 0x1c1c1c);
   bx(66, 140, -2, 114, 170, -1, 0x2b2b2b);
   for (i = 0; i < 4; i++)
      bx(28 + i * 60, 100, -4, 32 + i * 60, 130, -1, 0xc0c0c0);
   /* the bins, a calendar, a clock */
   at(1400, 0, 610, 0);
   bx(0, 0, 0, 30, 60, 34, 0xf2c14e);
   bx(34, 0, 0, 64, 60, 34, 0x3a86ff);
   bx(68, 0, 0, 98, 60, 34, 0x8ac926);
   at(1591, 150, 520, 1024);
   frame_pic(36, 46, poster[4]);
   at(1490, 200, 641, 2048);
   cyl(0, 0, 0, 14, 14, 3, 16, 0xf8f8f8);
   /* the bar between the living room and the kitchen, in the west wall's opening, three stools on the living room's side */
   at(0, 0, 0, 0);
   mat(TEX3D_WOOD, 100);
   bx(880, 0, 300, 920, 100, 470, 0x3d5a80);
   plain();
   mat(TEX3D_MARBLE, 100);
   bx(860, 100, 296, 924, 104, 474, 0xffffff);
   plain();
   for (i = 0; i < 3; i++)
      stool(842, 330 + i * 55, 76, 0xc0703d);
   mug(870, 104, 340, 0x264653);
   cyl(900, 104, 430, 6, 6, 18, 8, 0xa8dadc);     /* a jug of water */
}

static void office(void)
{
   int32_t i;
   /* the desk against the north wall, two screens of code, the work's whole kit */
   at(860, 0, 1168, 0);
   if (!piece("desk", FACE_MZ, 0, 0, 0, 180, 76, 72))
   {
      mat(TEX3D_WOOD, 100);
      bx(0, 72, 0, 180, 76, 72, 0x2b2b2b);
      plain();
      bx(4, 0, 4, 8, 72, 68, 0x1c1c1c);
      bx(172, 0, 4, 176, 72, 68, 0x1c1c1c);
      bx(4, 66, 64, 176, 72, 68, 0x1c1c1c);
   }
   /* the package's computer under the desk, its light on */
   if (piece("computer", FACE_MZ | NO_SOLID, 146, 0, 22, 164, 42, 60))
   {
      r3d_glow(400);
      bx(153, 36, 21, 157, 37, 22, 0x4cc9f0);
      r3d_glow(0);
   }
   at(860, 76, 1168, 0);
   if (model3d_find("monitor"))
   {
      /* the package's screens to the back, the keyboard and the mouse at the front edge, under the hands */
      int32_t row, col;
      monitor(30, 26, 60, 34, poster[8]);
      monitor(96, 26, 60, 34, poster[9]);
      bx(54, 0, 3, 116, 2, 21, 0x1d1d22);
      r3d_glow(160);
      bx(53, 0, 2, 117, 1, 22, 0x7b2cbf); /* its light underneath */
      r3d_glow(0);
      for (row = 0; row < 4; row++)
         for (col = 0; col < 14; col++)
            bx(57 + col * 4, 2, 5 + row * 4, 60 + col * 4, 3, 8 + row * 4, row == 0 && col > 3 && col < 10 ? 0x9a9aa6 : 0xd6d6de);
      bx(73, 2, 5, 97, 3, 8, 0xd6d6de); /* the space bar */
      bx(120, 0, 2, 148, 1, 24, 0x3c3c46);
      if (!piece("mouse", FACE_MZ | NO_SOLID, 129, 1, 7, 137, 5, 18))
         bx(129, 1, 7, 137, 4, 17, 0x222226);
   }
   else
   {
      monitor(30, 0, 60, 34, poster[8]);
      monitor(96, 0, 60, 34, poster[9]);
      bx(50, 0, 14, 110, 2, 30, 0x222226);        /* keyboard, glowing a little */
      r3d_glow(140);
      bx(52, 2, 16, 108, 3, 28, 0x7b2cbf);
      r3d_glow(0);
      bx(118, 0, 16, 126, 3, 26, 0x222226);       /* mouse */
      bx(112, 0, 10, 140, 1, 32, 0x3c3c46);
   }
   mug(14, 0, 20, 0xe9c46a);
   can(150, 0, 18, 0x06d6a0);
   can(160, 0, 26, 0xef476f);
   rubber_duck(150, 0, 50);
   bx(4, 0, 40, 28, 18, 62, 0xd9d9d9);            /* a NAS that blinks */
   r3d_glow(400);
   bx(8, 12, 39, 10, 14, 40, 0x38b000);
   r3d_glow(0);
   if (piece("desk_lamp", FACE_MZ | NO_SOLID, 159, 0, 40, 173, 32, 66)) /* a desk lamp, its bulb in the shade */
   {
      r3d_glow(300);
      ball(166, 19, 46, 4, 2, 4, 3, 0xfff1c0);
   }
   else
   {
      cyl(166, 0, 56, 5, 3, 20, 6, 0x333333);
      r3d_glow(300);
      ball(166, 22, 50, 6, 4, 6, 3, 0xfff1c0);
   }
   r3d_glow(0);
   r3d_light(CM(166), CM(30), CM(40), M(2), 0xffe9c0, 260);
   cyl(-8, 0, 40, 6, 6, 2, 10, 0x333333);         /* headphones on their stand */
   bx(-9, 2, 39, -7, 30, 41, 0x333333);
   ball(-8, 30, 40, 9, 6, 3, 3, 0x1d1d1d);
   /* the gaming chair on its round mat */
   at(950, 0, 1110, 2048 + 200);
   desk_chair();
   at(950, 0, 1110, 0);
   r3d_occlude(0);
   cyl(0, 0, 0, 70, 70, 1, 24, 0x274c77);
   cyl(0, 1, 0, 55, 55, 1, 24, 0x6096ba);
   r3d_occlude(1);
   /* a ring light with its tripod */
   at(790, 0, 1200, 0);
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
   at(759, 100, 950, 3072);
   bx(0, 0, 0, 160, 100, 3, 0xbdbdbd);
   pane(poster[7], 3, 3, -1, 157, 97, 0, 0xffffff);
   at(880, 150, 1241, 0);
   bx(0, 0, 0, 90, 60, 3, 0x5c4033);
   mat(TEX3D_CORK, 128);
   bxf(3, 3, -1, 87, 57, 0, 0xffffff, R3D_PANE);
   plain();
   for (i = 0; i < 6; i++)
      bx(10 + (i % 3) * 26, 10 + (i / 3) * 24, -2, 28 + (i % 3) * 26, 28 + (i / 3) * 24, -1, i & 1 ? 0xffe66d : 0xff8fab);
   at(1141, 130, 1010, 1024);
   frame_pic(50, 70, poster[5]);
   /* a bookshelf of tech books, boxes from deliveries, plants */
   at(1112, 0, 960, 1024);
   bookshelf(100, 180, 5, 0x3d3d3d, 31);
   at(770, 0, 820, 0);
   cardboard_box(40, 30, 34);
   at(774, 30, 824, 200);
   cardboard_box(30, 20, 26);
   at(1110, 0, 1210, 0);
   tall_plant();
   at(1120, 0, 830, 0);
   plant_pot(16, 0x264653, 5);
   /* a reading corner by the west wall: an armchair, a floor lamp, a rug, a side table with a book */
   at(846, 0, 930, 3072);
   mat(TEX3D_FABRIC, 96);
   bx(0, 10, 0, 80, 44, 80, 0x588157);
   bx(0, 44, 64, 80, 90, 80, 0x3a5a40);
   bx(0, 44, 0, 14, 66, 64, 0x3a5a40);
   bx(66, 44, 0, 80, 66, 64, 0x3a5a40);
   plain();
   at(790, 0, 870, 0);
   floor_lamp(0xf6e7c1);
   r3d_light(0, CM(150), 0, M(3), 0xffd59a, 220);
   at(780, 0, 900, 0);
   mat(TEX3D_RUG, 0);
   r3d_occlude(0);
   bxf(0, 0, 0, 160, 1, 140, 0xffffff, R3D_TOP_ONLY);
   r3d_occlude(1);
   plain();
   at(800, 0, 1080, 0);
   cyl(0, 0, 0, 18, 18, 45, 12, 0x6b4423);
   bx(-8, 45, -10, 8, 48, 10, 0x9d0208);
}

static void bedroom(void)
{
   int32_t i;
   /* the bed against the west wall: a frame, the mattress, a knitted duvet, pillows */
   at(10, 0, 930, 0);
   if (!piece("bed", FACE_PX, 0, 0, 0, 200, 100, 160))
   {
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
   }
   /* nightstands with lamps, a book and a phone charging */
   for (i = 0; i < 2; i++)
   {
      at(10, 0, i ? 1095 : 880, 0);
      if (!piece("nightstand", FACE_PX, 0, 0, 0, 45, 50, 40))
      {
         mat(TEX3D_WOOD, 100);
         bx(0, 0, 0, 45, 50, 40, 0xd4b48c);
         plain();
      }
      cyl(20, 50, 20, 6, 6, 2, 8, 0x333333);
      cyl(20, 52, 20, 1, 1, 24, 4, 0x333333);
      r3d_glow(280);
      cyl(20, 70, 20, 12, 8, 14, 10, 0xf6e7c1);
      r3d_glow(0);
      r3d_light(CM(20), CM(76), CM(20), CM(220), 0xffd59a, 200);
   }
   at(30, 50, 885, 0);
   bx(0, 0, 0, 7, 1, 14, 0x111111);
   at(20, 50, 1100, 0);
   bx(0, 0, 0, 16, 3, 22, 0x6d597a);
   /* the wardrobe on the north wall, a pile of clothes */
   at(300, 0, 1180, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 0, 0, 180, 220, 60, 0xefe9df);
   plain();
   for (i = 0; i < 3; i++)
   {
      bx(i * 60 + 2, 4, -1, i * 60 + 58, 216, 0, 0xf6f1e8);
      bx(i * 60 + 52, 100, -3, i * 60 + 54, 130, -1, 0x999999);
   }
   at(300, 0, 900, 0);
   mat(TEX3D_FABRIC, 128);
   for (i = 0; i < 6; i++)
      bx(i * 5, i * 6, i * 3 + (i & 1) * 4, 50 - i * 3, i * 6 + 7, 40 - i * 2, i % 3 == 0 ? 0x3d5a80 : i % 3 == 1 ? 0x98c1d9 : 0xee6c4d);
   plain();
   /* a laundry basket, a mirror, a rug, plants, pictures, shoes */
   at(450, 0, 1030, 0);
   cyl(0, 0, 0, 22, 25, 50, 12, 0xd6c3a1);
   mat(TEX3D_FABRIC, 128);
   ball(0, 50, 0, 18, 8, 18, 3, 0xffffff);
   plain();
   at(491, 0, 1150, 1024);
   bx(0, 0, 0, 50, 170, 4, 0xc9a66b);
   r3d_glow(170);
   bxf(4, 4, -1, 46, 166, 0, 0xbcd4e6, R3D_PANE);
   r3d_glow(0);
   at(230, 0, 950, 0);
   mat(TEX3D_RUG2, 0);
   r3d_occlude(0);
   bxf(0, 0, 0, 140, 1, 200, 0xffffff, R3D_TOP_ONLY);
   r3d_occlude(1);
   plain();
   at(250, 0, 1215, 0);
   plant_pot(20, 0xbc6c25, 9);
   at(9, 140, 840, 3072);
   frame_pic(80, 56, poster[1]);
   at(9, 150, 1140, 3072);
   frame_pic(100, 60, poster[3]);
   at(220, 0, 880, 900);
   bx(0, 0, 0, 12, 10, 30, 0x2b2d42);
   bx(16, 0, 4, 28, 10, 34, 0x2b2d42);
}

static void bathroom(void)
{
   int32_t i;
   /* the tub with a glass screen, the toilet, the sink with its mirror, towels (planned at 450..700 x 500..950, built moved) */
   at(458, 0, 810, 0);
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
   if (!piece("toilet", FACE_PZ, 0, 0, -16, 40, 80, 50))
   {
      bx(0, 0, 0, 40, 40, 50, 0xffffff);            /* the toilet */
      bx(2, 40, 0, 38, 44, 50, 0xf1f1f1);
      bx(0, 40, -16, 40, 80, 0, 0xffffff);
   }
   cyl(52, 0, 30, 6, 6, 30, 8, 0xffffff);        /* a toilet brush */
   at(514, 0, 512, 0);
   if (piece("sink", FACE_PZ | NO_SOLID, 5, 0, 0, 75, 100, 45)) /* the sink, beside the toilet (the door is east of it) */
      r3d_solid_box(CM(30), 0, 0, CM(50), CM(25), CM(20)); /* its pipe, against the wall */
   else
   {
      bx(0, 0, 0, 80, 80, 45, 0x6c757d);
      bx(-4, 80, -4, 84, 86, 49, 0xf8f9fa);
      bx(16, 84, 10, 64, 86, 38, 0xdfe7ec);
      cyl(40, 86, 6, 2, 2, 18, 6, 0xc0c0c0);
      cyl(10, 86, 30, 3, 3, 12, 6, 0x90e0ef);    /* a toothbrush cup */
   }
   at(514, 0, 506, 0);
   bx(0, 110, 0, 80, 190, 2, 0x9a9a9a);           /* the mirror */
   r3d_glow(180);
   bxf(3, 113, 2, 77, 187, 3, 0xd8e6ee, R3D_SKIP_BOTTOM | R3D_SKIP_TOP | R3D_SKIP_X);
   r3d_glow(0);
   at(500, 0, 640, 0);
   cyl(0, 0, 0, 18, 22, 46, 10, 0x8d99ae);        /* the laundry basket */
   for (i = 0; i < 3; i++)
   {
      at(470 + i * 30, 120, 942, 2048);
      bx(0, -50, 0, 24, 0, 6, i == 1 ? 0xffb4a2 : 0x9bc1bc);
   }
   /* a bath mat, a towel rail by the tub, a plant on the floor */
   at(560, 0, 760, 0);
   mat(TEX3D_RUG2, 0);
   r3d_occlude(0);
   bxf(0, 0, 0, 70, 1, 45, 0xffffff, R3D_TOP_ONLY);
   r3d_occlude(1);
   plain();
   at(690, 0, 760, 0);
   cyl(0, 0, 0, 2, 2, 110, 4, 0xc0c0c0);
   bx(-3, 70, -20, 3, 110, 20, 0x9bc1bc);
   at(680, 0, 520, 0);
   plant_pot(16, 0xffffff, 6);
}

static void hall(void)
{
   int32_t i;
   /* the front door at the east end, a coat rack, shoes, a console with keys, an umbrella stand, a mirror */
   at(1592, 0, 690, 3072);
   bx(0, 0, 0, 80, 210, 6, 0x6d4c41);
   bx(64, 100, -4, 70, 104, 0, 0xc0a060);
   at(1480, 0, 788, 0);
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
   at(1390, 0, 750, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 0, 0, 90, 12, 28, 0x8d6e63);
   bx(0, 30, 0, 90, 34, 28, 0x8d6e63);
   plain();
   for (i = 0; i < 4; i++)
      bx(6 + i * 21, 12, 4, 20 + i * 21, 20, 26, i & 1 ? 0x222222 : 0xe9ecef);
   at(270, 0, 768, 0);
   mat(TEX3D_WOOD, 100);
   bx(0, 0, 0, 100, 80, 26, 0x4e342e);
   plain();
   cyl(30, 80, 13, 10, 12, 4, 10, 0xd4a373);
   bx(26, 84, 10, 34, 86, 16, 0xffd166);           /* keys */
   at(250, 80, 783, 0);
   plant_pot(12, 0xffffff, 2);
   at(270, 120, 792, 0);
   bx(0, 0, 0, 100, 70, 3, 0xc9a66b);
   r3d_glow(170);
   bxf(4, 4, -1, 96, 66, 0, 0xbcd4e6, R3D_PANE);
   r3d_glow(0);
   at(1578, 0, 668, 0); /* in the corner, clear of the door */
   cyl(0, 0, 0, 12, 12, 50, 10, 0x2a9d8f);
   cyl(-4, 30, -2, 2, 2, 50, 4, 0x222222);
   cyl(5, 30, 3, 2, 2, 46, 4, 0xe63946);
   /* a parcel nobody opened, against the wall out of the way to the door */
   at(1300, 0, 662, 60);
   cardboard_box(44, 28, 32);
   /* a runner rug along the hall */
   at(330, 0, 690, 0);
   mat(TEX3D_RUG2, 128);
   r3d_occlude(0);
   bxf(0, 0, 0, 1000, 1, 70, 0xffffff, R3D_TOP_ONLY);
   r3d_occlude(1);
   plain();
}

static void balcony(void)
{
   int32_t i;
   /* the terrace along the living room: 9 m by 2, tiles, a railing */
   at(0, 0, 0, 0);
   mat(TEX3D_TILES, 160);
   bxf(40, -10, -200, 880, 0, 0, 0xd8cfc4, R3D_SKIP_BOTTOM);
   plain();
   bx(40, 0, -202, 880, 4, -196, 0x2d2d2d);
   bx(40, 100, -202, 880, 104, -196, 0x2d2d2d);
   for (i = 0; i <= 84; i++)
      bx(40 + i * 10, 4, -201, 42 + i * 10, 100, -198, 0x2d2d2d);
   for (i = 0; i <= 19; i++)
   {
      bx(40, 4, -200 + i * 10, 42, 100, -198 + i * 10, 0x2d2d2d);
      bx(878, 4, -200 + i * 10, 880, 100, -198 + i * 10, 0x2d2d2d);
   }
   bx(40, 100, -200, 44, 104, 0, 0x2d2d2d);
   bx(876, 100, -200, 880, 104, 0, 0x2d2d2d);
   /* a sun lounger with a towel, a parasol */
   at(560, 0, -170, 0);
   bx(0, 0, 0, 4, 30, 4, 0xdddddd);
   bx(186, 0, 0, 190, 30, 4, 0xdddddd);
   bx(0, 0, 60, 4, 30, 64, 0xdddddd);
   bx(186, 0, 60, 190, 30, 64, 0xdddddd);
   mat(TEX3D_FABRIC, 96);
   bx(0, 30, 0, 140, 38, 64, 0x2a9d8f);
   bx(140, 30, 0, 146, 70, 64, 0x2a9d8f);
   plain();
   for (i = 0; i < 6; i++)
      bx(146 + i * 7, 70 - i * 6, 0, 153 + i * 7, 78 - i * 6, 64, 0x2a9d8f); /* the back, leaning */
   mat(TEX3D_FABRIC, 128);
   bx(20, 38, 8, 110, 40, 56, 0xffb703);
   plain();
   at(800, 0, -130, 0);
   cyl(0, 0, 0, 20, 20, 4, 12, 0x555555);
   cyl(0, 4, 0, 2, 2, 210, 6, 0xeeeeee);
   cyl(0, 200, 0, 110, 2, 30, 16, 0xe76f51);
   /* two folding chairs and a small table with a drink */
   at(150, 0, -150, 300);
   chair_wood(0x2a9d8f, 0x2a9d8f);
   at(260, 0, -160, 3800);
   chair_wood(0x2a9d8f, 0x2a9d8f);
   at(220, 0, -110, 0);
   cyl(0, 0, 0, 3, 3, 60, 6, 0x2d2d2d);
   cyl(0, 60, 0, 26, 26, 3, 16, 0x2d2d2d);
   cyl(-6, 63, 4, 4, 3, 12, 8, 0xffb703);
   /* plants along the railing (the ones to water), a drying rack, the AC */
   for (i = 0; i < 6; i++)
   {
      at(360 + i * 26, 0, -180, 0);
      plant_pot(12 + (i % 3) * 4, i & 1 ? 0xbc6c25 : 0xe9edc9, (uint32_t)i);
   }
   at(60, 0, -190, 0);
   tall_plant();
   at(80, 0, -60, 1024);
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
   at(820, 0, -45, 0);
   bx(0, 0, 0, 50, 60, 40, 0xf1f1f1);
   cyl(25, 30, -1, 18, 18, 2, 16, 0x9a9a9a);
}

/* The laundry at the hall's west end (0..250 x 650..800): the washing machine, a dryer on it, a sink, shelves, a basket. */
static void laundry(void)
{
   int32_t i;
   at(66, 0, 664, 3072);
   if (!piece("washer", FACE_MZ, 0, 0, 0, 60, 85, 60))
   {
      bx(0, 0, 0, 60, 85, 60, 0xf8f9fa);             /* the washing machine, its door toward the room */
      cyl(30, 40, -1, 18, 18, 2, 16, 0x9aa7b0);
      bx(6, 70, -1, 30, 80, 0, 0x3a3a3a);
      r3d_glow(300);
      bx(40, 74, -1, 44, 78, 0, 0x38b000);
      r3d_glow(0);
   }
   at(66, 85, 664, 3072);
   if (!piece("washer", FACE_MZ, 0, 0, 0, 60, 80, 60))
   {
      bx(0, 0, 0, 60, 80, 60, 0xe9ecef);              /* the dryer on top */
      cyl(30, 38, -1, 16, 16, 2, 16, 0xb8c0c8);
   }
   at(160, 0, 794, 2048);
   bx(0, 0, 0, 70, 84, 50, 0xd8d2c4);              /* a deep sink on a cupboard, against the north wall */
   bx(-2, 84, -2, 72, 88, 52, 0xf1f1f1);
   bx(8, 84, 8, 62, 88, 44, 0xc9ccd1);
   cyl(35, 88, 46, 2, 2, 24, 6, 0xc0c0c0);
   at(6, 140, 790, 2048);
   for (i = 0; i < 2; i++)
      bx(-230, i * 40, 0, -10, i * 40 + 3, 30, 0xe6dccb); /* shelves with detergent */
   for (i = 0; i < 6; i++)
      bx(-220 + i * 36, 3, 6, -196 + i * 36, 28, 22, i % 3 == 0 ? 0x3a86ff : i % 3 == 1 ? 0xff006e : 0xffbe0b);
   at(35, 0, 765, 0);
   cyl(0, 0, 0, 18, 22, 46, 10, 0xd6c3a1);         /* a basket of clothes to wash */
   mat(TEX3D_FABRIC, 128);
   ball(0, 46, 0, 16, 8, 16, 3, 0x98c1d9);
   plain();
   r3d_light(-CM(75), CM(220), -CM(45), M(2), 0xfff3e0, 180);
}

/* The game room (1150..1600 x 800..1250): a TV with consoles, a beanbag, an arcade cabinet, the guitar, posters. */
static void game_room(void)
{
   int32_t i;
   at(1265, 0, 1202, 0);
   tv_unit(poster[4]);
   /* a beanbag and a round rug in front of the TV */
   at(1375, 0, 1030, 0);
   r3d_occlude(0);
   mat(TEX3D_RUG2, 0);
   cyl(0, 0, 0, 110, 110, 1, 24, 0xffffff);
   plain();
   r3d_occlude(1);
   mat(TEX3D_FABRIC, 96);
   ball(0, 28, 0, 46, 30, 42, 4, 0xe76f51);
   ball(0, 50, 22, 40, 26, 18, 3, 0xe76f51);
   plain();
   bx(40, 0, -60, 70, 4, -40, 0x1d1d1d);          /* a controller left on the floor */
   /* an arcade cabinet in the corner */
   at(1230, 0, 1050, 3072);
   bx(0, 0, 0, 70, 180, 70, 0x1d3557);
   bx(4, 100, -2, 66, 106, 30, 0x111111);          /* the control panel */
   for (i = 0; i < 4; i++)
      cyl(30 + i * 8, 106, 10, 3, 3, 2, 8, i & 1 ? 0xe63946 : 0xffd166);
   cyl(14, 106, 12, 2, 2, 8, 6, 0x111111);
   ball(14, 115, 12, 3, 3, 3, 2, 0xe63946);
   r3d_glow(320);
   pane(poster[6], 8, 110, -1, 62, 170, 0, 0xffffff);
   bx(4, 172, -2, 66, 180, 0, 0xffd166);           /* the marquee */
   r3d_glow(0);
   /* the guitar on its stand, an amplifier */
   at(1560, 0, 1150, 0);
   bx(-8, 0, -6, 8, 30, 6, 0x222222);
   ball(0, 40, 0, 16, 18, 5, 3, 0x9c5a2b);
   ball(0, 66, 0, 12, 12, 5, 3, 0x9c5a2b);
   bx(-2, 76, -2, 2, 120, 2, 0x3b2a1a);
   at(1500, 0, 1100, 1024);
   bx(0, 0, 0, 50, 45, 30, 0x222222);
   bx(4, 8, -1, 46, 40, 0, 0x3a3a3a);
   /* shelves of games, posters, a neon */
   at(1558, 0, 980, 1024);
   bookshelf(100, 120, 3, 0x2b2b2b, 41);
   at(1480, 150, 809, 2048);
   frame_pic(60, 80, poster[2]);
   at(1570, 140, 809, 2048);
   frame_pic(70, 50, poster[7]);
   at(1450, 200, 1243, 0);
   r3d_glow(380);
   bx(0, 0, 0, 90, 4, 3, 0xff4fd8);
   bx(0, -20, 0, 4, 4, 3, 0xff4fd8);
   r3d_glow(0);
   r3d_light(CM(45), CM(-20), -CM(40), M(3), 0xff9ae8, 160);
   r3d_light(-CM(75), CM(30), -CM(218), M(3), 0xfff1dc, 160);
}


/* The building under the flat, the street and the city around it. */
static void city(void)
{
   int32_t i;
   /* the blocks around: lower than this one, so the view sees over their roofs */
   static const int32_t blocks[][5] = {
      { 1900, -800, 3000, 1200, 1100 }, { 1900, 1600, 3000, 3100, 1300 }, { -2400, -600, -500, 1600, 1000 },
      { -400, 1700, 1600, 3200, 1200 }, { -2600, -2600, -1600, -900, 900 }, { 2000, -2800, 3400, -1200, 1400 },
   };
   r3d_detail(0);
   at(0, 0, 0, 0);
   /* this building: five floors down to the street, its roof around the house and the terrace */
   mat(TEX3D_FACADE, 80);
   bxf(-40, -1500, -280, 1640, -10, 1290, 0xffffff, R3D_SKIP_TOP | R3D_SKIP_BOTTOM);
   plain();
   mat(TEX3D_CONCRETE, 64);
   bxf(-40, -10, -280, 1640, 0, -200, 0xd9d4cc, R3D_SKIP_BOTTOM);
   bxf(-40, -10, -200, 40, 0, 0, 0xd9d4cc, R3D_SKIP_BOTTOM);
   bxf(880, -10, -200, 1640, 0, 0, 0xd9d4cc, R3D_SKIP_BOTTOM);
   bxf(-40, -10, 1250, 1640, 0, 1290, 0xd9d4cc, R3D_SKIP_BOTTOM);
   bxf(-40, -10, 0, 0, 0, 1250, 0xd9d4cc, R3D_SKIP_BOTTOM);
   bxf(1600, -10, 0, 1640, 0, 1250, 0xd9d4cc, R3D_SKIP_BOTTOM);
   plain();
   /* the street, the pavement and a park */
   mat(TEX3D_CONCRETE, 32);
   bxf(-3000, -1520, -3000, 4200, -1500, 4000, 0x6f747a, R3D_TOP_ONLY);
   bxf(-200, -1500, -700, 1800, -1488, -280, 0xc8c3ba, R3D_TOP_ONLY);
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
/*
 * What people hold (actor3d_pose.prop), each built around where it is
 * gripped, its front toward -z like the person's face.
 */
static void build_props(void)
{
   r3d_place(0, 0, 0, 0);
   r3d_detail(0);
   r3d_bevel(0);
   /* a mug of coffee */
   r3d_begin();
   cyl(0, -5, -4, 4, 4, 9, 10, 0xe76f51);
   cyl(0, 3, -4, 3, 3, 1, 10, 0x4a2c1a);
   bx(4, -3, -5, 6, 2, -3, 0xe76f51);
   props[ROOM3D_PROP_MUG] = r3d_end();
   /* a frying pan, the handle in the hands */
   r3d_begin();
   bx(-2, -1, -16, 2, 1, 2, 0x2b2b2b);
   cyl(0, -2, -28, 13, 13, 4, 12, 0x333333);
   cyl(0, 1, -28, 11, 11, 1, 12, 0xc8a165); /* what is frying */
   props[ROOM3D_PROP_PAN] = r3d_end();
   /* a can from the fridge */
   r3d_begin();
   cyl(0, -6, -3, 3, 3, 12, 10, 0xd62828);
   cyl(0, 6, -3, 2, 2, 1, 10, 0xcccccc);
   props[ROOM3D_PROP_CAN] = r3d_end();
   /* a bowl and a fork */
   r3d_begin();
   cyl(0, -5, -8, 5, 9, 6, 12, 0xf1faee);
   cyl(0, 0, -8, 8, 8, 1, 12, 0xe9c46a);
   bx(4, 1, -12, 5, 2, 2, 0xb0b0b0);
   props[ROOM3D_PROP_BOWL] = r3d_end();
   /* a game controller with its buttons */
   r3d_begin();
   bx(-8, -2, -6, 8, 2, 2, 0x1d1d1d);
   ball(-8, -2, -1, 3, 3, 4, 2, 0x1d1d1d);
   ball(8, -2, -1, 3, 3, 4, 2, 0x1d1d1d);
   bx(3, 2, -4, 5, 3, -2, 0xe63946);
   bx(5, 2, -2, 7, 3, 0, 0x2a9d8f);
   bx(-6, 2, -4, -3, 3, -2, 0x6c757d);
   props[ROOM3D_PROP_PAD] = r3d_end();
   /* the guitar, around its sound hole: the body toward the strumming side, the neck to the other one (the pose's roll turns it up) */
   r3d_begin();
   ball(8, -6, -6, 16, 15, 5, 3, 0x9c5a2b);
   ball(-10, -2, -6, 12, 11, 5, 3, 0x9c5a2b);
   ball(0, -4, -11, 4, 4, 1, 2, 0x2b1a10);
   bx(-54, -1, -8, -18, 3, -4, 0xd9a066);
   bx(-64, -2, -9, -54, 5, -3, 0x2b1a10);
   props[ROOM3D_PROP_GUITAR] = r3d_end();
   /* a watering can, held by its handle */
   r3d_begin();
   cyl(0, -18, -4, 7, 7, 14, 10, 0x2a9d8f);
   bx(-1, -4, -6, 1, 0, 6, 0x2a9d8f);
   bx(-1, -10, -26, 1, -8, -10, 0x2a9d8f);
   ball(0, -9, -27, 3, 3, 2, 2, 0x21867a);
   props[ROOM3D_PROP_WATERING] = r3d_end();
   /* the delivery's parcel */
   r3d_begin();
   bx(-16, -12, -24, 16, 10, 2, 0xc49a6c);
   bx(-2, 10, -24, 2, 11, 2, 0xe9d8a6);
   props[ROOM3D_PROP_PARCEL] = r3d_end();
   /* headphones */
   r3d_begin();
   ball(-9, -2, -4, 4, 5, 3, 2, 0x222222);
   ball(9, -2, -4, 4, 5, 3, 2, 0x222222);
   bx(-9, 4, -5, 9, 6, -3, 0x333333);
   props[ROOM3D_PROP_HEADPHONES] = r3d_end();
   /* the shower's water: see-through streaks in a cone under the head, one every 28 cm down, wider as they fall */
   r3d_begin();
   r3d_occlude(0);
   r3d_glass(190);
   {
      int32_t i, j;
      for (i = 0; i < 16; i++)
      {
         int32_t a = i * 4096 / 16 + (i & 1) * 128, rr = 8 + (i % 4) * 3;
         for (j = 0; j < 6; j++)
         {
            int32_t y = -j * 28 - (i * 9) % 28, r = rr + j * 4;
            int32_t x = r * hd_cos(a) / 16384, z = r * hd_sin(a) / 16384;
            bx(x, y - 16, z, x + 2, y, z + 2, 0x8ecae6);
         }
      }
   }
   r3d_glass(0);
   r3d_occlude(1);
   props[ROOM3D_PROP_SHOWER] = r3d_end();
}

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
   person_shadow = shadow;
   /* a ginger tabby with green eyes, and a smaller shadow */
   {
      static const pet3d_look ginger = { 0xc0601c, 0x7a3208, 0xf3ece0, 0x9ef01a, 0xf4978e };
      r3d_place(0, 0, 0, 0);
      r3d_begin();
      r3d_decal(1);
      r3d_occlude(0);
      r3d_material(T[TEX3D_ROUND_SHADOW], 0);
      r3d_box(-CM(16), CM(1), -CM(26), CM(16), CM(1) + 1, CM(26), 0x3a3a50, R3D_TOP_ONLY);
      r3d_decal(0);
      r3d_occlude(1);
      plain();
      shadow = r3d_end();
      pet3d_build(&cat, &ginger);
      cat.shadow = shadow;
   }
   build_props();
}

void hd_r3d_build(void)
{
   /* the openings of each wall (see the plan at the top) */
   static const opening liv_s[] = { { 80, 400, OPEN_BIG, 0 }, { 480, 820, OPEN_BIG, 0 } };
   static const opening kit_s[] = { { 1100, 1400, OPEN_WINDOW, 0 } };
   static const opening liv_w[] = { { 150, 350, OPEN_WINDOW, CURTAIN_UP } };
   static const opening north_bed[] = { { 40, 200, OPEN_WINDOW, CURTAIN_DOWN } };
   static const opening north_off[] = { { 1050, 1130, OPEN_WINDOW, CURTAIN_DOWN } };
   static const opening north_game[] = { { 1440, 1580, OPEN_WINDOW, CURTAIN_DOWN } };
   static const opening east_kit[] = { { 200, 450, OPEN_WINDOW, 0 } };
   static const opening east_hall[] = { { 685, 775, OPEN_DOOR, 0 } };
   static const opening east_game[] = { { 900, 1100, OPEN_WINDOW, CURTAIN_DOWN } };
   static const opening hall_s[] = { { 550, 800, OPEN_DOOR, 0 }, { 1000, 1100, OPEN_DOOR, DOOR_DOWN_BEFORE } };
   static const opening hall_n[] = { { 380, 470, OPEN_DOOR, DOOR_UP_BEFORE }, { 650, 740, OPEN_DOOR, DOOR_DOWN_AFTER }, { 850, 940, OPEN_DOOR, DOOR_UP_AFTER }, { 1250, 1340, OPEN_DOOR, DOOR_UP_BEFORE } };
   static const opening liv_kit[] = { { 300, 640, OPEN_DOOR, 0 } };
   static const opening laundry_e[] = { { 690, 780, OPEN_DOOR, 0 } };
   if (ready && built_for == model3d_gen)
      return;
   T = tex3d_build();
   if (!ready)
   {
      poster[0] = tex3d_poster(0, 4);
      poster[1] = tex3d_poster(0, 1);
      poster[2] = tex3d_poster(1, 2);
      poster[3] = tex3d_poster(3, 3);
      poster[4] = tex3d_poster(1, 5);
      poster[5] = tex3d_poster(2, 5);
      poster[6] = tex3d_poster(2, 3);
      poster[7] = tex3d_poster(3, 0);
      poster[8] = tex3d_poster(2, 11); /* the desk's two screens, redrawn as the work goes */
      poster[9] = tex3d_poster(2, 12);
   }
   /* built again when a package brings other models (its furniture goes in before the light is baked) */
   built_for = model3d_gen;
   builds++;
   bodies_gen = -1;
   bodies_first = -1;
   r3d_clear();
   r3d_detail(CM(60));
   r3d_begin();
   /* floors */
   floor_of(0, 0, 900, 650, TEX3D_PARQUET, 160, 0xffffff);
   floor_of(900, 0, 1600, 650, TEX3D_TILES, 128, 0xf3ecdf);
   floor_of(250, 650, 1600, 800, TEX3D_PLANKS, 96, 0xffffff);
   floor_of(0, 650, 250, 800, TEX3D_TILES, 160, 0xe8e4dc);
   floor_of(0, 800, 500, 1250, TEX3D_PLANKS, 96, 0xf0e6dc);
   floor_of(500, 800, 750, 1250, TEX3D_TILES, 160, 0xffffff);
   floor_of(750, 800, 1150, 1250, TEX3D_PARQUET, 160, 0xd8cbbd);
   floor_of(1150, 800, 1600, 1250, TEX3D_PLANKS, 96, 0xc9d6e3);
   /* outer walls (light outside, each room's color inside) */
   wall(0, 0, 0, 900, 0xf2efe9, 0xf3e3d3, liv_s, 2);
   wall(0, 0, 900, 1600, 0xf2efe9, 0xf6f3ea, kit_s, 1);
   wall(1, 0, 0, 650, 0xf2efe9, 0xc8553d, liv_w, 1);
   wall(1, 0, 650, 800, 0xf2efe9, 0xe8eef2, NULL, 0);
   wall(1, 0, 800, 1250, 0xf2efe9, 0xdfe7dc, NULL, 0);
   wall(0, 1250, 0, 500, 0xdfe7dc, 0xf2efe9, north_bed, 1);
   wall(0, 1250, 500, 750, 0xe9f1f5, 0xf2efe9, NULL, 0);
   wall(0, 1250, 750, 1150, 0x2f3e46, 0xf2efe9, north_off, 1);
   wall(0, 1250, 1150, 1600, 0x3d405b, 0xf2efe9, north_game, 1);
   wall(1, 1600, 0, 650, 0xf6f3ea, 0xf2efe9, east_kit, 1);
   wall(1, 1600, 650, 800, 0xefe8de, 0xf2efe9, east_hall, 1);
   wall(1, 1600, 800, 1250, 0x3d405b, 0xf2efe9, east_game, 1);
   /* inner walls */
   wall(0, 650, 0, 250, 0xf3e3d3, 0xe8eef2, NULL, 0);
   wall(0, 650, 250, 1600, 0xf3e3d3, 0xefe8de, hall_s, 2);
   wall(0, 800, 0, 250, 0xe8eef2, 0xdfe7dc, NULL, 0);
   wall(0, 800, 250, 1600, 0xefe8de, 0xdfe7dc, hall_n, 4);
   wall(1, 900, 0, 650, 0xf3e3d3, 0xf6f3ea, liv_kit, 1);
   wall(1, 250, 650, 800, 0xe8eef2, 0xefe8de, laundry_e, 1);
   wall(1, 500, 800, 1250, 0xdfe7dc, 0xe9f1f5, NULL, 0);
   wall(1, 750, 800, 1250, 0xe9f1f5, 0x2f3e46, NULL, 0);
   wall(1, 1150, 800, 1250, 0x2f3e46, 0x3d405b, NULL, 0);
   /* tiles in the bathroom, cut at its door */
   at(0, 0, 0, 0);
   mat(TEX3D_SUBWAY, 128);
   bxf(506, 0, 806, 650, 140, 807, 0xffffff, R3D_PANE);
   bxf(506, 0, 1243, 744, 140, 1244, 0xffffff, R3D_PANE);
   plain();
   /* furniture with soft edges */
   r3d_bevel(CM(2));
   living();
   kitchen();
   office();
   bedroom();
   shift_x = 50, shift_z = 300;
   bathroom();
   shift_x = 0, shift_z = 0;
   hall();
   laundry();
   game_room();
   balcony();
   r3d_bevel(0);
   city();
   scene = r3d_end();
   memset(&sunlight, 0, sizeof sunlight);
   /* shadows over the flat and its balcony; the city around is lit without them */
   sunlight.lo.x = BASE - CM(60), sunlight.lo.y = -CM(20), sunlight.lo.z = BASE - CM(300);
   sunlight.hi.x = BASE + CM(1660), sunlight.hi.y = CM(280), sunlight.hi.z = BASE + CM(1300);
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

int32_t room3d_desk_screen(int32_t which)
{
   hd_r3d_build();
   return poster[8 + (which & 1)];
}

const r3d_mesh *room3d_scene(void)
{
   hd_r3d_build();
   return &scene;
}

const r3d_light_cfg *room3d_light(void)
{
   hd_r3d_build();
   return &sunlight;
}

const pet3d_kit *room3d_cat(void)
{
   hd_r3d_build();
   return &cat;
}

const r3d_mesh *room3d_prop(int32_t which)
{
   hd_r3d_build();
   return &props[hd_clamp(which, 0, ROOM3D_PROPS - 1)];
}

int32_t room3d_builds(void)
{
   hd_r3d_build();
   return builds;
}

const r3d_mesh *room3d_shadow(void)
{
   hd_r3d_build();
   return &person_shadow;
}

model3d_body *room3d_person_model(int32_t who)
{
   static const char *const names[ROOM3D_PEOPLE] = { "worker", "flatmate" };
   int32_t i;
   hd_r3d_build();
   if (bodies_gen != model3d_gen)
   {
      /* the package's people, built again after its models changed (in place of the last ones) */
      if (bodies_first >= 0)
         r3d_truncate(bodies_first);
      bodies_first = r3d_triangles();
      r3d_place(0, 0, 0, 0);
      for (i = 0; i < ROOM3D_PEOPLE; i++)
         model3d_body_build(&bodies[i], model3d_find(names[i]));
      bodies_gen = model3d_gen;
   }
   who = hd_clamp(who, 0, ROOM3D_PEOPLE - 1);
   return bodies[who].model && model3d_bones(bodies[who].model) ? &bodies[who] : NULL;
}

const actor3d_kit *room3d_person(int32_t who)
{
   hd_r3d_build();
   return &people[hd_clamp(who, 0, ROOM3D_PEOPLE - 1)];
}

void hd_r3d_start(hd_state *s)
{
   memset(&s->show, 0, sizeof s->show);
   s->show.on = 2;
   s->show.kx = BASE + CM(800);
   s->show.ky = BASE + CM(600);
   s->show.ka = 3600;
   s->show.kv = 640;
   s->show.rpos = 2100;
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
   v.bloom = 200;
   v.vignette = 110;
   v.light = &sunlight;
   /* at the desk: sitting and typing, now and then a look at the other screen */
   memset(&pose, 0, sizeof pose);
   pose.at.x = BASE + CM(950);
   pose.at.z = BASE + CM(1110);
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
   pose.at.x = BASE + CM(800);
   pose.at.z = BASE + CM(300) + (int32_t)((int64_t)CM(250) * (leg < 300 ? leg : 600 - leg) / 300);
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
