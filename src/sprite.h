/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * A package's own characters (format 3's "sprites"): pictures of any size,
 * an animation per state, a skin per player. The game's rules stay the
 * same; only the pictures and their timing change.
 */
#ifndef HD_SPRITE_H
#define HD_SPRITE_H

#include "hd.h"
#include "pack.h"
#include "gfx.h"

/* The states a hero is drawn in. */
enum
{
   ANIM_IDLE = 0, /* standing */
   ANIM_RUN,      /* moving on the ground */
   ANIM_JUMP,     /* in the air: its frames go from rising to falling */
   ANIM_HURT,     /* just hit (played once) */
   ANIM_BORED,    /* standing still for a long while (played now and then) */
   ANIM_WIN,      /* the stage is cleared */
   ANIM_SHOT,     /* the skin's shot flying (format 3's weapon; facing right) */
   ANIM_SHOT_HIT, /* its burst where it hits (played once) */
   ANIM_KO,       /* knocked out (format 3's health): played once over the knockout */
   ANIM_COUNT
};

typedef struct
{
   hd_image *frames; /* NULL: the skin has no such animation */
   int32_t count;    /* frames */
   int32_t fps;      /* frames per second, 1 to 60 */
   int32_t feet;     /* empty pixels under the feet in every frame */
   int32_t stride;   /* pixels walked in one whole cycle (run): the frames follow the distance, not the time; 0 by time */
} hd_anim;

/* A rubber-hose puppet (rig.c): parts pictures and the hoses' sizes, in pixels. */
typedef struct
{
   hd_anim body, hand, foot; /* faces, gloves and shoes, each a row of pictures */
   hd_anim worn;             /* optional: the body's faces worn out, shown with little health left */
   int32_t limb, leg, arm, stride, lift, bob;
} hd_rig;

typedef struct
{
   hd_anim anim[ANIM_COUNT];
   int32_t has_rig; /* drawn as a puppet instead of the animations */
   hd_rig rig;
} hd_skin;

/* Draws a player as its skin's puppet, its feet at (feet_x, feet_y) on the surface. */
void hd_rig_draw(hd_surface *s, const hd_rig *rig, const hd_state *st, const hd_player *p, int32_t i,
                 int32_t feet_x, int32_t feet_y, int32_t white);

#define MAX_SKINS 8

/* The level's things drawn from a package's sprites: "coin", "checkpoint" (off, on), "goal", "enemy" (walk, squashed). */
enum { OBJ_COIN = 0, OBJ_CHECK_OFF, OBJ_CHECK_ON, OBJ_GOAL, OBJ_ENEMY_WALK, OBJ_ENEMY_SQUASHED, OBJ_COUNT };
extern hd_anim hd_objects[OBJ_COUNT];

/* Format 3's "textures": a tile kind painted as a picture repeated over the level (sides multiples of 16). */
extern hd_image hd_textures[TL_COUNT];

/* The loaded skins and the one each player wears; hd_skin_count 0 means the built-in hero. */
extern hd_skin hd_skins[MAX_SKINS];
extern int32_t hd_skin_count;
extern int32_t hd_skin_of[MAX_PLAYERS];

/*
 * Format 3's "layers": painted pictures behind (or in front of) the level,
 * repeated across it, moving at their own share of the camera's speed.
 */
typedef struct
{
   hd_image img;
   int32_t speed; /* hundredths of the camera's movement: 0 stays, 100 moves with the level */
   int32_t y;     /* its top, in level pixels (at speed 100) */
   int32_t front; /* drawn over the characters */
} hd_layer;

#define MAX_LAYERS 8
extern hd_layer hd_layers[MAX_LAYERS];
extern int32_t hd_layer_count;

/*
 * Format 3's "screens": pictures over the whole screen, scaled to it when
 * the package loads: "title" (before the game), "intro" (the level's card,
 * after start: a few seconds, or until the jump button is held) and
 * "ending" (after the stage is cleared).
 */
enum { SCREEN_TITLE = 0, SCREEN_INTRO, SCREEN_ENDING, SCREEN_COUNT };
extern hd_image hd_screens[SCREEN_COUNT];
extern int32_t hd_intro_frames; /* how long the intro shows by itself */
#define SKIP_HOLD_FRAMES 45      /* holding jump this long skips the intro */
const char *hd_screens_load(const hd_zip *zip, const json *screens);

/* Reads "textures" (NULL is fine). */
const char *hd_textures_load(const hd_zip *zip, const json *textures);
/* Reads "layers" (NULL is fine). */
const char *hd_layers_load(const hd_zip *zip, const json *layers);
/* Draws the back (front 0) or front (1) layers into a surface whose top-left is the camera at (cx, cy). */
void hd_layers_draw(uint32_t *px, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t front);

/* sound.c: format 3's "sounds" and "music" (WAV files). */
extern const int16_t *hd_pkg_music; /* stereo, NULL: the built-in tune */
extern int32_t hd_pkg_music_frames, hd_pkg_music_loop, hd_pkg_music_vol;
const char *hd_sounds_load(const hd_zip *zip, const json *sounds, const json *music);
void hd_sounds_free(void);

/* Frames standing still before the hero looks bored. */
#define BORED_AFTER 360

/* Reads "sprites" (NULL is fine); NULL or the reason it cannot. */
const char *hd_sprites_load(const hd_zip *zip, const json *sprites);
/* Back to the built-in hero, freeing the pictures. */
void hd_sprites_free(void);
/* The animation a skin uses for a state, falling back to another it has (idle at the end); NULL when it has none. */
const hd_anim *hd_skin_anim(const hd_skin *sk, int32_t state);

#endif
