/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The engine's tests: the art is well formed, the same inputs give the same
 * frames and sound, a save state continues exactly where it was taken, bad
 * save states are refused or made safe, and the API (golink_hd.h) works as
 * a host uses it. Built with the address and undefined behaviour sanitizers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hd.h"
#ifndef HD_NO_3D
#include "r3d.h"
#include "room3d.h"
#include "play3d.h"
#include "model3d.h"
#include "games/remote/remote.h"
#endif
#include "pack.h"
#include "gfx.h"
#include "bones.h"
#include "path.h"
#include "text.h"

static int failures;

#define CHECK(cond)                                                       \
   do                                                                     \
   {                                                                      \
      if (!(cond))                                                        \
      {                                                                   \
         fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
         failures++;                                                      \
      }                                                                   \
   } while (0)

static uint32_t fnv(const void *data, size_t n, uint32_t h)
{
   const uint8_t *p = (const uint8_t *)data;
   size_t i;
   for (i = 0; i < n; i++)
      h = (h ^ p[i]) * 16777619u;
   return h;
}

/* A bot for player `port`: presses start, then runs right jumping now and then. */
static uint32_t bot(int32_t frame, int32_t port)
{
   uint32_t pad = 0;
   if (frame < 10 + port * 30)
      return 0;
   if (frame < 14 + port * 30)
      return PAD_START;
   pad = PAD_RIGHT | ((frame / 200) % 3 ? PAD_RUN : 0);
   if ((frame + port * 7) % 50 < 18)
      pad |= PAD_JUMP;
   return pad;
}

static void pads_at(int32_t frame, int32_t players, hd_input pads[MAX_PLAYERS])
{
   int32_t i;
   memset(pads, 0, sizeof(hd_input) * MAX_PLAYERS);
   for (i = 0; i < MAX_PLAYERS; i++)
      pads[i].buttons = i < players ? bot(frame, i) : 0;
}

static uint32_t run(hd_state *s, int32_t from, int32_t to, int32_t players, uint32_t *fb, int16_t *audio)
{
   uint32_t h = 2166136261u;
   hd_input pads[MAX_PLAYERS];
   int32_t f;
   for (f = from; f < to; f++)
   {
      pads_at(f, players, pads);
      hd_step(s, pads);
      hd_draw(s, fb);
      hd_mix(s, audio, 1);
      h = fnv(fb, sizeof(uint32_t) * HD_W * HD_H, h);
      h = fnv(audio, sizeof(int16_t) * HD_SAMPLES_PER_FRAME * 2, h);
   }
   return h;
}

static uint32_t fb[HD_OUT_MAX_W * HD_OUT_MAX_H];
static int16_t audio[HD_SAMPLES_PER_FRAME * 2];

static void test_art(void)
{
   CHECK(hd_art_build() == 1);
}

static void test_determinism(void)
{
   static hd_state a, b;
   uint32_t ha, hb;
   hd_reset(&a);
   hd_reset(&b);
   ha = run(&a, 0, 1500, 4, fb, audio);
   hb = run(&b, 0, 1500, 4, fb, audio);
   CHECK(ha == hb);
   CHECK(memcmp(&a, &b, sizeof a) == 0);
}

static void test_play(void)
{
   static hd_state s;
   int32_t i, coins = 0, far = 0;
   hd_reset(&s);
   run(&s, 0, 1500, 2, fb, audio);
   CHECK(s.phase != PH_TITLE);
   CHECK(s.p[0].active && s.p[1].active);
   CHECK(!s.p[2].active);
   for (i = 0; i < MAX_PLAYERS; i++)
   {
      coins += s.p[i].coins;
      far = hd_max(far, FX_INT(s.p[i].x));
   }
   printf("  play: after 1500 frames the players are at x=%d with %d coins\n", (int)far, (int)coins);
   CHECK(far > 400);
   CHECK(FX_INT(s.cam_x) > 0);
}

static void test_save_state(void)
{
   static hd_state a, b;
   static uint8_t save[HD_SAVE_SIZE];
   uint32_t ha, hb;
   hd_reset(&a);
   run(&a, 0, 700, 3, fb, audio);
   hd_save(&a, save);
   ha = run(&a, 700, 1400, 3, fb, audio);
   memset(&b, 0x5a, sizeof b);
   CHECK(hd_load(&b, save, HD_SAVE_SIZE) == 1);
   hb = run(&b, 700, 1400, 3, fb, audio);
   CHECK(ha == hb);
   CHECK(memcmp(&a, &b, sizeof a) == 0);
}

static void test_bad_save_states(void)
{
   static hd_state s, before;
   static uint8_t save[HD_SAVE_SIZE];
   uint32_t w, sum = 2166136261u, i;
   hd_reset(&s);
   run(&s, 0, 300, 1, fb, audio);
   before = s;
   hd_save(&s, save);

   save[4] ^= 1; /* another version */
   CHECK(hd_load(&s, save, HD_SAVE_SIZE) == 0);
   save[4] ^= 1;
   save[100] ^= 0xff; /* damaged */
   CHECK(hd_load(&s, save, HD_SAVE_SIZE) == 0);
   save[100] ^= 0xff;
   CHECK(hd_load(&s, save, HD_SAVE_SIZE - 1) == 0); /* cut short */
   CHECK(hd_load(&s, (const uint8_t *)"nope", 4) == 0);
   CHECK(memcmp(&s, &before, sizeof s) == 0); /* refused loads change nothing */

   /* garbage with a right checksum loads, and the game still runs safely */
   for (i = HD_SAVE_HEADER; i < HD_SAVE_SIZE; i++)
      save[i] = (uint8_t)(i * 2654435761u >> 24);
   for (i = HD_SAVE_HEADER; i < HD_SAVE_SIZE; i++)
      sum = (sum ^ save[i]) * 16777619u;
   w = sum;
   save[12] = (uint8_t)w;
   save[13] = (uint8_t)(w >> 8);
   save[14] = (uint8_t)(w >> 16);
   save[15] = (uint8_t)(w >> 24);
   CHECK(hd_load(&s, save, HD_SAVE_SIZE) == 1);
   run(&s, 300, 420, 4, fb, audio);
}

static uint8_t *slurp(const char *path, size_t *n)
{
   FILE *f = fopen(path, "rb");
   uint8_t *buf;
   long len;
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   len = ftell(f);
   fseek(f, 0, SEEK_SET);
   buf = (uint8_t *)malloc((size_t)len);
   if (fread(buf, 1, (size_t)len, f) != (size_t)len)
   {
      free(buf);
      buf = NULL;
   }
   fclose(f);
   *n = (size_t)len;
   return buf;
}

/* Runs frames and hashes only those after `from` (the title screen shows the game's own title). */
static uint32_t run_from(hd_state *s, int32_t to, int32_t from)
{
   uint32_t h = 2166136261u;
   hd_input pads[MAX_PLAYERS];
   int32_t f;
   for (f = 0; f < to; f++)
   {
      pads_at(f, 4, pads);
      hd_step(s, pads);
      hd_draw(s, fb);
      hd_mix(s, audio, 1);
      if (f >= from)
      {
         h = fnv(fb, sizeof(uint32_t) * HD_W * HD_H, h);
         h = fnv(audio, sizeof(int16_t) * HD_SAMPLES_PER_FRAME * 2, h);
      }
   }
   return h;
}

/*
 * The demo exported as a package (zip with deflate, PNGs saved by an image
 * library with real filters and a palette) plays exactly like the built-in one.
 */
static void test_package(void)
{
   static hd_state a, b;
   static uint8_t save[HD_SAVE_SIZE];
   uint8_t *pkg;
   size_t n, i;
   const char *err;
   uint32_t ha, hb;
   int fails = 0, loaded = 0;
   static uint8_t map_before[MAP_MAX_H][MAP_MAX_W];
   static uint32_t hero_before[16 * 24];

   pkg = slurp("tests/data/demo-deflate.glhd", &n);
   CHECK(pkg != NULL);
   if (!pkg)
      return;
   hd_content_builtin();
   hd_reset(&a);
   ha = run_from(&a, 1200, 20);
   CHECK(hd_content_load(pkg, n, &err) == 1);
   CHECK(strcmp(hd_title, "go-link HD demo") == 0);
   hd_reset(&b);
   hb = run_from(&b, 1200, 20);
   CHECK(ha == hb);
   CHECK(memcmp(&a, &b, sizeof a) == 0);

   /* a save state belongs to its game */
   hd_save(&b, save);
   CHECK(hd_load(&b, save, HD_SAVE_SIZE) == 1);
   memcpy(hero_before, hd_hero[3][3].px, sizeof hero_before);
   hd_content_builtin();
   CHECK(hd_load(&a, save, HD_SAVE_SIZE) == 0);

   /* every cut fails cleanly, and every damaged byte either fails cleanly or loads
      the very same game (a byte of a zip's unchecked fields, like a date); the
      sanitizers watch for any bad read */
   for (i = 0; i < n; i += 7)
      if (hd_content_load(pkg, i, &err))
         fails++;
   CHECK(fails == 0);
   CHECK(hd_content_load(pkg, n, &err) == 1);
   memcpy(map_before, hd_map, sizeof map_before);
   for (i = 0; i < n; i++)
   {
      pkg[i] ^= 0x5a;
      if (hd_content_load(pkg, n, &err))
      {
         loaded++;
         if (memcmp(map_before, hd_map, sizeof map_before) != 0 || memcmp(hero_before, hd_hero[3][3].px, sizeof hero_before) != 0)
            fails++;
      }
      pkg[i] ^= 0x5a;
   }
   printf("  package: %u of %u damaged copies loaded, all the same game\n", (unsigned)loaded, (unsigned)n);
   CHECK(fails == 0);
   CHECK(hd_content_load(pkg, n, &err) == 1); /* the buffer was restored */
   hd_content_builtin();
   free(pkg);
}

static void test_json(void)
{
   const char *err;
   static const char text[] = "{\"a\": [1, -2, \"x\\u00e9\"], \"b\": {\"c\": true}}";
   json *j = hd_json_parse(text, sizeof text - 1, &err);
   CHECK(j != NULL);
   if (j)
   {
      CHECK(hd_json_at(hd_json_get(j, "a"), 1)->num == -2);
      CHECK(strcmp(hd_json_at(hd_json_get(j, "a"), 2)->str, "x\xc3\xa9") == 0);
      CHECK(hd_json_get(hd_json_get(j, "b"), "c")->num == 1);
      hd_json_free(j);
   }
   CHECK(hd_json_parse("1.5", 3, &err) == NULL);
   CHECK(hd_json_parse("[1,", 3, &err) == NULL);
   CHECK(hd_json_parse("{\"a\" 1}", 7, &err) == NULL);
   CHECK(hd_json_parse("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]", 68, &err) == NULL);
}

static void test_sha256(void)
{
   uint8_t out[32];
   static const uint8_t abc[32] = { 0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                                    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad };
   hd_sha256((const uint8_t *)"abc", 3, out);
   CHECK(memcmp(out, abc, 32) == 0);
}

/* A zip of stored files, for packages made by the tests. */
/* A file of a test package: text, or len bytes when len is set. */
typedef struct { const char *name; const char *text; size_t len; } zfile;

static void le16(uint8_t **p, uint32_t v) { *(*p)++ = (uint8_t)v; *(*p)++ = (uint8_t)(v >> 8); }
static void le32(uint8_t **p, uint32_t v) { le16(p, v & 0xffff); le16(p, v >> 16); }

static size_t make_zip(uint8_t *out, const zfile *files, int n)
{
   uint8_t *p = out;
   uint32_t offs[8], at, cd;
   int i;
   for (i = 0; i < n; i++)
   {
      uint32_t len = (uint32_t)(files[i].len ? files[i].len : strlen(files[i].text)), nl = (uint32_t)strlen(files[i].name);
      offs[i] = (uint32_t)(p - out);
      le32(&p, 0x04034b50u); le16(&p, 20); le16(&p, 0); le16(&p, 0); le16(&p, 0); le16(&p, 0);
      le32(&p, hd_crc32((const uint8_t *)files[i].text, len)); le32(&p, len); le32(&p, len);
      le16(&p, nl); le16(&p, 0);
      memcpy(p, files[i].name, nl); p += nl;
      memcpy(p, files[i].text, len); p += len;
   }
   cd = (uint32_t)(p - out);
   for (i = 0; i < n; i++)
   {
      uint32_t len = (uint32_t)(files[i].len ? files[i].len : strlen(files[i].text)), nl = (uint32_t)strlen(files[i].name);
      le32(&p, 0x02014b50u); le16(&p, 20); le16(&p, 20); le16(&p, 0); le16(&p, 0); le16(&p, 0); le16(&p, 0);
      le32(&p, hd_crc32((const uint8_t *)files[i].text, len)); le32(&p, len); le32(&p, len);
      le16(&p, nl); le16(&p, 0); le16(&p, 0); le16(&p, 0); le16(&p, 0); le32(&p, 0); le32(&p, offs[i]);
      memcpy(p, files[i].name, nl); p += nl;
   }
   at = (uint32_t)(p - out);
   le32(&p, 0x06054b50u); le16(&p, 0); le16(&p, 0); le16(&p, (uint32_t)n); le16(&p, (uint32_t)n);
   le32(&p, at - cd); le32(&p, cd); le16(&p, 0);
   return (size_t)(p - out);
}

/* A level of w x h cells with ground in its last three rows, as JSON. */
static void flat_level(char *out, size_t cap, int w, int h)
{
   int x, y;
   size_t n = (size_t)snprintf(out, cap, "{\"width\": %d, \"height\": %d, \"start\": [2, %d], \"rows\": [", w, h, h - 4);
   for (y = 0; y < h; y++)
   {
      out[n++] = '"';
      for (x = 0; x < w; x++)
         out[n++] = y >= h - 3 ? '#' : '.';
      out[n++] = '"';
      if (y + 1 < h)
         out[n++] = ',';
   }
   snprintf(out + n, cap - n, "]}");
}

/* Up to 8 players, a vertical screen, the left stick. */
static void test_players_screens_sticks(void)
{
   static uint8_t zip[200000];
   static char level[100000];
   static hd_state s;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, i, x0;
   zfile files[2] = {{0}};

   flat_level(level, sizeof level, 60, 24);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 1, \"title\": \"Eight\", \"players\": 8, \"level\": \"level.json\"}";
   files[1].name = "level.json";
   files[1].text = level;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   CHECK(hd_players == 8 && HD_W == 640 && HD_H == 360);
   hd_reset(&s);
   for (f = 0; f < 120; f++)
   {
      memset(in, 0, sizeof in);
      for (i = 0; i < MAX_PLAYERS; i++)
         if (f >= 10 + i * 8 && f < 13 + i * 8)
            in[i].buttons = PAD_START;
      hd_step(&s, in);
      hd_draw(&s, fb);
   }
   for (i = 0; i < MAX_PLAYERS; i++)
      CHECK(s.p[i].active);

   /* the stick walks right, with no D-pad */
   x0 = FX_INT(s.p[0].x);
   for (f = 0; f < 60; f++)
   {
      memset(in, 0, sizeof in);
      in[0].lx = 30000;
      hd_step(&s, in);
   }
   CHECK(FX_INT(s.p[0].x) > x0 + 40);

   /* a vertical game: 360 x 640, a level as narrow as the screen */
   flat_level(level, sizeof level, 23, 40);
   files[0].text = "{\"format\": 1, \"title\": \"Tall\", \"screen\": \"9:16\", \"players\": 2, \"level\": \"level.json\"}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   CHECK(HD_W == 360 && HD_H == 640 && hd_players == 2);
   hd_reset(&s);
   for (f = 0; f < 200; f++)
   {
      memset(in, 0, sizeof in);
      for (i = 0; i < MAX_PLAYERS; i++)
         in[i].buttons = f == 10 ? PAD_START : (f > 20 ? PAD_RIGHT | ((f / 30) % 2 ? PAD_JUMP : 0) : 0);
      hd_step(&s, in);
      hd_draw(&s, fb);
   }
   CHECK(s.p[0].active && s.p[1].active && !s.p[2].active); /* a game of 2 takes 2 */

   /* a screen the engine does not know, or more players than 8, are refused */
   files[0].text = "{\"format\": 1, \"title\": \"X\", \"screen\": \"21:9\", \"level\": \"level.json\"}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0);
   files[0].text = "{\"format\": 1, \"title\": \"X\", \"players\": 9, \"level\": \"level.json\"}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0);
   hd_content_builtin();
}

static void test_trig(void)
{
   CHECK(hd_sin(0) == 0 && hd_sin(1024) == TRIG_ONE && hd_sin(3072) == -TRIG_ONE && hd_cos(0) == TRIG_ONE);
   CHECK(hd_abs(hd_sin(512) - 11585) <= 1); /* sin 45 degrees */
   CHECK(hd_abs(hd_sin(341) - 8186) <= 2);  /* sin(341 of 4096 turns) = 0.4996 */
}

static const char *maze[10] = {
   "..........",
   "....#.....",
   "....#.....",
   "....#.....",
   "....#.....",
   "....#.....",
   "....#.....",
   "....#.....",
   "....#.....",
   "....#.....",
};

static int maze_walk(void *ctx, int32_t x, int32_t y)
{
   (void)ctx;
   return x >= 0 && y >= 0 && x < 10 && y < 10 && maze[y][x] == '.';
}

/* A* goes around the wall, over its top, and never steps into it. */
static void test_path(void)
{
   int32_t x = 1, y = 8, steps = 0, nx, ny;
   while ((x != 8 || y != 8) && steps < 40 && hd_path_next(x, y, 8, 8, maze_walk, NULL, 500, &nx, &ny))
   {
      CHECK(maze_walk(NULL, nx, ny));
      CHECK(hd_abs(nx - x) <= 1 && hd_abs(ny - y) <= 1);
      x = nx;
      y = ny;
      steps++;
   }
   CHECK(x == 8 && y == 8);
   CHECK(steps >= 14 && steps <= 18); /* up to the top row and down again */
   CHECK(!hd_path_next(3, 3, 3, 3, maze_walk, NULL, 500, &nx, &ny));
}

static void test_bones(void)
{
   hd_anim a;
   hd_pose p;
   memset(&a, 0, sizeof a);
   a.length = 20;
   a.loop = 1;
   a.count = 2;
   a.key[0].frame = 0;
   a.key[0].angle[0] = 0;
   a.key[1].frame = 10;
   a.key[1].angle[0] = 400;
   a.key[1].angle[1] = 4000; /* the short way from 0 to 4000 is backwards */
   bones_pose(&a, 5, &p);
   CHECK(p.angle[0] == 200);
   CHECK(p.angle[1] == -48);
   bones_pose(&a, 15, &p); /* looping back from the last key to the first */
   CHECK(p.angle[0] == 200);
   bones_pose(&a, 25, &p);
   CHECK(p.angle[0] == 200);
}

static uint32_t small_px[64 * 36], other_px[64 * 36];

static void test_fx(void)
{
   hd_surface s = { small_px, 64, 36 }, t = { other_px, 64, 36 };
   uint32_t sprite_px[4 * 3];
   hd_image im = { 4, 3, sprite_px };
   hd_style st;
   int32_t i, x, y, same = 1;
   for (i = 0; i < 64 * 36; i++)
      small_px[i] = (uint32_t)(i * 2654435761u) & 0xffffffu;
   fx_fade(&s, 0xff0000u, 256);
   for (i = 0; i < 64 * 36; i++)
      same &= small_px[i] == 0xff0000u;
   CHECK(same);
   for (i = 0; i < 64 * 36; i++)
      small_px[i] = (uint32_t)(i * 2654435761u) & 0xffffffu;
   fx_grade_build(GRADE_GREY, NULL);
   fx_grade(&s, 256);
   for (i = 0, same = 1; i < 64 * 36; i++)
      same &= ((small_px[i] >> 16) & 0xff) == (small_px[i] & 0xff) && ((small_px[i] >> 8) & 0xff) == (small_px[i] & 0xff);
   CHECK(same);
   fx_grade_build(GRADE_NONE, NULL);
   fx_pixelate(&s, 4);
   for (y = 0, same = 1; y < 36; y++)
      for (x = 0; x < 64; x++)
         same &= small_px[y * 64 + x] == small_px[(y / 4 * 4) * 64 + x / 4 * 4];
   CHECK(same);
   fx_lights(&s, 256, NULL, 0); /* all dark, no lights */
   for (i = 0, same = 1; i < 64 * 36; i++)
      same &= small_px[i] == 0;
   CHECK(same);
   /* a sprite turned by 0 at scale 1 lands exactly where a plain one does */
   for (i = 0; i < 12; i++)
      sprite_px[i] = 0xff000000u | (uint32_t)(i * 0x151515u);
   memset(small_px, 0, sizeof small_px);
   memset(other_px, 0, sizeof other_px);
   gfx_blit(&s, &im, 10, 7, NULL);
   gfx_blit_rot(&t, &im, 10, 7, 0, 0, 0, FX_ONE, FX_ONE, NULL);
   CHECK(memcmp(small_px, other_px, sizeof small_px) == 0);
   /* a quarter turn: the picture's first row becomes its last column, drawn downward */
   memset(other_px, 0, sizeof other_px);
   gfx_blit_rot(&t, &im, 20, 10, 0, 0, 1024, FX_ONE, FX_ONE, NULL);
   CHECK(other_px[10 * 64 + 19] == (sprite_px[0] & 0xffffffu) && other_px[13 * 64 + 19] == (sprite_px[3] & 0xffffffu));
   /* blend modes */
   memset(&st, 0, sizeof st);
   small_px[0] = 0x808080u;
   sprite_px[0] = 0xff404040u;
   st.blend = BLEND_ADD;
   gfx_blit(&s, &im, 0, 0, &st);
   CHECK(small_px[0] == 0xc0c0c0u);
   small_px[0] = 0x808080u;
   st.blend = BLEND_MULTIPLY;
   gfx_blit(&s, &im, 0, 0, &st);
   CHECK(small_px[0] == 0x202020u);
   /* the outline: a ring around a one pixel sprite */
   {
      uint32_t dot_px[1] = { 0xffffffffu };
      hd_image dot = { 1, 1, dot_px };
      memset(small_px, 0, sizeof small_px);
      memset(&st, 0, sizeof st);
      st.outline = 0xff00ff00u;
      gfx_blit(&s, &dot, 5, 5, &st);
      CHECK(small_px[5 * 64 + 5] == 0xffffffu && small_px[4 * 64 + 5] == 0x00ff00u && small_px[5 * 64 + 6] == 0x00ff00u && small_px[4 * 64 + 4] == 0);
   }
   /* text: accents are drawn above the capital, a dialog counts letters, not bytes */
   CHECK(text_glyphs("\xc2\xa1" "Hola, ni\xc3\xb1o!") == 12);
   CHECK(text_width("ABC", 2) == 34);
}

static uint32_t show_hash(hd_state *s, int32_t from, int32_t to)
{
   uint32_t h = 2166136261u;
   hd_input in[MAX_PLAYERS];
   int32_t f;
   for (f = from; f < to; f++)
   {
      memset(in, 0, sizeof in);
      /* every 300 frames the next scene; in between drive, walk, jump, swim and try the colors */
      if (f % 300 == 0)
         in[0].buttons = PAD_R;
      else if (f % 300 < 200)
         in[0].buttons = PAD_B | ((f / 60) % 2 ? PAD_RIGHT : PAD_LEFT) | ((f % 40) < 3 ? PAD_Y : 0) | ((f % 50) == 7 ? PAD_UP : 0);
      hd_step(s, in);
      hd_draw(s, fb);
      hd_mix(s, audio, 1);
      h = fnv(fb, sizeof(uint32_t) * HD_W * HD_H, h);
      h = fnv(audio, sizeof(int16_t) * HD_SAMPLES_PER_FRAME * 2, h);
   }
   return h;
}

/* The showcase: every scene, the same twice, and a save state in the middle continues exactly. */
static void test_showcase(void)
{
   static hd_state a, b;
   static uint8_t save[HD_SAVE_SIZE];
   uint32_t ha, hb;
   int32_t lang;
   for (lang = 0; lang < LANGS; lang++)
   {
      hd_lang = lang;
      hd_reset(&a);
      hd_show_start(&a);
      hd_reset(&b);
      hd_show_start(&b);
      ha = show_hash(&a, 0, 1600);
      hb = show_hash(&b, 0, 1600);
      CHECK(ha == hb);
   }
   hd_lang = 0;
   CHECK(a.show.on && a.show.scene >= 0 && a.show.scene < 5);
   hd_reset(&a);
   hd_show_start(&a);
   show_hash(&a, 0, 750);
   hd_save(&a, save);
   ha = show_hash(&a, 750, 1500);
   CHECK(hd_load(&b, save, HD_SAVE_SIZE) == 1);
   hb = show_hash(&b, 750, 1500);
   CHECK(ha == hb);
   CHECK(memcmp(&a, &b, sizeof a) == 0);
}

#ifndef HD_NO_3D
/* The 3D room: the same frames twice, and a save state in the middle continues exactly (on any number of cores). */
static void test_3d(void)
{
   static hd_state a, b;
   static uint8_t save[HD_SAVE_SIZE];
   uint32_t ha, hb, h0;
   hd_input in[MAX_PLAYERS];
   int32_t f;
   hd_res = 1;
   hd_reset(&a);
   hd_r3d_start(&a);
   hd_reset(&b);
   hd_r3d_start(&b);
   ha = hb = 2166136261u;
   for (f = 0; f < 40; f++)
   {
      memset(in, 0, sizeof in);
      in[0].buttons = (f / 10) % 2 ? PAD_RIGHT | PAD_Y : PAD_LEFT | PAD_L;
      hd_step(&a, in);
      hd_draw(&a, fb);
      ha = fnv(fb, sizeof(uint32_t) * HD_W * HD_H, ha);
      hd_step(&b, in);
      hd_draw(&b, fb);
      hb = fnv(fb, sizeof(uint32_t) * HD_W * HD_H, hb);
   }
   CHECK(ha == hb);
   CHECK(r3d_triangles() > 20000 && r3d_drawn() > 1000);
   h0 = ha;
   hd_save(&a, save);
   memset(in, 0, sizeof in);
   for (f = 0; f < 20; f++)
   {
      hd_step(&a, in);
      hd_draw(&a, fb);
      ha = fnv(fb, sizeof(uint32_t) * HD_W * HD_H, ha);
   }
   CHECK(hd_load(&b, save, HD_SAVE_SIZE) == 1);
   hb = h0;
   for (f = 0; f < 20; f++)
   {
      hd_step(&b, in);
      hd_draw(&b, fb);
      hb = fnv(fb, sizeof(uint32_t) * HD_W * HD_H, hb);
   }
   CHECK(ha == hb);
   CHECK(a.show.on == 2 && memcmp(&a, &b, sizeof a) == 0);
}

/* TELETRABAJO: CÁMARA APAGADA, phase 2: an activity's points and energy, the repeat rule, and work at the desk. */
static void test_remote_activities(void)
{
   static hd_state a;
   hd_input in[MAX_PLAYERS];
   hd_remote *g = HD_GAME3D(&a, hd_remote);
   int32_t f, round, e0;
   hd_reset(&a);
   hd_remote_new_day(&a);
   /* coffee three times: 3 game minutes each, +120 energy, 30 points twice then a fifth */
   for (round = 0; round < 3; round++)
   {
      g->x = ROOM3D_AT(1495), g->z = ROOM3D_AT(105), g->energy = 500;
      memset(in, 0, sizeof in);
      hd_step(&a, in);
      CHECK(g->prompt == RA_COFFEE + 1);
      in[0].buttons = PAD_JUMP;
      hd_step(&a, in);
      CHECK(g->act == RA_COFFEE + 1);
      memset(in, 0, sizeof in);
      for (f = 0; f < 3 * REMOTE_FRAMES_PER_MINUTE + 5 && g->act; f++)
         hd_step(&a, in);
      CHECK(!g->act && g->energy == 620);
   }
   CHECK(g->points == 30 + 30 + 6 && g->done[RA_COFFEE] == 3);
   /* stopped halfway: half the points, the energy had so far */
   g->x = ROOM3D_AT(1450), g->z = ROOM3D_AT(108), g->energy = 300, g->points = 0;
   memset(in, 0, sizeof in);
   in[0].buttons = PAD_JUMP;
   hd_step(&a, in);
   CHECK(g->act == RA_COOK + 1);
   memset(in, 0, sizeof in);
   for (f = 0; f < 25 * REMOTE_FRAMES_PER_MINUTE / 2 - 1; f++)
      hd_step(&a, in);
   in[0].buttons = PAD_JUMP;
   hd_step(&a, in);
   CHECK(!g->act && g->points >= 120 && g->points <= 126 && g->energy >= 448 && g->energy <= 452);
   /* work: full speed with energy, half when tired, nothing asleep; it costs energy */
   g->x = ROOM3D_AT(950), g->z = ROOM3D_AT(1035), g->energy = 800, g->work = 0;
   memset(in, 0, sizeof in);
   hd_step(&a, in);
   in[0].buttons = PAD_JUMP;
   hd_step(&a, in);
   CHECK(g->act == RA_WORK + 1);
   memset(in, 0, sizeof in);
   e0 = g->energy;
   for (f = 0; f < 10 * REMOTE_FRAMES_PER_MINUTE; f++)
      hd_step(&a, in);
   CHECK(g->work == 10 * REMOTE_WORK_UNIT && g->energy == e0 - 10 * REMOTE_WORK_DRAIN);
   g->energy = 200;
   for (f = 0; f < REMOTE_FRAMES_PER_MINUTE; f++)
      hd_step(&a, in);
   CHECK(g->work == 10 * REMOTE_WORK_UNIT + REMOTE_FRAMES_PER_MINUTE);
   g->energy = 50;
   for (f = 0; f < REMOTE_FRAMES_PER_MINUTE; f++)
      hd_step(&a, in);
   CHECK(g->work == 10 * REMOTE_WORK_UNIT + REMOTE_FRAMES_PER_MINUTE);
}

/* TELETRABAJO: CÁMARA APAGADA, phase 3: suspicion, the boss's events, noise on a call, the cat, and the day's three endings. */
static void remote_steps(hd_state *s, int32_t n, uint32_t buttons)
{
   hd_input in[MAX_PLAYERS];
   int32_t f;
   for (f = 0; f < n; f++)
   {
      memset(in, 0, sizeof in);
      in[0].buttons = f == 0 ? buttons : 0;
      hd_step(s, in);
   }
}

/* The next event comes now: the boss's or the house's. */
static void remote_event(hd_remote *g, int32_t kind)
{
   g->ev_next = g->ev_count - 1;
   g->ev_kind[g->ev_next] = kind;
   g->ev_at[g->ev_next] = g->t / REMOTE_FRAMES_PER_MINUTE;
}

static void test_remote_day(void)
{
   static hd_state a, b;
   hd_remote *g = HD_GAME3D(&a, hd_remote);
   const int32_t fpm = REMOTE_FRAMES_PER_MINUTE;
   int32_t f, s0;
   hd_reset(&a);
   hd_remote_new_day(&a);
   /* the day's events: the meeting first, 4 to 6 from the boss in all, 2 to 4 from the house, in order */
   CHECK(g->ev_kind[0] == RE_MEETING && g->ev_count >= 7 && g->ev_count <= 10);
   for (f = 1; f < g->ev_count; f++)
      CHECK(g->ev_at[f] > g->ev_at[f - 1] && g->ev_at[f] < REMOTE_DAY_MINUTES);
   g->ev_next = g->ev_count;
   /* away from the desk: nothing for 10 minutes, then REMOTE_AWAY_RISE a minute */
   remote_steps(&a, 25 * fpm, 0);
   CHECK(g->suspicion >= 14 * REMOTE_AWAY_RISE && g->suspicion <= 16 * REMOTE_AWAY_RISE);
   /* a call nobody answers: +300 when its 45 seconds run out */
   s0 = g->suspicion;
   remote_event(g, RE_CALL);
   remote_steps(&a, 2, 0);
   CHECK(g->boss == RE_CALL + 1 && !g->boss_call);
   remote_steps(&a, 45 * 60, 0);
   CHECK(!g->boss && g->suspicion >= s0 + 300 && g->phone_text[(g->phone_n - 1) % REMOTE_PHONE] == RT_BOSS_MISS + RE_CALL);
   /* answered at the desk: its calm (100 for a call, REMOTE_ANSWER_CALM of it), then the worker stays on the call, and the boss hears the washing machine */
   g->x = ROOM3D_AT(950), g->z = ROOM3D_AT(1035), g->energy = 800;
   remote_steps(&a, 1, 0);
   remote_steps(&a, 1, PAD_JUMP);
   CHECK(g->act == RA_WORK + 1);
   g->washer_t = 100;
   s0 = g->suspicion;
   remote_event(g, RE_CALL);
   remote_steps(&a, 2, 0);
   /* (a minute of work may have passed too: -1) */
   CHECK(g->boss == RE_CALL + 1 && g->boss_call && g->suspicion >= s0 - 100 * REMOTE_ANSWER_CALM / 100 - 1 + REMOTE_NOISE &&
         g->suspicion <= s0 - 100 * REMOTE_ANSWER_CALM / 100 + REMOTE_NOISE);
   remote_steps(&a, 1, PAD_JUMP);
   CHECK(g->act == RA_WORK + 1); /* no leaving a call */
   remote_steps(&a, 10 * fpm, 0);
   CHECK(!g->boss);
   /* the cat on the keyboard: no work until it is taken off */
   g->cat_keys = 2, g->cat_mode = CAT_REST, g->cat_spot = 5, g->cat_y = FX_FRAC(79, 100);
   s0 = g->work;
   remote_steps(&a, 60, 0);
   CHECK(g->work == s0);
   remote_steps(&a, 1, PAD_JUMP);
   CHECK(!g->cat_keys && g->act == RA_WORK + 1);
   remote_steps(&a, 60, 0);
   CHECK(g->work > s0);
   /* 18:00 with the hours done: survived, with the bonuses */
   g->t = REMOTE_DAY_MINUTES * fpm - 1, g->work = REMOTE_WORK_GOAL * REMOTE_WORK_UNIT, g->points = 500, g->suspicion = 200;
   remote_steps(&a, 1, 0);
   CHECK(g->over == REND_SURVIVED && g->score == 500 + 1000 + REMOTE_SUSPICION_MAX - g->suspicion && g->suspicion >= 199);
   remote_steps(&a, 100, 0);
   remote_steps(&a, 1, PAD_START);
   /* START: the morning of another day, the session's record kept */
   CHECK(!g->over && g->t == 0 && g->screen == RSC_MORNING && g->best >= 2300 && g->best <= 2301);
   remote_steps(&a, 1, 0);
   remote_steps(&a, 1, PAD_JUMP); /* the morning skipped */
   CHECK(g->screen == RSC_DAY);
   /* missing hours halves the points; full suspicion fires at once */
   g->t = REMOTE_DAY_MINUTES * fpm - 1, g->work = 0, g->points = 500;
   remote_steps(&a, 1, 0);
   CHECK(g->over == REND_MISSED && g->score == 250);
   hd_remote_new_day(&a);
   g->ev_next = g->ev_count;
   g->suspicion = REMOTE_SUSPICION_MAX - 1;
   remote_steps(&a, 12 * fpm, 0);
   CHECK(g->over == REND_FIRED);
   /* a whole day without touching anything, twice: the same day (the lazy one is fired before 18:00) */
   hd_reset(&a);
   hd_remote_new_day(&a);
   hd_reset(&b);
   hd_remote_new_day(&b);
   for (f = 0; f < REMOTE_DAY_MINUTES * fpm && !g->over; f++)
   {
      remote_steps(&a, 1, 0);
      remote_steps(&b, 1, 0);
   }
   CHECK(g->over == REND_FIRED && g->t < REMOTE_DAY_MINUTES * fpm && memcmp(&a, &b, sizeof a) == 0);
}

/* TELETRABAJO: CÁMARA APAGADA, phase 4: the title, how to play, the demo, the morning and the pause, each drawn once. */
static void test_remote_screens(void)
{
   static hd_state a;
   hd_remote *g = HD_GAME3D(&a, hd_remote);
   int32_t x0, t0, page;
   hd_res = 1;
   hd_reset(&a);
   hd_remote_start(&a);
   CHECK(g->screen == RSC_TITLE && g->menu == 0);
   hd_draw(&a, fb);
   /* down to "how to play", its pages, back to the title */
   remote_steps(&a, 2, PAD_DOWN);
   CHECK(g->menu == 1);
   remote_steps(&a, 2, PAD_JUMP);
   CHECK(g->screen == RSC_HELP && g->help_page == 0);
   hd_draw(&a, fb);
   for (page = 0; page < REMOTE_HELP_PAGES; page++)
      remote_steps(&a, 2, PAD_JUMP);
   CHECK(g->screen == RSC_TITLE);
   /* the main character: a woman with right (or A), her words in Spanish, kept by the day; back to a man */
   remote_steps(&a, 2, PAD_DOWN);
   remote_steps(&a, 2, PAD_DOWN);
   remote_steps(&a, 2, PAD_DOWN);
   CHECK(g->menu == 3 && g->hero == 0);
   remote_steps(&a, 2, PAD_RIGHT);
   CHECK(g->hero == 1);
   hd_draw(&a, fb);
   {
      int32_t lang = hd_lang;
      hd_lang = 1;
      CHECK(strcmp(remote_text(RT_ASLEEP), "Dormida sobre el teclado. Zzz...") == 0 && strcmp(remote_text(RT_WORK), "Trabajo") == 0);
      hd_lang = 0;
      CHECK(strcmp(remote_text(RT_ASLEEP), "Asleep on the keyboard. Zzz...") == 0);
      CHECK(strcmp(remote_text(RT_GREETING), "Morning, Alberta. Lots to do today.") == 0); /* her name, in every language */
      hd_lang = lang;
   }
   remote_steps(&a, 2, PAD_JUMP);
   CHECK(g->hero == 0);
   hd_draw(&a, fb);
   CHECK(remote_text_hero == 0);
   /* the credits: from the menu's last line, they roll by themselves back to the title; A cuts them short after two seconds */
   remote_steps(&a, 2, PAD_DOWN);
   CHECK(g->menu == 4);
   remote_steps(&a, 2, PAD_JUMP);
   CHECK(g->screen == RSC_CREDITS);
   remote_steps(&a, 60, 0);
   hd_draw(&a, fb);
   for (page = 0; page < 20000 && g->screen == RSC_CREDITS; page++)
      remote_steps(&a, 1, 0);
   CHECK(g->screen == RSC_TITLE && page > 800);
   g->menu = 4;
   remote_steps(&a, 2, 0); /* the title takes a held button as held: let go first */
   remote_steps(&a, 2, PAD_JUMP);
   CHECK(g->screen == RSC_CREDITS);
   remote_steps(&a, 30, 0);
   remote_steps(&a, 2, PAD_JUMP);
   CHECK(g->screen == RSC_CREDITS); /* not before two seconds */
   remote_steps(&a, 100, 0);
   remote_steps(&a, 2, PAD_JUMP);
   CHECK(g->screen == RSC_TITLE && g->menu == 0);
   /* nobody plays: the demo walks somewhere, and a button brings the title back */
   remote_steps(&a, REMOTE_ATTRACT_AFTER + 2, 0);
   CHECK(g->screen == RSC_DAY && g->attract);
   x0 = g->x;
   remote_steps(&a, 300, 0);
   CHECK(g->x != x0 && g->t >= 300);
   hd_draw(&a, fb);
   remote_steps(&a, 1, PAD_RUN);
   CHECK(g->screen == RSC_TITLE && !g->attract);
   /* START: the morning, which ends by itself with the worker by the bed */
   g->menu = 0;
   remote_steps(&a, 1, 0);
   remote_steps(&a, 1, PAD_START);
   CHECK(g->screen == RSC_MORNING);
   remote_steps(&a, 100, 0);
   hd_draw(&a, fb);
   remote_steps(&a, REMOTE_MORNING_FRAMES, 0);
   CHECK(g->screen == RSC_DAY && g->t > 0 && g->x == ROOM3D_AT(255) && g->z == ROOM3D_AT(1010));
   /* the pause stops the clock; "go on" goes on, "restart the day" brings the morning */
   remote_steps(&a, 2, PAD_START);
   CHECK(g->paused);
   t0 = g->t;
   remote_steps(&a, 100, 0);
   hd_draw(&a, fb);
   CHECK(g->t == t0);
   remote_steps(&a, 1, PAD_JUMP);
   CHECK(!g->paused && g->t == t0);
   remote_steps(&a, 10, 0);
   CHECK(g->t == t0 + 10);
   remote_steps(&a, 2, PAD_START);
   remote_steps(&a, 2, PAD_DOWN);
   remote_steps(&a, 2, PAD_DOWN);
   remote_steps(&a, 2, PAD_JUMP);
   CHECK(g->screen == RSC_MORNING && g->t == 0);
   /* the end, drawn while it counts up */
   remote_steps(&a, REMOTE_MORNING_FRAMES + 1, 0);
   g->t = REMOTE_DAY_MINUTES * REMOTE_FRAMES_PER_MINUTE - 1;
   remote_steps(&a, 200, 0);
   CHECK(g->screen == RSC_END);
   hd_draw(&a, fb);
   /* a day won: A brings the credits, then the title */
   g->over = REND_SURVIVED;
   remote_steps(&a, 2, PAD_JUMP);
   CHECK(g->screen == RSC_CREDITS);
   remote_steps(&a, 130, 0);
   remote_steps(&a, 2, PAD_START);
   CHECK(g->screen == RSC_TITLE);
}

/* The boss's later events: the weekend question answered either way (or not at all), a hotfix deployed at the desk, a camera call that sees the cat. */
static void test_remote_boss(void)
{
   static hd_state a;
   hd_remote *g = HD_GAME3D(&a, hd_remote);
   int32_t s0, p0;
   hd_reset(&a);
   hd_remote_new_day(&a);
   g->x = ROOM3D_AT(950), g->z = ROOM3D_AT(1035), g->energy = 800;
   remote_steps(&a, 1, 0);
   remote_steps(&a, 1, PAD_JUMP);
   CHECK(g->act == RA_WORK + 1);
   /* "of course": calmer, the weekend sold, still at the desk */
   g->suspicion = 300;
   remote_event(g, RE_WEEKEND);
   remote_steps(&a, 3, 0);
   CHECK(g->boss == RE_WEEKEND + 1);
   remote_steps(&a, 1, PAD_JUMP);
   CHECK(!g->boss && g->weekend == 1 && g->suspicion < 300 && g->act == RA_WORK + 1);
   /* "I have plans": a little more suspicion */
   g->weekend = 0;
   s0 = g->suspicion;
   remote_event(g, RE_WEEKEND);
   remote_steps(&a, 3, 0);
   remote_steps(&a, 1, PAD_RUN);
   CHECK(!g->boss && g->weekend == 2 && g->suspicion >= s0 + REMOTE_WEEKEND_NO - 1);
   /* not answered: it counts as a yes, and it is missed */
   g->weekend = 0;
   s0 = g->suspicion;
   remote_event(g, RE_WEEKEND);
   remote_steps(&a, 31 * 60, 0);
   CHECK(!g->boss && g->weekend == 1 && g->suspicion >= s0 + 150 - 31 * 60 / REMOTE_FRAMES_PER_MINUTE / REMOTE_WORK_CALM_EVERY - 1); /* working calmed it meanwhile */
   /* a hotfix: deploying at the desk, then it pays */
   p0 = g->points;
   remote_event(g, RE_HOTFIX);
   remote_steps(&a, 3, 0);
   CHECK(g->boss == RE_HOTFIX + 1 && g->boss_call);
   remote_steps(&a, 20 * REMOTE_FRAMES_PER_MINUTE + 2, 0);
   CHECK(!g->boss && g->hotfixes == 1 && g->points == p0 + REMOTE_HOTFIX_POINTS);
   /* the camera sees the cat on the keyboard */
   remote_event(g, RE_CAMERA);
   remote_steps(&a, 3, 0);
   CHECK(g->boss == RE_CAMERA + 1 && g->boss_call);
   s0 = g->suspicion;
   g->cat_keys = 2, g->cat_mode = CAT_REST, g->cat_spot = 5, g->cat_y = FX_FRAC(79, 100);
   remote_steps(&a, 2, 0);
   CHECK((g->boss_noise & (1 << 5)) && g->suspicion >= s0 + REMOTE_NOISE - 1);
   hd_draw(&a, fb);
}

/* Every text of TELETRABAJO exists in the three languages and fits the 640 logical pixels at the size it is drawn (the help's lines wrap). */
static void test_remote_texts(void)
{
   int32_t lang = hd_lang, l, id;
   for (l = 0; l < LANGS; l++)
   {
      hd_lang = l;
      for (id = 0; id < RT_COUNT; id++)
      {
         const char *t = remote_text(id);
         int32_t scale = 1;
         CHECK(t != NULL && t[0] != 0);
         if (!t || id == RT_TITLE || (id >= RT_HELP_LINE && id < RT_HELP_LINE + REMOTE_HELP_PAGES * 4))
            continue;
         if (id >= RT_BOSS_MUSE && id < RT_BOSS_MUSE + RE_BOSS_KINDS * 2)
         {
            CHECK(strlen(t) < 46 * 3); /* wrapped in the thought's cloud: three lines at most */
            continue;
         }
         if (id == RT_PAUSE)
            scale = 4;
         else if ((id >= RT_END_TITLE && id < RT_END_TITLE + 5) || (id >= RT_HELP_TITLE && id < RT_HELP_TITLE + REMOTE_HELP_PAGES) || id == RT_MORNING_RING)
            scale = 3;
         else if (id == RT_MENU_PLAY || id == RT_MENU_HELP || id == RT_MENU_CREDITS || id == RT_PAUSE_GO_ON || id == RT_PAUSE_RESTART ||
                  id == RT_PAUSE_QUIT || id == RT_DEMO || id == RT_NEW_RECORD || id == RT_END_TOTAL || id == RT_END_FIRED_MSG)
            scale = 2;
         if (text_width(t, scale) + 24 * scale > 620)
            fprintf(stderr, "  remote: text %d in language %d is too wide: %s\n", (int)id, (int)l, t);
         CHECK(text_width(t, scale) + 24 * scale <= 620);
      }
   }
   hd_lang = lang;
}

/* Every place the day uses is reached on foot from the hall, by the worker's size, along play3d_route. */
static void test_remote_places(void)
{
   static hd_state a;
   hd_remote *g = HD_GAME3D(&a, hd_remote);
   int32_t kind, i, px, pz;
   hd_reset(&a);
   hd_remote_new_day(&a);
   for (kind = REMOTE_PLACE_ACTIVITY; kind <= REMOTE_PLACE_MATE; kind++)
      for (i = 0; remote_place(kind, i, &px, &pz); i++)
      {
         int32_t x = ROOM3D_AT(450), z = ROOM3D_AT(725), tx = ROOM3D_AT(px), tz = ROOM3D_AT(pz), f, dx, dz;
         int64_t d2 = 0;
         for (f = 0; f < 3000; f++)
         {
            d2 = (int64_t)(x - tx) * (x - tx) + (int64_t)(z - tz) * (z - tz);
            if (d2 < (int64_t)FX_FRAC(15, 100) * FX_FRAC(15, 100) || !play3d_route(3, x, z, tx, tz, &dx, &dz))
               break;
            play3d_move(&x, &z, (int32_t)(((int64_t)REMOTE_WALK * dx) >> 14), (int32_t)(((int64_t)REMOTE_WALK * dz) >> 14), REMOTE_RADIUS);
         }
         if (d2 >= (int64_t)FX_FRAC(15, 100) * FX_FRAC(15, 100))
            fprintf(stderr, "  remote: place %d of kind %d at (%d, %d) not reached\n", (int)i, (int)kind, (int)px, (int)pz);
         CHECK(d2 < (int64_t)FX_FRAC(15, 100) * FX_FRAC(15, 100));
      }
   (void)g;
}

/* A WAV of n samples, 16-bit mono PCM at 48 kHz, a square wave of `amp`; its size. */
static size_t tiny_wav(uint8_t *out, int32_t n, int32_t amp)
{
   uint8_t *p = out;
   int32_t i;
   memcpy(p, "RIFF", 4); p += 4; le32(&p, (uint32_t)(36 + 2 * n));
   memcpy(p, "WAVEfmt ", 8); p += 8; le32(&p, 16);
   le16(&p, 1); le16(&p, 1); le32(&p, 48000); le32(&p, 96000); le16(&p, 2); le16(&p, 16);
   memcpy(p, "data", 4); p += 4; le32(&p, (uint32_t)(2 * n));
   for (i = 0; i < n; i++)
      le16(&p, (uint32_t)(uint16_t)(int16_t)((i / 50) & 1 ? amp : -amp));
   return (size_t)(p - out);
}

/* The channel playing a sample, -1 when none. */
static int32_t channel_of(const hd_state *s, int32_t sample)
{
   int32_t i;
   for (i = 0; i < MAX_CHANNELS; i++)
      if (s->ch[i].sample == sample)
         return i;
   return -1;
}

/* remote_steps with the sound mixed after each frame, as a host does. */
static void remote_plays(hd_state *s, int32_t n, uint32_t buttons)
{
   static int16_t out[HD_SAMPLES_PER_FRAME * 2];
   int32_t f;
   for (f = 0; f < n; f++)
   {
      remote_steps(s, 1, f == 0 ? buttons : 0);
      hd_mix(s, out, 1);
   }
}

/* A small .hdm: a triangle on two bones (the second a child 1 m up), one clip of 2 frames that turns the child a quarter around z. */
static size_t tiny_hdm(uint8_t *out)
{
   uint8_t *p = out;
   int32_t f, b, k;
   memcpy(p, "HDM1", 4); p += 4;
   le32(&p, 1); le32(&p, 3); le32(&p, 1); le32(&p, 2); le32(&p, 1);
   for (k = 0; k < 3; k++)
   {
      /* x, y, z; a normal +z; the first vertex on bone 0, the others on bone 1 */
      le32(&p, (uint32_t)(k == 1 ? FX(1) : 0)); le32(&p, (uint32_t)(k ? FX(2) : 0)); le32(&p, 0);
      le16(&p, 0); le16(&p, 0); le16(&p, 16384);
      *p++ = (uint8_t)(k ? 1 : 0); *p++ = 0; *p++ = 0; *p++ = 0;
      *p++ = 255; *p++ = 0; *p++ = 0; *p++ = 0;
   }
   le16(&p, 0); le16(&p, 1); le16(&p, 2); le16(&p, 0); le32(&p, 0x336699);
   for (b = 0; b < 2; b++)
   {
      le32(&p, (uint32_t)(b ? 0 : -1));
      memset(p, 0, 24); memcpy(p, b ? "Arm" : "Hips", b ? 3 : 4); p += 24;
      le16(&p, 0); le16(&p, 0); le16(&p, 0); le16(&p, 16384);
      le32(&p, 0); le32(&p, (uint32_t)(b ? FX(1) : 0)); le32(&p, 0);
   }
   memset(p, 0, 24); memcpy(p, "Wave", 4); p += 24;
   le32(&p, 2); le32(&p, 30);
   for (f = 0; f < 2; f++)
      for (b = 0; b < 2; b++)
      {
         /* frame 1 turns the arm 90 degrees around z: (0, 0, sin 45, cos 45) */
         int32_t turn = f == 1 && b == 1;
         le16(&p, 0); le16(&p, 0); le16(&p, (uint32_t)(turn ? 11585 : 0)); le16(&p, (uint32_t)(turn ? 11585 : 16384));
         le32(&p, 0); le32(&p, (uint32_t)(b ? FX(1) : 0)); le32(&p, 0);
      }
   return (size_t)(p - out);
}

/* Models made by artists (model3d.h): read, animated, skinned, refused when broken, and brought by a package. */
static void test_model3d(void)
{
   static uint8_t hdm[4096], zip[16384];
   model3d_xf local[MODEL3D_BONES], global[MODEL3D_BONES];
   const model3d *m;
   model3d_body body;
   size_t n = tiny_hdm(hdm), z;
   int32_t place[9], first;
   zfile files[2] = {{0}};
   const char *err;
   r3d_vec at = { 0, 0, 0 };
   hd_content_builtin();
   CHECK(model3d_load("tiny", hdm, n) == NULL);
   m = model3d_find("tiny");
   CHECK(m && model3d_bones(m) == 2 && model3d_bone(m, "Arm") == 1 && model3d_parent(m, 1) == 0 && model3d_clip(m, "Wave") == 0);
   CHECK(model3d_clip_frames(m, 0) == 4); /* 2 frames at 30 a second: 4 game frames */
   /* at rest the arm's joint is 1 m up; at frame 1 of the clip it is turned a quarter: its +y points to -x */
   model3d_sample(m, -1, 0, local);
   model3d_globals(m, local, global);
   CHECK(global[1].t.y == FX(1) && global[1].r[4] == 16384);
   model3d_sample(m, 0, 2 << 16, local);
   model3d_globals(m, local, global);
   CHECK(hd_abs(global[1].r[1] + 16384) < 40 && hd_abs(global[1].r[4]) < 40);
   /* halfway between the frames: half the turn */
   model3d_sample(m, 0, 1 << 16, local);
   CHECK(hd_abs(local[1].r[4] - 11585) < 60);
   /* skinned: the vertex 1 m up the arm swings to the left, the one on the hips stays */
   r3d_place(0, 0, 0, 0);
   first = r3d_triangles();
   model3d_body_build(&body, m);
   CHECK(body.mesh.count == 1 && body.mesh.first == first);
   model3d_sample(m, 0, 2 << 16, local);
   model3d_globals(m, local, global);
   r3d_rotation(0, 0, 0, place);
   model3d_body_pose(&body, global, place, at);
   r3d_truncate(first);
   CHECK(r3d_triangles() == first);
   /* broken files are refused, never read past their end */
   CHECK(model3d_load("short", hdm, 40) != NULL);
   hdm[0] = 'X';
   CHECK(model3d_load("bad", hdm, n) != NULL);
   hdm[0] = 'H';
   CHECK(model3d_find("short") == NULL && model3d_find("bad") == NULL);
   /* a package brings them by name */
   hd_content_builtin();
   CHECK(model3d_find("tiny") == NULL);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Remote\", \"game\": \"remote\", \"models\": {\"worker\": \"w.hdm\"}}";
   files[1].name = "w.hdm";
   files[1].text = (const char *)hdm;
   files[1].len = n;
   z = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, z, &err) == 1);
   CHECK(model3d_find("worker") != NULL);
   files[0].text = "{\"format\": 3, \"title\": \"Remote\", \"game\": \"remote\", \"models\": {\"worker\": \"missing.hdm\"}}";
   z = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, z, &err) == 0 && strstr(err, "missing.hdm") != NULL && model3d_find("worker") == NULL);
   hd_content_builtin();
}

/* TELETRABAJO as a package ("game": "remote") with its effects and tracks by name: the title's music, the alarm in the morning, loops that keep their channel, a call's music and back. */
static void test_remote_sound(void)
{
   static uint8_t zip[200000], loop_wav[20000], shot_wav[10000], song[40000];
   static hd_state a;
   static int16_t out[HD_SAMPLES_PER_FRAME * 2];
   hd_remote *g = HD_GAME3D(&a, hd_remote);
   zfile files[4] = {{0}};
   const char *err;
   size_t n;
   int32_t i, alarm, drill, title, day, call, loud = 0;
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Remote\", \"game\": \"remote\", \"audio\": {"
                   "\"effects\": {\"alarm\": {\"file\": \"loop.wav\", \"loop\": true}, \"drill\": {\"file\": \"loop.wav\", \"loop\": true, \"volume\": 128},"
                   " \"ui_move\": \"shot.wav\", \"step\": \"shot.wav\","
                   " \"voice_m_en_done_coffee\": {\"file\": \"shot.wav\", \"on_demand\": true}, \"voice_m_es_done_coffee\": {\"file\": \"shot.wav\", \"on_demand\": true},"
                   " \"voice_m_es_cat_moved\": \"shot.wav\", \"voice_w_es_cat_moved\": {\"file\": \"shot.wav\", \"on_demand\": true},"
                   " \"voice_m_en_read_call_a\": \"shot.wav\", \"voice_m_en_muse_call_a\": {\"file\": \"song.wav\", \"on_demand\": true},"
                   " \"voice_m_en_read_call_b\": \"shot.wav\", \"voice_m_en_muse_call_b\": {\"file\": \"song.wav\", \"on_demand\": true}, \"voice_m_en_say_call\": \"shot.wav\"},"
                   " \"tracks\": {\"title\": {\"file\": \"song.wav\"}, \"day\": {\"file\": \"song.wav\", \"volume\": 150}, \"call\": {\"file\": \"song.wav\"}}}}";
   files[1].name = "loop.wav";
   files[1].text = (const char *)loop_wav;
   files[1].len = tiny_wav(loop_wav, 4800, 8000);
   files[2].name = "shot.wav";
   files[2].text = (const char *)shot_wav;
   files[2].len = tiny_wav(shot_wav, 4000, 8000);
   files[3].name = "song.wav";
   files[3].text = (const char *)song;
   files[3].len = tiny_wav(song, 9600, 6000);
   n = make_zip(zip, files, 4);
   CHECK(hd_content_load(zip, n, &err) == 1);
   CHECK(hd_code_game == HD_GAME_REMOTE && hd_res == 2); /* no level needed; 720p by default */
   alarm = hd_effect("alarm"), drill = hd_effect("drill");
   title = hd_track("title"), day = hd_track("day"), call = hd_track("call");
   CHECK(alarm >= HD_SFX_NAMED && drill > alarm && hd_effect("doorbell") == -1);
   CHECK(title == 2 && day == 3 && call == 4 && hd_track("end") == 0);
   hd_res = 1;
   hd_reset(&a);
   hd_remote_start(&a);
   /* the title plays its track, and the mix is not silent */
   remote_plays(&a, 30, 0);
   CHECK(a.music_track == title && a.music_pos > 0);
   for (i = 0; i < 10; i++)
   {
      int32_t k;
      hd_mix(&a, out, 1);
      for (k = 0; k < HD_SAMPLES_PER_FRAME * 2; k++)
         loud = hd_max(loud, hd_abs(out[k]));
   }
   CHECK(loud > 1000);
   /* the menu moves with a blip; the language line changes the texts' language */
   remote_plays(&a, 2, PAD_DOWN);
   CHECK(channel_of(&a, hd_effect("ui_move")) >= 0);
   remote_plays(&a, 2, PAD_DOWN);
   CHECK(g->menu == 2);
   i = hd_lang;
   remote_plays(&a, 2, PAD_RIGHT);
   CHECK(hd_lang == (i + 1) % LANGS);
   remote_plays(&a, 2, PAD_LEFT);
   CHECK(hd_lang == i);
   remote_plays(&a, 2, PAD_UP);
   remote_plays(&a, 2, PAD_UP);
   CHECK(g->menu == 0);
   /* the morning: no music, the alarm loops until the worker is up */
   remote_plays(&a, 2, PAD_START);
   CHECK(g->screen == RSC_MORNING);
   remote_plays(&a, 60, 0);
   CHECK(a.music_track == 0 && channel_of(&a, alarm) >= 0 && a.ch[channel_of(&a, alarm)].vol > 100);
   remote_plays(&a, REMOTE_MORNING_FRAMES, 0);
   CHECK(g->screen == RSC_DAY && channel_of(&a, alarm) == -1 && a.music_track == day);
   /* the drill: a loop no other effect takes the channel of */
   g->drill_t = 600;
   remote_plays(&a, 2, 0);
   i = channel_of(&a, drill);
   CHECK(i >= 0);
   {
      int32_t k;
      for (k = 0; k < 3 * MAX_CHANNELS; k++)
         hd_play_at(&a, hd_effect("step"), 200, 0, FX_ONE);
      CHECK(channel_of(&a, drill) == i);
   }
   /* a call: its music, then the day's again from where it was */
   g->boss = RE_CALL + 1;
   g->boss_call = 1;
   g->boss_t = 120;
   remote_plays(&a, 2, 0);
   CHECK(a.music_track == call && g->music_resume > 0);
   i = g->music_resume;
   remote_plays(&a, 130, 0);
   CHECK(a.music_track == day && a.music_pos >= i && a.music_pos < i + HD_SAMPLES_PER_FRAME * 3);
   /* a thought shown is said in the texts' language, and a new one cuts it short */
   i = hd_lang;
   hd_lang = 1;
   g->toast = RT_DONE + RA_COFFEE + 1;
   g->toast_t = REMOTE_TOAST_FRAMES + 1;
   remote_plays(&a, 1, 0);
   CHECK(channel_of(&a, hd_effect("voice_m_es_done_coffee")) >= 0 && channel_of(&a, hd_effect("voice_m_en_done_coffee")) == -1);
   g->toast = RT_CAT_MOVED + 1;
   g->toast_t = REMOTE_TOAST_FRAMES + 1;
   remote_plays(&a, 1, 0);
   CHECK(channel_of(&a, hd_effect("voice_m_es_cat_moved")) >= 0 && channel_of(&a, hd_effect("voice_m_es_done_coffee")) == -1);
   hd_lang = 0;
   g->toast = RT_DONE + RA_COFFEE + 1;
   g->toast_t = REMOTE_TOAST_FRAMES + 1;
   remote_plays(&a, 1, 0);
   CHECK(channel_of(&a, hd_effect("voice_m_en_done_coffee")) >= 0);
   g->hero = 1; /* the woman's voice, made into samples when first played */
   hd_lang = 1;
   g->toast = RT_CAT_MOVED + 1;
   g->toast_t = REMOTE_TOAST_FRAMES + 1;
   remote_plays(&a, 1, 0);
   CHECK(channel_of(&a, hd_effect("voice_w_es_cat_moved")) >= 0 && channel_of(&a, hd_effect("voice_m_es_cat_moved")) == -1);
   g->hero = 0;
   hd_lang = 0;
   g->toast = RT_DONE + RA_COFFEE + 1;
   g->toast_t = REMOTE_TOAST_FRAMES + 1;
   remote_plays(&a, 1, 0);
   g->toast = RT_AGAIN + 1; /* a line with no recording: nothing said, nothing cut */
   g->toast_t = REMOTE_TOAST_FRAMES + 1;
   remote_plays(&a, 1, 0);
   CHECK(channel_of(&a, hd_effect("voice_m_en_done_coffee")) >= 0);
   /* the boss's ask: read aloud at once, cutting the thought before, then what the worker thinks of it, once the reading ended */
   {
      int32_t k, read, muse, c, ask;
      g->boss = 0;
      g->net_t = 0;
      g->ev_next = 0;
      g->ev_count = 1;
      g->ev_kind[0] = RE_CALL;
      g->ev_at[0] = 0;
      remote_plays(&a, 1, 0);
      ask = g->phone_text[(g->phone_n - 1) % REMOTE_PHONE] - RT_BOSS_ASK - RE_CALL * 2;
      CHECK(g->boss == RE_CALL + 1 && (ask == 0 || ask == 1) && g->muse == RT_BOSS_MUSE + RE_CALL * 2 + ask + 1);
      read = hd_effect(ask ? "voice_m_en_read_call_b" : "voice_m_en_read_call_a");
      muse = hd_effect(ask ? "voice_m_en_muse_call_b" : "voice_m_en_muse_call_a");
      c = channel_of(&a, muse);
      CHECK(channel_of(&a, read) >= 0 && c >= 0 && channel_of(&a, hd_effect("voice_m_en_done_coffee")) == -1);
      if (c >= 0)
      {
         CHECK(a.ch[c].pos < -hd_sample_len(read) + HD_SAMPLES_PER_FRAME * 2);
         for (k = 0; k < 10; k++)
            hd_mix(&a, out, 1);
         CHECK(a.ch[c].sample == muse && a.ch[c].pos < 0); /* still waiting while the ask is read */
         for (k = 0; k < 60 && a.ch[c].pos < 0; k++)
            hd_mix(&a, out, 1);
         hd_mix(&a, out, 1);
         CHECK(a.ch[c].sample == muse && a.ch[c].pos > 0 && channel_of(&a, read) == -1);
      }
      /* the answer, at the desk at once, waits until the thought is over */
      g->act = RA_WORK + 1;
      g->energy = REMOTE_ENERGY_MAX;
      remote_plays(&a, 1, 0);
      CHECK(g->phone_who[(g->phone_n - 1) % REMOTE_PHONE] == RW_YOU && c >= 0 && a.ch[c].sample == muse);
      c = channel_of(&a, hd_effect("voice_m_en_say_call"));
      CHECK(c >= 0 && a.ch[c].pos < -HD_SAMPLES_PER_FRAME);
      g->boss = 0;
      g->act = 0;
   }
   hd_lang = i;
   /* the pause silences the loops */
   g->drill_t = 600;
   remote_plays(&a, 2, PAD_START);
   CHECK(g->paused);
   remote_plays(&a, 40, 0);
   CHECK(channel_of(&a, drill) == -1);
   /* packages the engine refuses */
   files[0].text = "{\"format\": 3, \"title\": \"Remote\", \"game\": \"tetris\"}";
   n = make_zip(zip, files, 4);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "game") != NULL);
   files[0].text = "{\"format\": 3, \"title\": \"Remote\", \"game\": \"remote\", \"audio\": {\"effects\": {\"a\": \"shot.wav\", \"a\": \"shot.wav\"}}}";
   n = make_zip(zip, files, 4);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "same name") != NULL);
   files[0].text = "{\"format\": 3, \"title\": \"Remote\", \"game\": \"remote\", \"audio\": {\"effects\": {\"a\": 5}}}";
   n = make_zip(zip, files, 4);
   CHECK(hd_content_load(zip, n, &err) == 0);
   files[0].text = "{\"format\": 3, \"title\": \"Remote\", \"game\": \"remote\", \"audio\": {\"tracks\": {\"t\": {\"file\": \"missing.wav\"}}}}";
   n = make_zip(zip, files, 4);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "missing.wav") != NULL);
   CHECK(hd_effect("alarm") == -1 && hd_code_game == HD_GAME_LEVELS); /* a refused package leaves the built-in game */
   hd_content_builtin();
}

/* TELETRABAJO: CÁMARA APAGADA: the same walk twice, through a save state, and never out of the flat. */
static void test_remote(void)
{
   static hd_state a, b;
   static uint8_t save[HD_SAVE_SIZE];
   uint32_t ha, hb, h0;
   hd_input in[MAX_PLAYERS];
   const hd_remote *g = HD_GAME3D_C(&a, hd_remote);
   int32_t f;
   hd_res = 1;
   hd_reset(&a);
   hd_remote_new_day(&a);
   hd_reset(&b);
   hd_remote_new_day(&b);
   CHECK(a.show.on == 3 && memcmp(&a, &b, sizeof a) == 0);
   ha = hb = 2166136261u;
   /* east along the hall (the camera looks east) at a run, turning the camera now and then */
   for (f = 0; f < 400; f++)
   {
      memset(in, 0, sizeof in);
      in[0].buttons = PAD_UP | PAD_RUN | ((f / 50) % 4 == 3 ? PAD_L : 0);
      in[0].lx = (f / 25) % 2 ? 9000 : -9000;
      hd_step(&a, in);
      hd_step(&b, in);
      if (f % 40 == 0)
      {
         hd_draw(&a, fb);
         ha = fnv(fb, sizeof(uint32_t) * HD_W * HD_H, ha);
         hd_draw(&b, fb);
         hb = fnv(fb, sizeof(uint32_t) * HD_W * HD_H, hb);
      }
   }
   CHECK(ha == hb);
   /* it walked, and stayed inside the house's outer walls (the front door does not let it out) */
   CHECK(g->t == 400 && g->x > ROOM3D_AT(500) && g->x < ROOM3D_AT(1600) && g->z > ROOM3D_AT(-210) && g->z < ROOM3D_AT(1250));
   h0 = ha;
   hd_save(&a, save);
   memset(in, 0, sizeof in);
   for (f = 0; f < 30; f++)
   {
      in[0].buttons = PAD_DOWN;
      hd_step(&a, in);
   }
   hd_draw(&a, fb);
   ha = fnv(fb, sizeof(uint32_t) * HD_W * HD_H, h0);
   CHECK(hd_load(&b, save, HD_SAVE_SIZE) == 1);
   for (f = 0; f < 30; f++)
      hd_step(&b, in);
   hd_draw(&b, fb);
   hb = fnv(fb, sizeof(uint32_t) * HD_W * HD_H, h0);
   CHECK(ha == hb && memcmp(&a, &b, sizeof a) == 0);
}
#endif

/* A package of format 2: a dark level with lights, colors, waves, zoom, outlines, sound and a dialog. */
static void test_effects_package(void)
{
   static uint8_t zip[200000];
   static char level[100000], with_fx[110000];
   static hd_state s;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, i, opened = 0, closed = 0, zoomed = 0;
   zfile files[2] = {{0}};
   flat_level(level, sizeof level, 120, 48); /* tall enough to zoom out: the view never shows past the level */
   level[strlen(level) - 1] = 0; /* drop the closing brace to add the effects */
   snprintf(with_fx, sizeof with_fx, "%s, \"effects\": {\"darkness\": 200, \"player_light\": 90, \"lights\": [{\"x\": 10, \"y\": 18, \"radius\": 80, \"color\": \"#ff9040\", \"flicker\": 8}],"
            " \"grade\": \"night\", \"grade_amount\": 200, \"bloom\": 180, \"waves\": {\"row\": 20, \"amplitude\": 2}, \"zoom\": \"auto\","
            " \"outline\": \"#1a1020\", \"shadows\": true, \"lowpass\": 80, \"echo\": 150,"
            " \"dialogs\": [{\"column\": 6, \"name\": \"Ana\", \"text\": {\"en\": \"Hello there\", \"es\": \"\xc2\xa1Hola!\"}}]}}", level);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 2, \"title\": \"Dark\", \"players\": 2, \"level\": \"level.json\"}";
   files[1].name = "level.json";
   files[1].text = with_fx;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  effects package: %s\n", err);
   CHECK(hd_fx.darkness == 200 && hd_fx.lights == 1 && hd_fx.grade == GRADE_NIGHT && hd_fx.zoom_auto && hd_fx.dialogs == 1);
   CHECK(strcmp(hd_fx.dialog_text[0][2], "Hello there") == 0); /* Portuguese left out: English */
   hd_reset(&s);
   CHECK(s.lowpass == 80 && s.echo == 150 * HD_RATE / 1000);
   for (f = 0; f < 900; f++)
   {
      memset(in, 0, sizeof in);
      if (f == 10)
         in[0].buttons = PAD_START;
      if (f == 12)
         in[1].buttons = PAD_START;
      if (f > 20)
         in[0].buttons = PAD_RIGHT | PAD_RUN; /* player 1 runs off, player 2 stays: the camera zooms out */
      if (s.dlg)
      {
         opened = 1;
         if (f % 20 == 0)
            in[0].buttons |= PAD_JUMP;
      }
      else if (opened)
         closed = 1;
      hd_step(&s, in);
      hd_draw(&s, fb);
      hd_mix(&s, audio, 1);
      if (s.zoom < 240)
         zoomed = 1;
   }
   CHECK(opened && closed && (s.dlg_done & 1));
   CHECK(zoomed);
   for (i = 0; i < 2; i++)
      CHECK(s.p[i].active);
   /* effects that make no sense are refused */
   snprintf(with_fx, sizeof with_fx, "%s, \"effects\": {\"grade\": \"purple\"}}", level);
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0);
   snprintf(with_fx, sizeof with_fx, "%s, \"effects\": {\"darkness\": 999}}", level);
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0);
   files[0].text = "{\"format\": 4, \"title\": \"Later\", \"level\": \"level.json\"}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "newer") != NULL);
   hd_content_builtin();
}

/* One cell of a flat_level's text. */
static void put_cell(char *level, int w, int x, int y, char c)
{
   char *rows = strstr(level, "\"rows\": [") + 9; /* the '[' of the rows */
   rows[1 + y * (w + 3) + 1 + x] = c;
}

/* The goal counts near its height: running far under it (a level that climbs) does not clear the stage. */
static void test_goal_height(void)
{
   static uint8_t zip[200000];
   static char level[100000];
   static hd_state s;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, high;
   zfile files[2] = {{0}};
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 2, \"title\": \"Tall\", \"players\": 1, \"screen\": \"9:16\", \"level\": \"level.json\"}";
   files[1].name = "level.json";
   files[1].text = level;
   for (high = 1; high >= 0; high--)
   {
      flat_level(level, sizeof level, 24, 64); /* ground on rows 61 to 63, the players start on row 60 */
      put_cell(level, 24, 14, high ? 20 : 60, 'F');
      n = make_zip(zip, files, 2);
      CHECK(hd_content_load(zip, n, &err) == 1);
      if (err)
         printf("  goal height: %s\n", err);
      hd_reset(&s);
      for (f = 0; f < 400; f++)
      {
         memset(in, 0, sizeof in);
         if (f == 10)
            in[0].buttons = PAD_START;
         if (f > 20)
            in[0].buttons = PAD_RIGHT;
         hd_step(&s, in);
      }
      CHECK(!high || FX_INT(s.p[0].x) > 16 * 16); /* under the high goal, it ran past its column */
      CHECK(high ? s.phase != PH_CLEAR : s.phase == PH_CLEAR);
   }
   hd_content_builtin();
}

/* Format 3's physics: a bigger hero that stands on the floor and jumps as high as asked. */
static void test_physics(void)
{
   static uint8_t zip[200000];
   static char level[100000];
   static hd_state s;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, top, floor_y;
   zfile files[2] = {{0}};
   flat_level(level, sizeof level, 60, 30); /* ground on rows 27 to 29 */
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Big\", \"players\": 1, \"level\": \"level.json\","
                   " \"physics\": {\"hitbox\": [36, 76], \"walk\": 300, \"jump\": 1000, \"gravity\": 50, \"fall_max\": 1200}}";
   files[1].name = "level.json";
   files[1].text = level;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  physics: %s\n", err);
   CHECK(PW == 36 && PH == 76 && hd_phys.walk_max == FX(3) && hd_phys.jump_speed == FX(10) && hd_phys.run_max == FX(4));
   floor_y = 27 * TILE;
   hd_reset(&s);
   top = 1 << 30;
   for (f = 0; f < 200; f++)
   {
      memset(in, 0, sizeof in);
      if (f == 10)
         in[0].buttons = PAD_START;
      if (f >= 60 && f < 80)
         in[0].buttons = PAD_JUMP;
      hd_step(&s, in);
      if (f == 59)
         CHECK(FX_INT(s.p[0].y) + PH == floor_y); /* standing on the floor */
      top = hd_min(top, FX_INT(s.p[0].y));
   }
   /* walking on flat ground: on the ground every frame, never past the top speed (a standing player used to be in the air every other frame) */
   for (f = 0; f < 120; f++)
   {
      memset(in, 0, sizeof in);
      in[0].buttons = PAD_RIGHT;
      hd_step(&s, in);
      if (f > 20)
      {
         CHECK(s.p[0].ground == 1);
         CHECK(s.p[0].vx <= hd_phys.walk_max);
      }
   }
   /* v^2 / 2g with v = 10 px and g = 0.28 while held (the default hold gravity): about 178 px */
   CHECK(floor_y - PH - top > 150);
   CHECK(FX_INT(s.p[0].y) + PH == floor_y); /* and back on the floor */
   /* out of range */
   files[0].text = "{\"format\": 3, \"title\": \"Bad\", \"level\": \"level.json\", \"physics\": {\"hitbox\": [2, 500]}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0);
   files[0].text = "{\"format\": 3, \"title\": \"Bad\", \"level\": \"level.json\", \"physics\": {\"jump\": -5}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0);
   /* the music's loop point is checked before anything is read: a huge one is refused, never wrapped around */
   files[0].text = "{\"format\": 3, \"title\": \"Loop\", \"level\": \"level.json\", \"music\": {\"file\": \"m.wav\", \"loop_from\": 999999999999999}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "loop_from") != NULL);
   files[0].text = "{\"format\": 3, \"title\": \"Loop\", \"level\": \"level.json\", \"music\": {\"file\": \"m.wav\", \"loop_from\": 600001}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "loop_from") != NULL);
   hd_content_builtin();
   CHECK(PW == 10 && PH == 22); /* the built-in game's again */
}

/* A w x h PNG (RGBA, every pixel opaque grey) with stored deflate blocks; its size. */
/* tiny_png's color, RGBA (half see-through gray by default) */
static uint32_t tiny_rgba = 0x80808080u;

static size_t tiny_png(uint8_t *out, uint32_t w, uint32_t h)
{
   static uint8_t raw[1 << 16];
   uint8_t *p = out, *crc_from;
   uint32_t raw_len = (w * 4 + 1) * h, i, a = 1, b = 0;
   for (i = 0; i < raw_len; i++)
      raw[i] = (uint8_t)(tiny_rgba >> (24 - 8 * ((i % (w * 4 + 1) + 3) % 4)));
   for (i = 0; i < h; i++)
      raw[i * (w * 4 + 1)] = 0;
   memcpy(p, "\x89PNG\r\n\x1a\n", 8);
   p += 8;
#define BE32(v) do { uint32_t v_ = (v); *p++ = (uint8_t)(v_ >> 24); *p++ = (uint8_t)(v_ >> 16); *p++ = (uint8_t)(v_ >> 8); *p++ = (uint8_t)v_; } while (0)
   BE32(13);
   crc_from = p;
   memcpy(p, "IHDR", 4); p += 4;
   BE32(w); BE32(h);
   *p++ = 8; *p++ = 6; *p++ = 0; *p++ = 0; *p++ = 0;
   BE32(hd_crc32(crc_from, (size_t)(p - crc_from)));
   BE32(2 + 5 + raw_len + 4);
   crc_from = p;
   memcpy(p, "IDAT", 4); p += 4;
   *p++ = 0x78; *p++ = 0x01;
   *p++ = 1; *p++ = (uint8_t)raw_len; *p++ = (uint8_t)(raw_len >> 8); *p++ = (uint8_t)~raw_len; *p++ = (uint8_t)(~raw_len >> 8);
   memcpy(p, raw, raw_len); p += raw_len;
   for (i = 0; i < raw_len; i++)
   {
      a = (a + raw[i]) % 65521;
      b = (b + a) % 65521;
   }
   BE32(b << 16 | a);
   BE32(hd_crc32(crc_from, (size_t)(p - crc_from)));
   BE32(0);
   crc_from = p;
   memcpy(p, "IEND", 4); p += 4;
   BE32(hd_crc32(crc_from, 4));
#undef BE32
   return (size_t)(p - out);
}

/* Format 3's screens: a very thin picture is refused, never read out of bounds; a fitting one loads. */
static void test_screens(void)
{
   static uint8_t zip[300000], png[70000];
   static char level[100000];
   const char *err;
   size_t n;
   zfile files[3] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Thin\", \"level\": \"level.json\", \"screens\": {\"title\": \"t.png\"}}";
   files[1].name = "level.json";
   files[1].text = level;
   files[2].name = "t.png";
   files[2].text = (const char *)png;
   files[2].len = tiny_png(png, 1, 1000); /* 1 x 1000: a crop of zero columns before the fix */
   n = make_zip(zip, files, 3);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "16 x 16") != NULL);
   files[2].len = tiny_png(png, 16, 900); /* thin but allowed: cropped to one column at least */
   n = make_zip(zip, files, 3);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  screens: %s\n", err);
   files[2].len = tiny_png(png, 40, 30);
   n = make_zip(zip, files, 3);
   CHECK(hd_content_load(zip, n, &err) == 1);
   hd_content_builtin();
}

/* A weapon (format 3): held, it fires; shots hurt an enemy that takes two hits; the fire button no
   longer runs; a save state taken with shots in the air plays on exactly; bad weapons are refused. */
static void test_weapon(void)
{
   static uint8_t zip[300000], save[HD_SAVE_SIZE];
   static char level[100000];
   static hd_state s, again;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, flying = 0, popped = 0, fastest = 0, row;
   char *at;
   zfile files[2] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   /* an enemy on the floor 18 cells ahead of the start: row 26 is the 27th quoted row */
   for (at = level, row = 0; row < 27 * 2 - 1; at++)
      if (*at == '"')
         row++;
   at[20] = 'E';
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Shots\", \"level\": \"level.json\","
                   " \"weapon\": {\"rate\": 5, \"speed\": 600, \"range\": 300, \"enemy_health\": 2}}";
   files[1].name = "level.json";
   files[1].text = level;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  weapon: %s\n", err);
   CHECK(hd_weapon.on && hd_weapon.button == PAD_RUN && hd_weapon.life == 50);
   hd_reset(&s);
   memset(in, 0, sizeof in);
   in[0].buttons = PAD_START;
   hd_step(&s, in);
   CHECK(s.phase == PH_PLAY && s.p[0].active);
   CHECK(s.e[0].alive == 1 && s.e[0].hp == 2);
   for (f = 0; f < 200; f++)
   {
      int32_t k, live = 0;
      in[0].buttons = PAD_RUN | (f < 100 ? PAD_RIGHT : 0);
      hd_step(&s, in);
      for (k = 0; k < MAX_SHOTS; k++)
         live += s.shot[k].life > 0;
      flying = hd_max(flying, live);
      fastest = hd_max(fastest, hd_abs(s.p[0].vx));
      if (s.e[0].alive == 2)
         popped = 1;
      if (f == 3)
      {
         /* a shot in the air: the same game from a save state */
         int32_t g;
         hd_save(&s, save);
         again = s;
         for (g = 0; g < 40; g++)
            hd_step(&again, in);
         CHECK(hd_load(&s, save, HD_SAVE_SIZE) == 1);
      }
      if (f == 3 + 40)
         CHECK(memcmp(&s, &again, sizeof s) == 0);
   }
   CHECK(flying >= 3);       /* held, it fires again and again */
   CHECK(popped);            /* two hits pop the enemy */
   CHECK(fastest <= hd_phys.walk_max); /* the fire button does not run */
   CHECK(s.p[0].respawn == 0 && s.p[0].hurt == 0);
   files[0].text = "{\"format\": 3, \"title\": \"Shots\", \"level\": \"level.json\", \"weapon\": {\"button\": \"start\"}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "button") != NULL);
   CHECK(!hd_weapon.on);
   files[0].text = "{\"format\": 3, \"title\": \"Shots\", \"level\": \"level.json\", \"weapon\": {\"rate\": 1}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "out of range") != NULL);
   hd_content_builtin();
}

extern int32_t hd_stage_count; /* sprite.h (not included here: its names meet the bones') */
void hd_stage_select(int32_t k);

/* format 3's resolution: the same game drawn 2 or 3 times bigger with pictures made for that size (the
   API's frame grows, the rules do not change); a texture of the wrong size for it is refused. */
static void test_resolution(void)
{
   static uint8_t zip[400000], png[200000];
   static char level[100000], man[400];
   static hd_state s, t;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, k;
   uint32_t first = 0;
   zfile files[3] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   files[1].name = "level.json";
   files[1].text = level;
   files[2].name = "ground.png";
   files[2].text = (const char *)png;
   for (k = 2; k <= 3; k++)
   {
      snprintf(man, sizeof man, "{\"format\": 3, \"title\": \"Big\", \"level\": \"level.json\", \"resolution\": \"%s\","
               " \"textures\": {\"ground\": \"ground.png\"}, \"effects\": {}}", k == 2 ? "720p" : "1080p");
      files[0].name = "manifest.json";
      files[0].text = man;
      files[2].len = tiny_png(png, 16 * k * 2, 16 * k);
      n = make_zip(zip, files, 3);
      CHECK(hd_content_load(zip, n, &err) == 1);
      if (err)
         printf("  resolution: %s\n", err);
      CHECK(hd_res == k && HD_OUT_W == 640 * k && HD_OUT_H == 360 * k && HD_W == 640);
      hd_reset(&s);
      memset(in, 0, sizeof in);
      in[0].buttons = PAD_START;
      hd_step(&s, in);
      for (f = 0; f < 120; f++)
      {
         in[0].buttons = PAD_RIGHT | (f % 40 < 10 ? PAD_JUMP : 0);
         hd_step(&s, in);
      }
      if (k == 2)
         t = s; /* the rules are the same at every size */
      else
         CHECK(memcmp(&s, &t, sizeof s) == 0);
      hd_draw(&s, fb);
      first = fb[0];
      hd_draw(&s, fb);
      CHECK(fb[0] == first && fb[(size_t)HD_OUT_W * HD_OUT_H - 1] != 0xdeadbeefu);
      files[2].len = tiny_png(png, 16, 16); /* made for 360p */
      n = make_zip(zip, files, 3);
      CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "multiples of 16") != NULL && hd_res == 1);
   }
   hd_content_builtin();
}

/* picture.c and sprite.c (sprite.h is not included here: its names meet the bones') */
extern int32_t hd_art;
extern hd_image hd_textures[]; /* TL_COUNT kinds, then their left and right ends */
void hd_pic_clean(uint32_t *px, int64_t n);
uint32_t *hd_pic_shrink(const uint32_t *px, int32_t pitch, int32_t sw, int32_t sh, int32_t dw, int32_t dh);

/* Format 3's art_scale: an image AI's almost solid pixels become solid; pictures painted bigger are made
   smaller by area (a see-through pixel's color left out); one package draws at the size the host asks. */
static void test_art_scale(void)
{
   static uint8_t zip[400000], png[200000];
   static char level[100000], man[400];
   uint32_t px[6] = { 0xfd102030u, 0x05ffffffu, 0x80405060u, 0xffff0000u, 0x0000ff00u, 0xffff0000u }, *out;
   uint32_t flat[5 * 3];
   const char *err;
   size_t n;
   int32_t k;
   zfile files[3] = {{0}};
   hd_pic_clean(px, 3);
   CHECK(px[0] == 0xff102030u && px[1] == 0 && px[2] == 0x80405060u);
   /* a 2 x 2 square, half solid red, half empty green: one pixel, half see-through and red */
   {
      uint32_t sq[4] = { 0xffff0000u, 0x0000ff00u, 0x0000ff00u, 0xffff0000u };
      out = hd_pic_shrink(sq, 2, 2, 2, 1, 1);
      CHECK(out && out[0] == 0x80ff0000u);
      free(out);
   }
   /* 5 x 3 to 3 x 2 (not a whole ratio): an even color stays that color, everywhere */
   for (k = 0; k < 15; k++)
      flat[k] = 0xff336699u;
   out = hd_pic_shrink(flat, 5, 5, 3, 3, 2);
   CHECK(out != NULL);
   for (k = 0; out && k < 6; k++)
      CHECK(out[k] == 0xff336699u);
   free(out);

   flat_level(level, sizeof level, 60, 30);
   files[0].name = "manifest.json";
   files[0].text = man;
   files[1].name = "level.json";
   files[1].text = level;
   files[2].name = "ground.png";
   files[2].text = (const char *)png;
   files[2].len = tiny_png(png, 96, 96); /* a cell's piece painted 6 times: 2160p */
   snprintf(man, sizeof man, "{\"format\": 3, \"title\": \"Art\", \"level\": \"level.json\", \"art_scale\": 6,"
            " \"textures\": {\"ground\": \"ground.png\"}}");
   n = make_zip(zip, files, 3);
   for (k = 0; k <= 3; k++)
   {
      hd_res_host = k;
      CHECK(hd_content_load(zip, n, &err) == 1);
      if (err)
         printf("  art_scale: %s\n", err);
      /* no choice: the package's resolution (360p by default) */
      CHECK(hd_art == 6 && hd_res == (k ? k : 1) && HD_OUT_W == 640 * hd_res);
      CHECK(hd_textures[TL_GROUND].w == 16 * hd_res && hd_textures[TL_GROUND].h == 16 * hd_res);
   }
   /* never drawn bigger than its pictures: painted for 720p, asked for 1080p */
   files[2].len = tiny_png(png, 32, 32);
   snprintf(man, sizeof man, "{\"format\": 3, \"title\": \"Art\", \"level\": \"level.json\", \"resolution\": \"720p\","
            " \"textures\": {\"ground\": \"ground.png\"}}");
   n = make_zip(zip, files, 3);
   hd_res_host = 3;
   CHECK(hd_content_load(zip, n, &err) == 1 && hd_res == 2 && hd_art == 2 && hd_textures[TL_GROUND].w == 32);
   hd_res_host = 1; /* and a 720p package played at 360p */
   CHECK(hd_content_load(zip, n, &err) == 1 && hd_res == 1 && hd_textures[TL_GROUND].w == 16);
   snprintf(man, sizeof man, "{\"format\": 3, \"title\": \"Art\", \"level\": \"level.json\", \"art_scale\": 7}");
   n = make_zip(zip, files, 3);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "art_scale") != NULL);
   hd_res_host = 0;
   hd_content_builtin();
   CHECK(hd_res == 1 && hd_art == 1);
}

/* Format 3's texture ends: the cell where a floor stops at a pit is drawn from "ground_left" or
   "ground_right" (laid like "ground"); an end of another size is refused. */
static void test_texture_ends(void)
{
   static uint8_t zip[400000], png[3][20000];
   static char level[20000];
   static hd_state s;
   const char *err;
   size_t n, k;
   int32_t x, y, seen[3] = { 0 };
   const uint32_t colors[3] = { 0x204060ffu, 0xc02020ffu, 0x20c020ffu }; /* RGBA: ground, left end, right end */
   zfile files[5] = {{0}};
   /* floor from x 0 to 11, a pit 12..15, floor again: the left floor's right end and the right floor's left end */
   n = (size_t)snprintf(level, sizeof level, "{\"width\": 60, \"height\": 30, \"start\": [2, 26], \"rows\": [");
   for (y = 0; y < 30; y++)
   {
      level[n++] = '"';
      for (x = 0; x < 60; x++)
         level[n++] = y >= 27 && (x < 12 || x > 15) ? '#' : '.';
      n += (size_t)snprintf(level + n, sizeof level - n, "\"%s", y < 29 ? "," : "]}");
   }
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Ends\", \"level\": \"level.json\", \"textures\": {\"ground_top\": \"g.png\", \"ground\": \"g.png\","
                   " \"ground_top_left\": \"l.png\", \"ground_left\": \"l.png\", \"ground_top_right\": \"r.png\", \"ground_right\": \"r.png\"}}";
   files[1].name = "level.json";
   files[1].text = level;
   for (k = 0; k < 3; k++)
   {
      tiny_rgba = colors[k];
      files[2 + k].name = k == 0 ? "g.png" : k == 1 ? "l.png" : "r.png";
      files[2 + k].text = (const char *)png[k];
      files[2 + k].len = tiny_png(png[k], 32, 32);
   }
   tiny_rgba = 0x80808080u;
   n = make_zip(zip, files, 5);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  texture ends: %s\n", err);
   hd_reset(&s);
   hd_draw(&s, fb);
   for (k = 0; k < (size_t)HD_OUT_W * HD_OUT_H; k++)
      for (x = 0; x < 3; x++)
         if (fb[k] == colors[x] >> 8)
            seen[x]++;
   /* every floor cell column but the two ends is ground; each end column is its own color */
   CHECK(seen[0] > 0 && seen[1] > 0 && seen[2] > 0 && seen[1] == seen[2] && seen[0] > seen[1] * 10);
   /* an end of another size than its kind's */
   files[3].len = tiny_png(png[1], 16, 16);
   n = make_zip(zip, files, 5);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "same size") != NULL);
   hd_content_builtin();
}

/* A level of w x 30 cells: a floor, a coin row near the start and a goal near the end. */
static void goal_level(char *out, size_t cap, int w)
{
   int x, y;
   size_t n = (size_t)snprintf(out, cap, "{\"width\": %d, \"height\": 30, \"start\": [2, 26], \"rows\": [", w);
   for (y = 0; y < 30; y++)
   {
      out[n++] = '"';
      for (x = 0; x < w; x++)
         out[n++] = y >= 27 ? '#' : (y == 26 && x == w - 6) ? 'F' : (y == 26 && x >= 5 && x < 9) ? 'o' : '.';
      out[n++] = '"';
      if (y + 1 < 30)
         out[n++] = ',';
   }
   snprintf(out + n, cap - n, "]}");
}

/* Several levels in one package (format 3's levels): clearing one starts the next, keeping the coins;
   a save state brings back the level it was taken in; the last one ends the game. */
static void test_stages(void)
{
   static uint8_t zip[400000], save[HD_SAVE_SIZE];
   static char one[100000], two[100000];
   static hd_state s, b;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, coins = 0;
   zfile files[3] = {{0}};
   goal_level(one, sizeof one, 50);
   goal_level(two, sizeof two, 70);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Stages\", \"levels\": [{\"level\": \"one.json\", \"sky\": [\"#102030\", \"#203040\"]},"
                   " {\"level\": \"two.json\"}]}";
   files[1].name = "one.json";
   files[1].text = one;
   files[2].name = "two.json";
   files[2].text = two;
   n = make_zip(zip, files, 3);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  stages: %s\n", err);
   CHECK(hd_stage_count == 2 && hd_map_w == 50 && hd_sky_top == 0x102030u);
   hd_reset(&s);
   memset(in, 0, sizeof in);
   in[0].buttons = PAD_START;
   hd_step(&s, in);
   for (f = 0; f < 2000 && s.stage == 0; f++)
   {
      in[0].buttons = PAD_RIGHT;
      hd_step(&s, in);
      coins = s.p[0].coins;
   }
   CHECK(s.stage == 1 && hd_map_w == 70 && s.phase == PH_PLAY);
   CHECK(coins == 4 && s.p[0].coins == 4 && s.p[0].active); /* kept into the next level */
   CHECK(FX_INT(s.p[0].x) < 100);                           /* at the new level's start */
   hd_save(&s, save);
   hd_reset(&b); /* level 1 again ... */
   CHECK(hd_map_w == 50);
   CHECK(hd_load(&b, save, HD_SAVE_SIZE) == 1); /* ... and the save state's level 2 back */
   in[0].buttons = PAD_RIGHT;
   hd_step(&b, in);
   CHECK(b.stage == 1 && hd_map_w == 70);
   for (f = 0; f < 3000 && s.phase != PH_TITLE; f++)
   {
      in[0].buttons = PAD_RIGHT;
      hd_step(&s, in);
   }
   CHECK(s.phase == PH_TITLE && s.stage == 0 && hd_map_w == 50); /* the last level ends the game */
   files[0].text = "{\"format\": 3, \"title\": \"Stages\", \"levels\": [{\"level\": \"missing.json\"}]}";
   n = make_zip(zip, files, 3);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "level 1") != NULL && hd_stage_count == 0);
   hd_content_builtin();
}

/* The super attack: nothing without charge; shots' hits charge it; its button throws the granules at the
   release, the player standing still and unhurt the whole time; granules fall and cost `damage` hits. */
static void test_super(void)
{
   static uint8_t zip[300000];
   static char level[100000];
   static hd_state s;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, k, row, granules = 0, fell = 0;
   char *at;
   zfile files[2] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   for (at = level, row = 0; row < 27 * 2 - 1; at++)
      if (*at == '"')
         row++;
   at[14] = 'E';
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Super\", \"level\": \"level.json\", \"weapon\": {\"button\": \"x\", \"rate\": 4,"
                   " \"enemy_health\": 20, \"super\": {\"button\": \"y\", \"charge\": 2, \"granules\": 6, \"damage\": 3, \"frames\": 30, \"release\": 10}}}";
   files[1].name = "level.json";
   files[1].text = level;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  super: %s\n", err);
   CHECK(hd_weapon.super_on && hd_weapon.super_button == PAD_Y && hd_weapon.super_count == 6);
   hd_reset(&s);
   memset(in, 0, sizeof in);
   in[0].buttons = PAD_START;
   hd_step(&s, in);
   in[0].buttons = PAD_Y; /* no charge yet: nothing */
   hd_step(&s, in);
   CHECK(s.p[0].super_t == 0);
   for (f = 0; f < 120 && s.p[0].charge < 2; f++)
   {
      in[0].buttons = PAD_X;
      hd_step(&s, in);
   }
   CHECK(s.p[0].charge == 2);
   in[0].buttons = 0;
   hd_step(&s, in);
   in[0].buttons = PAD_Y;
   hd_step(&s, in);
   CHECK(s.p[0].super_t == 1 && s.p[0].charge == 0);
   in[0].buttons = 0;
   for (f = 0; f < 40; f++)
   {
      /* an enemy on the player until the granules leave: no harm during the attack */
      if (s.p[0].super_t && s.p[0].super_t < hd_weapon.super_release - 1)
      {
         s.e[1] = s.e[0];
         s.e[1].alive = 1;
         s.e[1].x = s.p[0].x;
         s.e[1].y = s.p[0].y + FX(PH - EH);
      }
      else
         s.e[1].alive = 0; /* gone before the granules leave, so they fly and fall */
      if (s.p[0].super_t)
         CHECK(s.p[0].vx == 0 && s.p[0].hurt < HURT_FRAMES - 10); /* only what is left of joining */
      hd_step(&s, in);
      {
         int32_t now = 0;
         for (k = 0; k < MAX_SHOTS; k++)
         {
            now += s.shot[k].granule && (s.shot[k].life || s.shot[k].hit);
            if (s.shot[k].granule && s.shot[k].life && s.shot[k].vy > 0)
               fell = 1;
         }
         granules = hd_max(granules, now);
      }
   }
   CHECK(granules == 6 && fell);
   {
      /* a granule costs `damage` hits */
      hd_shot *q = &s.shot[0];
      memset(q, 0, sizeof *q);
      s.e[2] = s.e[0];
      s.e[2].alive = 1;
      s.e[2].hp = 20;
      s.e[2].x += FX(300); /* away from the others */
      q->life = 10;
      q->granule = 1;
      q->damage = hd_weapon.super_damage;
      q->x = s.e[2].x + FX(EW / 2);
      q->y = s.e[2].y + FX(EH / 2);
      hd_step(&s, in);
      CHECK(s.e[2].hp == 17);
   }
   CHECK(s.p[0].super_t == 0 && s.p[0].hp == 0 && !s.p[0].ko); /* no health in this game: hp stays 0 */
   files[0].text = "{\"format\": 3, \"title\": \"Super\", \"level\": \"level.json\", \"weapon\": {\"super\": {\"frames\": 20, \"release\": 20}}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "super") != NULL);
   hd_content_builtin();
}

/* Health (format 3): an enemy's hits cost health, not coins; the last knocks the player out, who comes
   back at the checkpoint with all of it; nothing hurts a knocked-out player; bad health is refused. */
static void test_health(void)
{
   static uint8_t zip[300000];
   static char level[100000];
   static hd_state s;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, row, lowest = 99, knocked = 0, back = 0;
   char *at;
   zfile files[2] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   for (at = level, row = 0; row < 27 * 2 - 1; at++)
      if (*at == '"')
         row++;
   at[8] = 'E'; /* close: it walks into the player standing at the start */
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Health\", \"level\": \"level.json\", \"health\": {\"hits\": 2, \"worn\": 1, \"knockout\": 20}}";
   files[1].name = "level.json";
   files[1].text = level;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  health: %s\n", err);
   hd_reset(&s);
   memset(in, 0, sizeof in);
   in[0].buttons = PAD_START;
   hd_step(&s, in);
   in[0].buttons = 0;
   CHECK(s.p[0].hp == 2);
   s.p[0].coins = 5;
   for (f = 0; f < 1500 && !back; f++)
   {
      /* the enemy comes back at the player whenever it can be hurt again */
      if (!s.p[0].hurt && !s.p[0].ko && !s.p[0].respawn && s.p[0].ground)
      {
         s.e[0].alive = 1;
         s.e[0].awake = 1;
         s.e[0].x = s.p[0].x + FX(PW - 2);
         s.e[0].y = s.p[0].y + FX(PH - EH);
         s.e[0].vx = -FX(1);
      }
      hd_step(&s, in);
      lowest = hd_min(lowest, s.p[0].hp);
      if (s.p[0].ko)
      {
         knocked = 1;
         CHECK(s.p[0].hp == 0 && s.p[0].vx == 0 && s.p[0].hurt == 0);
      }
      else if (knocked && !s.p[0].respawn && s.p[0].hp == 2)
         back = 1;
   }
   CHECK(lowest == 0 && knocked && back);
   CHECK(s.p[0].coins == 5); /* hits cost health, not coins */
   files[0].text = "{\"format\": 3, \"title\": \"Health\", \"level\": \"level.json\", \"health\": {\"hits\": 2, \"worn\": 3}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "health") != NULL);
   CHECK(!hd_health.on);
   hd_content_builtin();
}

/* A rubber-hose puppet: loads, refuses bad sizes, and draws in every pose, facing both ways and at the
   screen's edges, without leaving the screen's memory. */
static void test_rig(void)
{
   static uint8_t zip[300000], png[70000];
   static char level[100000];
   static hd_state s;
   const char *err;
   size_t n;
   int32_t pose, x;
   zfile files[3] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Puppet\", \"level\": \"level.json\","
                   " \"sprites\": {\"hero\": {\"players\": [\"a\"], \"skins\": {\"a\": {\"rig\": {"
                   "\"body\": {\"file\": \"a.png\", \"frame\": [16, 24], \"feet\": 2},"
                   " \"hand\": {\"file\": \"a.png\", \"frame\": [16, 24]},"
                   " \"foot\": {\"file\": \"a.png\", \"frame\": [16, 24], \"feet\": 2},"
                   " \"limb\": 4, \"leg\": 20, \"arm\": 15, \"stride\": 84}}}}}}";
   files[1].name = "level.json";
   files[1].text = level;
   files[2].name = "a.png";
   files[2].text = (const char *)png;
   files[2].len = tiny_png(png, 48, 24); /* 3 parts, fewer than a rig names: the last one is used */
   n = make_zip(zip, files, 3);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  rig: %s\n", err);
   hd_reset(&s);
   s.phase = PH_PLAY;
   s.p[0].active = 1;
   for (pose = 0; pose < 6; pose++)
      for (x = -2; x <= 2; x++)
      {
         hd_player *p = &s.p[0];
         p->ground = pose != 2 && pose != 3;
         p->vx = pose == 1 ? FX(2) : 0;
         p->vy = pose == 2 ? -FX(3) : FX(2);
         p->facing = x < 0 ? -1 : 1;
         p->hurt = pose == 4 ? HURT_FRAMES : 0;
         p->still = pose == 5 ? 400 : 0; /* past BORED_AFTER (360): yawning */
         p->landed = pose == 0 ? 2 : 100;
         p->anim = 1 << 30;
         s.frame = pose == 5 ? 1 << 30 : pose * 37;
         /* the camera follows the player: put it near the level's ends and the floor */
         p->x = FX(x < 0 ? 2 : x > 0 ? 60 * 16 - 20 : 300);
         p->y = FX(x == 2 ? 2 : 26 * 16 - 40);
         hd_draw(&s, fb);
      }
   files[0].text = "{\"format\": 3, \"title\": \"Puppet\", \"level\": \"level.json\","
                   " \"sprites\": {\"hero\": {\"players\": [\"a\"], \"skins\": {\"a\": {\"rig\": {"
                   "\"body\": {\"file\": \"a.png\", \"frame\": [16, 24]}, \"hand\": {\"file\": \"a.png\", \"frame\": [16, 24]},"
                   " \"foot\": {\"file\": \"a.png\", \"frame\": [16, 24]}, \"limb\": 0}}}}}}";
   n = make_zip(zip, files, 3);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "limb 1-32") != NULL);
   hd_content_builtin();
}

/* A sprite drawn when the state's counters are at their highest (2^30): the frame stays inside the animation. */
static void test_sprite_counters(void)
{
   static uint8_t zip[300000], png[70000];
   static char level[100000];
   static hd_state s;
   const char *err;
   size_t n;
   int32_t i;
   zfile files[3] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Big counters\", \"level\": \"level.json\","
                   " \"sprites\": {\"hero\": {\"players\": [\"a\"], \"skins\": {\"a\": {\"idle\": {\"file\": \"a.png\", \"frame\": [16, 24], \"fps\": 60}}}},"
                   " \"coin\": {\"file\": \"a.png\", \"frame\": [16, 24], \"fps\": 60}, \"enemy\": {\"walk\": {\"file\": \"a.png\", \"frame\": [16, 24], \"fps\": 60}}}}";
   files[1].name = "level.json";
   files[1].text = level;
   files[2].name = "a.png";
   files[2].text = (const char *)png;
   files[2].len = tiny_png(png, 48, 24); /* 3 frames */
   n = make_zip(zip, files, 3);
   CHECK(hd_content_load(zip, n, &err) == 1);
   hd_reset(&s);
   s.phase = PH_PLAY;
   s.p[0].active = 1;
   s.p[0].ground = 1;
   s.frame = 1 << 30;
   for (i = 0; i < MAX_ENEMIES; i++)
      s.e[i].anim = 1 << 30;
   hd_draw(&s, fb); /* with 32-bit math the frame index was negative: out of bounds */
   hd_content_builtin();
}

static int32_t logged;

static void count_log(void *user, int32_t level, const char *msg)
{
   (void)user;
   (void)level;
   (void)msg;
   logged++;
}

/* The API (include/golink_hd.h), as a host uses it. */
static void test_api(void)
{
   golinkhd_config cfg;
   golinkhd_engine *e, *other;
   golinkhd_info info;
   golinkhd_frame_out out;
   golinkhd_pad pads[GOLINKHD_MAX_PLAYERS];
   const char *err;
   static uint8_t save[HD_SAVE_SIZE + 64];
   uint8_t *pkg;
   size_t n;
   uint32_t h1 = 0, h2 = 0;
   int32_t f;

   memset(&cfg, 0, sizeof cfg);
   cfg.api_version = GOLINKHD_API_VERSION + 1; /* a host from the future */
   CHECK(golinkhd_create(&cfg, &err) == NULL && err != NULL);
   cfg.api_version = GOLINKHD_API_VERSION;
   cfg.log = count_log;
   e = golinkhd_create(&cfg, &err);
   CHECK(e != NULL);
   if (!e)
      return;
   CHECK(golinkhd_create(&cfg, &err) == NULL); /* one at a time in this version */
   CHECK(golinkhd_api_version() == GOLINKHD_API_VERSION && strcmp(golinkhd_version(), HD_VERSION) == 0);
   golinkhd_get_info(e, &info);
   CHECK(info.width == 640 && info.height == 360 && info.fps == 60 && info.sample_rate == 48000 && info.players == 4);

   memset(pads, 0, sizeof pads);
   for (f = 0; f < 200; f++)
   {
      pads[0].buttons = f >= 10 && f < 14 ? GOLINKHD_START : (f >= 14 ? GOLINKHD_RIGHT | GOLINKHD_LEFT | GOLINKHD_JUMP : 0);
      golinkhd_frame(e, pads, 1, &out);
   }
   CHECK(out.width == 640 && out.height == 360 && out.pitch == 640 && out.audio_frames == HD_SAMPLES_PER_FRAME);
   CHECK(golinkhd_state_size(e) == HD_SAVE_SIZE);
   CHECK(!golinkhd_state_save(e, save, HD_SAVE_SIZE - 1));
   CHECK(golinkhd_state_save(e, save, sizeof save));
   for (f = 0; f < 60; f++)
   {
      golinkhd_frame(e, pads, 1, &out);
      h1 = fnv(out.pixels, sizeof(uint32_t) * 640 * 360, h1);
   }
   CHECK(golinkhd_state_load(e, save, sizeof save, &err));
   for (f = 0; f < 60; f++)
   {
      golinkhd_frame(e, pads, 1, &out);
      h2 = fnv(out.pixels, sizeof(uint32_t) * 640 * 360, h2);
   }
   CHECK(h1 == h2);
   save[0] = 'X';
   CHECK(!golinkhd_state_load(e, save, sizeof save, &err) && err != NULL);

   /* a package, then a damaged one: the engine says why and keeps its demo */
   pkg = slurp("tests/data/demo-deflate.glhd", &n);
   CHECK(pkg && golinkhd_load(e, pkg, n, &err));
   golinkhd_get_info(e, &info);
   CHECK(strcmp(info.title, "go-link HD demo") == 0 && (info.sha256[0] | info.sha256[1]) != 0);
   CHECK(logged > 0);
   CHECK(!golinkhd_load(e, pkg, n / 2, &err) && err != NULL);
   golinkhd_get_info(e, &info);
   CHECK(info.sha256[0] == 0 && info.sha256[1] == 0);
   free(pkg);
   golinkhd_load_demo(e, 1);
   golinkhd_set_language(e, "pt");
   golinkhd_set_music(e, 0);
   golinkhd_frame(e, NULL, 0, &out);
   CHECK(out.pixels != NULL);
   golinkhd_restart(e);
   golinkhd_destroy(e);
   other = golinkhd_create(&cfg, &err); /* free again once destroyed */
   CHECK(other != NULL);
   golinkhd_destroy(other);
   hd_lang = 0;
}

/* The cell (x, y) of a level made by flat_level: the character after its row's opening quote. */
static void set_cell(char *level, int x, int y, char c)
{
   char *at;
   int row;
   for (at = level, row = 0; row < 2 * y + 1; at++)
      if (*at == '"')
         row++;
   at[x] = c;
}

static int32_t count_kind(const hd_state *s, int32_t kind)
{
   int32_t i, n = 0;
   for (i = 0; i < MAX_ENEMIES; i++)
      n += s->e[i].alive == 1 && s->e[i].kind == kind;
   return n;
}

static int32_t live_bolts(const hd_state *s)
{
   int32_t i, n = 0;
   for (i = 0; i < MAX_BOLTS; i++)
      n += s->bolt[i].life > 0;
   return n;
}

/* format 3's spore and spitter: the spitter lobs at a player in front of it, the spore flies at them, both die to shots. */
static void test_enemy_kinds(void)
{
   static uint8_t zip[300000], save[HD_SAVE_SIZE];
   static char level[100000];
   static hd_state s, again;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, spat = 0, hp0, spore_x0, spitter = -1, spore = -1, i;
   zfile files[2] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   set_cell(level, 12, 26, 'P');
   set_cell(level, 24, 18, 'S');
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Kinds\", \"level\": \"level.json\", \"health\": {\"hits\": 50},"
                   " \"weapon\": {\"rate\": 5, \"speed\": 600, \"range\": 300},"
                   " \"enemies\": {\"spore\": {\"hitbox\": [12, 12], \"health\": 2, \"speed\": 60, \"range\": 400},"
                   " \"spitter\": {\"hitbox\": [14, 14], \"health\": 3, \"rate\": 40, \"range\": 300}}}";
   files[1].name = "level.json";
   files[1].text = level;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  kinds: %s\n", err);
   hd_reset(&s);
   memset(in, 0, sizeof in);
   in[0].buttons = PAD_START;
   hd_step(&s, in);
   for (i = 0; i < MAX_ENEMIES; i++)
   {
      if (s.e[i].alive && s.e[i].kind == EK_SPITTER)
         spitter = i;
      if (s.e[i].alive && s.e[i].kind == EK_SPORE)
         spore = i;
   }
   CHECK(spitter >= 0 && spore >= 0);
   if (spitter < 0 || spore < 0)
      return;
   CHECK(s.e[spitter].hp == 3 && s.e[spore].hp == 2);
   hp0 = s.p[0].hp;
   spore_x0 = FX_INT(s.e[spore].x);
   in[0].buttons = 0;
   for (f = 0; f < 240; f++)
   {
      hd_step(&s, in);
      spat = hd_max(spat, live_bolts(&s));
      if (f == 60)
      {
         /* a lob in the air: the same game from a save state */
         int32_t g;
         hd_save(&s, save);
         again = s;
         for (g = 0; g < 30; g++)
            hd_step(&again, in);
         CHECK(hd_load(&s, save, HD_SAVE_SIZE) == 1);
      }
      if (f == 60 + 30)
         CHECK(memcmp(&s, &again, sizeof s) == 0);
   }
   CHECK(spat >= 1);                                  /* it spat */
   CHECK(FX_INT(s.e[spore].x) < spore_x0 || s.e[spore].alive != 1); /* the spore came at the player */
   CHECK(s.p[0].hp < hp0);                            /* and something hurt them */
   /* shots: facing right, fired until the spitter pops */
   for (f = 0; f < 400 && s.e[spitter].alive == 1; f++)
   {
      in[0].buttons = PAD_RUN | (f < 4 ? PAD_RIGHT : 0);
      hd_step(&s, in);
   }
   CHECK(s.e[spitter].alive != 1);
   /* a boss's cell needs a boss */
   set_cell(level, 40, 26, 'X');
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "boss") != NULL);
   hd_content_builtin();
}

/* A level's boss: it wakes on screen, lets its brood out and spits, gets angry at half its health, and the goal opens once it is beaten. */
static void test_boss(void)
{
   static uint8_t zip[300000];
   static char level[100000];
   static hd_state s;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, boss = -1, i, broods = 0, spat = 0, angry = 0, open_before;
   zfile files[2] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   set_cell(level, 30, 26, 'X');
   set_cell(level, 55, 26, 'F');
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Boss\", \"level\": \"level.json\", \"health\": {\"hits\": 99},"
                   " \"weapon\": {\"rate\": 4, \"speed\": 700, \"range\": 500},"
                   " \"boss\": {\"name\": \"BIG\", \"hitbox\": [40, 40], \"health\": 30, \"attacks\": [\"brood\", \"spit\"],"
                   " \"rest\": 20, \"brood\": 4, \"spit\": 3, \"minion\": {\"hitbox\": [8, 8]}}}";
   files[1].name = "level.json";
   files[1].text = level;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  boss: %s\n", err);
   hd_reset(&s);
   memset(in, 0, sizeof in);
   in[0].buttons = PAD_START;
   hd_step(&s, in);
   for (i = 0; i < MAX_ENEMIES; i++)
      if (s.e[i].alive && s.e[i].kind == EK_BOSS)
         boss = i;
   CHECK(boss >= 0);
   if (boss < 0)
      return;
   open_before = hd_goal_open(&s);
   in[0].buttons = 0;
   for (f = 0; f < 200; f++)
   {
      hd_step(&s, in);
      broods = hd_max(broods, count_kind(&s, EK_MINION));
      spat = hd_max(spat, live_bolts(&s));
   }
   CHECK(s.boss == boss + 1 && s.boss_max == 30); /* awake, its bar full at 30 */
   CHECK(broods >= 2);                            /* its brood came out */
   CHECK(spat >= 2);                              /* and it spat a fan */
   CHECK(!open_before && !hd_goal_open(&s));      /* the goal waits for it */
   for (f = 0; f < 3000 && !s.boss_beaten; f++)
   {
      /* the player keeps firing at it, turning to it */
      in[0].buttons = PAD_RUN | ((f & 31) < 2 ? (FX_INT(s.p[0].x) < FX_INT(s.e[boss].x) ? PAD_RIGHT : PAD_LEFT) : 0);
      hd_step(&s, in);
      angry |= s.boss_angry;
   }
   CHECK(angry);           /* at half its health it got angry */
   CHECK(s.boss_beaten);   /* and it was beaten */
   CHECK(hd_goal_open(&s));
   CHECK(count_kind(&s, EK_MINION) == 0 && live_bolts(&s) == 0); /* its brood and shots went with it */
   hd_content_builtin();
}

/* format 3's dash: forward at its speed with no gravity for its frames, then a wait; once in the air. */
static void test_dash(void)
{
   static uint8_t zip[300000];
   static char level[100000];
   static hd_state s;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, x0, y0;
   zfile files[2] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Dash\", \"level\": \"level.json\","
                   " \"dash\": {\"button\": \"a\", \"speed\": 800, \"frames\": 10, \"cooldown\": 20}}";
   files[1].name = "level.json";
   files[1].text = level;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   CHECK(hd_dash.on && hd_dash.button == PAD_A && hd_dash.frames == 10);
   hd_reset(&s);
   memset(in, 0, sizeof in);
   in[0].buttons = PAD_START;
   hd_step(&s, in);
   in[0].buttons = 0;
   for (f = 0; f < 90; f++)
      hd_step(&s, in);
   CHECK(s.p[0].ground);
   x0 = FX_INT(s.p[0].x);
   in[0].buttons = PAD_A;
   hd_step(&s, in);
   CHECK(s.p[0].dash_t == 1);
   for (f = 0; f < 10; f++)
   {
      hd_step(&s, in);
      CHECK(s.p[0].vy == 0 || s.p[0].dash_t == 0);
   }
   CHECK(FX_INT(s.p[0].x) - x0 >= 75 && FX_INT(s.p[0].x) - x0 <= 82); /* 10 frames of 8 pixels */
   CHECK(s.p[0].dash_t == 0 && s.p[0].dash_wait > 0);
   in[0].buttons = 0;
   hd_step(&s, in);
   in[0].buttons = PAD_A;
   hd_step(&s, in);
   CHECK(s.p[0].dash_t == 0); /* still waiting */
   in[0].buttons = 0;
   for (f = 0; f < 30; f++)
      hd_step(&s, in);
   /* in the air: one dash, not two */
   in[0].buttons = PAD_JUMP;
   hd_step(&s, in);
   hd_step(&s, in);
   in[0].buttons = PAD_JUMP | PAD_A;
   hd_step(&s, in);
   CHECK(s.p[0].dash_t == 1 && s.p[0].dash_air);
   y0 = FX_INT(s.p[0].y);
   in[0].buttons = PAD_JUMP;
   for (f = 0; f < 40; f++)
      hd_step(&s, in);
   in[0].buttons = PAD_JUMP | PAD_A;
   hd_step(&s, in);
   CHECK(s.p[0].ground || s.p[0].dash_t == 0);
   (void)y0;
   files[0].text = "{\"format\": 3, \"title\": \"Dash\", \"level\": \"level.json\", \"dash\": {\"frames\": 0}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "dash") != NULL);
   hd_content_builtin();
}

extern const int16_t *hd_pkg_music; /* sprite.h */
extern int32_t hd_pkg_music_frames;

/* Music in IMA ADPCM: the samples decoded exactly; blocks that are not the IMA's are refused. */
static void test_adpcm(void)
{
   static uint8_t zip[300000], wav[1000];
   static char level[100000];
   const char *err;
   size_t n;
   uint8_t *p = wav;
   int32_t b;
   zfile files[3] = {{0}};
   /* mono, 2 blocks of 36 bytes: 65 samples each; the first block's codes all 0 (each a small step up) */
   memcpy(p, "RIFF", 4); p += 4; le32(&p, 4 + 28 + 8 + 72);
   memcpy(p, "WAVEfmt ", 8); p += 8; le32(&p, 20);
   le16(&p, 0x11); le16(&p, 1); le32(&p, 48000); le32(&p, 48000 * 36 / 65); le16(&p, 36); le16(&p, 4); le16(&p, 2); le16(&p, 65);
   memcpy(p, "data", 4); p += 4; le32(&p, 72);
   for (b = 0; b < 2; b++)
   {
      le16(&p, 1000); *p++ = 10; *p++ = 0;
      memset(p, b ? 0x77 : 0, 32);
      p += 32;
   }
   flat_level(level, sizeof level, 60, 30);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Tune\", \"level\": \"level.json\", \"music\": {\"file\": \"tune.wav\"}}";
   files[1].name = "level.json";
   files[1].text = level;
   files[2].name = "tune.wav";
   files[2].text = (const char *)wav;
   files[2].len = (size_t)(p - wav);
   n = make_zip(zip, files, 3);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  adpcm: %s\n", err);
   CHECK(hd_pkg_music_frames == 130);
   if (hd_pkg_music)
   {
      /* 1000, then + 19/8 (step 10), + 17/8 (step 9), + 16/8 (step 8): left and right the same */
      CHECK(hd_pkg_music[0] == 1000 && hd_pkg_music[1] == 1000);
      CHECK(hd_pkg_music[2] == 1002 && hd_pkg_music[4] == 1004 && hd_pkg_music[6] == 1006);
      CHECK(hd_pkg_music[2 * 65] == 1000); /* the second block starts from its own header */
   }
   wav[38] = 64; /* samples per block that do not match the block */
   n = make_zip(zip, files, 3);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "ADPCM") != NULL);
   hd_content_builtin();
}

/* Lives: each knockout costs one; at none the player waits to continue (its own countdown, the others play on),
   start brings it back with all of them, the time running out takes it out, and with nobody left the game is over. */
static void test_lives(void)
{
   static uint8_t zip[300000], save[HD_SAVE_SIZE];
   static char level[100000];
   static hd_state s, again;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f;
   zfile files[2] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Lives\", \"players\": 2, \"level\": \"level.json\","
                   " \"health\": {\"hits\": 2, \"knockout\": 10, \"invulnerable\": 120, \"lives\": 2, \"continue\": 3}}";
   files[1].name = "level.json";
   files[1].text = level;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  lives: %s\n", err);
   CHECK(hd_health.lives == 2 && hd_health.cont == 3 && HURT_FRAMES == 120);
   hd_reset(&s);
   memset(in, 0, sizeof in);
   in[0].buttons = in[1].buttons = PAD_START;
   hd_step(&s, in);
   in[0].buttons = in[1].buttons = 0;
   for (f = 0; f < 60; f++)
      hd_step(&s, in);
   CHECK(s.p[0].active && s.p[1].active && s.p[0].lives == 2);
   /* the first knockout: one life less, back after the respawn */
   s.p[0].ko = 1;
   hd_step(&s, in);
   CHECK(s.p[0].lives == 1 && s.p[0].respawn > 0 && !s.p[0].cont);
   for (f = 0; f < 60; f++)
      hd_step(&s, in);
   CHECK(!s.p[0].respawn && s.p[0].hp == 2);
   /* the last one: three seconds to continue, out of the game's way, the other player and the game going on */
   s.p[0].ko = 1;
   hd_step(&s, in);
   CHECK(s.p[0].lives == 0 && s.p[0].cont == 180 && s.p[0].respawn == 1 && s.p[0].active);
   for (f = 0; f < 60; f++)
      hd_step(&s, in);
   CHECK(s.p[0].cont == 120 && s.p[0].respawn == 1 && s.phase == PH_PLAY && !s.paused);
   hd_save(&s, save);
   again = s;
   /* start continues: all its lives, back at the checkpoint, and it does not pause the game */
   in[0].buttons = PAD_START;
   hd_step(&s, in);
   in[0].buttons = 0;
   hd_step(&s, in);
   CHECK(!s.p[0].cont && s.p[0].lives == 2 && !s.p[0].respawn && !s.paused && s.p[0].hp == 2);
   /* from the save state, the time runs out: it leaves; the other player still plays */
   CHECK(hd_load(&again, save, HD_SAVE_SIZE) == 1);
   for (f = 0; f < 120; f++)
      hd_step(&again, in);
   CHECK(!again.p[0].active && again.p[1].active && again.phase == PH_PLAY);
   /* the other one out of lives and time too: game over, then the title */
   again.p[1].ko = 1;
   hd_step(&again, in);
   for (f = 0; f < 60; f++)
      hd_step(&again, in);
   again.p[1].ko = 1;
   hd_step(&again, in);
   CHECK(again.p[1].cont == 180);
   for (f = 0; f < 180; f++)
      hd_step(&again, in);
   CHECK(again.phase == PH_OVER && !again.p[1].active);
   for (f = 0; f < OVER_FRAMES; f++)
      hd_step(&again, in);
   CHECK(again.phase == PH_TITLE);
   /* a value out of range is refused */
   files[0].text = "{\"format\": 3, \"title\": \"Lives\", \"level\": \"level.json\", \"health\": {\"lives\": 100}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "lives") != NULL);
   hd_content_builtin();
   CHECK(HURT_FRAMES == 90 && !hd_health.lives); /* the built-in game as it was */
}

/* A level with same_art is another act of the one before: its sky and pictures, none loaded twice. */
static void test_same_art(void)
{
   static uint8_t zip[400000], png[20000];
   static char one[100000], two[100000];
   const char *err;
   size_t n;
   const uint32_t *ground;
   zfile files[4] = {{0}};
   flat_level(one, sizeof one, 60, 30);
   flat_level(two, sizeof two, 80, 30);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Acts\", \"levels\": [{\"level\": \"one.json\", \"sky\": [\"#102030\", \"#203040\"],"
                   " \"intro\": \"g.png\", \"textures\": {\"ground\": \"g.png\"}}, {\"level\": \"two.json\", \"same_art\": true}]}";
   files[1].name = "one.json";
   files[1].text = one;
   files[2].name = "two.json";
   files[2].text = two;
   files[3].name = "g.png";
   files[3].text = (const char *)png;
   files[3].len = tiny_png(png, 16, 16);
   n = make_zip(zip, files, 4);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  same art: %s\n", err);
   CHECK(hd_stage_count == 2 && hd_textures[TL_GROUND].px != NULL);
   ground = hd_textures[TL_GROUND].px;
   hd_stage_select(1);
   CHECK(hd_map_w == 80 && hd_textures[TL_GROUND].px == ground && hd_sky_top == 0x102030u);
   hd_stage_select(0);
   files[0].text = "{\"format\": 3, \"title\": \"Acts\", \"levels\": [{\"level\": \"one.json\", \"same_art\": true}]}";
   n = make_zip(zip, files, 4);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "first level") != NULL);
   files[0].text = "{\"format\": 3, \"title\": \"Acts\", \"levels\": [{\"level\": \"one.json\"}, {\"level\": \"two.json\", \"same_art\": 1}]}";
   n = make_zip(zip, files, 4);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "true or false") != NULL);
   hd_content_builtin(); /* the shared pictures freed once */
}

/* The four more kinds: a roller rolls at the player, a hopper leaps, a puffer puffs spores up, a splitter splits in two halves. */
static void test_more_kinds(void)
{
   static uint8_t zip[300000];
   static char level[100000];
   static hd_state s;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, i, roller = -1, hopper = -1, puffer = -1, splitter = -1, rx0, leapt = 0, puffs = 0, halves = 0;
   zfile files[2] = {{0}};
   flat_level(level, sizeof level, 80, 30);
   set_cell(level, 14, 26, 'R');
   set_cell(level, 20, 26, 'H');
   set_cell(level, 26, 26, 'U');
   set_cell(level, 34, 26, 'K');
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"More\", \"level\": \"level.json\", \"health\": {\"hits\": 99, \"invulnerable\": 10},"
                   " \"weapon\": {\"rate\": 4, \"speed\": 700, \"range\": 600},"
                   " \"enemies\": {\"roller\": {\"hitbox\": [14, 14], \"health\": 2, \"speed\": 300, \"range\": 300},"
                   " \"hopper\": {\"hitbox\": [14, 14], \"health\": 2, \"rate\": 40, \"range\": 600},"
                   " \"puffer\": {\"hitbox\": [16, 16], \"health\": 3, \"rate\": 40, \"spores\": 4, \"range\": 600},"
                   " \"splitter\": {\"hitbox\": [18, 18], \"health\": 4, \"speed\": 50, \"range\": 400}}}";
   files[1].name = "level.json";
   files[1].text = level;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  more kinds: %s\n", err);
   hd_reset(&s);
   memset(in, 0, sizeof in);
   in[0].buttons = PAD_START;
   hd_step(&s, in);
   for (i = 0; i < MAX_ENEMIES; i++)
   {
      if (!s.e[i].alive)
         continue;
      if (s.e[i].kind == EK_ROLLER)
         roller = i;
      if (s.e[i].kind == EK_HOPPER)
         hopper = i;
      if (s.e[i].kind == EK_PUFFER)
         puffer = i;
      if (s.e[i].kind == EK_SPLITTER)
         splitter = i;
   }
   CHECK(roller >= 0 && hopper >= 0 && puffer >= 0 && splitter >= 0);
   if (roller < 0 || hopper < 0 || puffer < 0 || splitter < 0)
      return;
   CHECK(s.e[roller].hp == 2 && s.e[puffer].hp == 3 && s.e[splitter].hp == 4);
   rx0 = FX_INT(s.e[roller].x);
   in[0].buttons = 0;
   for (f = 0; f < 200; f++)
   {
      hd_step(&s, in);
      leapt |= s.e[hopper].alive == 1 && !s.e[hopper].ground && s.e[hopper].vy < 0;
      puffs = hd_max(puffs, live_bolts(&s));
   }
   CHECK(s.e[roller].alive != 1 || FX_INT(s.e[roller].x) != rx0); /* it rolled */
   CHECK(leapt);                                                   /* the hopper leapt */
   CHECK(puffs >= 4);                                              /* a fan of 4 spores */
   /* the splitter, shot down: two halves, smaller, half its health */
   s.e[splitter].hp = 1;
   s.e[splitter].awake = 1;
   {
      hd_shot *q = &s.shot[0];
      memset(q, 0, sizeof *q);
      q->life = 10;
      q->x = s.e[splitter].x + FX(9);
      q->y = s.e[splitter].y + FX(9);
      q->damage = 1;
   }
   hd_step(&s, in);
   for (i = 0; i < MAX_ENEMIES; i++)
      if (s.e[i].alive == 1 && s.e[i].kind == EK_SPLITTER && s.e[i].seq == 1)
      {
         halves++;
         CHECK(s.e[i].hp == 2);
      }
   CHECK(s.e[splitter].alive == 2 && halves == 2);
   hd_content_builtin();
}

/* A boss with "evolve": beaten once it becomes its second form (its health, a bigger hitbox, its name), beaten again for good. */
static void test_boss_evolves(void)
{
   static uint8_t zip[300000], save[HD_SAVE_SIZE];
   static char level[100000];
   static hd_state s, b;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, boss = -1, i;
   zfile files[2] = {{0}};
   flat_level(level, sizeof level, 60, 30);
   set_cell(level, 30, 26, 'X');
   set_cell(level, 55, 26, 'F');
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Evolve\", \"level\": \"level.json\", \"health\": {\"hits\": 99},"
                   " \"weapon\": {\"rate\": 4, \"speed\": 700, \"range\": 500},"
                   " \"boss\": {\"name\": \"ONE\", \"hitbox\": [40, 40], \"health\": 10, \"attacks\": [\"spit\"], \"rest\": 30,"
                   " \"evolve\": {\"name\": \"TWO\", \"hitbox\": [60, 70], \"health\": 20}}}";
   files[1].name = "level.json";
   files[1].text = level;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  evolve: %s\n", err);
   hd_reset(&s);
   memset(in, 0, sizeof in);
   in[0].buttons = PAD_START;
   hd_step(&s, in);
   for (i = 0; i < MAX_ENEMIES; i++)
      if (s.e[i].alive && s.e[i].kind == EK_BOSS)
         boss = i;
   CHECK(boss >= 0);
   if (boss < 0)
      return;
   for (f = 0; f < 3000 && !s.boss_form; f++)
   {
      in[0].buttons = PAD_RUN | ((f & 31) < 2 ? (FX_INT(s.p[0].x) < FX_INT(s.e[boss].x) ? PAD_RIGHT : PAD_LEFT) : 0);
      hd_step(&s, in);
   }
   CHECK(s.boss_form == 1 && !s.boss_beaten && s.e[boss].alive == 1);
   CHECK(s.boss_max == 20 && hd_kinds[EK_BOSS].h == 70 && strcmp(hd_boss.name, "TWO") == 0);
   CHECK(hd_boss.attacks == 1 && hd_boss.rest == 30); /* what it does not say stays */
   CHECK(!hd_goal_open(&s));
   /* a save state in the second form brings it back */
   hd_save(&s, save);
   hd_reset(&b);
   CHECK(hd_kinds[EK_BOSS].h == 40);
   CHECK(hd_load(&b, save, HD_SAVE_SIZE) == 1 && hd_kinds[EK_BOSS].h == 70);
   for (f = 0; f < 4000 && !s.boss_beaten; f++)
   {
      in[0].buttons = PAD_RUN | ((f & 31) < 2 ? (FX_INT(s.p[0].x) < FX_INT(s.e[boss].x) ? PAD_RIGHT : PAD_LEFT) : 0);
      hd_step(&s, in);
   }
   CHECK(s.boss_beaten && hd_goal_open(&s));
   files[0].text = "{\"format\": 3, \"title\": \"Evolve\", \"level\": \"level.json\","
                   " \"boss\": {\"evolve\": {\"evolve\": {}}}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "evolve again") != NULL);
   hd_content_builtin();
}

/* Credits: after the last level they roll, start skips them, then the title. */
static void test_credits(void)
{
   static uint8_t zip[300000];
   static char one[100000];
   static hd_state s;
   hd_input in[MAX_PLAYERS];
   const char *err;
   size_t n;
   int32_t f, rolled = 0;
   zfile files[2] = {{0}};
   goal_level(one, sizeof one, 50);
   files[0].name = "manifest.json";
   files[0].text = "{\"format\": 3, \"title\": \"Credits\", \"levels\": [{\"level\": \"one.json\"}],"
                   " \"credits\": [\"# MADE BY\", \"SOMEONE\", \"\", \"# THANKS\", \"EVERYONE\"]}";
   files[1].name = "one.json";
   files[1].text = one;
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1);
   if (err)
      printf("  credits: %s\n", err);
   CHECK(hd_credit_lines() == 5 && hd_credit_rows == 5 && strcmp(hd_credit_line(0), "# MADE BY") == 0);
   hd_reset(&s);
   memset(in, 0, sizeof in);
   in[0].buttons = PAD_START;
   hd_step(&s, in);
   for (f = 0; f < 4000 && s.phase != PH_TITLE; f++)
   {
      in[0].buttons = s.phase == PH_CREDITS ? (f & 1 ? PAD_START : 0) : PAD_RIGHT;
      hd_step(&s, in);
      rolled |= s.phase == PH_CREDITS;
   }
   CHECK(rolled && s.phase == PH_TITLE);
   files[0].text = "{\"format\": 3, \"title\": \"Credits\", \"levels\": [{\"level\": \"one.json\"}], \"credits\": [1]}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 0 && strstr(err, "credits") != NULL);
   /* one list for each language: the longest sets how long they roll */
   files[0].text = "{\"format\": 3, \"title\": \"Credits\", \"levels\": [{\"level\": \"one.json\"}],"
                   " \"credits\": {\"en\": [\"# MADE BY\", \"A\"], \"es\": [\"# HECHO POR\", \"A\", \"\", \"# GRACIAS\"]}}";
   n = make_zip(zip, files, 2);
   CHECK(hd_content_load(zip, n, &err) == 1 && hd_credit_rows == 4);
   {
      int32_t lang = hd_lang;
      hd_lang = 1;
      CHECK(hd_credit_lines() == 4 && strcmp(hd_credit_line(0), "# HECHO POR") == 0);
      hd_lang = 2; /* no "pt": the first list given */
      CHECK(hd_credit_lines() == 2 && strcmp(hd_credit_line(0), "# MADE BY") == 0 && hd_credit_line(5)[0] == 0);
      hd_lang = lang;
   }
   hd_content_builtin();
   CHECK(hd_credit_lines() == 0 && hd_credit_rows == 0);
}

int main(void)
{
   hd_static_init();
   printf("go-link HD tests (state %u bytes, save state %u bytes)\n", (unsigned)sizeof(hd_state), (unsigned)HD_SAVE_SIZE);
   test_art();
   test_determinism();
   test_play();
   test_save_state();
   test_bad_save_states();
   test_json();
   test_sha256();
   test_package();
   test_players_screens_sticks();
   test_trig();
   test_rig();
   test_weapon();
   test_health();
   test_super();
   test_stages();
   test_resolution();
   test_art_scale();
   test_texture_ends();
   test_path();
   test_bones();
   test_fx();
   test_showcase();
#ifndef HD_NO_3D
   test_3d();
   test_remote();
   test_remote_activities();
   test_remote_day();
   test_remote_places();
   test_remote_texts();
   test_remote_boss();
   test_remote_screens();
   test_remote_sound();
   test_model3d();
#endif
   test_effects_package();
   test_goal_height();
   test_physics();
   test_screens();
   test_sprite_counters();
   test_enemy_kinds();
   test_boss();
   test_dash();
   test_lives();
   test_same_art();
   test_more_kinds();
   test_boss_evolves();
   test_credits();
   test_adpcm();
   test_api();
   if (failures)
   {
      printf("FAIL: %d checks failed\n", failures);
      return 1;
   }
   printf("ok\n");
   return 0;
}
