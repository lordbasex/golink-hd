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

static uint32_t fb[HD_MAX_W * HD_MAX_H];
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
typedef struct { const char *name; const char *text; } zfile;

static void le16(uint8_t **p, uint32_t v) { *(*p)++ = (uint8_t)v; *(*p)++ = (uint8_t)(v >> 8); }
static void le32(uint8_t **p, uint32_t v) { le16(p, v & 0xffff); le16(p, v >> 16); }

static size_t make_zip(uint8_t *out, const zfile *files, int n)
{
   uint8_t *p = out;
   uint32_t offs[8], at, cd;
   int i;
   for (i = 0; i < n; i++)
   {
      uint32_t len = (uint32_t)strlen(files[i].text), nl = (uint32_t)strlen(files[i].name);
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
      uint32_t len = (uint32_t)strlen(files[i].text), nl = (uint32_t)strlen(files[i].name);
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
   zfile files[2];

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
   zfile files[2];
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
   zfile files[2];
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
   zfile files[2];
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
   hd_content_builtin();
   CHECK(PW == 10 && PH == 22); /* the built-in game's again */
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
   test_path();
   test_bones();
   test_fx();
   test_showcase();
   test_effects_package();
   test_goal_height();
   test_physics();
   test_api();
   if (failures)
   {
      printf("FAIL: %d checks failed\n", failures);
      return 1;
   }
   printf("ok\n");
   return 0;
}
