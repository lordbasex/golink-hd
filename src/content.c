/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The loaded game: the built-in demo, or a game package (.glhd).
 *
 * A package (format 1) is a zip with:
 *   manifest.json  {"format": 1, "title": "...", "version": "...", "genre": "platformer",
 *                   "players": 1-8 (4), "screen": "16:9" | "4:3" | "9:16" ("16:9"),
 *                   "level": "level.json", "sky": ["#3a6ad0", "#bfe6fa"],
 *                   "pictures": {"hero": ..., "enemy": ..., "tiles": ..., "coin": ...,
 *                                "checkpoint": ..., "goal": ...}}
 *   level.json     {"width": W, "height": H, "start": [col, row], "rows": ["....", ...]}
 *   the pictures   PNG files of fixed sizes (see the table below); each one is
 *                  optional, a missing one keeps the built-in demo's
 * Level cells: '.' empty, '#' ground, 'B' brick, '=' one-way platform, 'o' coin,
 * 'C' checkpoint, 'F' goal, 'E' an enemy's start; start is the cell the
 * players stand in. Unknown manifest keys are ignored, so newer packages
 * with extra data still load when their format is one this engine reads.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "hd.h"
#include "pack.h"
#include "gfx.h"
#include "sprite.h"

char hd_title[64];
hd_fx_config hd_fx;
int32_t hd_lang;
hd_image hd_portrait;
static uint32_t portrait_px[64 * 64];
int32_t hd_w = 640, hd_h = 360;
int32_t hd_res = 1;
int32_t hd_players = DEFAULT_PLAYERS;
uint32_t hd_sky_top, hd_sky_bottom;
uint8_t hd_content_id[32];
int32_t hd_content_gen;

void hd_content_builtin(void)
{
   hd_art_build();
   hd_stages_free(); /* first: the globals may point into its levels */
   hd_sprites_free();
   hd_sounds_free(); /* the built-in effects and tune */
   hd_physics_default(); /* before the level: its start stands on the hitbox's height */
   hd_weapon_default();
   hd_level_build();
   strcpy(hd_title, "GO-LINK HD DEMO");
   hd_w = 640;
   hd_h = 360;
   hd_res = 1;
   hd_players = DEFAULT_PLAYERS;
   hd_sky_top = 0x3a6ad0u;
   hd_sky_bottom = 0xbfe6fau;
   memset(&hd_fx, 0, sizeof hd_fx);
   memset(&hd_portrait, 0, sizeof hd_portrait);
   fx_grade_build(GRADE_NONE, NULL);
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
   { "hero", 16, 24, HERO_FRAMES, MAX_PLAYERS }, /* a row per player (1 to 8 rows): idle, walk, walk, jump */
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
   {
      static char msg[120];
      snprintf(msg, sizeof msg, "the level's width must be %d to %d cells and its height %d to %d (for this screen)", (int)MAP_MIN_W, MAP_MAX_W, (int)MAP_MIN_H, MAP_MAX_H);
      return msg;
   }
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
   if (!name)
      return NULL; /* not in the package: the built-in picture stays */
   if (name->type != JSON_STRING)
   {
      snprintf(msg, sizeof msg, "the manifest's \"%s\" picture must be a file name", sh->key);
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
   /* the hero's sheet may have 1 to 8 rows: a player without a row of its own wears row (player mod rows) */
   if (s == 0 && w == sh->fw * sh->cols && h % sh->fh == 0 && h / sh->fh >= 1 && h / sh->fh <= MAX_PLAYERS)
   {
      int32_t rows = h / sh->fh;
      for (r = 0; r < MAX_PLAYERS; r++)
         for (c = 0; c < sh->cols; c++)
         {
            hd_image *im = frame_of(s, c, r);
            for (y = 0; y < sh->fh; y++)
               memcpy(im->px + y * sh->fw, px + ((r % rows) * sh->fh + y) * w + c * sh->fw, (size_t)sh->fw * 4);
         }
      free(px);
      return NULL;
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

static int32_t num(const json *obj, const char *key, int32_t lo, int32_t hi, int32_t fallback, int *bad)
{
   const json *j = hd_json_get(obj, key);
   if (!j)
      return fallback;
   if (j->type != JSON_INT || j->num < lo || j->num > hi)
   {
      *bad = 1;
      return fallback;
   }
   return (int32_t)j->num;
}

/*
 * The manifest's "physics" (format 3): the players' hitbox in pixels and
 * their movement in hundredths of a pixel per frame (per frame squared for
 * accelerations), every key optional:
 *   {"hitbox": [w, h], "enemy_hitbox": [w, h], "walk": 250, "run": 400, "accel": 30, "air_accel": 18,
 *    "friction": 25, "air_friction": 5, "gravity": 45, "gravity_hold": 28,
 *    "fall_max": 700, "jump": 640, "jump_cut": 200, "bounce": 450, "bounce_held": 700}
 */
static const char *load_physics(const json *ph)
{
   static const struct { const char *key; int32_t lo, hi; size_t at; int neg; } speeds[] = {
      { "walk", 10, 2000, offsetof(hd_physics, walk_max), 0 },
      { "run", 10, 2000, offsetof(hd_physics, run_max), 0 },
      { "accel", 1, 500, offsetof(hd_physics, accel_ground), 0 },
      { "air_accel", 1, 500, offsetof(hd_physics, accel_air), 0 },
      { "friction", 1, 500, offsetof(hd_physics, friction_ground), 0 },
      { "air_friction", 0, 500, offsetof(hd_physics, friction_air), 0 },
      { "gravity", 1, 500, offsetof(hd_physics, gravity), 0 },
      { "gravity_hold", 1, 500, offsetof(hd_physics, gravity_hold), 0 },
      { "fall_max", 50, 3000, offsetof(hd_physics, fall_max), 0 },
      { "jump", 50, 3000, offsetof(hd_physics, jump_speed), 0 },
      { "jump_cut", 0, 3000, offsetof(hd_physics, jump_cut), 1 },
      { "bounce", 50, 3000, offsetof(hd_physics, bounce), 1 },
      { "bounce_held", 50, 3000, offsetof(hd_physics, bounce_held), 1 },
   };
   const json *hb;
   size_t i;
   int bad = 0;
   if (!ph)
      return NULL;
   if (ph->type != JSON_OBJECT)
      return "manifest.json's physics must be an object";
   if ((hb = hd_json_get(ph, "hitbox")))
   {
      if (hb->type != JSON_ARRAY || hb->count != 2 || hd_json_at(hb, 0)->type != JSON_INT || hd_json_at(hb, 1)->type != JSON_INT ||
          hd_json_at(hb, 0)->num < 4 || hd_json_at(hb, 0)->num > 128 || hd_json_at(hb, 1)->num < 4 || hd_json_at(hb, 1)->num > 192)
         return "the physics' hitbox must be [width, height]: 4 to 128 and 4 to 192 pixels";
      hd_phys.pw = (int32_t)hd_json_at(hb, 0)->num;
      hd_phys.ph = (int32_t)hd_json_at(hb, 1)->num;
   }
   if ((hb = hd_json_get(ph, "enemy_hitbox")))
   {
      if (hb->type != JSON_ARRAY || hb->count != 2 || hd_json_at(hb, 0)->type != JSON_INT || hd_json_at(hb, 1)->type != JSON_INT ||
          hd_json_at(hb, 0)->num < 4 || hd_json_at(hb, 0)->num > 128 || hd_json_at(hb, 1)->num < 4 || hd_json_at(hb, 1)->num > 128)
         return "the physics' enemy_hitbox must be [width, height]: 4 to 128 pixels each";
      hd_phys.ew = (int32_t)hd_json_at(hb, 0)->num;
      hd_phys.eh = (int32_t)hd_json_at(hb, 1)->num;
   }
   for (i = 0; i < sizeof speeds / sizeof speeds[0]; i++)
   {
      if (!hd_json_get(ph, speeds[i].key))
         continue;
      {
         int32_t v = FX_FRAC(num(ph, speeds[i].key, speeds[i].lo, speeds[i].hi, 0, &bad), 100);
         *(int32_t *)((char *)&hd_phys + speeds[i].at) = speeds[i].neg ? -v : v;
      }
   }
   if (bad)
      return "the physics have a value out of range or that is not a whole number";
   return NULL;
}

/*
 * The manifest's "weapon" (format 3): a button that fires shots straight
 * ahead, every key optional but the object itself:
 *   {"button": "run", "rate": 10, "speed": 700, "range": 400, "muzzle": [12, -14], "enemy_health": 1}
 * button is "run" (the second action, which then no longer runs), "a",
 * "b", "x", "y", "l" or "r"; rate the frames between shots (2 to 120);
 * speed in hundredths of a pixel a frame (100 to 4000); range in pixels
 * (16 to 4000); muzzle where shots start, in pixels in front of the
 * hitbox's middle and from its feet (up is negative); enemy_health the
 * hits an enemy takes (1 to 100). With a weapon the run button no longer
 * runs.
 *
 * Its optional "super" is the super attack: charged by `charge` hits of the
 * player's shots, its button throws `granules` in a fan `spread` degrees
 * wide that fall in arcs, each costing an enemy `damage` hits; the player
 * stands still and cannot be hurt for `frames`, the granules leaving
 * `release` frames into it:
 *   {"button": "y", "charge": 8, "granules": 10, "spread": 70, "speed": 650,
 *    "range": 260, "damage": 2, "frames": 48, "release": 22}
 */
static const struct { const char *name; uint32_t bit; } weapon_buttons[] = {
   { "run", PAD_RUN }, { "a", PAD_A }, { "b", PAD_B }, { "x", PAD_X }, { "y", PAD_Y }, { "l", PAD_L }, { "r", PAD_R },
};

/* A weapon's button by name, 0 when it is not one. */
static uint32_t weapon_button(const json *b)
{
   size_t i;
   for (i = 0; b->type == JSON_STRING && i < sizeof weapon_buttons / sizeof weapon_buttons[0]; i++)
      if (!strcmp(b->str, weapon_buttons[i].name))
         return weapon_buttons[i].bit;
   return 0;
}

static const char *load_super(const json *sp)
{
   const json *b;
   int bad = 0;
   int32_t range;
   if (sp->type != JSON_OBJECT)
      return "the weapon's super must be an object";
   hd_weapon.super_on = 1;
   hd_weapon.super_button = PAD_R;
   if ((b = hd_json_get(sp, "button")) && !(hd_weapon.super_button = weapon_button(b)))
      return "the super's button must be \"run\", \"a\", \"b\", \"x\", \"y\", \"l\" or \"r\"";
   hd_weapon.super_charge = num(sp, "charge", 1, 200, 8, &bad);
   hd_weapon.super_count = num(sp, "granules", 1, 24, 10, &bad);
   hd_weapon.super_spread = num(sp, "spread", 0, 180, 70, &bad) * ANGLE_FULL / 360;
   hd_weapon.super_speed = FX_FRAC(num(sp, "speed", 100, 4000, 650, &bad), 100);
   range = num(sp, "range", 16, 4000, 260, &bad);
   hd_weapon.super_damage = num(sp, "damage", 1, 100, 2, &bad);
   hd_weapon.super_frames = num(sp, "frames", 10, 240, 48, &bad);
   hd_weapon.super_release = num(sp, "release", 1, 240, 22, &bad);
   if (bad || hd_weapon.super_release >= hd_weapon.super_frames)
      return "the super has a value out of range (charge 1-200, granules 1-24, spread 0-180, speed 100-4000, range 16-4000, damage 1-100, frames 10-240, release before frames)";
   hd_weapon.super_life = hd_max(1, (int32_t)((int64_t)FX(range) / hd_weapon.super_speed));
   return NULL;
}

static const char *load_weapon(const json *w)
{
   const json *b, *m;
   int bad = 0;
   int32_t range;
   if (!w)
      return NULL;
   if (w->type != JSON_OBJECT)
      return "manifest.json's weapon must be an object";
   hd_weapon.on = 1;
   hd_weapon.button = PAD_RUN;
   if ((b = hd_json_get(w, "button")))
   {
      hd_weapon.button = weapon_button(b);
      if (!hd_weapon.button)
         return "the weapon's button must be \"run\", \"a\", \"b\", \"x\", \"y\", \"l\" or \"r\"";
   }
   hd_weapon.rate = num(w, "rate", 2, 120, 10, &bad);
   hd_weapon.speed = FX_FRAC(num(w, "speed", 100, 4000, 700, &bad), 100);
   range = num(w, "range", 16, 4000, 400, &bad);
   hd_weapon.enemy_health = num(w, "enemy_health", 1, 100, 1, &bad);
   if (bad)
      return "the weapon has a value out of range or that is not a whole number";
   /* frames to fly the range: range / (speed / 100) */
   hd_weapon.life = hd_max(1, (int32_t)((int64_t)FX(range) / hd_weapon.speed));
   hd_weapon.muzzle_x = PW / 2 + 4;
   hd_weapon.muzzle_y = -PH / 2;
   if ((m = hd_json_get(w, "muzzle")))
   {
      if (m->type != JSON_ARRAY || m->count != 2 || hd_json_at(m, 0)->type != JSON_INT || hd_json_at(m, 1)->type != JSON_INT ||
          hd_json_at(m, 0)->num < -256 || hd_json_at(m, 0)->num > 256 || hd_json_at(m, 1)->num < -256 || hd_json_at(m, 1)->num > 256)
         return "the weapon's muzzle must be [x, y], -256 to 256 pixels each";
      hd_weapon.muzzle_x = (int32_t)hd_json_at(m, 0)->num;
      hd_weapon.muzzle_y = (int32_t)hd_json_at(m, 1)->num;
   }
   if ((m = hd_json_get(w, "super")))
      return load_super(m);
   return NULL;
}

/*
 * The manifest's "health" (format 3): {"hits": 3, "worn": 1, "knockout": 90}:
 * the hits a player takes (1 to 99), how many left look worn (0 to hits),
 * the frames a knockout lasts (10 to 600).
 */
static const char *load_health(const json *h)
{
   int bad = 0;
   if (!h)
      return NULL;
   if (h->type != JSON_OBJECT)
      return "manifest.json's health must be an object";
   hd_health.on = 1;
   hd_health.hits = num(h, "hits", 1, 99, 3, &bad);
   hd_health.worn = num(h, "worn", 0, 99, 1, &bad);
   hd_health.knockout = num(h, "knockout", 10, 600, 90, &bad);
   if (bad || hd_health.worn > hd_health.hits)
      return "the health has a value out of range (hits 1-99, worn 0 to hits, knockout 10-600)";
   return NULL;
}

/*
 * A level's "effects" (format 2): light and darkness, color grading, bloom,
 * waves, the camera's zoom, outlines and shadows, the sound, and dialogs.
 */
static const char *load_effects(const json *fx)
{
   static char msg[160];
   static const char *const grades[] = { "none", "night", "sepia", "underwater", "sunset", "grey", "lut" };
   const json *j, *it;
   int bad = 0;
   int32_t i, k;
   if (!fx)
      return NULL;
   if (fx->type != JSON_OBJECT)
      return "the level's effects must be an object";
   hd_fx.darkness = num(fx, "darkness", 0, 256, 0, &bad);
   hd_fx.player_light = num(fx, "player_light", 0, 400, 0, &bad);
   hd_fx.player_light_color = 0xffe0b0u;
   if ((j = hd_json_get(fx, "player_light_color")) && !parse_color(j, &hd_fx.player_light_color))
      bad = 1;
   if ((j = hd_json_get(fx, "lights")))
   {
      if (j->type != JSON_ARRAY || j->count > FX_LIGHTS_MAX)
         return "the level's lights must be a list of at most 32";
      for (it = j->child, i = 0; it; it = it->next, i++)
      {
         hd_fx.light_x[i] = num(it, "x", 0, MAP_MAX_W, 0, &bad) * TILE + TILE / 2;
         hd_fx.light_y[i] = num(it, "y", 0, MAP_MAX_H, 0, &bad) * TILE + TILE / 2;
         hd_fx.light_r[i] = num(it, "radius", 8, 400, 80, &bad);
         hd_fx.light_flicker[i] = num(it, "flicker", 0, 64, 0, &bad);
         hd_fx.light_color[i] = 0xffc070u;
         if (hd_json_get(it, "color") && !parse_color(hd_json_get(it, "color"), &hd_fx.light_color[i]))
            bad = 1;
      }
      hd_fx.lights = i;
   }
   if ((j = hd_json_get(fx, "grade")))
   {
      for (k = 0; k < (int32_t)(sizeof grades / sizeof grades[0]); k++)
         if (j->type == JSON_STRING && !strcmp(j->str, grades[k]))
            break;
      if (k == (int32_t)(sizeof grades / sizeof grades[0]))
         return "the level's grade must be none, night, sepia, underwater, sunset, grey or lut";
      hd_fx.grade = k;
      hd_fx.grade_amount = num(fx, "grade_amount", 0, 256, 256, &bad);
   }
   hd_fx.bloom = num(fx, "bloom", 0, 256, 0, &bad);
   hd_fx.bloom_threshold = num(fx, "bloom_threshold", 0, 254, 180, &bad);
   if ((j = hd_json_get(fx, "waves")))
   {
      hd_fx.waves_y = num(j, "row", 0, MAP_MAX_H, 0, &bad) * TILE;
      hd_fx.waves_amp = num(j, "amplitude", 0, 16, 3, &bad);
      hd_fx.waves_len = num(j, "wavelength", 8, 400, 60, &bad);
   }
   if ((j = hd_json_get(fx, "zoom")))
   {
      if (j->type != JSON_STRING || strcmp(j->str, "auto"))
         return "the level's zoom must be \"auto\"";
      hd_fx.zoom_auto = 1;
   }
   if ((j = hd_json_get(fx, "outline")))
   {
      uint32_t c;
      if (!parse_color(j, &c))
         bad = 1;
      hd_fx.outline = 0xff000000u | c;
   }
   if ((j = hd_json_get(fx, "shadows")))
      hd_fx.shadows = j->type == JSON_BOOL && j->num;
   hd_fx.lowpass = num(fx, "lowpass", 0, 256, 0, &bad);
   hd_fx.echo_ms = num(fx, "echo", 0, 300, 0, &bad);
   if ((j = hd_json_get(fx, "dialogs")))
   {
      static const char *const langs[LANGS] = { "en", "es", "pt" };
      if (j->type != JSON_ARRAY || j->count > DIALOGS_MAX)
         return "the level's dialogs must be a list of at most 16";
      for (it = j->child, i = 0; it; it = it->next, i++)
      {
         const json *name = hd_json_get(it, "name"), *text = hd_json_get(it, "text");
         hd_fx.dialog_col[i] = num(it, "column", 0, MAP_MAX_W, 0, &bad);
         if (name && name->type == JSON_STRING)
            snprintf(hd_fx.dialog_name[i], sizeof hd_fx.dialog_name[i], "%s", name->str);
         if (!text || text->type != JSON_OBJECT)
            return "each dialog needs a text: {\"en\": ..., \"es\": ..., \"pt\": ...}";
         for (k = 0; k < LANGS; k++)
         {
            /* a language left out shows the English text */
            const json *t = hd_json_get(text, langs[k]);
            if (!t)
               t = hd_json_get(text, "en");
            if (!t || t->type != JSON_STRING)
               return "each dialog needs at least its English text";
            snprintf(hd_fx.dialog_text[i][k], sizeof hd_fx.dialog_text[i][k], "%s", t->str);
         }
      }
      hd_fx.dialogs = i;
   }
   if (bad)
   {
      snprintf(msg, sizeof msg, "the level's effects have a value out of range or of the wrong kind");
      return msg;
   }
   return NULL;
}

/* A whole picture of the package, or NULL; its size in *w, *h. */
static uint32_t *picture(const hd_zip *zip, const json *pictures, const char *key, int32_t *w, int32_t *h, const char **err)
{
   static char msg[160];
   const json *name = hd_json_get(pictures, key);
   uint8_t *png;
   size_t size;
   uint32_t *px;
   *err = NULL;
   if (!name)
      return NULL;
   if (name->type != JSON_STRING)
   {
      snprintf(msg, sizeof msg, "the manifest's \"%s\" picture must be a file name", key);
      *err = msg;
      return NULL;
   }
   png = hd_zip_read(zip, name->str, &size, err);
   if (!png)
   {
      snprintf(msg, sizeof msg, "%s: %s", name->str, *err);
      *err = msg;
      return NULL;
   }
   px = hd_png_read(png, size, w, h, err);
   free(png);
   if (!px)
   {
      snprintf(msg, sizeof msg, "%s: %s", name->str, *err);
      *err = msg;
   }
   return px;
}

/* The dialogs' portrait (up to 64 x 64) and a color grading table (a 256 x 16 LUT strip). */
static const char *load_extras(const hd_zip *zip, const json *pictures)
{
   const char *err;
   int32_t w, h;
   uint32_t *px = picture(zip, pictures, "portrait", &w, &h, &err);
   if (err)
      return err;
   if (px)
   {
      if (w > 64 || h > 64)
      {
         free(px);
         return "the portrait must be at most 64 x 64 pixels";
      }
      memcpy(portrait_px, px, (size_t)(w * h) * 4);
      free(px);
      hd_portrait.w = w;
      hd_portrait.h = h;
      hd_portrait.px = portrait_px;
   }
   px = picture(zip, pictures, "lut", &w, &h, &err);
   if (err)
      return err;
   if (hd_fx.grade == GRADE_LUT && !px)
      return "the grade \"lut\" needs a \"lut\" picture";
   if (px && (w != 256 || h != 16))
   {
      free(px);
      return "the lut must be a 256 x 16 strip (16 slices of 16 x 16)";
   }
   fx_grade_build(hd_fx.grade, px);
   free(px);
   return NULL;
}

/* A level file of the package: its cells and its effects. */
static const char *load_level_file(const hd_zip *zip, const char *name)
{
   static char msg[200];
   const char *err;
   size_t len;
   json *lv = NULL;
   uint8_t *text = hd_zip_read(zip, name, &len, &err);
   if (text)
   {
      lv = hd_json_parse((const char *)text, len, &err);
      free(text);
   }
   if (!lv)
   {
      snprintf(msg, sizeof msg, "%s: %s", name, err);
      return msg;
   }
   err = load_level(lv);
   if (!err)
      err = load_effects(hd_json_get(lv, "effects"));
   hd_json_free(lv);
   return err;
}

/*
 * Format 3's "levels" (stage.c): each entry's level file, sky, layers,
 * textures, intro picture and music, loaded one after another and kept.
 */
static const char *load_stages(const hd_zip *zip, const json *levels)
{
   static char msg[200];
   uint32_t sky_top = hd_sky_top, sky_bottom = hd_sky_bottom;
   const json *it;
   int32_t k = 0;
   if (levels->type != JSON_ARRAY || levels->count < 1 || levels->count > MAX_STAGES)
      return "manifest.json's levels must be a list of 1 to 16 levels";
   free(hd_music_take()); /* each level has its own music */
   for (it = levels->child; it; it = it->next, k++)
   {
      const json *file = hd_json_get(it, "level"), *sky = hd_json_get(it, "sky"), *intro = hd_json_get(it, "intro"), *music = hd_json_get(it, "music");
      const char *err;
      if (it->type != JSON_OBJECT || !file || file->type != JSON_STRING)
         return "each of the levels needs a \"level\" file";
      memset(&hd_fx, 0, sizeof hd_fx);
      hd_sky_top = sky_top;
      hd_sky_bottom = sky_bottom;
      if (sky && (sky->type != JSON_ARRAY || sky->count != 2 || !parse_color(hd_json_at(sky, 0), &hd_sky_top) || !parse_color(hd_json_at(sky, 1), &hd_sky_bottom)))
         return "a level's sky must be two colors like \"#3a6ad0\"";
      err = load_level_file(zip, file->str);
      if (!err)
         err = hd_layers_load(zip, hd_json_get(it, "layers"));
      if (!err)
         err = hd_textures_load(zip, hd_json_get(it, "textures"));
      if (!err && intro)
         err = hd_screen_load(zip, intro, SCREEN_INTRO);
      if (!err && music)
         err = hd_music_load(zip, music);
      if (!err && !hd_stage_keep(k))
         err = "not enough memory for the levels";
      if (err)
      {
         snprintf(msg, sizeof msg, "level %d: %s", (int)k + 1, err);
         return msg;
      }
   }
   hd_stage_select(0);
   return NULL;
}

static const char *load_package(const uint8_t *data, size_t size)
{
   static char msg[160];
   hd_zip zip;
   const char *err;
   uint8_t *text;
   size_t len;
   json *man;
   const json *format, *title, *level, *pictures, *sky, *players, *screen, *levels, *res;
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
   players = hd_json_get(man, "players");
   screen = hd_json_get(man, "screen");
   levels = hd_json_get(man, "levels");
   res = hd_json_get(man, "resolution");
   if (!format || format->type != JSON_INT)
      err = "manifest.json has no format number";
   else if (format->num > HD_PACKAGE_FORMAT)
      err = "this game was made for a newer go-link HD: update the core";
   else if (format->num < 1)
      err = "manifest.json has an unknown format";
   else if (!title || title->type != JSON_STRING || !title->str[0])
      err = "manifest.json has no title";
   else if ((!level || level->type != JSON_STRING) && !levels)
      err = "manifest.json names no level";
   else if (pictures && pictures->type != JSON_OBJECT)
      err = "manifest.json's pictures must be an object";
   else if (players && (players->type != JSON_INT || players->num < 1 || players->num > MAX_PLAYERS))
      err = "manifest.json's players must be 1 to 8";
   else if (screen && (screen->type != JSON_STRING || (strcmp(screen->str, "16:9") && strcmp(screen->str, "4:3") && strcmp(screen->str, "9:16"))))
      err = "manifest.json's screen must be \"16:9\", \"4:3\" or \"9:16\"";
   else if (res && (res->type != JSON_STRING || (strcmp(res->str, "360p") && strcmp(res->str, "720p") && strcmp(res->str, "1080p"))))
      err = "manifest.json's resolution must be \"360p\", \"720p\" or \"1080p\"";
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
   if (players)
      hd_players = (int32_t)players->num;
   if (screen && !strcmp(screen->str, "4:3"))
   {
      hd_w = 480;
      hd_h = 360;
   }
   else if (screen && !strcmp(screen->str, "9:16"))
   {
      hd_w = 360;
      hd_h = 640;
   }
   /* its pictures are made for this size: the logical screen 1, 2 or 3 times */
   hd_res = res && !strcmp(res->str, "720p") ? 2 : res && !strcmp(res->str, "1080p") ? 3 : 1;

   err = load_physics(hd_json_get(man, "physics"));
   if (!err)
      err = load_weapon(hd_json_get(man, "weapon"));
   if (!err)
      err = load_health(hd_json_get(man, "health"));
   if (err)
   {
      hd_json_free(man);
      return err;
   }
   /* a package of several levels loads them last, after what they share */
   err = levels ? NULL : load_level_file(&zip, level->str);
   for (s = 0; !err && s < (int32_t)(sizeof sheets / sizeof sheets[0]); s++)
      err = load_sheet(&zip, pictures, s);
   if (!err)
      err = load_extras(&zip, pictures);
   if (!err)
      err = hd_sprites_load(&zip, hd_json_get(man, "sprites"));
   if (!err && !levels)
      err = hd_layers_load(&zip, hd_json_get(man, "layers"));
   if (!err && !levels)
      err = hd_textures_load(&zip, hd_json_get(man, "textures"));
   if (!err)
      err = hd_screens_load(&zip, hd_json_get(man, "screens"));
   if (!err)
      err = hd_sounds_load(&zip, hd_json_get(man, "sounds"), hd_json_get(man, "music"));
   if (!err && levels)
      err = load_stages(&zip, levels);
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
