/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * hdrun: a headless libretro frontend for tests and screenshots. It loads a
 * built core, plays a button script and writes chosen frames as PNG files.
 *
 *   tools/hdrun CORE [--content FILE] [--frames N] [--script FILE] [--shot F1,F2,...] [--out DIR]
 *
 * With --content the core gets that file's path (like go-link's device);
 * without it the core starts with no content.
 *
 * A script line is "FRAME PORT BUTTONS": from that frame on, the port holds
 * those buttons (comma separated: up down left right a b x y start select,
 * or "none"). Lines starting with # are comments. It prints a hash of every
 * frame and of the sound, so two runs (or two builds) can be compared.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

#ifdef _WIN32
#include <windows.h>
#define LOAD(p) (void *)LoadLibraryA(p)
#define SYM(h, n) (void *)GetProcAddress((HMODULE)(h), n)
#else
#include <dlfcn.h>
#define LOAD(p) dlopen(p, RTLD_NOW | RTLD_LOCAL)
#define SYM(h, n) dlsym(h, n)
#endif

#define MAX_LINES 4096

typedef struct { long frame; unsigned port; unsigned buttons; } line_t;

static line_t script[MAX_LINES];
static int lines;
static unsigned held[4];
static const void *last_frame;
static unsigned last_w, last_h;
static size_t last_pitch;
static uint32_t video_hash = 2166136261u, audio_hash = 2166136261u;
static long audio_frames;
static int pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;

static uint32_t fnv(const void *data, size_t n, uint32_t h)
{
   const uint8_t *p = (const uint8_t *)data;
   size_t i;
   for (i = 0; i < n; i++)
      h = (h ^ p[i]) * 16777619u;
   return h;
}

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   (void)level;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static bool env(unsigned cmd, void *data)
{
   switch (cmd)
   {
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      pixel_format = *(enum retro_pixel_format *)data;
      return true;
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      ((struct retro_log_callback *)data)->log = core_log;
      return true;
   case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      *(unsigned *)data = 2;
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = false;
      return true;
   case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
   case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
   case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
   case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      return true;
   default:
      return false;
   }
}

static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
   if (!data)
      return;
   last_frame = data;
   last_w = w;
   last_h = h;
   last_pitch = pitch;
   video_hash = fnv(data, pitch * h, video_hash);
}

static size_t audio_batch(const int16_t *data, size_t frames)
{
   audio_hash = fnv(data, frames * 4, audio_hash);
   audio_frames += (long)frames;
   return frames;
}

static void audio_one(int16_t l, int16_t r)
{
   int16_t s[2];
   s[0] = l;
   s[1] = r;
   audio_batch(s, 1);
}

static void poll(void) {}

static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id)
{
   (void)index;
   if (device != RETRO_DEVICE_JOYPAD || port >= 4)
      return 0;
   if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
      return (int16_t)held[port];
   return (int16_t)((held[port] >> id) & 1);
}

static unsigned parse_buttons(const char *s)
{
   static const struct { const char *name; unsigned id; } names[] = {
      { "b", RETRO_DEVICE_ID_JOYPAD_B }, { "y", RETRO_DEVICE_ID_JOYPAD_Y },
      { "select", RETRO_DEVICE_ID_JOYPAD_SELECT }, { "start", RETRO_DEVICE_ID_JOYPAD_START },
      { "up", RETRO_DEVICE_ID_JOYPAD_UP }, { "down", RETRO_DEVICE_ID_JOYPAD_DOWN },
      { "left", RETRO_DEVICE_ID_JOYPAD_LEFT }, { "right", RETRO_DEVICE_ID_JOYPAD_RIGHT },
      { "a", RETRO_DEVICE_ID_JOYPAD_A }, { "x", RETRO_DEVICE_ID_JOYPAD_X },
   };
   unsigned out = 0, i;
   char buf[256], *tok;
   strncpy(buf, s, sizeof buf - 1);
   buf[sizeof buf - 1] = 0;
   for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ","))
      for (i = 0; i < sizeof names / sizeof names[0]; i++)
         if (strcmp(tok, names[i].name) == 0)
            out |= 1u << names[i].id;
   return out;
}

static int load_script(const char *path)
{
   FILE *f = fopen(path, "r");
   char row[512];
   if (!f)
      return 0;
   while (fgets(row, sizeof row, f) && lines < MAX_LINES)
   {
      long frame;
      unsigned port;
      char buttons[256];
      if (row[0] == '#' || sscanf(row, "%ld %u %255s", &frame, &port, buttons) != 3 || port >= 4)
         continue;
      script[lines].frame = frame;
      script[lines].port = port;
      script[lines].buttons = parse_buttons(buttons);
      lines++;
   }
   fclose(f);
   return 1;
}

/* PNG with stored (uncompressed) deflate blocks: no zlib needed. */
static uint32_t crc_table[256];

static uint32_t crc(const uint8_t *p, size_t n, uint32_t c)
{
   size_t i;
   for (i = 0; i < n; i++)
      c = crc_table[(c ^ p[i]) & 0xff] ^ (c >> 8);
   return c;
}

static void be32(uint8_t *p, uint32_t v)
{
   p[0] = (uint8_t)(v >> 24);
   p[1] = (uint8_t)(v >> 16);
   p[2] = (uint8_t)(v >> 8);
   p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
   uint8_t head[8], tail[4];
   uint32_t c;
   be32(head, n);
   memcpy(head + 4, type, 4);
   fwrite(head, 1, 8, f);
   if (n)
      fwrite(data, 1, n, f);
   c = crc(head + 4, 4, 0xffffffffu);
   c = crc(data, n, c) ^ 0xffffffffu;
   be32(tail, c);
   fwrite(tail, 1, 4, f);
}

static void rgb_at(unsigned x, unsigned y, uint8_t *out)
{
   const uint8_t *row = (const uint8_t *)last_frame + y * last_pitch;
   if (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
   {
      uint32_t v;
      memcpy(&v, row + x * 4, 4);
      out[0] = (uint8_t)(v >> 16);
      out[1] = (uint8_t)(v >> 8);
      out[2] = (uint8_t)v;
   }
   else
   {
      uint16_t v;
      memcpy(&v, row + x * 2, 2);
      if (pixel_format == RETRO_PIXEL_FORMAT_RGB565)
      {
         out[0] = (uint8_t)((v >> 11) << 3);
         out[1] = (uint8_t)(((v >> 5) & 63) << 2);
      }
      else
      {
         out[0] = (uint8_t)(((v >> 10) & 31) << 3);
         out[1] = (uint8_t)(((v >> 5) & 31) << 3);
      }
      out[2] = (uint8_t)((v & 31) << 3);
   }
}

static int write_png(const char *path)
{
   uint32_t raw_len = (last_w * 3 + 1) * last_h, a = 1, b = 0, i, at = 0;
   uint32_t blocks = (raw_len + 65534) / 65535;
   uint8_t *raw, *z, hdr[13];
   size_t zlen = 2 + blocks * 5 + raw_len + 4, zi = 0;
   unsigned x, y;
   FILE *f;
   if (!last_frame)
      return 0;
   raw = (uint8_t *)malloc(raw_len);
   z = (uint8_t *)malloc(zlen);
   if (!raw || !z)
      return 0;
   for (y = 0; y < last_h; y++)
   {
      raw[at++] = 0;
      for (x = 0; x < last_w; x++, at += 3)
         rgb_at(x, y, raw + at);
   }
   z[zi++] = 0x78;
   z[zi++] = 0x01;
   for (i = 0; i < raw_len; i += 65535)
   {
      uint32_t n = raw_len - i < 65535 ? raw_len - i : 65535;
      z[zi++] = (uint8_t)(i + n >= raw_len);
      z[zi++] = (uint8_t)n;
      z[zi++] = (uint8_t)(n >> 8);
      z[zi++] = (uint8_t)~n;
      z[zi++] = (uint8_t)(~n >> 8);
      memcpy(z + zi, raw + i, n);
      zi += n;
   }
   for (i = 0; i < raw_len; i++)
   {
      a = (a + raw[i]) % 65521;
      b = (b + a) % 65521;
   }
   be32(z + zi, b << 16 | a);
   zi += 4;
   f = fopen(path, "wb");
   if (!f)
      return 0;
   fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
   be32(hdr, last_w);
   be32(hdr + 4, last_h);
   hdr[8] = 8;
   hdr[9] = 2;
   hdr[10] = hdr[11] = hdr[12] = 0;
   chunk(f, "IHDR", hdr, 13);
   chunk(f, "IDAT", z, (uint32_t)zi);
   chunk(f, "IEND", NULL, 0);
   fclose(f);
   free(raw);
   free(z);
   return 1;
}

int main(int argc, char **argv)
{
   void *core;
   long frames = 600, f, shots[64];
   int nshots = 0, i, next = 0;
   const char *out = ".", *content = NULL;
   struct retro_game_info game;
   void (*set_environment)(retro_environment_t);
   void (*set_video)(retro_video_refresh_t);
   void (*set_audio)(retro_audio_sample_t);
   void (*set_audio_batch)(retro_audio_sample_batch_t);
   void (*set_poll)(retro_input_poll_t);
   void (*set_input)(retro_input_state_t);
   void (*init)(void);
   bool (*load_game)(const struct retro_game_info *);
   void (*run)(void);
   void (*deinit)(void);
   unsigned n;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s CORE [--content FILE] [--frames N] [--script FILE] [--shot F1,F2] [--out DIR]\n", argv[0]);
      return 2;
   }
   for (i = 2; i < argc; i++)
   {
      if (!strcmp(argv[i], "--frames") && i + 1 < argc)
         frames = atol(argv[++i]);
      else if (!strcmp(argv[i], "--script") && i + 1 < argc)
      {
         if (!load_script(argv[++i]))
         {
            fprintf(stderr, "cannot read %s\n", argv[i]);
            return 1;
         }
      }
      else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
      {
         char *tok;
         for (tok = strtok(argv[++i], ","); tok && nshots < 64; tok = strtok(NULL, ","))
            shots[nshots++] = atol(tok);
      }
      else if (!strcmp(argv[i], "--out") && i + 1 < argc)
         out = argv[++i];
      else if (!strcmp(argv[i], "--content") && i + 1 < argc)
         content = argv[++i];
   }
   for (n = 0; n < 256; n++)
   {
      uint32_t c = n;
      int k;
      for (k = 0; k < 8; k++)
         c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
      crc_table[n] = c;
   }

   core = LOAD(argv[1]);
   if (!core)
   {
      fprintf(stderr, "cannot load %s\n", argv[1]);
      return 1;
   }
#define GET(var, name)                                     \
   *(void **)(&var) = SYM(core, name);                    \
   if (!var)                                              \
   {                                                      \
      fprintf(stderr, "the core has no %s\n", name);      \
      return 1;                                           \
   }
   GET(set_environment, "retro_set_environment");
   GET(set_video, "retro_set_video_refresh");
   GET(set_audio, "retro_set_audio_sample");
   GET(set_audio_batch, "retro_set_audio_sample_batch");
   GET(set_poll, "retro_set_input_poll");
   GET(set_input, "retro_set_input_state");
   GET(init, "retro_init");
   GET(load_game, "retro_load_game");
   GET(run, "retro_run");
   GET(deinit, "retro_deinit");
#undef GET

   set_environment(env);
   set_video(video);
   set_audio(audio_one);
   set_audio_batch(audio_batch);
   set_poll(poll);
   set_input(input);
   init();
   memset(&game, 0, sizeof game);
   game.path = content;
   if (!load_game(content ? &game : NULL))
   {
      fprintf(stderr, content ? "the core refused %s\n" : "the core refused to start with no content\n", content);
      return 1;
   }
   for (f = 0; f < frames; f++)
   {
      while (next < lines && script[next].frame <= f)
      {
         held[script[next].port] = script[next].buttons;
         next++;
      }
      run();
      for (i = 0; i < nshots; i++)
         if (shots[i] == f)
         {
            char path[1024];
            snprintf(path, sizeof path, "%s/frame-%05ld.png", out, f);
            if (!write_png(path))
               fprintf(stderr, "cannot write %s\n", path);
         }
   }
   printf("frames %ld video %08x audio %08x samples %ld\n", frames, video_hash, audio_hash, audio_frames);
   deinit();
   return 0;
}
