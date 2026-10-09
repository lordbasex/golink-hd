/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * hdrun: a headless host of go-link HD's library, for tests and screenshots.
 * It loads the built library (as go-link's device does), plays a button
 * script and writes chosen frames as PNG files.
 *
 *   tools/hdrun LIBRARY [--content FILE.glhd] [--demo showcase] [--language en|es|pt]
 *                       [--music off] [--frames N] [--script FILE] [--shot F1,F2,...]
 *                       [--out DIR] [--check]
 *
 * A script line is "FRAME PORT BUTTONS": from that frame on, the port (0-7)
 * holds those buttons (comma separated: up down left right a b x y start
 * select l r l2 r2 l3 r3, sticks as lx:N ly:N rx:N ry:N, or "none"). Lines
 * starting with # are comments. It prints a hash of every frame and of the
 * sound, so two runs (or two builds) can be compared.
 *
 * --check also tries the rest of the API on the same run: it prints the
 * game's info, saves the state halfway, then loads that state and plays the
 * second half again (it must give the very same frames and sound as the
 * first time), and restarts the game and plays it all again (the same
 * again). It exits with 1 when anything differs.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "golink_hd.h"

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
#define PORTS GOLINKHD_MAX_PLAYERS

typedef struct { long frame; unsigned port; golinkhd_pad pad; } line_t;

static line_t script[MAX_LINES];
static int lines;

static uint32_t fnv(const void *data, size_t n, uint32_t h)
{
   const uint8_t *p = (const uint8_t *)data;
   size_t i;
   for (i = 0; i < n; i++)
      h = (h ^ p[i]) * 16777619u;
   return h;
}

static void say(void *user, int32_t level, const char *msg)
{
   (void)user;
   (void)level;
   fprintf(stderr, "[go-link HD] %s\n", msg);
}

/* Buttons by a pad's names (b and a jump, y and x run), and sticks as lx:N ly:N rx:N ry:N. */
static golinkhd_pad parse_pad(const char *s)
{
   static const struct { const char *name; uint32_t bits; } names[] = {
      { "up", GOLINKHD_UP }, { "down", GOLINKHD_DOWN }, { "left", GOLINKHD_LEFT }, { "right", GOLINKHD_RIGHT },
      { "b", GOLINKHD_JUMP | GOLINKHD_B }, { "a", GOLINKHD_JUMP | GOLINKHD_A },
      { "y", GOLINKHD_RUN | GOLINKHD_Y }, { "x", GOLINKHD_RUN | GOLINKHD_X },
      { "start", GOLINKHD_START }, { "select", GOLINKHD_SELECT }, { "l", GOLINKHD_L }, { "r", GOLINKHD_R },
      { "l2", GOLINKHD_L2 }, { "r2", GOLINKHD_R2 }, { "l3", GOLINKHD_L3 }, { "r3", GOLINKHD_R3 },
   };
   golinkhd_pad pad;
   char buf[256], *tok;
   unsigned i;
   memset(&pad, 0, sizeof pad);
   strncpy(buf, s, sizeof buf - 1);
   buf[sizeof buf - 1] = 0;
   for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ","))
   {
      if (!strncmp(tok, "lx:", 3))
         pad.lx = atoi(tok + 3);
      else if (!strncmp(tok, "ly:", 3))
         pad.ly = atoi(tok + 3);
      else if (!strncmp(tok, "rx:", 3))
         pad.rx = atoi(tok + 3);
      else if (!strncmp(tok, "ry:", 3))
         pad.ry = atoi(tok + 3);
      for (i = 0; i < sizeof names / sizeof names[0]; i++)
         if (strcmp(tok, names[i].name) == 0)
            pad.buttons |= names[i].bits;
   }
   return pad;
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
      if (row[0] == '#' || sscanf(row, "%ld %u %255s", &frame, &port, buttons) != 3 || port >= PORTS)
         continue;
      script[lines].frame = frame;
      script[lines].port = port;
      script[lines].pad = parse_pad(buttons);
      lines++;
   }
   fclose(f);
   return 1;
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
   buf = (uint8_t *)malloc(len > 0 ? (size_t)len : 1);
   if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len)
   {
      free(buf);
      buf = NULL;
   }
   fclose(f);
   *n = (size_t)len;
   return buf;
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

static int write_png(const char *path, const golinkhd_frame_out *fr)
{
   uint32_t w = (uint32_t)fr->width, h = (uint32_t)fr->height;
   uint32_t raw_len = (w * 3 + 1) * h, a = 1, b = 0, i, at = 0, x, y;
   uint32_t blocks = (raw_len + 65534) / 65535;
   uint8_t *raw = (uint8_t *)malloc(raw_len), *z = (uint8_t *)malloc(2 + blocks * 5 + raw_len + 4), hdr[13];
   size_t zi = 0;
   FILE *f;
   if (!raw || !z)
      return 0;
   for (y = 0; y < h; y++)
   {
      raw[at++] = 0;
      for (x = 0; x < w; x++, at += 3)
      {
         uint32_t v = fr->pixels[y * (uint32_t)fr->pitch + x];
         raw[at] = (uint8_t)(v >> 16);
         raw[at + 1] = (uint8_t)(v >> 8);
         raw[at + 2] = (uint8_t)v;
      }
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
   be32(hdr, w);
   be32(hdr + 4, h);
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

typedef struct { uint32_t video, audio; long samples; } hash_t;

typedef struct
{
   void (*frame)(golinkhd_engine *, const golinkhd_pad *, int32_t, golinkhd_frame_out *);
   golinkhd_engine *e;
   long shots[64];
   int nshots;
   const char *out;
} host_t;

static hash_t fresh(void)
{
   hash_t h = { 2166136261u, 2166136261u, 0 };
   return h;
}

static int same(hash_t a, hash_t b)
{
   return a.video == b.video && a.audio == b.audio && a.samples == b.samples;
}

/* Plays frames [from, to) with the script into the hashes a (and b when
 * given); writes the asked frames as PNG when shoot is set. */
static void play(const host_t *h, long from, long to, hash_t *a, hash_t *b, int shoot)
{
   golinkhd_pad held[PORTS];
   long f;
   int i, next = 0;
   memset(held, 0, sizeof held);
   /* the buttons held when the run reaches `from` */
   while (next < lines && script[next].frame < from)
   {
      held[script[next].port] = script[next].pad;
      next++;
   }
   for (f = from; f < to; f++)
   {
      golinkhd_frame_out fr;
      hash_t *hs[2];
      int k;
      hs[0] = a;
      hs[1] = b;
      while (next < lines && script[next].frame <= f)
      {
         held[script[next].port] = script[next].pad;
         next++;
      }
      h->frame(h->e, held, PORTS, &fr);
      for (k = 0; k < 2; k++)
      {
         if (!hs[k])
            continue;
         for (i = 0; i < fr.height; i++)
            hs[k]->video = fnv(fr.pixels + i * fr.pitch, (size_t)fr.width * 4, hs[k]->video);
         hs[k]->audio = fnv(fr.audio, (size_t)fr.audio_frames * 4, hs[k]->audio);
         hs[k]->samples += fr.audio_frames;
      }
      for (i = 0; shoot && i < h->nshots; i++)
         if (h->shots[i] == f)
         {
            char path[1024];
            snprintf(path, sizeof path, "%s/frame-%05ld.png", h->out, f);
            if (!write_png(path, &fr))
               fprintf(stderr, "cannot write %s\n", path);
         }
   }
}

int main(int argc, char **argv)
{
   void *lib;
   long frames = 600, half;
   int i, check = 0, music = 1, failed = 0;
   const char *content = NULL, *language = NULL, *err = NULL;
   int showcase = 0;
   hash_t run = fresh();
   golinkhd_config cfg;
   golinkhd_engine *e;
   host_t host;
   unsigned n;
   golinkhd_engine *(*create)(const golinkhd_config *, const char **);
   void (*destroy)(golinkhd_engine *);
   int (*load)(golinkhd_engine *, const uint8_t *, size_t, const char **);
   void (*load_demo)(golinkhd_engine *, int32_t);
   void (*set_language)(golinkhd_engine *, const char *);
   void (*frame)(golinkhd_engine *, const golinkhd_pad *, int32_t, golinkhd_frame_out *);
   void (*set_music)(golinkhd_engine *, int);
   void (*get_info)(golinkhd_engine *, golinkhd_info *);
   void (*restart)(golinkhd_engine *);
   size_t (*state_size)(golinkhd_engine *);
   int (*state_save)(golinkhd_engine *, uint8_t *, size_t);
   int (*state_load)(golinkhd_engine *, const uint8_t *, size_t, const char **);
   const char *(*version)(void);
   int32_t (*api_version)(void);

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s LIBRARY [--content FILE] [--demo showcase] [--language en|es|pt] [--music off] [--frames N] [--script FILE] [--shot F1,F2] [--out DIR] [--check]\n", argv[0]);
      return 2;
   }
   memset(&host, 0, sizeof host);
   host.out = ".";
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
         for (tok = strtok(argv[++i], ","); tok && host.nshots < 64; tok = strtok(NULL, ","))
            host.shots[host.nshots++] = atol(tok);
      }
      else if (!strcmp(argv[i], "--out") && i + 1 < argc)
         host.out = argv[++i];
      else if (!strcmp(argv[i], "--music") && i + 1 < argc)
         music = strcmp(argv[++i], "off") != 0;
      else if (!strcmp(argv[i], "--check"))
         check = 1;
      else if (!strcmp(argv[i], "--content") && i + 1 < argc)
         content = argv[++i];
      else if (!strcmp(argv[i], "--language") && i + 1 < argc)
         language = argv[++i];
      else if (!strcmp(argv[i], "--demo") && i + 1 < argc)
         showcase = !strcmp(argv[++i], "showcase");
   }
   for (n = 0; n < 256; n++)
   {
      uint32_t c = n;
      int k;
      for (k = 0; k < 8; k++)
         c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
      crc_table[n] = c;
   }
   lib = LOAD(argv[1]);
   if (!lib)
   {
      fprintf(stderr, "cannot load %s\n", argv[1]);
      return 1;
   }
#define GET(var, name)                                    \
   *(void **)(&var) = SYM(lib, name);                     \
   if (!var)                                              \
   {                                                      \
      fprintf(stderr, "the library has no %s\n", name);   \
      return 1;                                           \
   }
   GET(create, "golinkhd_create");
   GET(destroy, "golinkhd_destroy");
   GET(load, "golinkhd_load");
   GET(load_demo, "golinkhd_load_demo");
   GET(set_language, "golinkhd_set_language");
   GET(frame, "golinkhd_frame");
   GET(set_music, "golinkhd_set_music");
   GET(get_info, "golinkhd_get_info");
   GET(restart, "golinkhd_restart");
   GET(state_size, "golinkhd_state_size");
   GET(state_save, "golinkhd_state_save");
   GET(state_load, "golinkhd_state_load");
   GET(version, "golinkhd_version");
   GET(api_version, "golinkhd_api_version");
#undef GET

   memset(&cfg, 0, sizeof cfg);
   cfg.api_version = GOLINKHD_API_VERSION;
   cfg.log = say;
   e = create(&cfg, &err);
   if (!e)
   {
      fprintf(stderr, "the engine did not start: %s\n", err);
      return 1;
   }
   if (language)
      set_language(e, language);
   if (content)
   {
      size_t size;
      uint8_t *data = slurp(content, &size);
      if (!data || !load(e, data, size, &err))
      {
         fprintf(stderr, "cannot play %s: %s\n", content, data ? err : "cannot read it");
         return 1;
      }
      free(data);
   }
   else if (showcase)
      load_demo(e, 1);
   if (!music)
      set_music(e, 0);
   host.frame = frame;
   host.e = e;
   half = frames / 2;
   if (!check)
      play(&host, 0, frames, &run, NULL, 1);
   else
   {
      golinkhd_info info;
      hash_t second = fresh(), again = fresh(), restarted = fresh();
      size_t size = state_size(e);
      uint8_t *state = (uint8_t *)malloc(size ? size : 1);
      get_info(e, &info);
      printf("engine %s, api %d: \"%s\" %dx%d, %d fps, %d Hz, %d players, sha256 ", version(), (int)api_version(),
             info.title, (int)info.width, (int)info.height, (int)info.fps, (int)info.sample_rate, (int)info.players);
      for (i = 0; i < 32; i++)
         printf("%02x", info.sha256[i]);
      printf("\n");
      /* the run, with its state saved halfway and its second half measured on its own */
      play(&host, 0, half, &run, NULL, 1);
      if (!state || !size || !state_save(e, state, size))
      {
         fprintf(stderr, "check: the state could not be saved\n");
         return 1;
      }
      play(&host, half, frames, &run, &second, 1);
      /* the second half again, from the saved state */
      if (!state_load(e, state, size, &err))
      {
         fprintf(stderr, "check: the saved state did not load: %s\n", err);
         return 1;
      }
      play(&host, half, frames, &again, NULL, 0);
      /* the whole run again, after a restart */
      restart(e);
      play(&host, 0, frames, &restarted, NULL, 0);
      printf("state: %ld bytes saved at frame %ld, the rest played again from it: %s\n", (long)size, half, same(second, again) ? "same" : "DIFFERENT");
      printf("restart: the whole run again: %s\n", same(run, restarted) ? "same" : "DIFFERENT");
      failed = !same(second, again) || !same(run, restarted);
      free(state);
   }
   printf("frames %ld video %08x audio %08x samples %ld\n", frames, run.video, run.audio, run.samples);
   destroy(e);
   return failed;
}
