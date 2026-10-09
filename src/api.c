/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The API (include/golink_hd.h) over the engine. This version keeps one
 * engine per program: the engine's pictures, level and tables are built once
 * and shared, so a second golinkhd_create is refused until the first is
 * destroyed.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hd.h"
#include "pack.h"

struct golinkhd_engine
{
   golinkhd_config config;
   hd_state state;
   uint32_t frame[HD_OUT_MAX_W * HD_OUT_MAX_H];
   int16_t audio[HD_SAMPLES_PER_FRAME * 2];
   int32_t music, demo, has_package;
};

static golinkhd_engine *the_engine;

static void say(golinkhd_engine *e, int32_t level, const char *fmt, ...)
{
   char msg[300];
   va_list ap;
   if (!e->config.log)
      return;
   va_start(ap, fmt);
   vsnprintf(msg, sizeof msg, fmt, ap);
   va_end(ap);
   e->config.log(e->config.user, level, msg);
}

const char *golinkhd_version(void)
{
   return HD_VERSION;
}

int32_t golinkhd_api_version(void)
{
   return GOLINKHD_API_VERSION;
}

golinkhd_engine *golinkhd_create(const golinkhd_config *config, const char **error)
{
   golinkhd_engine *e;
   if (error)
      *error = NULL;
   if (!config || config->api_version < 1 || config->api_version > GOLINKHD_API_VERSION)
   {
      if (error)
         *error = "the host asks for an API version this engine does not know";
      return NULL;
   }
   if (the_engine)
   {
      if (error)
         *error = "an engine already runs in this program (one at a time in this version)";
      return NULL;
   }
   e = (golinkhd_engine *)calloc(1, sizeof *e);
   if (!e)
   {
      if (error)
         *error = "out of memory";
      return NULL;
   }
   e->config = *config;
   e->music = 1;
   hd_res_host = 0;
   hd_static_init();
   hd_content_builtin();
   hd_reset(&e->state);
   the_engine = e;
   return e;
}

void golinkhd_destroy(golinkhd_engine *e)
{
   if (!e)
      return;
   if (the_engine == e)
      the_engine = NULL;
   hd_content_builtin();
   free(e);
}

void golinkhd_restart(golinkhd_engine *e)
{
   hd_reset(&e->state);
   if (!e->has_package && e->demo == 1)
      hd_show_start(&e->state);
}

int golinkhd_load(golinkhd_engine *e, const uint8_t *package, size_t size, const char **error)
{
   const char *err = NULL;
   if (error)
      *error = NULL;
   if (!package || size > PACK_MAX_BYTES || !hd_content_load(package, size, &err))
   {
      if (!err)
         err = "the package is missing or bigger than 256 MB";
      say(e, GOLINKHD_LOG_ERROR, "this game package cannot be played: %s", err);
      if (error)
         *error = err;
      e->has_package = 0;
      golinkhd_restart(e);
      return 0;
   }
   e->has_package = 1;
   say(e, GOLINKHD_LOG_INFO, "playing %s", hd_title);
   golinkhd_restart(e);
   return 1;
}

void golinkhd_load_demo(golinkhd_engine *e, int32_t demo)
{
   hd_content_builtin();
   e->has_package = 0;
   e->demo = demo == 1 ? 1 : 0;
   golinkhd_restart(e);
}

void golinkhd_get_info(golinkhd_engine *e, golinkhd_info *out)
{
   (void)e;
   memset(out, 0, sizeof *out);
   out->title = hd_title;
   out->width = HD_OUT_W;
   out->height = HD_OUT_H;
   out->fps = HD_FPS;
   out->sample_rate = HD_RATE;
   out->players = hd_players;
   memcpy(out->sha256, hd_content_id, 32);
}

void golinkhd_set_language(golinkhd_engine *e, const char *language)
{
   (void)e;
   hd_lang = language && !strcmp(language, "es") ? 1 : (language && !strcmp(language, "pt") ? 2 : 0);
}

void golinkhd_set_music(golinkhd_engine *e, int on)
{
   e->music = on ? 1 : 0;
}

void golinkhd_set_resolution(golinkhd_engine *e, int32_t lines)
{
   (void)e;
   hd_res_host = lines >= 1080 ? 3 : lines >= 720 ? 2 : lines > 0 ? 1 : 0;
}

void golinkhd_frame(golinkhd_engine *e, const golinkhd_pad *pads, int32_t count, golinkhd_frame_out *out)
{
   hd_input in[MAX_PLAYERS];
   memset(in, 0, sizeof in);
   int32_t i;
   if (pads && count > 0)
      memcpy(in, pads, sizeof(hd_input) * (size_t)hd_min(count, MAX_PLAYERS));
   /* both ways at once cancel out, on every host */
   for (i = 0; i < MAX_PLAYERS; i++)
   {
      if ((in[i].buttons & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT))
         in[i].buttons &= ~(uint32_t)(PAD_LEFT | PAD_RIGHT);
      if ((in[i].buttons & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN))
         in[i].buttons &= ~(uint32_t)(PAD_UP | PAD_DOWN);
   }
   hd_step(&e->state, in);
   hd_draw(&e->state, e->frame);
   hd_mix(&e->state, e->audio, e->music);
   out->pixels = e->frame;
   out->width = HD_OUT_W;
   out->height = HD_OUT_H;
   out->pitch = HD_OUT_W;
   out->audio = e->audio;
   out->audio_frames = HD_SAMPLES_PER_FRAME;
}

size_t golinkhd_state_size(golinkhd_engine *e)
{
   (void)e;
   return HD_SAVE_SIZE;
}

int golinkhd_state_save(golinkhd_engine *e, uint8_t *out, size_t size)
{
   if (!out || size < HD_SAVE_SIZE)
      return 0;
   hd_save(&e->state, out);
   return 1;
}

int golinkhd_state_load(golinkhd_engine *e, const uint8_t *in, size_t size, const char **error)
{
   if (error)
      *error = NULL;
   if (!in || !hd_load(&e->state, in, size > 0xffffffffu ? 0xffffffffu : (uint32_t)size))
   {
      if (error)
         *error = "this save state is not for this game or this version of the engine";
      return 0;
   }
   return 1;
}
