/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Format 3's sounds and music: a package's own WAV files.
 *
 *   "sounds": {"jump": "jump.wav", "coin": "gem.wav", ...}
 *      the effects it replaces: jump, coin, stomp, hurt, join, check, clear, pause, shoot, hit, knockout, super, yawn,
 *      spit, dash, roar, boss_hit, boss_down
 *   "music": {"file": "level.wav", "volume": 200, "loop_from": 0}
 *      played over and over instead of the built-in tune; loop_from (in
 *      milliseconds) is where it starts again after the end
 *
 * WAV files are PCM, 16 bits, or IMA ADPCM (4 bits a sample, a quarter of
 * the size: the music of a whole game), 48000 Hz, mono or stereo: effects are mixed
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

static const char *const sfx_names[SFX_COUNT] = { "jump", "coin", "stomp", "hurt", "join", "check", "clear", "pause", "shoot", "hit", "knockout", "super", "yawn",
                                                        "spit", "dash", "roar", "boss_hit", "boss_down" };

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

/* IMA ADPCM's tables (the IMA's 1992 recommendation, as WAV files use it). */
static const int16_t ima_step[89] = {
   7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
   130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
   1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
   7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int8_t ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

/* One 4-bit code into a sample, moving the channel's predictor and step. */
static int16_t ima_nibble(int32_t *pred, int32_t *index, uint32_t code)
{
   int32_t step = ima_step[*index], diff = step >> 3;
   if (code & 1)
      diff += step >> 2;
   if (code & 2)
      diff += step >> 1;
   if (code & 4)
      diff += step;
   *pred = hd_clamp(*pred + ((code & 8) ? -diff : diff), -32768, 32767);
   *index = hd_clamp(*index + ima_index[code & 15], 0, 88);
   return (int16_t)*pred;
}

/*
 * IMA ADPCM blocks into 16-bit stereo: each block of `align` bytes starts
 * with a header per channel (the first sample and the step's index), then
 * 4 bytes of each channel in turn, 8 samples in each (low nibble first).
 */
static int16_t *ima_decode(const uint8_t *data, uint32_t len, uint32_t channels, uint32_t align, uint32_t per_block, int32_t max_seconds,
                           int32_t *frames, const char **err)
{
   uint32_t blocks = len / align, b, c, total = 0;
   int16_t *out;
   if (align < 4 * channels + 4 || per_block != (align - 4 * channels) * 2 / channels + 1 || (align - 4 * channels) % (4 * channels) || blocks == 0)
   {
      *err = "an ADPCM WAV's blocks are not the IMA's";
      return NULL;
   }
   if ((uint64_t)blocks * per_block > (uint64_t)max_seconds * HD_RATE)
   {
      *err = "a WAV is empty or too long";
      return NULL;
   }
   out = (int16_t *)malloc((size_t)blocks * per_block * 4);
   if (!out)
   {
      *err = "not enough memory for the sound";
      return NULL;
   }
   for (b = 0; b < blocks; b++)
   {
      const uint8_t *blk = data + (size_t)b * align;
      int32_t pred[2], index[2];
      uint32_t n, at = 4 * channels;
      for (c = 0; c < channels; c++)
      {
         pred[c] = (int16_t)le16(blk + 4 * c);
         index[c] = hd_clamp(blk[4 * c + 2], 0, 88);
         out[2 * total + c] = (int16_t)pred[c];
      }
      if (channels == 1)
         out[2 * total + 1] = out[2 * total];
      for (n = 1; n < per_block; n += 8)
      {
         for (c = 0; c < channels; c++)
         {
            uint32_t k;
            for (k = 0; k < 8; k++)
            {
               uint32_t byte = blk[at + c * 4 + k / 2], code = k & 1 ? byte >> 4 : byte & 15;
               int16_t v = ima_nibble(&pred[c], &index[c], code);
               if (n + k < per_block)
               {
                  out[2 * (total + n + k) + c] = v;
                  if (channels == 1)
                     out[2 * (total + n + k) + 1] = v;
               }
            }
         }
         at += 4 * channels;
      }
      total += per_block;
   }
   *frames = (int32_t)total;
   return out;
}

/* A WAV's samples as 16-bit stereo (mono duplicated); NULL and *err when it is not one go-link HD plays. */
static int16_t *wav_read(const uint8_t *d, size_t n, int32_t max_seconds, int32_t *frames, const char **err)
{
   size_t at = 12;
   const uint8_t *data = NULL;
   uint32_t data_len = 0, channels = 0, rate = 0, bits = 0, format = 0, align = 0, per_block = 0, i, count;
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
         align = le16(d + at + 20);
         bits = le16(d + at + 22);
         if (len >= 20)
            per_block = le16(d + at + 26);
      }
      else if (!memcmp(d + at, "data", 4))
      {
         data = d + at + 8;
         data_len = len;
      }
      at += 8 + len + (len & 1);
   }
   if (format == 0x11 && bits == 4 && rate == HD_RATE && (channels == 1 || channels == 2) && data)
      return ima_decode(data, data_len, channels, align, per_block, max_seconds, frames, err);
   if ((format != 1 && format != 0xfffe) || bits != 16 || rate != HD_RATE || (channels != 1 && channels != 2) || !data)
   {
      *err = "a WAV must be PCM (16 bits) or IMA ADPCM, 48000 Hz, mono or stereo";
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
      const char *err = hd_music_load(zip, music);
      if (err)
      {
         hd_sounds_free();
         return err;
      }
   }
   return NULL;
}

int16_t *hd_music_take(void)
{
   int16_t *m = music_data;
   music_data = NULL;
   hd_pkg_music = NULL;
   hd_pkg_music_frames = hd_pkg_music_loop = 0;
   hd_pkg_music_vol = 200;
   return m;
}

const char *hd_music_load(const hd_zip *zip, const json *music)
{
   static char msg[200];
   int32_t frames;
   free(music_data);
   music_data = NULL;
   hd_pkg_music = NULL;
   hd_pkg_music_frames = hd_pkg_music_loop = 0;
   {
      const json *f = hd_json_get(music, "file"), *vol = hd_json_get(music, "volume"), *loop = hd_json_get(music, "loop_from");
      if (music->type != JSON_OBJECT || !f || f->type != JSON_STRING)
         return "manifest.json's music needs a \"file\"";
      if ((vol && (vol->type != JSON_INT || vol->num < 0 || vol->num > 256)) || (loop && (loop->type != JSON_INT || loop->num < 0 || loop->num > (int64_t)MUSIC_SECONDS_MAX * 1000)))
         return "the music's volume must be 0 to 256 and its loop_from a number of milliseconds";
      music_data = load_wav(zip, f->str, MUSIC_SECONDS_MAX, &frames, msg, sizeof msg);
      if (!music_data)
         return msg;
      hd_pkg_music = music_data;
      hd_pkg_music_frames = frames;
      hd_pkg_music_vol = vol ? (int32_t)vol->num : 200;
      /* loop_from is at most MUSIC_SECONDS_MAX seconds: the product fits, and the result stays inside the song */
      hd_pkg_music_loop = loop ? (int32_t)(loop->num * HD_RATE / 1000) : 0;
      hd_pkg_music_loop = hd_clamp(hd_pkg_music_loop, 0, frames - 1);
   }
   return NULL;
}
