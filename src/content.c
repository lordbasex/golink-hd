/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The loaded game: the built-in demo, or a game package (.glhd).
 *
 * A package (format 1) is a zip with:
 *   manifest.json  {"format": 1, "title": "...", "version": "...", "genre": "platformer",
 *                   "players": 4, "level": "level.json", "sky": ["#3a6ad0", "#bfe6fa"],
 *                   "pictures": {"hero": ..., "enemy": ..., "tiles": ..., "coin": ...,
 *                                "checkpoint": ..., "goal": ...}}
 *   level.json     {"width": W, "height": H, "start": [col, row], "rows": ["....", ...]}
 *   the pictures   PNG files of fixed sizes (see the table below)
 * Level cells: '.' empty, '#' ground, 'B' brick, '=' one-way platform, 'o' coin,
 * 'C' checkpoint, 'F' goal, 'E' an enemy's start; start is the cell the
 * players stand in. Unknown manifest keys are ignored, so newer packages
 * with extra data still load when their format is one this engine reads.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hd.h"
#include "pack.h"

char hd_title[64];
uint32_t hd_sky_top, hd_sky_bottom;
uint8_t hd_content_id[32];
int32_t hd_content_gen;

void hd_content_builtin(void)
{
   hd_art_build();
   hd_level_build();
   strcpy(hd_title, "GO-LINK HD DEMO");
   hd_sky_top = 0x3a6ad0u;
   hd_sky_bottom = 0xbfe6fau;
   memset(hd_content_id, 0, sizeof hd_content_id);
   hd_content_gen++;
}

/* A picture of the package cut into frames: cols x rows frames of fw x fh. */
typedef struct
{
   const char *key;
   int32_t fw, fh, cols, rows;
} sheet;

static const sheet sheets[] = {
   { "hero", 16, 24, HERO_FRAMES, MAX_PLAYERS }, /* a row per player: idle, walk, walk, jump */
   { "enemy", 16, 16, ENEMY_FRAMES, 1 },         /* walk, walk, squashed */
   { "tiles", 16, 16, TL_COUNT, 1 },             /* ground top, ground, brick, one-way platform */
   { "coin", 16, 16, COIN_FRAMES, 1 },           /* the spin */
   { "checkpoint", 16, 32, 2, 1 },               /* not reached, reached */
   { "goal", 32, 64, 1, 1 },
};

static hd_image *frame_of(int32_t s, int32_t col, int32_t row)
{
   switch (s)
   {
   case 0: return &hd_hero[row][col];
   case 1: return &hd_enemy_img[col];
   case 2: return &hd_tiles[col];
   case 3: return &hd_coin[col];
   case 4: return &hd_check[col];
   default: return &hd_flag;
   }
}

static int parse_color(const json *j, uint32_t *out)
{
   unsigned v;
   if (!j || j->type != JSON_STRING || strlen(j->str) != 7 || j->str[0] != '#' || sscanf(j->str + 1, "%6x", &v) != 1)
      return 0;
   *out = v;
   return 1;
}

static uint8_t cell_of(char c)
{
   switch (c)
   {
   case '#': return T_GROUND;
   case 'B': return T_BRICK;
   case '=': return T_PLATFORM;
   case 'o': return T_COIN;
   case 'C': return T_CHECK;
   case 'F': return T_FLAG;
   case 'E': return T_ENEMY;
   case '.': return T_EMPTY;
   default: return 255;
   }
}

static const char *load_level(const json *lv)
{
   const json *w = hd_json_get(lv, "width"), *h = hd_json_get(lv, "height");
   const json *start = hd_json_get(lv, "start"), *rows = hd_json_get(lv, "rows");
   const json *row;
   int32_t x, y;
   if (!w || w->type != JSON_INT || !h || h->type != JSON_INT || w->num < MAP_MIN_W || w->num > MAP_MAX_W || h->num < MAP_MIN_H || h->num > MAP_MAX_H)
      return "the level's width must be 40 to 1024 cells and its height 23 to 64";
   if (!rows || rows->type != JSON_ARRAY || rows->count != h->num)
      return "the level must have one row of text per cell of its height";
   if (!start || start->type != JSON_ARRAY || start->count != 2 || hd_json_at(start, 0)->type != JSON_INT || hd_json_at(start, 1)->type != JSON_INT)
      return "the level's start must be [column, row]";
   memset(hd_map, 0, sizeof hd_map);
   hd_map_w = (int32_t)w->num;
   hd_map_h = (int32_t)h->num;
   hd_enemy_count = 0;
   for (y = 0, row = rows->child; row; y++, row = row->next)
   {
      if (row->type != JSON_STRING || (int64_t)strlen(row->str) != w->num)
         return "every row of the level must be a text as long as its width";
      for (x = 0; x < hd_map_w; x++)
      {
         uint8_t t = cell_of(row->str[x]);
         if (t == 255)
            return "the level has a cell that is not one of . # B = o C F E";
         if (t == T_ENEMY)
         {
            if (hd_enemy_count >= MAX_ENEMIES)
               return "the level has more than 48 enemies";
            hd_enemy_start[hd_enemy_count][0] = x * TILE + (TILE - EW) / 2;
            hd_enemy_start[hd_enemy_count][1] = (y + 1) * TILE - EH;
            hd_enemy_count++;
            t = T_EMPTY;
         }
         hd_map[y][x] = t;
      }
   }
   x = (int32_t)hd_json_at(start, 0)->num;
   y = (int32_t)hd_json_at(start, 1)->num;
   if (x < 0 || x >= hd_map_w || y < 0 || y >= hd_map_h)
      return "the level's start is outside the level";
   hd_start_x = x * TILE;
   hd_start_y = (y + 1) * TILE - PH;
   return NULL;
}

/* Copies a picture's frames into the art's own buffers. */
static const char *load_sheet(const hd_zip *zip, const json *pictures, int32_t s)
{
   static char msg[160];
   const sheet *sh = &sheets[s];
   const json *name = hd_json_get(pictures, sh->key);
   const char *err;
   uint8_t *png;
   size_t size;
   uint32_t *px;
   int32_t w, h, c, r, y;
   if (!name || name->type != JSON_STRING)
   {
      snprintf(msg, sizeof msg, "the manifest names no \"%s\" picture", sh->key);
      return msg;
   }
   png = hd_zip_read(zip, name->str, &size, &err);
   if (!png)
   {
      snprintf(msg, sizeof msg, "%s: %s", name->str, err);
      return msg;
   }
   px = hd_png_read(png, size, &w, &h, &err);
   free(png);
   if (!px)
   {
      snprintf(msg, sizeof msg, "%s: %s", name->str, err);
      return msg;
   }
   if (w != sh->fw * sh->cols || h != sh->fh * sh->rows)
   {
      free(px);
      snprintf(msg, sizeof msg, "%s must be %d x %d pixels (%d x %d frames of %d x %d)", name->str,
               (int)(sh->fw * sh->cols), (int)(sh->fh * sh->rows), (int)sh->cols, (int)sh->rows, (int)sh->fw, (int)sh->fh);
      return msg;
   }
   for (r = 0; r < sh->rows; r++)
      for (c = 0; c < sh->cols; c++)
      {
         hd_image *im = frame_of(s, c, r);
         for (y = 0; y < sh->fh; y++)
            memcpy(im->px + y * sh->fw, px + (r * sh->fh + y) * w + c * sh->fw, (size_t)sh->fw * 4);
      }
   free(px);
   return NULL;
}

static const char *load_package(const uint8_t *data, size_t size)
{
   static char msg[160];
   hd_zip zip;
   const char *err;
   uint8_t *text;
   size_t len;
   json *man, *lv = NULL;
   const json *format, *title, *level, *pictures, *sky;
   int32_t s;

   zip.data = data;
   zip.size = size;
   text = hd_zip_read(&zip, "manifest.json", &len, &err);
   if (!text)
   {
      snprintf(msg, sizeof msg, "manifest.json: %s", err);
      return msg;
   }
   man = hd_json_parse((const char *)text, len, &err);
   free(text);
   if (!man)
   {
      snprintf(msg, sizeof msg, "manifest.json: %s", err);
      return msg;
   }
   format = hd_json_get(man, "format");
   title = hd_json_get(man, "title");
   level = hd_json_get(man, "level");
   pictures = hd_json_get(man, "pictures");
   sky = hd_json_get(man, "sky");
   if (!format || format->type != JSON_INT)
      err = "manifest.json has no format number";
   else if (format->num > HD_PACKAGE_FORMAT)
      err = "this game was made for a newer go-link HD: update the core";
   else if (format->num < 1)
      err = "manifest.json has an unknown format";
   else if (!title || title->type != JSON_STRING || !title->str[0])
      err = "manifest.json has no title";
   else if (!level || level->type != JSON_STRING)
      err = "manifest.json names no level";
   else if (!pictures || pictures->type != JSON_OBJECT)
      err = "manifest.json has no pictures";
   else if (sky && (sky->type != JSON_ARRAY || sky->count != 2 || !parse_color(hd_json_at(sky, 0), &hd_sky_top) || !parse_color(hd_json_at(sky, 1), &hd_sky_bottom)))
      err = "manifest.json's sky must be two colors like \"#3a6ad0\"";
   else
      err = NULL;
   if (err)
   {
      hd_json_free(man);
      return err;
   }
   snprintf(hd_title, sizeof hd_title, "%s", title->str);

   text = hd_zip_read(&zip, level->str, &len, &err);
   if (text)
   {
      lv = hd_json_parse((const char *)text, len, &err);
      free(text);
   }
   if (!lv)
   {
      snprintf(msg, sizeof msg, "%s: %s", level->str, err);
      hd_json_free(man);
      return msg;
   }
   err = load_level(lv);
   hd_json_free(lv);
   for (s = 0; !err && s < (int32_t)(sizeof sheets / sizeof sheets[0]); s++)
      err = load_sheet(&zip, pictures, s);
   hd_json_free(man);
   return err;
}

int hd_content_load(const uint8_t *data, size_t size, const char **err)
{
   hd_content_builtin(); /* defaults for what a package leaves out */
   *err = load_package(data, size);
   if (*err)
   {
      static char msg[200];
      snprintf(msg, sizeof msg, "%s", *err);
      *err = msg;
      hd_content_builtin();
      return 0;
   }
   hd_sha256(data, size, hd_content_id);
   hd_content_gen++;
   return 1;
}
