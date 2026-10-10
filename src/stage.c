/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Format 3's "levels": a game of several levels in one package.
 *
 *   "levels": [
 *     {"level": "colon.json", "sky": ["#3a1420", "#7a3a3a"], "layers": [...], "textures": {...},
 *      "intro": "intro_colon.png", "music": {"file": "colon.wav", "volume": 180}},
 *     {"level": "stomach.json", ...}
 *   ]
 *
 * Each level is loaded with the package by the same loaders as a package of
 * one level (they fill the engine's globals), then kept here; selecting a
 * level points the globals at its copy again. A level with "same_art": true
 * is another act of the one before: it shows that level's sky, layers,
 * textures and music (not loaded twice, no intro), with its own map and boss. The state's `stage` is the
 * level played, so a save state brings back the same one.
 */
#include <stdlib.h>
#include <string.h>
#include "sprite.h"

typedef struct
{
   uint8_t *map; /* w * h cells */
   int32_t w, h, enemies, start_x, start_y;
   int32_t enemy_start[MAX_ENEMIES][2];
   hd_fx_config fx;
   uint32_t sky_top, sky_bottom;
   hd_layer layers[MAX_LAYERS];
   int32_t layer_count;
   hd_image textures[TEX_COUNT];
   hd_image intro;
   int16_t *music;
   int32_t music_frames, music_loop, music_vol;
   int32_t enemy_kind[MAX_ENEMIES];
   hd_boss_form boss[2]; /* its boss and the form it evolves into */
   hd_enemy_kind minion_kind;
   int16_t *boss_music;
   int32_t boss_music_frames, boss_music_loop, boss_music_vol;
   int32_t shared; /* the pictures and music are another level's (not freed here) */
} stage;

const int16_t *hd_pkg_boss_music;
int32_t hd_pkg_boss_music_frames, hd_pkg_boss_music_loop, hd_pkg_boss_music_vol;
/* the boss music of the level being loaded (hd_stage_boss_music), until hd_stage_keep takes it */
static int16_t *pending_music;
static int32_t pending_frames, pending_loop, pending_vol;

void hd_stage_boss_music(void)
{
   free(pending_music);
   pending_frames = hd_pkg_music_frames;
   pending_loop = hd_pkg_music_loop;
   pending_vol = hd_pkg_music_vol;
   pending_music = hd_music_take();
}

static stage stages[MAX_STAGES];
int32_t hd_stage_count;
static int32_t selected = -1;

/* Takes what the loaders just put in the globals as level k (the globals are left empty). */
int hd_stage_keep(int32_t k)
{
   stage *st = &stages[k];
   int32_t y;
   st->map = (uint8_t *)malloc((size_t)hd_map_w * hd_map_h);
   if (!st->map)
      return 0;
   for (y = 0; y < hd_map_h; y++)
      memcpy(st->map + (size_t)y * hd_map_w, hd_map[y], (size_t)hd_map_w);
   st->w = hd_map_w;
   st->h = hd_map_h;
   st->enemies = hd_enemy_count;
   memcpy(st->enemy_start, hd_enemy_start, sizeof st->enemy_start);
   memcpy(st->enemy_kind, hd_enemy_kind_of, sizeof st->enemy_kind);
   memcpy(st->boss, hd_boss_forms, sizeof st->boss);
   st->minion_kind = hd_kinds[EK_MINION];
   memset(hd_boss_forms, 0, sizeof hd_boss_forms); /* the level's now */
   memset(hd_boss_anim, 0, sizeof hd_boss_anim);
   memset(&hd_boss, 0, sizeof hd_boss);
   st->boss_music = pending_music;
   st->boss_music_frames = pending_frames;
   st->boss_music_loop = pending_loop;
   st->boss_music_vol = pending_vol;
   pending_music = NULL;
   st->start_x = hd_start_x;
   st->start_y = hd_start_y;
   st->fx = hd_fx;
   st->sky_top = hd_sky_top;
   st->sky_bottom = hd_sky_bottom;
   memcpy(st->layers, hd_layers, sizeof st->layers);
   st->layer_count = hd_layer_count;
   memset(hd_layers, 0, sizeof hd_layers);
   hd_layer_count = 0;
   memcpy(st->textures, hd_textures, sizeof st->textures);
   memset(hd_textures, 0, sizeof hd_textures);
   st->intro = hd_screens[SCREEN_INTRO];
   memset(&hd_screens[SCREEN_INTRO], 0, sizeof hd_screens[SCREEN_INTRO]);
   st->music_frames = hd_pkg_music_frames;
   st->music_loop = hd_pkg_music_loop;
   st->music_vol = hd_pkg_music_vol;
   st->music = hd_music_take();
   hd_stage_count = k + 1;
   selected = -1;
   return 1;
}

void hd_stage_share(int32_t k)
{
   stage *st = &stages[k];
   const stage *from = &stages[k - 1];
   int32_t i;
   if (k < 1 || k >= hd_stage_count)
      return;
   for (i = 0; i < st->layer_count; i++)
      free(st->layers[i].img.px);
   for (i = 0; i < TEX_COUNT; i++)
      free(st->textures[i].px);
   free(st->intro.px);
   free(st->music);
   st->sky_top = from->sky_top;
   st->sky_bottom = from->sky_bottom;
   memcpy(st->layers, from->layers, sizeof st->layers);
   st->layer_count = from->layer_count;
   memcpy(st->textures, from->textures, sizeof st->textures);
   memset(&st->intro, 0, sizeof st->intro);
   st->music = from->music;
   st->music_frames = from->music_frames;
   st->music_loop = from->music_loop;
   st->music_vol = from->music_vol;
   st->shared = 1;
}

void hd_stage_select(int32_t k)
{
   const stage *st;
   int32_t y;
   if (!hd_stage_count)
      return;
   k = hd_clamp(k, 0, hd_stage_count - 1);
   if (k == selected)
      return;
   st = &stages[k];
   memset(hd_map, 0, sizeof hd_map);
   for (y = 0; y < st->h; y++)
      memcpy(hd_map[y], st->map + (size_t)y * st->w, (size_t)st->w);
   hd_map_w = st->w;
   hd_map_h = st->h;
   hd_enemy_count = st->enemies;
   memcpy(hd_enemy_start, st->enemy_start, sizeof st->enemy_start);
   memcpy(hd_enemy_kind_of, st->enemy_kind, sizeof st->enemy_kind);
   memcpy(hd_boss_forms, st->boss, sizeof hd_boss_forms);
   hd_kinds[EK_MINION] = st->minion_kind;
   hd_boss_form_use(0);
   hd_pkg_boss_music = st->boss_music;
   hd_pkg_boss_music_frames = st->boss_music_frames;
   hd_pkg_boss_music_loop = st->boss_music_loop;
   hd_pkg_boss_music_vol = st->boss_music_vol;
   hd_start_x = st->start_x;
   hd_start_y = st->start_y;
   if (hd_fx.grade != st->fx.grade && st->fx.grade != GRADE_LUT)
      fx_grade_build(st->fx.grade, NULL);
   hd_fx = st->fx;
   hd_sky_top = st->sky_top;
   hd_sky_bottom = st->sky_bottom;
   /* the globals only point at the level's pictures and music: they stay the level's */
   memcpy(hd_layers, st->layers, sizeof hd_layers);
   hd_layer_count = st->layer_count;
   memcpy(hd_textures, st->textures, sizeof hd_textures);
   hd_screens[SCREEN_INTRO] = st->intro;
   hd_pkg_music = st->music;
   hd_pkg_music_frames = st->music_frames;
   hd_pkg_music_loop = st->music_loop;
   hd_pkg_music_vol = st->music_vol;
   selected = k;
}

void hd_stages_free(void)
{
   int32_t k, i;
   if (hd_stage_count)
   {
      /* the globals point into the levels: empty them before the levels go */
      memset(hd_layers, 0, sizeof hd_layers);
      hd_layer_count = 0;
      memset(hd_textures, 0, sizeof hd_textures);
      memset(&hd_screens[SCREEN_INTRO], 0, sizeof hd_screens[SCREEN_INTRO]);
      hd_pkg_music = NULL;
      hd_pkg_music_frames = hd_pkg_music_loop = 0;
      memset(hd_boss_anim, 0, sizeof hd_boss_anim); /* the levels' */
      memset(hd_boss_forms, 0, sizeof hd_boss_forms);
      memset(&hd_boss, 0, sizeof hd_boss);
   }
   hd_pkg_boss_music = NULL;
   hd_pkg_boss_music_frames = hd_pkg_boss_music_loop = 0;
   free(pending_music);
   pending_music = NULL;
   for (k = 0; k < MAX_STAGES; k++)
   {
      stage *st = &stages[k];
      free(st->map);
      if (!st->shared)
      {
         for (i = 0; i < st->layer_count; i++)
            free(st->layers[i].img.px);
         for (i = 0; i < TEX_COUNT; i++)
            free(st->textures[i].px);
         free(st->intro.px);
         free(st->music);
      }
      free(st->boss_music);
      for (i = 0; i < BOSS_ANIMS; i++)
      {
         hd_anim_free(&st->boss[0].anim[i]);
         hd_anim_free(&st->boss[1].anim[i]);
      }
   }
   memset(stages, 0, sizeof stages);
   hd_stage_count = 0;
   selected = -1;
}
