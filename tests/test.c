/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The engine's tests: the art is well formed, the same inputs give the same
 * frames and sound, a save state continues exactly where it was taken, bad
 * save states are refused or made safe, and the libretro API works through
 * a fake frontend. Built with the address and undefined behaviour sanitizers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"
#include "hd.h"
#include "pack.h"

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

/* A fake frontend for the libretro API. */
static uint32_t fake_buttons[MAX_PLAYERS];
static unsigned frames_seen, samples_seen;
static uint32_t last_frame_hash;
static int saw_options;

static bool fake_env(unsigned cmd, void *data)
{
   switch (cmd)
   {
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
   case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      *(unsigned *)data = 2;
      return true;
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      saw_options = 1;
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE:
      ((struct retro_variable *)data)->value = "enabled";
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = false;
      return true;
   case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
   case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
   case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      return true;
   default:
      return false;
   }
}

static void fake_video(const void *data, unsigned w, unsigned h, size_t pitch)
{
   frames_seen++;
   last_frame_hash = fnv(data, pitch * h, 2166136261u);
   (void)w;
}

static size_t fake_audio(const int16_t *data, size_t frames)
{
   samples_seen += (unsigned)frames;
   (void)data;
   return frames;
}

static void fake_poll(void) {}

static int16_t fake_input(unsigned port, unsigned device, unsigned index, unsigned id)
{
   (void)index;
   if (device != RETRO_DEVICE_JOYPAD || port >= MAX_PLAYERS)
      return 0;
   if (id == RETRO_DEVICE_ID_JOYPAD_START)
      return (fake_buttons[port] & PAD_START) != 0;
   if (id == RETRO_DEVICE_ID_JOYPAD_RIGHT)
      return (fake_buttons[port] & PAD_RIGHT) != 0;
   if (id == RETRO_DEVICE_ID_JOYPAD_B)
      return (fake_buttons[port] & PAD_JUMP) != 0;
   return 0;
}

static void test_libretro(void)
{
   struct retro_system_info info;
   struct retro_system_av_info av;
   struct retro_game_info content = { "game.glhd", NULL, 0, NULL };
   static uint8_t save[HD_SAVE_SIZE + 64];
   uint32_t hash_after;
   unsigned i;

   retro_set_environment(fake_env);
   retro_set_video_refresh(fake_video);
   retro_set_audio_sample_batch(fake_audio);
   retro_set_input_poll(fake_poll);
   retro_set_input_state(fake_input);
   retro_init();
   CHECK(saw_options);
   CHECK(retro_api_version() == RETRO_API_VERSION);
   retro_get_system_info(&info);
   CHECK(strcmp(info.library_name, "go-link HD") == 0);
   CHECK(strcmp(info.valid_extensions, "glhd") == 0);
   retro_get_system_av_info(&av);
   CHECK(av.geometry.base_width == (unsigned)HD_W && av.geometry.base_height == (unsigned)HD_H);
   CHECK(av.timing.sample_rate == HD_RATE);

   CHECK(!retro_load_game(&content)); /* a path that does not exist */
   content.path = "tests/data/demo-deflate.glhd";
   CHECK(retro_load_game(&content)); /* read from its path, as go-link's device passes it */
   retro_unload_game();
   CHECK(retro_load_game(NULL));
   for (i = 0; i < 120; i++)
   {
      fake_buttons[0] = (i >= 10 && i < 14) ? PAD_START : (i >= 14 ? PAD_RIGHT : 0);
      retro_run();
   }
   CHECK(frames_seen == 120);
   CHECK(samples_seen == 120 * HD_SAMPLES_PER_FRAME);
   CHECK(retro_serialize_size() == HD_SAVE_SIZE);
   CHECK(!retro_serialize(save, HD_SAVE_SIZE - 1));
   CHECK(retro_serialize(save, sizeof save));
   for (i = 0; i < 60; i++)
      retro_run();
   hash_after = last_frame_hash;
   CHECK(retro_unserialize(save, sizeof save));
   for (i = 0; i < 60; i++)
      retro_run();
   CHECK(last_frame_hash == hash_after);
   save[0] = 'X';
   CHECK(!retro_unserialize(save, sizeof save));
   retro_reset();
   retro_unload_game();
   retro_deinit();
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
   test_libretro();
   if (failures)
   {
      printf("FAIL: %d checks failed\n", failures);
      return 1;
   }
   printf("ok\n");
   return 0;
}
