/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Textures painted in code for 3D worlds (r3d.h, TEX3D_*): floors, walls,
 * cloth, rugs, wood, the city outside, and posters. Integer noise only, so
 * every build makes the same pixels. Some are grey on purpose (plaster,
 * fabric, knit, leaves, metal): the piece's color tints them, so one
 * texture serves a blue sofa and a red jumper.
 */
#include <string.h>
#include "r3d.h"

static uint32_t hash3(int32_t x, int32_t y, uint32_t seed)
{
   uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
   h = (h ^ (h >> 13)) * 1274126177u;
   return h ^ (h >> 16);
}

/* Smooth noise 0..255 over a lattice of `cell` pixels, repeating every `wrap` cells (a seamless texture). */
static int32_t noise(int32_t x, int32_t y, int32_t cell, int32_t wrap, uint32_t seed)
{
   int32_t cx = x / cell, cy = y / cell, fx = (x % cell) * 256 / cell, fy = (y % cell) * 256 / cell;
   int32_t a = hash3(cx % wrap, cy % wrap, seed) & 255, b = hash3((cx + 1) % wrap, cy % wrap, seed) & 255;
   int32_t c = hash3(cx % wrap, (cy + 1) % wrap, seed) & 255, d = hash3((cx + 1) % wrap, (cy + 1) % wrap, seed) & 255;
   /* smoothstep */
   fx = fx * fx * (768 - 2 * fx) >> 16;
   fy = fy * fy * (768 - 2 * fy) >> 16;
   a = a + ((b - a) * fx >> 8);
   c = c + ((d - c) * fx >> 8);
   return a + ((c - a) * fy >> 8);
}

/* Three octaves of noise over a texture of `size` pixels, 0..255. */
static int32_t fbm(int32_t x, int32_t y, int32_t size, int32_t cell, uint32_t seed)
{
   int32_t n = noise(x, y, cell, size / cell, seed) * 4;
   n += noise(x, y, cell / 2, size / (cell / 2), seed + 1) * 2;
   n += noise(x, y, hd_max(1, cell / 4), size / hd_max(1, cell / 4), seed + 2);
   return n / 7;
}

static uint32_t rgb(int32_t r, int32_t g, int32_t b)
{
   return 0xff000000u | (uint32_t)hd_clamp(r, 0, 255) << 16 | (uint32_t)hd_clamp(g, 0, 255) << 8 | (uint32_t)hd_clamp(b, 0, 255);
}

/* c made lighter (k > 0) or darker (k < 0), k in -256..256. */
static uint32_t shade(uint32_t c, int32_t k)
{
   int32_t r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
   if (k >= 0)
      return rgb(r + ((255 - r) * k >> 8), g + ((255 - g) * k >> 8), b + ((255 - b) * k >> 8));
   return rgb(r + (r * k >> 8), g + (g * k >> 8), b + (b * k >> 8));
}

/* ---- the pictures ---- */

#define T256 (256 * 256)
static uint32_t px_parquet[T256], px_planks[T256], px_tiles[128 * 128], px_pattern[T256], px_subway[128 * 128];
static uint32_t px_plaster[128 * 128], px_fabric[64 * 64], px_knit[64 * 64], px_rug[T256], px_rug2[T256];
static uint32_t px_grass[128 * 128], px_concrete[128 * 128], px_brick[128 * 128], px_facade[T256], px_wood[128 * 128];
static uint32_t px_marble[128 * 128], px_metal[64 * 64], px_leaves[64 * 64], px_cork[64 * 64], px_shadow[64 * 64];

/* Oak parquet: blocks of 64 with four boards of 16, turned every other block (a basket weave), each board its own shade. */
static void make_parquet(void)
{
   int32_t x, y;
   for (y = 0; y < 256; y++)
      for (x = 0; x < 256; x++)
      {
         int32_t bx = x / 64, by = y / 64, turned = (bx + by) & 1;
         int32_t along = turned ? y % 64 : x % 64, across = turned ? x % 64 : y % 64, board = across / 16, edge = across % 16;
         uint32_t h = hash3(bx * 4 + board, by, turned ? 7 : 8);
         int32_t base = 150 + (int32_t)(h % 36) - 18;
         int32_t grain = (fbm((along * 4 + (int32_t)(h & 127)) % 256, (edge * 3 + (int32_t)(h >> 8 & 127)) % 256, 256, 32, 11) - 128) / 5;
         int32_t r = base + 62 + grain, g = base + 14 + grain, b = base - 48 + grain;
         if (edge == 0 || along == 0)
            r -= 64, g -= 56, b -= 46;
         else if (edge == 1 || along == 1)
            r -= 16, g -= 14, b -= 12;
         else if (edge == 15)
            r += 8, g += 7, b += 5;
         px_parquet[y * 256 + x] = rgb(r, g, b);
      }
}

/* Long boards (256 x 32), joints staggered. */
static void make_planks(void)
{
   int32_t x, y;
   for (y = 0; y < 256; y++)
      for (x = 0; x < 256; x++)
      {
         int32_t row = y / 32, off = (int32_t)(hash3(row, 0, 3) % 256), along = (x + off) % 256, len = 128 + (row & 1) * 64;
         int32_t board = row * 4 + (x + off) / len;
         uint32_t h = hash3(board, row, 5);
         int32_t base = 140 + (int32_t)(h % 30), grain = (fbm(x, (y * 6 + (int32_t)(h & 255)) % 256, 256, 64, 9) - 128) / 5;
         int32_t r = base + 50 + grain, g = base + grain, b = base - 55 + grain;
         if (y % 32 == 0 || (along % len) == 0)
            r -= 60, g -= 55, b -= 45;
         px_planks[y * 256 + x] = rgb(r, g, b);
      }
}

/* Square tiles of 32 with grout, each slightly different. */
static void make_tiles(void)
{
   int32_t x, y;
   for (y = 0; y < 128; y++)
      for (x = 0; x < 128; x++)
      {
         int32_t tx = x % 32, ty = y % 32, h = (int32_t)(hash3(x / 32, y / 32, 21) % 16) - 8;
         int32_t n = (fbm(x, y, 128, 16, 4) - 128) / 16;
         if (tx < 2 || ty < 2)
            px_tiles[y * 128 + x] = rgb(170 + n, 168 + n, 160 + n);
         else
         {
            int32_t edge = (tx == 2 || ty == 2) ? 12 : (tx == 31 || ty == 31) ? -10 : 0;
            px_tiles[y * 128 + x] = rgb(232 + h + n + edge, 230 + h + n + edge, 224 + h + n + edge);
         }
      }
}

/* Patterned cement tiles: each 64 x 64 tile a quarter of a star and rings, in three colors. */
static void make_pattern(void)
{
   static const uint32_t ink[3] = { 0xff1f5c7a, 0xffd9893b, 0xffefe6d6 };
   int32_t x, y;
   for (y = 0; y < 256; y++)
      for (x = 0; x < 256; x++)
      {
         int32_t tx = x % 64, ty = y % 64;
         int32_t dx = tx < 32 ? tx : 63 - tx, dy = ty < 32 ? ty : 63 - ty; /* distance to the tile's edge, mirrored */
         int32_t cx = tx - 32, cy = ty - 32, r2 = cx * cx + cy * cy, ring = r2 > 18 * 18 && r2 < 23 * 23;
         int32_t corner = dx * dx + dy * dy < 14 * 14, star = hd_abs(cx) + hd_abs(cy) < 18 || ((hd_abs(cx) < 4 || hd_abs(cy) < 4) && r2 < 18 * 18);
         uint32_t c = ink[2];
         int32_t n = (fbm(x, y, 256, 32, 8) - 128) / 14;
         if (corner)
            c = ink[1];
         if (ring)
            c = ink[0];
         if (star)
            c = (hd_abs(cx) + hd_abs(cy) < 8) ? ink[1] : ink[0];
         if (tx == 0 || ty == 0)
            c = 0xffb8b0a2;
         px_pattern[y * 256 + x] = shade(c, n * 4);
      }
}

/* White wall tiles 32 x 16, offset every row. */
static void make_subway(void)
{
   int32_t x, y;
   for (y = 0; y < 128; y++)
      for (x = 0; x < 128; x++)
      {
         int32_t row = y / 16, xx = (x + (row & 1) * 16) % 32, yy = y % 16;
         int32_t n = (int32_t)(hash3((x + (row & 1) * 16) / 32, row, 31) % 10);
         if (xx == 0 || yy == 0)
            px_subway[y * 128 + x] = rgb(190, 192, 192);
         else
         {
            int32_t hl = yy == 1 ? 14 : yy == 15 ? -12 : 0;
            px_subway[y * 128 + x] = rgb(238 + n + hl, 240 + n + hl, 238 + n + hl);
         }
      }
}

static void make_plaster(void)
{
   int32_t x, y;
   for (y = 0; y < 128; y++)
      for (x = 0; x < 128; x++)
      {
         int32_t n = 236 + (fbm(x, y, 128, 32, 13) - 128) / 10 + (int32_t)(hash3(x, y, 2) % 5);
         px_plaster[y * 128 + x] = rgb(n, n, n);
      }
}

static void make_fabric(void)
{
   int32_t x, y;
   for (y = 0; y < 64; y++)
      for (x = 0; x < 64; x++)
      {
         int32_t warp = ((x / 2 + y / 2) & 1) ? 5 : -4, n = (int32_t)(hash3(x, y, 17) % 8);
         int32_t v = 205 + warp + n + (fbm(x, y, 64, 16, 19) - 128) / 8;
         px_fabric[y * 64 + x] = rgb(v, v, v);
      }
}

static void make_knit(void)
{
   int32_t x, y;
   for (y = 0; y < 64; y++)
      for (x = 0; x < 64; x++)
      {
         int32_t xx = x % 8, yy = (y + (xx < 4 ? xx : 7 - xx)) % 6;
         int32_t v = 220 - yy * 5 + (int32_t)(hash3(x, y, 23) % 6) - (xx == 0 || xx == 4 ? 10 : 0);
         px_knit[y * 64 + x] = rgb(v, v, v);
      }
}

/* A rug: border bands, a field with small motifs and a medallion. */
static void make_rug(void)
{
   int32_t x, y;
   for (y = 0; y < 256; y++)
      for (x = 0; x < 256; x++)
      {
         int32_t e = hd_min(hd_min(x, 255 - x), hd_min(y, 255 - y)), cx = x - 128, cy = y - 128;
         int32_t d = hd_abs(cx) + hd_abs(cy), n = (int32_t)(hash3(x, y, 41) % 18) - 9;
         uint32_t c = 0xff9c3b34;
         if (e < 6)
            c = 0xff2a3550;
         else if (e < 22)
            c = ((x / 8 + y / 8) & 1) ? 0xffe3c79a : 0xff2a3550;
         else if (e < 26)
            c = 0xffe3c79a;
         else if (d < 40)
            c = d < 18 ? 0xffe3c79a : 0xff2a3550;
         else if (d < 46)
            c = 0xffe3c79a;
         else if (((x + 12) % 24 < 4 && (y + 12) % 24 < 4))
            c = 0xffd6a25a;
         px_rug[y * 256 + x] = shade(c, n * 3);
      }
}

/* A kilim: stripes with diamonds. */
static void make_rug2(void)
{
   static const uint32_t band[6] = { 0xff3b6e8f, 0xffefe3c8, 0xffcf6a3a, 0xff2d2d3a, 0xffefe3c8, 0xff8da35b };
   int32_t x, y;
   for (y = 0; y < 256; y++)
      for (x = 0; x < 256; x++)
      {
         int32_t b = (y / 16) % 6, yy = y % 16 - 8, xx = (x % 32) - 16, n = (int32_t)(hash3(x, y, 43) % 16) - 8;
         uint32_t c = band[b];
         if ((b == 1 || b == 4) && hd_abs(xx) + hd_abs(yy) * 2 < 12)
            c = band[(b + 2) % 6];
         px_rug2[y * 256 + x] = shade(c, n * 3);
      }
}

static void make_grass(void)
{
   int32_t x, y;
   for (y = 0; y < 128; y++)
      for (x = 0; x < 128; x++)
      {
         int32_t n = (fbm(x, y, 128, 32, 51) - 128) / 4, s = (int32_t)(hash3(x, y, 52) % 30);
         px_grass[y * 128 + x] = rgb(78 + n + s / 3, 128 + n + s, 62 + n / 2);
      }
}

static void make_concrete(void)
{
   int32_t x, y;
   for (y = 0; y < 128; y++)
      for (x = 0; x < 128; x++)
      {
         int32_t n = (fbm(x, y, 128, 32, 61) - 128) / 6 + (int32_t)(hash3(x, y, 62) % 12);
         if (x == 0 || y == 0)
            n -= 30;
         px_concrete[y * 128 + x] = rgb(170 + n, 168 + n, 164 + n);
      }
}

static void make_brick(void)
{
   int32_t x, y;
   for (y = 0; y < 128; y++)
      for (x = 0; x < 128; x++)
      {
         int32_t row = y / 16, xx = (x + (row & 1) * 16) % 32, yy = y % 16;
         int32_t h = (int32_t)(hash3((x + (row & 1) * 16) / 32, row, 71) % 40) - 20, n = (fbm(x, y, 128, 16, 72) - 128) / 10;
         if (xx < 2 || yy < 2)
            px_brick[y * 128 + x] = rgb(196 + n, 188 + n, 176 + n);
         else
            px_brick[y * 128 + x] = rgb(168 + h + n, 84 + h / 2 + n, 62 + n);
      }
}

/* A city building's face: floors of windows, some lit, some with blinds. */
static void make_facade(void)
{
   int32_t x, y;
   for (y = 0; y < 256; y++)
      for (x = 0; x < 256; x++)
      {
         int32_t wx = x % 32, wy = y % 42, cell = (x / 32) + (y / 42) * 8;
         uint32_t h = hash3(cell, 0, 81), c;
         int32_t n = (fbm(x, y, 256, 32, 82) - 128) / 12;
         c = rgb(214 + n, 200 + n, 178 + n);
         if (wx >= 8 && wx < 26 && wy >= 8 && wy < 32)
         {
            if (h % 5 == 0)
               c = rgb(255, 214, 140);           /* lit */
            else if (h % 5 == 1)
               c = rgb(200, 190, 170 - (wy % 4) * 6); /* blinds */
            else
               c = rgb(70 + wy, 92 + wy, 120 + wy); /* the sky in the glass */
            if (wx == 17)
               c = rgb(240, 240, 236);
         }
         else if (wy >= 32 && wy < 35 && wx >= 6 && wx < 28)
            c = rgb(236, 232, 224); /* sill */
         px_facade[y * 256 + x] = c;
      }
}

static void make_wood(void)
{
   int32_t x, y;
   for (y = 0; y < 128; y++)
      for (x = 0; x < 128; x++)
      {
         int32_t g = fbm(x / 4, y * 2 % 128, 128, 32, 91), ring = (g * 5 / 8) % 32;
         int32_t v = 150 + (g - 128) / 4 - (ring < 4 ? 18 : 0);
         px_wood[y * 128 + x] = rgb(v + 40, v, v - 45);
      }
}

static void make_marble(void)
{
   int32_t x, y;
   for (y = 0; y < 128; y++)
      for (x = 0; x < 128; x++)
      {
         int32_t n = fbm(x, y, 128, 32, 101), vein = hd_abs(((x + n / 2) % 64) - 32);
         int32_t v = 238 - (vein < 2 ? 50 : vein < 4 ? 20 : 0) + (n - 128) / 20;
         px_marble[y * 128 + x] = rgb(v, v, v + 2);
      }
}

static void make_metal(void)
{
   int32_t x, y;
   for (y = 0; y < 64; y++)
      for (x = 0; x < 64; x++)
      {
         int32_t v = 200 + (int32_t)(hash3(0, y, 111) % 30) + (int32_t)(hash3(x / 8, y, 112) % 8);
         px_metal[y * 64 + x] = rgb(v, v, v);
      }
}

static void make_leaves(void)
{
   int32_t x, y;
   for (y = 0; y < 64; y++)
      for (x = 0; x < 64; x++)
      {
         int32_t n = fbm(x, y, 64, 16, 121), v = 150 + (n - 128) / 2 + (int32_t)(hash3(x, y, 122) % 30);
         px_leaves[y * 64 + x] = rgb(v, v, v);
      }
}

static void make_cork(void)
{
   int32_t x, y;
   for (y = 0; y < 64; y++)
      for (x = 0; x < 64; x++)
      {
         int32_t d = (int32_t)(hash3(x, y, 131) % 100), n = (fbm(x, y, 64, 8, 132) - 128) / 6;
         px_cork[y * 64 + x] = d < 12 ? rgb(120 + n, 80 + n, 44 + n) : rgb(196 + n, 150 + n, 102 + n);
      }
}

/* Black, its alpha falling smoothly from the middle to the edge: a soft round shadow. */
static void make_shadow(void)
{
   int32_t x, y;
   for (y = 0; y < 64; y++)
      for (x = 0; x < 64; x++)
      {
         int32_t dx = x * 2 - 63, dy = y * 2 - 63, d2 = dx * dx + dy * dy, a = 0;
         if (d2 < 63 * 63)
         {
            a = 255 - d2 * 255 / (63 * 63);
            a = a * a / 255 * 3 / 4;
         }
         px_shadow[y * 64 + x] = (uint32_t)a << 24;
      }
}

static int32_t ids[TEX3D_COUNT];
static int built;

const int32_t *tex3d_build(void)
{
   if (built)
      return ids;
   make_parquet();
   make_planks();
   make_tiles();
   make_pattern();
   make_subway();
   make_plaster();
   make_fabric();
   make_knit();
   make_rug();
   make_rug2();
   make_grass();
   make_concrete();
   make_brick();
   make_facade();
   make_wood();
   make_marble();
   make_metal();
   make_leaves();
   make_cork();
   make_shadow();
   ids[TEX3D_PARQUET] = r3d_tex_add(8, 8, px_parquet);
   ids[TEX3D_PLANKS] = r3d_tex_add(8, 8, px_planks);
   ids[TEX3D_TILES] = r3d_tex_add(7, 7, px_tiles);
   ids[TEX3D_PATTERN] = r3d_tex_add(8, 8, px_pattern);
   ids[TEX3D_SUBWAY] = r3d_tex_add(7, 7, px_subway);
   ids[TEX3D_PLASTER] = r3d_tex_add(7, 7, px_plaster);
   ids[TEX3D_FABRIC] = r3d_tex_add(6, 6, px_fabric);
   ids[TEX3D_KNIT] = r3d_tex_add(6, 6, px_knit);
   ids[TEX3D_RUG] = r3d_tex_add(8, 8, px_rug);
   ids[TEX3D_RUG2] = r3d_tex_add(8, 8, px_rug2);
   ids[TEX3D_GRASS] = r3d_tex_add(7, 7, px_grass);
   ids[TEX3D_CONCRETE] = r3d_tex_add(7, 7, px_concrete);
   ids[TEX3D_BRICK] = r3d_tex_add(7, 7, px_brick);
   ids[TEX3D_FACADE] = r3d_tex_add(8, 8, px_facade);
   ids[TEX3D_WOOD] = r3d_tex_add(7, 7, px_wood);
   ids[TEX3D_MARBLE] = r3d_tex_add(7, 7, px_marble);
   ids[TEX3D_METAL] = r3d_tex_add(6, 6, px_metal);
   ids[TEX3D_LEAVES] = r3d_tex_add(6, 6, px_leaves);
   ids[TEX3D_CORK] = r3d_tex_add(6, 6, px_cork);
   ids[TEX3D_ROUND_SHADOW] = r3d_tex_add(6, 6, px_shadow);
   built = 1;
   return ids;
}

/* ---- posters ---- */

#define POSTERS_MAX 24
#define POSTER 256 /* texels a side: sharp on a TV seen up close */
static uint32_t poster_px[POSTERS_MAX][POSTER * POSTER];
static int32_t poster_count;

int32_t tex3d_poster(int32_t kind, uint32_t seed)
{
   static const uint32_t pal[6][3] = {
      { 0xfff2e8cf, 0xffe07a5f, 0xff3d405b }, { 0xff1d3557, 0xffe63946, 0xfff1faee }, { 0xfff4d35e, 0xff0d3b66, 0xffee964b },
      { 0xff2a9d8f, 0xffe9c46a, 0xff264653 }, { 0xfffefae0, 0xff606c38, 0xffbc6c25 }, { 0xff0b090a, 0xff66fcf1, 0xffc5c6c7 },
   };
   uint32_t *p;
   const uint32_t *c;
   int32_t x, y;
   if (poster_count >= POSTERS_MAX)
      return -1;
   p = poster_px[poster_count];
   c = pal[seed % 6];
   for (y = 0; y < POSTER; y++)
      for (x = 0; x < POSTER; x++)
      {
         /* drawn on a 64-step design scaled 4 times, with the edges between colors kept sharp */
         uint32_t v = c[0];
         int32_t dx = x - 128, dy = y - 104 - (int32_t)(seed % 7) * 4;
         switch (kind % 4)
         {
         case 0: /* a sun over two rows of hills, the sky fading */
            v = gfx_mix(c[0], 0xffffffff, 60 - y * 60 / POSTER) | 0xff000000u;
            if (dx * dx + dy * dy < 52 * 52)
               v = c[1];
            if (y > 150 + (int32_t)((hd_sin((x * 12 + (int32_t)seed * 37) & 4095) * 14) >> 14))
               v = gfx_mix(c[2], c[0], 70) | 0xff000000u;
            if (y > 172 + (int32_t)((hd_sin((x * 20 + 900 + (int32_t)seed * 53) & 4095) * 18) >> 14))
               v = c[2];
            break;
         case 1: /* bold stripes */
            if (((x + y + (int32_t)seed * 4) / 32) % 3 == 1)
               v = c[1];
            else if (((x + y + (int32_t)seed * 4) / 32) % 3 == 2)
               v = c[2];
            break;
         case 2: /* a terminal with lines of code, a prompt and a cursor */
            v = c[2] == 0xff264653 ? 0xff1b1f24 : 0xff15181d;
            if (y < 14)
               v = 0xff2b2f36; /* the window's title bar */
            else if (y > 24 && y < 236 && (y % 12) < 7)
            {
               int32_t row = y / 12, indent = (int32_t)(hash3(row, 2, seed) % 4) * 12, len = 20 + (int32_t)(hash3(row, 0, seed) % 170);
               if (x > 16 + indent && x < 16 + indent + len && ((x - indent) / 6) % 9 != 8)
                  v = (hash3(row, 1, seed) % 3) == 0 ? 0xff7fd18b : (hash3(row, 1, seed) % 3) == 1 ? 0xffe0c070 : 0xff8ab4f8;
            }
            if (y < 14 && x > 8 && x < 44 && (x - 8) % 12 < 8 && y > 3 && y < 11)
               v = (x - 8) / 12 == 0 ? 0xffff5f57 : (x - 8) / 12 == 1 ? 0xfffebc2e : 0xff28c840;
            break;
         default: /* circles */
            if (((dx * dx + dy * dy) / 960) % 2 == 0)
               v = c[1];
            if (hd_abs(dx) < 12)
               v = c[2];
            break;
         }
         if (kind % 4 != 2)
         {
            /* the paper's grain */
            int32_t n = (int32_t)(hash3(x, y, seed) % 13) - 6;
            v = 0xff000000u | (uint32_t)hd_clamp((int32_t)(v >> 16 & 255) + n, 0, 255) << 16 | (uint32_t)hd_clamp((int32_t)(v >> 8 & 255) + n, 0, 255) << 8 |
                (uint32_t)hd_clamp((int32_t)(v & 255) + n, 0, 255);
         }
         if (x < 8 || y < 8 || x > POSTER - 9 || y > POSTER - 9)
            v = kind % 4 == 2 ? 0xff2b2f36 : 0xfffaf7f0; /* the paper's white edge, the screen's frame */
         p[y * POSTER + x] = v;
      }
   return r3d_tex_add(8, 8, poster_px[poster_count++]);
}

void tex3d_terminal(int32_t id, uint32_t seed, int32_t scroll, int32_t on)
{
   uint32_t *p = r3d_tex_pixels(id);
   int32_t x, y;
   if (!p)
      return;
   for (y = 0; y < POSTER; y++)
      for (x = 0; x < POSTER; x++)
      {
         uint32_t v = 0xff15181d;
         if (!on)
         {
            /* asleep: dark, a slow logo drifting */
            int32_t cx = 128 + ((hd_sin((scroll * 3 + (int32_t)seed * 500) & 4095) * 60) >> 14), cy = 128 + ((hd_cos((scroll * 2) & 4095) * 50) >> 14);
            int32_t dx = x - cx, dy = y - cy;
            v = 0xff0b0d10;
            if (dx * dx + dy * dy < 14 * 14 && dx * dx + dy * dy > 9 * 9)
               v = 0xff2e5e8a;
         }
         else if (y < 14)
         {
            v = 0xff2b2f36; /* the title bar */
            if (x > 8 && x < 44 && (x - 8) % 12 < 8 && y > 3 && y < 11)
               v = (x - 8) / 12 == 0 ? 0xffff5f57 : (x - 8) / 12 == 1 ? 0xfffebc2e : 0xff28c840;
         }
         else
         {
            /* lines of code going up as the work goes on (scroll in pixels) */
            int32_t yy = y - 14 + scroll, row = yy / 12;
            if ((yy % 12) < 7 && x > 8 && x < POSTER - 8)
            {
               int32_t indent = (int32_t)(hash3(row, 2, seed) % 4) * 12, len = 20 + (int32_t)(hash3(row, 0, seed) % 170);
               if (x > 8 + indent && x < 8 + indent + len && ((x - indent) / 6) % 9 != 8)
                  v = (hash3(row, 1, seed) % 3) == 0 ? 0xff7fd18b : (hash3(row, 1, seed) % 3) == 1 ? 0xffe0c070 : 0xff8ab4f8;
            }
            /* the cursor at the bottom, blinking */
            if (y > POSTER - 22 && y < POSTER - 12 && x > 12 && x < 20 && (scroll / 20) % 2 == 0)
               v = 0xffe8e8e8;
         }
         if (x < 4 || y < 4 || x > POSTER - 5 || y > POSTER - 5)
            v = 0xff2b2f36;
         p[y * POSTER + x] = v;
      }
}

