/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * TELETRABAJO: CÁMARA APAGADA's poses, one picture each: the main character
 * (the man and the woman) in every activity, seen from four sides, to check
 * that the body sits on the chair, lies on the bed and holds what it holds
 * without going through the furniture.
 *
 *   make tools/remoteposes
 *   tools/remoteposes remote.glhd OUT_DIR [ACTIVITY ...]
 *
 * Writes OUT_DIR/<man|woman>-<activity>-<side>.png (720p).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hd.h"
#include "room3d.h"
#include "actor3d.h"
#include "r3d.h"
#include "games/remote/remote.h"

int hd_content_load(const uint8_t *data, size_t size, const char **err);

static hd_state st;
static uint32_t fb[1280 * 720];
static const char *const names[RA_COUNT] = { "work", "nap", "coffee", "cook", "fridge", "eat", "series", "console",
                                             "guitar", "sun", "plants", "shower", "washing", "door", "cat", "phones" };

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

int main(int argc, char **argv)
{
   hd_remote *g = HD_GAME3D(&st, hd_remote);
   hd_input in[MAX_PLAYERS];
   const char *err;
   uint8_t *data;
   long n;
   FILE *f;
   int32_t hero, a, side, i;
   char path[1024];
   if (argc < 3)
   {
      fprintf(stderr, "usage: tools/remoteposes remote.glhd OUT_DIR [ACTIVITY ...]\n");
      return 2;
   }
   f = fopen(argv[1], "rb");
   if (!f || fseek(f, 0, SEEK_END) || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET))
   {
      fprintf(stderr, "cannot read %s\n", argv[1]);
      return 1;
   }
   data = (uint8_t *)malloc((size_t)n);
   hd_static_init();
   if (!data || fread(data, 1, (size_t)n, f) != (size_t)n || !hd_content_load(data, (size_t)n, &err))
   {
      fprintf(stderr, "%s cannot be played\n", argv[1]);
      return 1;
   }
   fclose(f);
   hd_res = 2;
   hd_lang = 1;
   for (hero = 0; hero < 2; hero++)
      for (a = 0; a < RA_COUNT; a++)
      {
         int32_t ux, uz, wanted = argc == 3;
         for (i = 3; i < argc; i++)
            wanted |= !strcmp(argv[i], names[a]);
         if (!wanted || a == RA_CAT)
            continue;
         hd_reset(&st);
         hd_remote_new_day(&st);
         g->screen = RSC_DAY;
         g->hero = hero;
         g->ev_next = g->ev_count; /* a quiet day */
         g->chatter_at = 1 << 20;
         if (a == RA_DOOR)
            g->bell_t = 600;
         if (a == RA_PHONES)
            g->drill_t = 600;
         remote_place(REMOTE_PLACE_ACTIVITY, a, &ux, &uz);
         g->x = ROOM3D_AT(ux), g->z = ROOM3D_AT(uz);
         memset(in, 0, sizeof in);
         hd_step(&st, in);
         in[0].buttons = PAD_JUMP; /* A: start it */
         hd_step(&st, in);
         memset(in, 0, sizeof in);
         for (i = 0; i < 90; i++)
            hd_step(&st, in);
         if (g->act != a + 1)
         {
            printf("%s: %s did not start (act %d)\n", hero ? "woman" : "man", names[a], (int)g->act);
            continue;
         }
         if (getenv("POSE_X"))
            g->x = ROOM3D_AT(atoi(getenv("POSE_X"))), g->z = ROOM3D_AT(atoi(getenv("POSE_Z")));
         for (side = 0; side < 4; side++)
         {
            g->cam_yaw = (g->yaw + side * 1024 + 300) & (ANGLE_FULL - 1);
            g->cam_pitch = getenv("POSE_PITCH") ? atoi(getenv("POSE_PITCH")) : 420;
            g->cam_lift = 0;
            g->cam_dist = FX_FRAC(getenv("POSE_DIST") ? atoi(getenv("POSE_DIST")) : 230, 100);
            hd_draw(&st, fb);
            snprintf(path, sizeof path, "%s/%s-%s-%d.png", argv[2], hero ? "woman" : "man", names[a], (int)side);
            if (!write_png(path, fb, 1280, 720))
            {
               fprintf(stderr, "cannot write %s\n", path);
               return 1;
            }
         }
         {
            /* the joints, in centimeters: x and z of the plan (from the worker's feet), y over the floor */
            static const char *const jn[ACTOR3D_JOINTS] = { "hips", "knee.R", "knee.L", "foot.R", "foot.L", "head" };
            r3d_vec probe = { g->x, FX_FRAC(75, 100), g->z };
            int32_t top = r3d_surface_below(probe);
            printf("%s %s yaw %d, under the hips %d cm\n", hero ? "woman" : "man", names[a], (int)g->yaw,
                   top == R3D_NOTHING ? -1 : (int)((int64_t)top * 100 >> 16));
            for (i = 0; i < ACTOR3D_JOINTS; i++)
               printf("   %-7s x %5d y %4d z %5d\n", jn[i], (int)((int64_t)(actor3d_joints[i].x - g->x) * 100 >> 16),
                      (int)((int64_t)actor3d_joints[i].y * 100 >> 16), (int)((int64_t)(actor3d_joints[i].z - g->z) * 100 >> 16));
         }
      }
   return 0;
}
