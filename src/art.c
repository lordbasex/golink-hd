/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The demo's pixel art, made in code so the core plays with no content.
 * Characters are drawn as text (one letter per pixel, each letter a color of
 * the drawing's palette, '.' transparent); tiles and coins are computed.
 * Players 2 to 4 are the same drawing with other shirt colors (a palette swap).
 */
#include <string.h>
#include "hd.h"

#define OUTLINE 0xff1a1020u

const uint32_t hd_player_color[MAX_PLAYERS] = { 0xffd83a3au, 0xff3a78e0u, 0xff3ab84au, 0xffe8b820u };

hd_image hd_hero[MAX_PLAYERS][HERO_FRAMES];
hd_image hd_enemy_img[ENEMY_FRAMES];
hd_image hd_tiles[TL_COUNT];
hd_image hd_coin[COIN_FRAMES];
hd_image hd_check[2];
hd_image hd_flag;

static uint32_t hero_px[MAX_PLAYERS][HERO_FRAMES][16 * 24];
static uint32_t enemy_px[ENEMY_FRAMES][16 * 16];
static uint32_t tile_px[TL_COUNT][16 * 16];
static uint32_t coin_px[COIN_FRAMES][16 * 16];
static uint32_t check_px[2][16 * 32];
static uint32_t flag_px[32 * 64];

/* The head and body, shared by every frame (rows 0-17). */
static const char *hero_top[18] = {
   "................",
   ".....kkkkkk.....",
   "....kssssssk....",
   "...ksssssssskk..",
   "...kSSSSSSSSSSk.",
   "...khhfffffk....",
   "...khfffwkfk....",
   "...khfffwkfk....",
   "....kfffffffk...",
   "....kFfffkkk....",
   ".....kFfffk.....",
   "....kkkkkkkk....",
   "...kssssssssk...",
   "..kfksssssSkfk..",
   "..kfkssssSSkfk..",
   "...kkSSSSSSkk...",
   "....kbbbbbbk....",
   "....kbbbbbbk....",
};

/* The legs of each frame (rows 18-23). */
static const char *hero_legs[HERO_FRAMES][6] = {
   { "....kbbkkbbk....", "....kbbkkbbk....", "....kbbkkbbk....", "....kbbkkbbk....", "....koookoook...", "....kkkkkkkkk..." },
   { "....kbbkkbbk....", "...kbbk..kbbk...", "...kbbk...kbbk..", "..kbbk....kbbk..", "..kooook..kooook", "..kkkkkk..kkkkkk" },
   { "....kbbkkbbk....", "....kbbkkbbk....", ".....kbbbbk.....", ".....kbbbbk.....", "....kooooook....", "....kkkkkkkk...." },
   { "....kbbkkbbk....", "...kbbkkkkbbk...", "..kbbk....kbbk..", "..kook....kook..", "..kkkk....kkkk..", "................" },
};

static const char *enemy_art[ENEMY_FRAMES][16] = {
   { "................", "................", ".....kkkkkk.....", "...kkggggggkk...",
     "..kggggggggggk..", ".kggwwkggwwkggk.", ".kggwkkggwkkggk.", ".kggggggggggggk.",
     ".kGggggggggggGk.", ".kGGggggggggGGk.", "..kGGGGGGGGGGk..", "...kkkkkkkkkk...",
     "...kyyk..kyyk...", "..kyyyk..kyyyk..", "..kkkk....kkkk..", "................" },
   { "................", "................", ".....kkkkkk.....", "...kkggggggkk...",
     "..kggggggggggk..", ".kggwwkggwwkggk.", ".kggwkkggwkkggk.", ".kggggggggggggk.",
     ".kGggggggggggGk.", ".kGGggggggggGGk.", "..kGGGGGGGGGGk..", "...kkkkkkkkkk...",
     "....kyyk..kyyk..", "....kyyyk.kyyyk.", "....kkkk..kkkk..", "................" },
   { "................", "................", "................", "................",
     "................", "................", "................", "................",
     "................", "................", "..kkkkkkkkkkkk..", ".kgkgkggggkgkgk.",
     ".kGGGGGGGGGGGGk.", "..kkkkkkkkkkkk..", ".kyyk......kyyk.", ".kkkk......kkkk." },
};

static uint32_t shade(uint32_t c, int32_t num, int32_t den)
{
   uint32_t r = ((c >> 16) & 0xff) * (uint32_t)num / (uint32_t)den;
   uint32_t g = ((c >> 8) & 0xff) * (uint32_t)num / (uint32_t)den;
   uint32_t b = (c & 0xff) * (uint32_t)num / (uint32_t)den;
   return 0xff000000u | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
}

/* A letter's color, or 0 (transparent). */
static uint32_t ink(char c, uint32_t shirt)
{
   switch (c)
   {
   case 'k': return OUTLINE;
   case 's': return shirt;
   case 'S': return shade(shirt, 7, 10);
   case 'f': return 0xfff2b48cu;
   case 'F': return 0xffc98a64u;
   case 'w': return 0xffffffffu;
   case 'h': return 0xff5a3420u;
   case 'b': return 0xff3048a0u;
   case 'o': return 0xff6b3a22u;
   case 'g': return 0xff8a3cc8u;
   case 'G': return 0xff5e2390u;
   case 'y': return 0xffe8a030u;
   default: return 0;
   }
}

static int paint_rows(uint32_t *px, int32_t w, int32_t y0, const char *const *rows, int32_t n, uint32_t shirt)
{
   int32_t y, x;
   for (y = 0; y < n; y++)
   {
      if ((int32_t)strlen(rows[y]) != w)
         return 0;
      for (x = 0; x < w; x++)
         px[(y0 + y) * w + x] = ink(rows[y][x], shirt);
   }
   return 1;
}

/* A number from a cell's position, the same on every computer. */
static uint32_t hash2(int32_t x, int32_t y, uint32_t seed)
{
   uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
   h = (h ^ (h >> 13)) * 1274126177u;
   return h ^ (h >> 16);
}

static void make_tiles(void)
{
   int32_t x, y;
   for (y = 0; y < 16; y++)
      for (x = 0; x < 16; x++)
      {
         uint32_t n = hash2(x, y, 1) & 15;
         uint32_t dirt = n < 2 ? 0xff6e4424u : (n < 4 ? 0xffa06c40u : 0xff8a5a32u);
         uint32_t grass = (y < 4 || (y == 4 && (hash2(x, 0, 2) & 1))) ? (y == 0 ? 0xff7ad04eu : 0xff58b838u) : 0;
         int32_t row;
         tile_px[TL_GROUND][y * 16 + x] = dirt;
         tile_px[TL_GROUND_TOP][y * 16 + x] = grass ? grass : (y == 5 ? 0xff3c8a28u : dirt);
         /* bricks: rows of 8 px, every other row shifted half a brick */
         row = y / 8;
         if (y % 8 == 7 || (x + row * 8) % 16 == 15)
            tile_px[TL_BRICK][y * 16 + x] = 0xff70301au;
         else
            tile_px[TL_BRICK][y * 16 + x] = (y % 8 == 0) ? 0xffd06a40u : 0xffb8502cu;
         /* one-way platform: a plank on top, nothing under it */
         if (y < 6)
            tile_px[TL_PLATFORM][y * 16 + x] = (y == 0 || y == 5) ? 0xff6a3e1cu : ((x == 0 || x == 15) ? 0xff8a5a2cu : (y == 1 ? 0xffe0a860u : 0xffc8904cu));
         else
            tile_px[TL_PLATFORM][y * 16 + x] = 0;
      }
   for (x = 0; x < TL_COUNT; x++)
   {
      hd_tiles[x].w = 16;
      hd_tiles[x].h = 16;
      hd_tiles[x].px = tile_px[x];
   }
}

/* A spinning coin: an ellipse 12 px tall that gets narrower each frame. */
static void make_coins(void)
{
   static const int32_t widths[COIN_FRAMES] = { 12, 8, 3, 8 };
   int32_t f, x, y;
   for (f = 0; f < COIN_FRAMES; f++)
   {
      int32_t w = widths[f], h = 12;
      for (y = 0; y < 16; y++)
         for (x = 0; x < 16; x++)
         {
            /* doubled coordinates around the centre (16, 16) of a 32 x 32 grid */
            int32_t dx = 2 * x + 1 - 16, dy = 2 * y + 1 - 16;
            int32_t wi = w - 3, hi = h - 3; /* the face inside the rim */
            int64_t in = (int64_t)dx * dx * h * h + (int64_t)dy * dy * w * w;
            int64_t inner = (int64_t)dx * dx * hi * hi + (int64_t)dy * dy * wi * wi;
            uint32_t c = 0;
            if (in <= (int64_t)w * w * h * h)
            {
               c = 0xff6a4008u;
               if (w <= 4)
                  c = 0xfff8c838u;
               else if (inner <= (int64_t)wi * wi * hi * hi)
                  c = (dx < 0 && dy < 0) ? 0xfffff0a0u : (dx > 2 ? 0xffc88a10u : 0xfff8c838u);
            }
            coin_px[f][y * 16 + x] = c;
         }
      hd_coin[f].w = 16;
      hd_coin[f].h = 16;
      hd_coin[f].px = coin_px[f];
   }
}

/* A checkpoint post (16 x 32) with a small flag: grey before, green after. */
static void make_checks(void)
{
   int32_t i, x, y;
   for (i = 0; i < 2; i++)
   {
      uint32_t flag = i ? 0xff3ad85au : 0xff9aa0b0u;
      for (y = 0; y < 32; y++)
         for (x = 0; x < 16; x++)
         {
            uint32_t c = 0;
            if (x >= 3 && x <= 4 && y >= 2)
               c = x == 3 ? 0xffe8e8f0u : 0xffa8a8b8u;
            else if (y >= 3 && y < 11 && x >= 5 && x < 5 + (11 - y) + 4)
               c = flag;
            if (y < 3 && x >= 2 && x <= 5)
               c = 0xfff8c838u;
            check_px[i][y * 16 + x] = c;
         }
      hd_check[i].w = 16;
      hd_check[i].h = 32;
      hd_check[i].px = check_px[i];
   }
}

/* The goal: a pole four tiles tall with a big checkered flag (32 x 64). */
static void make_flag(void)
{
   int32_t x, y;
   for (y = 0; y < 64; y++)
      for (x = 0; x < 32; x++)
      {
         uint32_t c = 0;
         if (x >= 2 && x <= 4 && y >= 4)
            c = x == 2 ? 0xfff0f0f8u : (x == 3 ? 0xffc8c8d8u : 0xff8a8aa0u);
         else if (y < 5 && x >= 1 && x <= 5 && (x - 3) * (x - 3) + (y - 2) * (y - 2) <= 5)
            c = 0xfff8c838u;
         else if (x >= 5 && x < 31 && y >= 6 && y < 24)
            c = (((x - 5) / 5 + (y - 6) / 6) & 1) ? OUTLINE : 0xfff8f8f8u;
         flag_px[y * 32 + x] = c;
      }
   hd_flag.w = 32;
   hd_flag.h = 64;
   hd_flag.px = flag_px;
}

int hd_art_build(void)
{
   int32_t p, f, ok = 1;
   for (p = 0; p < MAX_PLAYERS; p++)
      for (f = 0; f < HERO_FRAMES; f++)
      {
         ok &= paint_rows(hero_px[p][f], 16, 0, hero_top, 18, hd_player_color[p]);
         ok &= paint_rows(hero_px[p][f], 16, 18, hero_legs[f], 6, hd_player_color[p]);
         hd_hero[p][f].w = 16;
         hd_hero[p][f].h = 24;
         hd_hero[p][f].px = hero_px[p][f];
      }
   for (f = 0; f < ENEMY_FRAMES; f++)
   {
      ok &= paint_rows(enemy_px[f], 16, 0, enemy_art[f], 16, 0);
      hd_enemy_img[f].w = 16;
      hd_enemy_img[f].h = 16;
      hd_enemy_img[f].px = enemy_px[f];
   }
   make_tiles();
   make_coins();
   make_checks();
   make_flag();
   return ok;
}
