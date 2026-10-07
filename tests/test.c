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

static void pads_at(int32_t frame, int32_t players, uint32_t pads[MAX_PLAYERS])
{
   int32_t i;
   for (i = 0; i < MAX_PLAYERS; i++)
      pads[i] = i < players ? bot(frame, i) : 0;
}

static uint32_t run(hd_state *s, int32_t from, int32_t to, int32_t players, uint32_t *fb, int16_t *audio)
{
   uint32_t h = 2166136261u, pads[MAX_PLAYERS];
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

static uint32_t fb[HD_W * HD_H];
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
   for (i = 16; i < HD_SAVE_SIZE; i++)
      save[i] = (uint8_t)(i * 2654435761u >> 24);
   for (i = 16; i < HD_SAVE_SIZE; i++)
      sum = (sum ^ save[i]) * 16777619u;
   w = sum;
   save[12] = (uint8_t)w;
   save[13] = (uint8_t)(w >> 8);
   save[14] = (uint8_t)(w >> 16);
   save[15] = (uint8_t)(w >> 24);
   CHECK(hd_load(&s, save, HD_SAVE_SIZE) == 1);
   run(&s, 300, 420, 4, fb, audio);
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
   CHECK(av.geometry.base_width == HD_W && av.geometry.base_height == HD_H);
   CHECK(av.timing.sample_rate == HD_RATE);

   CHECK(!retro_load_game(&content)); /* packages come later */
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
   test_libretro();
   if (failures)
   {
      printf("FAIL: %d checks failed\n", failures);
      return 1;
   }
   printf("ok\n");
   return 0;
}
