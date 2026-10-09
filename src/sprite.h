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

/* The states a hero is drawn in. */
enum
{
   ANIM_IDLE = 0, /* standing */
   ANIM_RUN,      /* moving on the ground */
   ANIM_JUMP,     /* in the air: its frames go from rising to falling */
   ANIM_HURT,     /* just hit (played once) */
   ANIM_BORED,    /* standing still for a long while (played now and then) */
   ANIM_WIN,      /* the stage is cleared */
   ANIM_COUNT
};

typedef struct
{
   hd_image *frames; /* NULL: the skin has no such animation */
   int32_t count;    /* frames */
   int32_t fps;      /* frames per second, 1 to 60 */
   int32_t feet;     /* empty pixels under the feet in every frame */
} hd_anim;

typedef struct
{
   hd_anim anim[ANIM_COUNT];
} hd_skin;

#define MAX_SKINS 8

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
