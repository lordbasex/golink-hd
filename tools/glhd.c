/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * glhd: game package tools.
 *
 *   tools/glhd export-demo DIR    writes the built-in demo as a package folder
 *                                 (manifest.json, level.json, PNG pictures)
 *   tools/glhd pack DIR OUT.glhd  zips a package folder (stored, no compression): the
 *                                 manifest, its level and the pictures it names
 *   tools/glhd check FILE.glhd    loads a package like the core does and prints
 *                                 its title and SHA-256, or why it cannot play
 *
 * The demo exported and packed plays exactly like the built-in demo (same
 * frames, same sound): tests/test.c checks it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hd.h"
#include "pack.h"

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MAKE_DIR(p) mkdir(p, 0755)
#endif

/* PNG with stored deflate blocks (RGBA, 8 bit). */
static void be32(uint8_t *p, uint32_t v)
{
   p[0] = (uint8_t)(v >> 24);
   p[1] = (uint8_t)(v >> 16);
   p[2] = (uint8_t)(v >> 8);
   p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
   uint8_t head[8], tail[4], *buf = (uint8_t *)malloc(n + 4);
   be32(head, n);
   memcpy(head + 4, type, 4);
   fwrite(head, 1, 8, f);
   if (n)
      fwrite(data, 1, n, f);
   memcpy(buf, type, 4);
   if (n)
      memcpy(buf + 4, data, n);
   be32(tail, hd_crc32(buf, n + 4));
   fwrite(tail, 1, 4, f);
   free(buf);
}

static int write_png(const char *path, const uint32_t *px, int32_t w, int32_t h)
{
   uint32_t raw_len = (uint32_t)(w * 4 + 1) * (uint32_t)h, i, at = 0;
   uint32_t blocks = (raw_len + 65534) / 65535;
   uint8_t *raw = (uint8_t *)malloc(raw_len), *z = (uint8_t *)malloc(2 + blocks * 5 + raw_len + 4), hdr[13];
   size_t zi = 0;
   int32_t x, y;
   FILE *f = fopen(path, "wb");
   if (!f || !raw || !z)
      return 0;
   for (y = 0; y < h; y++)
   {
      raw[at++] = 0;
      for (x = 0; x < w; x++)
      {
         uint32_t c = px[y * w + x];
         raw[at++] = (uint8_t)(c >> 16);
         raw[at++] = (uint8_t)(c >> 8);
         raw[at++] = (uint8_t)c;
         raw[at++] = (uint8_t)(c >> 24);
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
   be32(z + zi, hd_adler32(raw, raw_len));
   zi += 4;
   fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
   be32(hdr, (uint32_t)w);
   be32(hdr + 4, (uint32_t)h);
   hdr[8] = 8;
   hdr[9] = 6;
   hdr[10] = hdr[11] = hdr[12] = 0;
   chunk(f, "IHDR", hdr, 13);
   chunk(f, "IDAT", z, (uint32_t)zi);
   chunk(f, "IEND", NULL, 0);
   fclose(f);
   free(raw);
   free(z);
   return 1;
}

/* Frames side by side (cols) and rows into one picture. */
static int sheet(const char *dir, const char *name, hd_image *const *frames, int32_t cols, int32_t rows)
{
   char path[1024];
   int32_t fw = frames[0]->w, fh = frames[0]->h, r, c, y, ok;
   uint32_t *px = (uint32_t *)calloc((size_t)(fw * cols * fh * rows), 4);
   for (r = 0; r < rows; r++)
      for (c = 0; c < cols; c++)
         for (y = 0; y < fh; y++)
            memcpy(px + (r * fh + y) * fw * cols + c * fw, frames[r * cols + c]->px + y * fw, (size_t)fw * 4);
   snprintf(path, sizeof path, "%s/%s", dir, name);
   ok = write_png(path, px, fw * cols, fh * rows);
   free(px);
   return ok;
}

static int export_demo(const char *dir)
{
   static const char cells[] = ".#B=oCF"; /* by T_ value */
   hd_image *frames[MAX_PLAYERS * HERO_FRAMES];
   char path[1024];
   FILE *f;
   int32_t x, y, i, r;
   hd_static_init();
   MAKE_DIR(dir); /* it may exist already */
   for (r = 0; r < MAX_PLAYERS; r++)
      for (i = 0; i < HERO_FRAMES; i++)
         frames[r * HERO_FRAMES + i] = &hd_hero[r][i];
   if (!sheet(dir, "hero.png", frames, HERO_FRAMES, MAX_PLAYERS))
      return 0;
   for (i = 0; i < ENEMY_FRAMES; i++)
      frames[i] = &hd_enemy_img[i];
   sheet(dir, "enemy.png", frames, ENEMY_FRAMES, 1);
   for (i = 0; i < TL_COUNT; i++)
      frames[i] = &hd_tiles[i];
   sheet(dir, "tiles.png", frames, TL_COUNT, 1);
   for (i = 0; i < COIN_FRAMES; i++)
      frames[i] = &hd_coin[i];
   sheet(dir, "coin.png", frames, COIN_FRAMES, 1);
   frames[0] = &hd_check[0];
   frames[1] = &hd_check[1];
   sheet(dir, "checkpoint.png", frames, 2, 1);
   frames[0] = &hd_flag;
   sheet(dir, "goal.png", frames, 1, 1);

   snprintf(path, sizeof path, "%s/level.json", dir);
   f = fopen(path, "w");
   if (!f)
      return 0;
   fprintf(f, "{\n  \"width\": %d,\n  \"height\": %d,\n  \"start\": [%d, %d],\n  \"rows\": [\n", (int)MAP_W, (int)MAP_H,
           (int)(hd_start_x / TILE), (int)((hd_start_y + PH) / TILE - 1));
   for (y = 0; y < MAP_H; y++)
   {
      fputs("    \"", f);
      for (x = 0; x < MAP_W; x++)
      {
         char c = cells[hd_map[y][x]];
         for (i = 0; i < hd_enemy_count; i++)
            if (hd_enemy_start[i][0] / TILE == x && (hd_enemy_start[i][1] + EH) / TILE - 1 == y)
               c = 'E';
         fputc(c, f);
      }
      fputs(y + 1 < MAP_H ? "\",\n" : "\"\n", f);
   }
   fputs("  ]\n}\n", f);
   fclose(f);

   snprintf(path, sizeof path, "%s/manifest.json", dir);
   f = fopen(path, "w");
   if (!f)
      return 0;
   fprintf(f, "{\n"
              "  \"format\": %d,\n"
              "  \"title\": \"go-link HD demo\",\n"
              "  \"version\": \"1.0.0\",\n"
              "  \"genre\": \"platformer\",\n"
              "  \"players\": 4,\n"
              "  \"level\": \"level.json\",\n"
              "  \"sky\": [\"#%06x\", \"#%06x\"],\n"
              "  \"pictures\": {\n"
              "    \"hero\": \"hero.png\",\n"
              "    \"enemy\": \"enemy.png\",\n"
              "    \"tiles\": \"tiles.png\",\n"
              "    \"coin\": \"coin.png\",\n"
              "    \"checkpoint\": \"checkpoint.png\",\n"
              "    \"goal\": \"goal.png\"\n"
              "  }\n"
              "}\n",
           HD_PACKAGE_FORMAT, (unsigned)hd_sky_top, (unsigned)hd_sky_bottom);
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

static void le16(FILE *f, uint32_t v)
{
   fputc((int)(v & 0xff), f);
   fputc((int)((v >> 8) & 0xff), f);
}

static void le32(FILE *f, uint32_t v)
{
   le16(f, v & 0xffff);
   le16(f, v >> 16);
}

/* The files a package folder's manifest names, in a fixed order: the
 * manifest, the level, then its pictures in the format's order. Names stay
 * inside the folder (no absolute paths, no ".."). */
#define PACK_MAX 16
static int pack_names(const char *dir, char names[PACK_MAX][256], int *count)
{
   static const char *const pictures[] = { "hero", "enemy", "tiles", "coin", "checkpoint", "goal", "portrait", "lut" };
   char path[1024];
   size_t n;
   uint8_t *text;
   const char *err = NULL;
   json *man;
   const json *j, *pics;
   unsigned k;
   int i, ok = 1;
   snprintf(path, sizeof path, "%s/manifest.json", dir);
   text = slurp(path, &n);
   if (!text)
   {
      fprintf(stderr, "cannot read %s\n", path);
      return 0;
   }
   man = hd_json_parse((const char *)text, n, &err);
   free(text);
   if (!man)
   {
      fprintf(stderr, "%s: %s\n", path, err);
      return 0;
   }
   strcpy(names[0], "manifest.json");
   *count = 1;
   j = hd_json_get(man, "level");
   if (!j || j->type != JSON_STRING)
   {
      fprintf(stderr, "%s names no level\n", path);
      ok = 0;
   }
   else
      snprintf(names[(*count)++], 256, "%s", j->str);
   pics = hd_json_get(man, "pictures");
   for (k = 0; ok && pics && k < sizeof pictures / sizeof pictures[0]; k++)
   {
      j = hd_json_get(pics, pictures[k]);
      if (!j)
         continue;
      if (j->type != JSON_STRING)
      {
         fprintf(stderr, "%s: pictures.%s must be a file name\n", path, pictures[k]);
         ok = 0;
         break;
      }
      for (i = 0; i < *count && strcmp(names[i], j->str); i++)
         ;
      if (i == *count)
         snprintf(names[(*count)++], 256, "%s", j->str);
   }
   for (i = 0; ok && i < *count; i++)
      if (!names[i][0] || names[i][0] == '/' || names[i][0] == '\\' || strstr(names[i], "..") || strchr(names[i], ':'))
      {
         fprintf(stderr, "%s: \"%s\" is not a file inside the package\n", path, names[i]);
         ok = 0;
      }
   hd_json_free(man);
   return ok;
}

/* A zip of the files a package folder's manifest names, stored, in a fixed
 * order (same bytes every time). */
static int pack(const char *dir, const char *out)
{
   char names[PACK_MAX][256];
   int N = 0;
   uint32_t offs[PACK_MAX], crcs[PACK_MAX], sizes[PACK_MAX], at = 0, cd_at, cd_size;
   FILE *f;
   int i;
   if (!pack_names(dir, names, &N))
      return 0;
   f = fopen(out, "wb");
   if (!f)
      return 0;
   for (i = 0; i < N; i++)
   {
      char path[1024];
      size_t n;
      uint8_t *data;
      snprintf(path, sizeof path, "%s/%s", dir, names[i]);
      data = slurp(path, &n);
      if (!data)
      {
         fprintf(stderr, "cannot read %s\n", path);
         fclose(f);
         return 0;
      }
      offs[i] = at;
      crcs[i] = hd_crc32(data, n);
      sizes[i] = (uint32_t)n;
      le32(f, 0x04034b50u);
      le16(f, 20);
      le16(f, 0);
      le16(f, 0);
      le16(f, 0);
      le16(f, 0x21); /* a fixed date: 1980-01-01 */
      le32(f, crcs[i]);
      le32(f, sizes[i]);
      le32(f, sizes[i]);
      le16(f, (uint32_t)strlen(names[i]));
      le16(f, 0);
      fputs(names[i], f);
      fwrite(data, 1, n, f);
      at += 30 + (uint32_t)strlen(names[i]) + (uint32_t)n;
      free(data);
   }
   cd_at = at;
   for (i = 0; i < N; i++)
   {
      le32(f, 0x02014b50u);
      le16(f, 20);
      le16(f, 20);
      le16(f, 0);
      le16(f, 0);
      le16(f, 0);
      le16(f, 0x21);
      le32(f, crcs[i]);
      le32(f, sizes[i]);
      le32(f, sizes[i]);
      le16(f, (uint32_t)strlen(names[i]));
      le16(f, 0);
      le16(f, 0);
      le16(f, 0);
      le16(f, 0);
      le32(f, 0);
      le32(f, offs[i]);
      fputs(names[i], f);
      at += 46 + (uint32_t)strlen(names[i]);
   }
   cd_size = at - cd_at;
   le32(f, 0x06054b50u);
   le16(f, 0);
   le16(f, 0);
   le16(f, N);
   le16(f, N);
   le32(f, cd_size);
   le32(f, cd_at);
   le16(f, 0);
   fclose(f);
   return 1;
}

static int check(const char *path)
{
   size_t n;
   uint8_t *data = slurp(path, &n);
   const char *err;
   int i;
   if (!data)
   {
      fprintf(stderr, "cannot read %s\n", path);
      return 0;
   }
   hd_static_init();
   if (!hd_content_load(data, n, &err))
   {
      fprintf(stderr, "%s cannot be played: %s\n", path, err);
      free(data);
      return 0;
   }
   printf("%s: \"%s\", level %dx%d, %d enemies, sha256 ", path, hd_title, (int)MAP_W, (int)MAP_H, (int)hd_enemy_count);
   for (i = 0; i < 32; i++)
      printf("%02x", hd_content_id[i]);
   printf("\n");
   free(data);
   return 1;
}

int main(int argc, char **argv)
{
   if (argc == 3 && !strcmp(argv[1], "export-demo"))
   {
      if (export_demo(argv[2]))
         return 0;
      fprintf(stderr, "cannot write the demo into %s\n", argv[2]);
      return 1;
   }
   if (argc == 4 && !strcmp(argv[1], "pack"))
   {
      if (pack(argv[2], argv[3]))
         return 0;
      fprintf(stderr, "cannot write %s\n", argv[3]);
      return 1;
   }
   if (argc == 3 && !strcmp(argv[1], "check"))
      return check(argv[2]) ? 0 : 1;
   fprintf(stderr, "usage: %s export-demo DIR | pack DIR OUT.glhd | check FILE.glhd\n", argv[0]);
   return 2;
}
