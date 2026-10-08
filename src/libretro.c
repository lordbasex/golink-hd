/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The libretro API: what RetroArch, go-link's device or any other libretro
 * frontend calls. It only moves data between the frontend and the engine
 * (hd.h): buttons in, one fixed 60 Hz step, a 640 x 360 XRGB8888 frame and
 * 800 stereo samples at 48 kHz out. Started with no content it plays the
 * built-in demo; given a game package (.glhd) it plays that game.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"
#include "hd.h"
#include "pack.h"

#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static hd_state state;
static uint32_t frame_buf[HD_MAX_W * HD_MAX_H];
static int16_t audio_buf[HD_SAMPLES_PER_FRAME * 2];
static int music_on = 1;
static int showcase;   /* the "Demo" option: 0 platformer, 1 showcase */
static int has_content;
static int use_bitmasks;

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   (void)level;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

/* Core options: v2 when the frontend has it, the old variables otherwise. */
static struct retro_core_option_v2_category option_cats[] = { { NULL, NULL, NULL } };

static struct retro_core_option_v2_definition option_defs[] = {
   {
      "golink_hd_music",
      "Music",
      NULL,
      "Play the game's music. Sound effects always play.",
      NULL,
      NULL,
      { { "enabled", NULL }, { "disabled", NULL }, { NULL, NULL } },
      "enabled",
   },
   {
      "golink_hd_demo",
      "Demo",
      NULL,
      "With no game loaded: the platformer for 1 to 4 players, or the showcase of every effect (Mode 7, the road, lights, bones, waves, colors; L and R change scenes).",
      NULL,
      NULL,
      { { "platformer", "Platformer" }, { "showcase", "Showcase" }, { NULL, NULL } },
      "platformer",
   },
   {
      "golink_hd_language",
      "Language",
      NULL,
      "The language of the game's texts and dialogs.",
      NULL,
      NULL,
      { { "auto", "Frontend's" }, { "en", "English" }, { "es", "Espa\xc3\xb1ol" }, { "pt", "Portugu\xc3\xaas" }, { NULL, NULL } },
      "auto",
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, { { NULL, NULL } }, NULL },
};

static struct retro_core_options_v2 options_v2 = { option_cats, option_defs };

static void set_options(void)
{
   unsigned version = 0;
   if (environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && version >= 2)
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options_v2);
   else
   {
      static const struct retro_variable vars[] = {
         { "golink_hd_music", "Music; enabled|disabled" },
         { "golink_hd_demo", "Demo; platformer|showcase" },
         { "golink_hd_language", "Language; auto|en|es|pt" },
         { NULL, NULL },
      };
      environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)vars);
   }
}

/* Reads the options; returns 1 when the demo changed (the game starts again). */
static int read_options(void)
{
   struct retro_variable var = { "golink_hd_music", NULL };
   int was = showcase;
   unsigned lang = RETRO_LANGUAGE_ENGLISH;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      music_on = strcmp(var.value, "disabled") != 0;
   var.key = "golink_hd_demo";
   var.value = NULL;
   showcase = environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && !strcmp(var.value, "showcase");
   var.key = "golink_hd_language";
   var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "auto"))
      hd_lang = !strcmp(var.value, "es") ? 1 : (!strcmp(var.value, "pt") ? 2 : 0);
   else
   {
      if (!environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &lang))
         lang = RETRO_LANGUAGE_ENGLISH;
      hd_lang = lang == RETRO_LANGUAGE_SPANISH ? 1 : (lang == RETRO_LANGUAGE_PORTUGUESE_BRAZIL || lang == RETRO_LANGUAGE_PORTUGUESE_PORTUGAL ? 2 : 0);
   }
   return was != showcase;
}

/* A new game: the package's, or the demo the option picks. */
static void start(void)
{
   hd_reset(&state);
   if (!has_content && showcase)
      hd_show_start(&state);
}

void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_controller_description pads[] = {
      { "RetroPad", RETRO_DEVICE_JOYPAD },
   };
   static const struct retro_controller_info ports[MAX_PLAYERS + 1] = {
      { pads, 1 }, { pads, 1 }, { pads, 1 }, { pads, 1 }, { pads, 1 }, { pads, 1 }, { pads, 1 }, { pads, 1 }, { NULL, 0 },
   };
   bool no_game = true;
   environ_cb = cb;
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
   cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);
   set_options();
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void)
{
   struct retro_log_callback logging;
   log_cb = fallback_log;
   if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging) && logging.log)
      log_cb = logging.log;
   use_bitmasks = environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);
   hd_static_init();
}

void retro_deinit(void) {}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof *info);
   info->library_name = "go-link HD";
   info->library_version = HD_VERSION GIT_VERSION;
   info->valid_extensions = "glhd";
   info->need_fullpath = false;
   info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof *info);
   info->timing.fps = HD_FPS;
   info->timing.sample_rate = HD_RATE;
   info->geometry.base_width = (unsigned)HD_W; /* the loaded game's screen */
   info->geometry.base_height = (unsigned)HD_H;
   info->geometry.max_width = HD_MAX_W;
   info->geometry.max_height = HD_MAX_H;
   info->geometry.aspect_ratio = (float)HD_W / (float)HD_H;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

void retro_reset(void) { start(); }

/*
 * RetroPad buttons to the engine's: B or A jump, Y or X run (each face
 * button also has its own bit), the shoulders and the stick clicks, and both
 * analog sticks.
 */
static hd_input read_pad(unsigned port)
{
   static const struct { unsigned id; uint32_t bit; } map[] = {
      { RETRO_DEVICE_ID_JOYPAD_UP, PAD_UP }, { RETRO_DEVICE_ID_JOYPAD_DOWN, PAD_DOWN },
      { RETRO_DEVICE_ID_JOYPAD_LEFT, PAD_LEFT }, { RETRO_DEVICE_ID_JOYPAD_RIGHT, PAD_RIGHT },
      { RETRO_DEVICE_ID_JOYPAD_B, PAD_JUMP | PAD_B }, { RETRO_DEVICE_ID_JOYPAD_A, PAD_JUMP | PAD_A },
      { RETRO_DEVICE_ID_JOYPAD_Y, PAD_RUN | PAD_Y }, { RETRO_DEVICE_ID_JOYPAD_X, PAD_RUN | PAD_X },
      { RETRO_DEVICE_ID_JOYPAD_START, PAD_START }, { RETRO_DEVICE_ID_JOYPAD_SELECT, PAD_SELECT },
      { RETRO_DEVICE_ID_JOYPAD_L, PAD_L }, { RETRO_DEVICE_ID_JOYPAD_R, PAD_R },
      { RETRO_DEVICE_ID_JOYPAD_L2, PAD_L2 }, { RETRO_DEVICE_ID_JOYPAD_R2, PAD_R2 },
      { RETRO_DEVICE_ID_JOYPAD_L3, PAD_L3 }, { RETRO_DEVICE_ID_JOYPAD_R3, PAD_R3 },
   };
   hd_input in;
   uint32_t pad = 0, held = 0;
   unsigned i;
   if (use_bitmasks)
      held = (uint32_t)input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
   for (i = 0; i < sizeof map / sizeof map[0]; i++)
   {
      int on = use_bitmasks ? (int)((held >> map[i].id) & 1) : input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, map[i].id) != 0;
      if (on)
         pad |= map[i].bit;
   }
   /* both directions at once cancel out */
   if ((pad & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT))
      pad &= ~(uint32_t)(PAD_LEFT | PAD_RIGHT);
   if ((pad & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN))
      pad &= ~(uint32_t)(PAD_UP | PAD_DOWN);
   in.buttons = pad;
   in.lx = input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
   in.ly = input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
   in.rx = input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
   in.ry = input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
   return in;
}

void retro_run(void)
{
   hd_input pads[MAX_PLAYERS];
   bool updated = false;
   unsigned i;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated && read_options() && !has_content)
      start();
   input_poll_cb();
   for (i = 0; i < MAX_PLAYERS; i++)
      pads[i] = read_pad(i);
   hd_step(&state, pads);
   hd_draw(&state, frame_buf);
   hd_mix(&state, audio_buf, music_on);
   video_cb(frame_buf, (unsigned)HD_W, (unsigned)HD_H, (size_t)HD_W * sizeof(uint32_t));
   audio_batch_cb(audio_buf, HD_SAMPLES_PER_FRAME);
}

static size_t content_size;

/*
 * A package from its path, for frontends that pass only the path although
 * need_fullpath is false (go-link's device does).
 */
static int read_file(const char *path, uint8_t **out, size_t *size)
{
   FILE *f = fopen(path, "rb");
   long n;
   uint8_t *buf;
   if (!f)
      return 0;
   if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || (unsigned long)n > PACK_MAX_BYTES || fseek(f, 0, SEEK_SET) != 0)
   {
      fclose(f);
      return 0;
   }
   buf = (uint8_t *)malloc(n ? (size_t)n : 1);
   if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n)
   {
      free(buf);
      fclose(f);
      return 0;
   }
   fclose(f);
   *out = buf;
   *size = (size_t)n;
   return 1;
}

static void describe_input(void)
{
#define PAD_DESC(port)                                                                   \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },               \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },             \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },                   \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down (+ Jump: drop)" }, \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Jump" },                  \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Jump" },                  \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Run" },                   \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Run" },                   \
   { port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Join / Pause" },             \
   { port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Move" }
   static struct retro_input_descriptor desc[] = {
      PAD_DESC(0), PAD_DESC(1), PAD_DESC(2), PAD_DESC(3), PAD_DESC(4), PAD_DESC(5), PAD_DESC(6), PAD_DESC(7), { 0, 0, 0, 0, NULL },
   };
#undef PAD_DESC
   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
}

bool retro_load_game(const struct retro_game_info *game)
{
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      log_cb(RETRO_LOG_ERROR, "[go-link HD] the frontend has no XRGB8888 frames\n");
      return false;
   }
   if (game && (game->data || game->path))
   {
      const char *err = NULL;
      uint8_t *file = NULL;
      int ok;
      if (!game->data && !read_file(game->path, &file, &content_size))
      {
         log_cb(RETRO_LOG_ERROR, "[go-link HD] cannot read %s: it is missing, unreadable or over 256 MB\n", game->path);
         return false;
      }
      ok = file ? hd_content_load(file, content_size, &err) : hd_content_load((const uint8_t *)game->data, game->size, &err);
      free(file);
      if (!ok)
      {
         log_cb(RETRO_LOG_ERROR, "[go-link HD] this game package cannot be played: %s\n", err);
         return false;
      }
      log_cb(RETRO_LOG_INFO, "[go-link HD] playing %s\n", hd_title);
      has_content = 1;
   }
   else
   {
      hd_content_builtin();
      has_content = 0;
   }
   describe_input();
   read_options();
   start();
   return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

void retro_unload_game(void) { hd_content_builtin(); }

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

size_t retro_serialize_size(void) { return HD_SAVE_SIZE; }

bool retro_serialize(void *data, size_t size)
{
   if (size < HD_SAVE_SIZE)
      return false;
   hd_save(&state, (uint8_t *)data);
   return true;
}

bool retro_unserialize(const void *data, size_t size)
{
   if (!hd_load(&state, (const uint8_t *)data, (uint32_t)size))
   {
      log_cb(RETRO_LOG_WARN, "[go-link HD] this save state is not for this version of the core\n");
      return false;
   }
   return true;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}
