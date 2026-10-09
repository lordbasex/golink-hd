/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Format 3's sounds and music: a package's own WAV files.
 *
 *   "sounds": {"jump": "jump.wav", "coin": "gem.wav", ...}
 *      the effects it replaces: jump, coin, stomp, hurt, join, check, clear, pause
 *   "music": {"file": "level.wav", "volume": 200, "loop_from": 0}
 *      played over and over instead of the built-in tune; loop_from (in
 *      milliseconds) is where it starts again after the end
 *
 * WAV files are PCM, 16 bits, 48000 Hz, mono or stereo: effects are mixed
 * to mono (the game places them left or right), the music keeps stereo.
 * Every number is an integer and the music's position is in the state, so
 * the sound is the same on every computer and after a save state.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sprite.h"

const int16_t *hd_pkg_music;
int32_t hd_pkg_music_frames, hd_pkg_music_loop, hd_pkg_music_vol;

static int16_t *sfx_data[SFX_COUNT];
static int16_t *music_data;

#define SFX_SECONDS_MAX 10
#define MUSIC_SECONDS_MAX 600

static const char *const sfx_names[SFX_COUNT] = { "jump", "coin", "stomp", "hurt", "join", "check", "clear", "pause" };

void hd_sounds_free(void)
{
   int32_t i;
   for (i = 0; i < SFX_COUNT; i++)
   {
      free(sfx_data[i]);
      sfx_data[i] = NULL;
   }
   free(music_data);
   music_data = NULL;
   hd_pkg_music = NULL;
   hd_pkg_music_frames = hd_pkg_music_loop = 0;
   hd_pkg_music_vol = 200;
   hd_audio_build(); /* the built-in effects back */
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

/* A WAV's samples as 16-bit stereo (mono duplicated); NULL and *err when it is not one go-link HD plays. */
static int16_t *wav_read(const uint8_t *d, size_t n, int32_t max_seconds, int32_t *frames, const char **err)
{
   size_t at = 12;
   const uint8_t *data = NULL;
   uint32_t data_len = 0, channels = 0, rate = 0, bits = 0, format = 0, i, count;
   int16_t *out;
   if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4))
   {
      *err = "not a WAV file";
      return NULL;
   }
   while (at + 8 <= n)
   {
      uint32_t len = le32(d + at + 4);
      if (len > n - at - 8)
         break;
      if (!memcmp(d + at, "fmt ", 4) && len >= 16)
      {
         format = le16(d + at + 8);
         channels = le16(d + at + 10);
         rate = le32(d + at + 12);
         bits = le16(d + at + 22);
      }
      else if (!memcmp(d + at, "data", 4))
      {
         data = d + at + 8;
         data_len = len;
      }
      at += 8 + len + (len & 1);
   }
   if ((format != 1 && format != 0xfffe) || bits != 16 || rate != HD_RATE || (channels != 1 && channels != 2) || !data)
   {
      *err = "a WAV must be PCM, 16 bits, 48000 Hz, mono or stereo";
      return NULL;
   }
   count = data_len / (2 * channels);
   if (count == 0 || count > (uint32_t)max_seconds * HD_RATE)
   {
      *err = "a WAV is empty or too long";
      return NULL;
   }
   out = (int16_t *)malloc((size_t)count * 4);
   if (!out)
   {
      *err = "not enough memory for the sound";
      return NULL;
   }
   for (i = 0; i < count; i++)
   {
      const uint8_t *p = data + (size_t)i * 2 * channels;
      int16_t l = (int16_t)le16(p), r = channels == 2 ? (int16_t)le16(p + 2) : l;
      out[2 * i] = l;
      out[2 * i + 1] = r;
   }
   *frames = (int32_t)count;
   return out;
}

static int16_t *load_wav(const hd_zip *zip, const char *name, int32_t max_seconds, int32_t *frames, char *msg, size_t cap)
{
   const char *err;
   size_t size;
   uint8_t *raw = hd_zip_read(zip, name, &size, &err);
   int16_t *pcm;
   if (!raw)
   {
      snprintf(msg, cap, "%s: %s", name, err);
      return NULL;
   }
   pcm = wav_read(raw, size, max_seconds, frames, &err);
   free(raw);
   if (!pcm)
      snprintf(msg, cap, "%s: %s", name, err);
   return pcm;
}

const char *hd_sounds_load(const hd_zip *zip, const json *sounds, const json *music)
{
   static char msg[200];
   int32_t i, frames;
   hd_sounds_free();
   if (sounds)
   {
      if (sounds->type != JSON_OBJECT)
         return "manifest.json's sounds must be an object";
      for (i = 0; i < SFX_COUNT; i++)
      {
         const json *f = hd_json_get(sounds, sfx_names[i]);
         int16_t *st, *mono;
         int32_t k;
         if (!f)
            continue;
         if (f->type != JSON_STRING)
            return "each sound must be a file name";
         st = load_wav(zip, f->str, SFX_SECONDS_MAX, &frames, msg, sizeof msg);
         if (!st)
         {
            hd_sounds_free();
            return msg;
         }
         mono = (int16_t *)malloc((size_t)frames * 2);
         if (!mono)
         {
            free(st);
            hd_sounds_free();
            return "not enough memory for the sounds";
         }
         for (k = 0; k < frames; k++)
            mono[k] = (int16_t)(((int32_t)st[2 * k] + st[2 * k + 1]) / 2);
         free(st);
         sfx_data[i] = mono;
         hd_audio_sample(i, mono, frames);
      }
   }
   if (music)
   {
      const json *f = hd_json_get(music, "file"), *vol = hd_json_get(music, "volume"), *loop = hd_json_get(music, "loop_from");
      if (music->type != JSON_OBJECT || !f || f->type != JSON_STRING)
         return "manifest.json's music needs a \"file\"";
      if ((vol && (vol->type != JSON_INT || vol->num < 0 || vol->num > 256)) || (loop && (loop->type != JSON_INT || loop->num < 0)))
         return "the music's volume must be 0 to 256 and its loop_from a number of milliseconds";
      music_data = load_wav(zip, f->str, MUSIC_SECONDS_MAX, &frames, msg, sizeof msg);
      if (!music_data)
      {
         hd_sounds_free();
         return msg;
      }
      hd_pkg_music = music_data;
      hd_pkg_music_frames = frames;
      hd_pkg_music_vol = vol ? (int32_t)vol->num : 200;
      hd_pkg_music_loop = loop ? (int32_t)hd_min((int32_t)((int64_t)loop->num * HD_RATE / 1000), frames - 1) : 0;
   }
   return NULL;
}
