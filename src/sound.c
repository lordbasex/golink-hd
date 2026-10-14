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
 *   "audio": {"effects": {"doorbell": "bell.wav", "drill": {"file": "drill.wav", "loop": true, "volume": 200}},
 *             "tracks": {"title": {"file": "title.wav", "volume": 200, "loop_from": 0}, ...}}
 *      named effects and music tracks for a game coded in the engine (the
 *      manifest's "game"), which asks for them by name (hd_effect, hd_track):
 *      up to HD_NAMED_MAX effects of up to 30 seconds ("loop": played over and
 *      over by hd_loop; "volume" 0..256 scales it as it loads) and
 *      HD_TRACKS_MAX tracks
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

#define NAME_LEN 32
#define NAMED_SECONDS_MAX 30
static struct
{
   char name[NAME_LEN];
   int16_t *data;
} named[HD_NAMED_MAX];
static int32_t named_count;
static struct
{
   char name[NAME_LEN];
   int16_t *data;
   int32_t frames, loop, vol;
} tracks[HD_TRACKS_MAX];
static int32_t track_count;

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
   for (i = 0; i < named_count; i++)
   {
      free(named[i].data);
      hd_audio_named(i, NULL, 0, 0);
   }
   memset(named, 0, sizeof named);
   named_count = 0;
   for (i = 0; i < track_count; i++)
      free(tracks[i].data);
   memset(tracks, 0, sizeof tracks);
   track_count = 0;
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

int32_t hd_effect(const char *name)
{
   int32_t i;
   for (i = 0; name && i < named_count; i++)
      if (!strcmp(named[i].name, name))
         return HD_SFX_NAMED + i;
   return -1;
}

int32_t hd_track(const char *name)
{
   int32_t i;
   for (i = 0; name && i < track_count; i++)
      if (!strcmp(tracks[i].name, name))
         return 2 + i;
   return 0;
}

const int16_t *hd_track_music(int32_t track, int32_t *frames, int32_t *loop, int32_t *vol)
{
   if (track == 0 && hd_pkg_music)
   {
      *frames = hd_pkg_music_frames, *loop = hd_pkg_music_loop, *vol = hd_pkg_music_vol;
      return hd_pkg_music;
   }
   if (track == 1 && hd_pkg_boss_music && hd_pkg_music)
   {
      *frames = hd_pkg_boss_music_frames, *loop = hd_pkg_boss_music_loop, *vol = hd_pkg_boss_music_vol;
      return hd_pkg_boss_music;
   }
   if (track >= 2 && track - 2 < track_count)
   {
      *frames = tracks[track - 2].frames, *loop = tracks[track - 2].loop, *vol = tracks[track - 2].vol;
      return tracks[track - 2].data;
   }
   return NULL;
}

/* A name of the "audio" section: short, and one a game can ask for. */
static int good_name(const char *name)
{
   return name && name[0] && strlen(name) < NAME_LEN;
}

static const char *load_effects(const hd_zip *zip, const json *effects)
{
   static char msg[200];
   const json *it;
   for (it = effects->child; it; it = it->next)
   {
      const json *file = it->type == JSON_STRING ? it : hd_json_get(it, "file"), *loop = it->type == JSON_OBJECT ? hd_json_get(it, "loop") : NULL;
      const json *vol = it->type == JSON_OBJECT ? hd_json_get(it, "volume") : NULL;
      int16_t *st, *mono;
      int32_t frames, k, v;
      if (!good_name(it->key))
         return "each of audio's effects needs a name shorter than 32 letters";
      if (named_count >= HD_NAMED_MAX)
         return "audio has more than 256 effects";
      if (hd_effect(it->key) >= 0)
         return "two of audio's effects have the same name";
      if (!file || file->type != JSON_STRING || (loop && loop->type != JSON_BOOL) || (vol && (vol->type != JSON_INT || vol->num < 0 || vol->num > 256)))
         return "each of audio's effects is a file name, or {\"file\", \"loop\": true|false, \"volume\": 0..256}";
      st = load_wav(zip, file->str, NAMED_SECONDS_MAX, &frames, msg, sizeof msg);
      if (!st)
         return msg;
      mono = (int16_t *)malloc((size_t)frames * 2);
      if (!mono)
      {
         free(st);
         return "not enough memory for the sounds";
      }
      v = vol ? (int32_t)vol->num : 256;
      for (k = 0; k < frames; k++)
         mono[k] = (int16_t)((((int32_t)st[2 * k] + st[2 * k + 1]) / 2) * v / 256);
      free(st);
      snprintf(named[named_count].name, NAME_LEN, "%s", it->key);
      named[named_count].data = mono;
      hd_audio_named(named_count, mono, frames, loop && loop->num);
      named_count++;
   }
   return NULL;
}

static const char *load_tracks(const hd_zip *zip, const json *list)
{
   static char msg[200];
   const json *it;
   for (it = list->child; it; it = it->next)
   {
      const json *f = hd_json_get(it, "file"), *vol = hd_json_get(it, "volume"), *loop = hd_json_get(it, "loop_from");
      int32_t frames;
      if (!good_name(it->key))
         return "each of audio's tracks needs a name shorter than 32 letters";
      if (track_count >= HD_TRACKS_MAX)
         return "audio has more than 16 tracks";
      if (hd_track(it->key))
         return "two of audio's tracks have the same name";
      if (it->type != JSON_OBJECT || !f || f->type != JSON_STRING)
         return "each of audio's tracks needs a \"file\"";
      if ((vol && (vol->type != JSON_INT || vol->num < 0 || vol->num > 256)) || (loop && (loop->type != JSON_INT || loop->num < 0 || loop->num > (int64_t)MUSIC_SECONDS_MAX * 1000)))
         return "a track's volume must be 0 to 256 and its loop_from a number of milliseconds";
      tracks[track_count].data = load_wav(zip, f->str, MUSIC_SECONDS_MAX, &frames, msg, sizeof msg);
      if (!tracks[track_count].data)
         return msg;
      snprintf(tracks[track_count].name, NAME_LEN, "%s", it->key);
      tracks[track_count].frames = frames;
      tracks[track_count].vol = vol ? (int32_t)vol->num : 200;
      tracks[track_count].loop = hd_clamp(loop ? (int32_t)(loop->num * HD_RATE / 1000) : 0, 0, frames - 1);
      track_count++;
   }
   return NULL;
}

const char *hd_audio_load(const hd_zip *zip, const json *audio)
{
   const json *effects, *list;
   const char *err = NULL;
   if (!audio)
      return NULL;
   effects = hd_json_get(audio, "effects");
   list = hd_json_get(audio, "tracks");
   if (audio->type != JSON_OBJECT || (effects && effects->type != JSON_OBJECT) || (list && list->type != JSON_OBJECT))
      return "manifest.json's audio must be an object with \"effects\" and \"tracks\" objects";
   if (effects)
      err = load_effects(zip, effects);
   if (!err && list)
      err = load_tracks(zip, list);
   if (err)
      hd_sounds_free();
   return err;
}
