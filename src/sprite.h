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

/* Frames standing still before the hero looks bored. */
#define BORED_AFTER 360

/* Reads "sprites" (NULL is fine); NULL or the reason it cannot. */
const char *hd_sprites_load(const hd_zip *zip, const json *sprites);
/* Back to the built-in hero, freeing the pictures. */
void hd_sprites_free(void);
/* The animation a skin uses for a state, falling back to another it has (idle at the end); NULL when it has none. */
const hd_anim *hd_skin_anim(const hd_skin *sk, int32_t state);

#endif
