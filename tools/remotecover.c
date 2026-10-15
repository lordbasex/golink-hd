/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * TELETRABAJO: CÁMARA APAGADA's picture for game libraries (go-link
 * Console's rooms and lists): the package's flat with the worker at the desk
 * and the cat walking on the keyboard, the title over it, no HUD. build.py
 * packs it as the package's title screen (screens.title).
 *
 *   make tools/remotecover
 *   tools/remotecover remote.glhd cover.png [man|woman]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hd.h"
#include "room3d.h"
#include "games/remote/remote.h"

int hd_content_load(const uint8_t *data, size_t size, const char **err);

static hd_state st;
static uint32_t fb[1920 * 1080];

/* PNG with stored (uncompressed) deflate blocks: no zlib needed; build.py makes it a JPEG. */
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

static int write_png(const char *path, const uint32_t *px, uint32_t w, uint32_t h)
{
   uint32_t raw_len = (w * 3 + 1) * h, a = 1, b = 0, i, at = 0, x, y, k;
   uint32_t blocks = (raw_len + 65534) / 65535;
   uint8_t *raw = (uint8_t *)malloc(raw_len), *z = (uint8_t *)malloc(2 + blocks * 5 + raw_len + 4), hdr[13];
   size_t zi = 0;
   FILE *f;
   for (i = 0; i < 256; i++)
   {
      uint32_t c = i;
      for (k = 0; k < 8; k++)
         c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
      crc_table[i] = c;
   }
   if (!raw || !z)
      return 0;
   for (y = 0; y < h; y++)
   {
      raw[at++] = 0;
      for (x = 0; x < w; x++, at += 3)
      {
         uint32_t v = px[y * w + x];
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

/* One frame with the camera held behind the worker's shoulder, toward the desk and the window. */
static void step(uint32_t buttons)
{
   hd_remote *g = HD_GAME3D(&st, hd_remote);
   hd_input in[MAX_PLAYERS];
   memset(in, 0, sizeof in);
   in[0].buttons = buttons;
   hd_step(&st, in);
   g->cam_yaw = (g->yaw + 2048 + 450) & (ANGLE_FULL - 1);
   g->cam_pitch = 520;
}

int main(int argc, char **argv)
{
   hd_remote *g = HD_GAME3D(&st, hd_remote);
   const char *err;
   int32_t px, pz, f;
   uint8_t *data;
   long n;
   FILE *in;
   if (argc < 3)
   {
      fprintf(stderr, "usage: tools/remotecover remote.glhd cover.png [man|woman]\n");
      return 2;
   }
   in = fopen(argv[1], "rb");
   if (!in || fseek(in, 0, SEEK_END) || (n = ftell(in)) <= 0 || fseek(in, 0, SEEK_SET))
   {
      fprintf(stderr, "cannot read %s\n", argv[1]);
      return 1;
   }
   data = (uint8_t *)malloc((size_t)n);
   hd_static_init();
   if (!data || fread(data, 1, (size_t)n, in) != (size_t)n || !hd_content_load(data, (size_t)n, &err))
   {
      fprintf(stderr, "%s cannot be played\n", argv[1]);
      return 1;
   }
   fclose(in);
   hd_res = 2;  /* 720p */
   hd_lang = 1; /* the title as the game is named, in Spanish */
   hd_reset(&st);
   hd_remote_new_day(&st);
   g->screen = RSC_DAY;
   g->hero = argc > 3 && !strcmp(argv[3], "woman");
   g->ev_next = g->ev_count; /* a quiet day: no boss, no bell */
   g->chatter_at = 1 << 20;
   /* the worker at the desk, typing; the cat comes for the keyboard */
   remote_place(REMOTE_PLACE_ACTIVITY, RA_WORK, &px, &pz);
   g->x = ROOM3D_AT(px), g->z = ROOM3D_AT(pz);
   step(0);
   step(PAD_JUMP);
   g->cat_keys = 1;
   for (f = 0; f < 60 * 60 && !(g->cat_keys == 2 && g->cat_keys_t > 40); f++)
      step(0);
   remote_cover = 1;
   hd_draw(&st, fb);
   if (!write_png(argv[2], fb, 1280, 720))
   {
      fprintf(stderr, "cannot write %s\n", argv[2]);
      return 1;
   }
   printf("%s: %s at the desk%s\n", argv[2], g->hero ? "the woman" : "the man", g->cat_keys == 2 ? ", the cat on the keyboard" : "");
   return 0;
}
