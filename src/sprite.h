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
   ANIM_SUPER,    /* the super attack (format 3's weapon): played once over it */
   ANIM_GRANULE,  /* a super attack's granule (looped) */
   ANIM_DASH,     /* the dash (format 3's "dash"): played once over it */
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

/*
 * The level's things drawn from a package's sprites: "coin", "checkpoint"
 * (off, on), "goal", "enemy" (walk, squashed), "spore" (fly, pop),
 * "spitter" (idle, spit, squashed) and "spit" (an enemy's shot, looped).
 */
enum
{
   OBJ_COIN = 0, OBJ_CHECK_OFF, OBJ_CHECK_ON, OBJ_GOAL, OBJ_ENEMY_WALK, OBJ_ENEMY_SQUASHED,
   OBJ_SPORE_FLY, OBJ_SPORE_POP, OBJ_SPITTER_IDLE, OBJ_SPITTER_SPIT, OBJ_SPITTER_SQUASHED, OBJ_SPIT,
   OBJ_COUNT
};
extern hd_anim hd_objects[OBJ_COUNT];

/*
 * A level's boss (format 3's levels, "boss"): its pictures, kept with its
 * level like the textures: idle (looped), windup and attack (held over the
 * attack's two parts), hurt, down (beaten), its minions' walk and its own
 * shot.
 */
enum { BOSS_IDLE = 0, BOSS_WINDUP, BOSS_ATTACK, BOSS_HURT, BOSS_DOWN, BOSS_MINION, BOSS_SHOT, BOSS_ANIMS };
extern hd_anim hd_boss_anim[BOSS_ANIMS];
/* Reads a level's "boss" (NULL is fine: no boss) into hd_boss, hd_kinds[EK_BOSS], hd_kinds[EK_MINION] and hd_boss_anim. */
const char *hd_boss_load(const hd_zip *zip, const json *boss);
void hd_boss_free(void);

/* boss.c: the enemies' kinds (defaults, the manifest's "enemies") */
void hd_kinds_default(void);
const char *hd_enemies_load(const json *enemies);

/* One animation of a package (a "file" cut in "frame"s, "fps", "feet", "from", "frames"); named in errors by who and what. */
const char *hd_anim_load(const hd_zip *zip, const json *def, hd_anim *an, const char *who, const char *what);
void hd_anim_free(hd_anim *an);

/*
 * Format 3's "textures": a tile kind painted as a picture repeated over the
 * level (sides multiples of 16), and optionally its ends: the same picture
 * cut for the cell where a run of that kind stops on the left or the right
 * (a floor at a pit, a platform's tip), at TEX_LEFT(kind) and TEX_RIGHT(kind).
 */
/* a wall's own pictures past the tile kinds: its top band and its bottom row when it floats */
enum { TX_BRICK_TOP = TL_COUNT, TX_BRICK_BOTTOM, TX_KINDS };
#define TEX_COUNT (TX_KINDS * 3)
#define TEX_LEFT(kind) (TX_KINDS + (kind))
#define TEX_RIGHT(kind) (TX_KINDS * 2 + (kind))
extern hd_image hd_textures[TEX_COUNT];

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
   int32_t opaque; /* every pixel solid: rows are copied, and what lies under it need not be drawn */
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
/* One screen picture (a file name) into hd_screens[i], scaled to the screen. */
const char *hd_screen_load(const hd_zip *zip, const json *file, int32_t i);

/* Reads "textures" (NULL is fine). */
const char *hd_textures_load(const hd_zip *zip, const json *textures);
/* Reads "layers" (NULL is fine). */
const char *hd_layers_load(const hd_zip *zip, const json *layers);
/* Draws the back (front 0) or front (1) layers into a surface whose top-left is the camera at (cx, cy). */
void hd_layers_draw(uint32_t *px, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t front);
/* Whether the back layers alone cover every pixel of a w x h surface (then the sky need not be drawn). */
int hd_layers_cover(int32_t h, int32_t cy);

/* sound.c: format 3's "sounds" and "music" (WAV files). */
extern const int16_t *hd_pkg_music; /* stereo, NULL: the built-in tune */
extern int32_t hd_pkg_music_frames, hd_pkg_music_loop, hd_pkg_music_vol;
/* the level's boss music, played from when its boss wakes up (NULL: the level's music goes on) */
extern const int16_t *hd_pkg_boss_music;
extern int32_t hd_pkg_boss_music_frames, hd_pkg_boss_music_loop, hd_pkg_boss_music_vol;
const char *hd_sounds_load(const hd_zip *zip, const json *sounds, const json *music);
/* Only the music (the effects stay); and handing its samples over (the caller frees them). */
const char *hd_music_load(const hd_zip *zip, const json *music);
int16_t *hd_music_take(void);

/*
 * Format 3's "levels": a game of several levels, each with its own level
 * file, effects, sky, layers, textures, intro picture and music; the rest
 * (heroes, sounds, physics, weapon, health, title and ending) is shared.
 * Everything is loaded with the package; the state's `stage` says which
 * one is played, and hd_stage_select points the engine at it.
 */
#define MAX_STAGES 16
extern int32_t hd_stage_count; /* 0: a package of one level (no "levels") */
void hd_stage_select(int32_t k);
/* Keeps the music just loaded as the level's boss music (the level being loaded). */
void hd_stage_boss_music(void);
int hd_stage_keep(int32_t k); /* takes what the loaders just loaded as level k */
void hd_stage_share(int32_t k); /* level k shows level k - 1's sky, pictures and music */
void hd_stages_free(void);
void hd_sounds_free(void);

/*
 * picture.c: a package's pictures as they load. hd_art is format 3's
 * "art_scale", how many times the logical screen they were painted (the
 * drawing's hd_res by default); pictures painted bigger are made hd_res
 * sized, and so are the pixel sizes that go with them (hd_art_px).
 */
extern int32_t hd_art;
int32_t hd_art_px(int32_t v);
/* Nearly solid pixels made solid and nearly empty ones empty (the soft edges stay). */
void hd_pic_clean(uint32_t *px, int64_t n);
/* A new dw x dh picture (no bigger) from sw x sh pixels in rows of `pitch`, by area; NULL without memory. */
uint32_t *hd_pic_shrink(const uint32_t *px, int32_t pitch, int32_t sw, int32_t sh, int32_t dw, int32_t dh);

/* Frames standing still before the hero looks bored. */
#define BORED_AFTER 360

/* Reads "sprites" (NULL is fine); NULL or the reason it cannot. */
const char *hd_sprites_load(const hd_zip *zip, const json *sprites);
/* Back to the built-in hero, freeing the pictures. */
void hd_sprites_free(void);
/* The animation a skin uses for a state, falling back to another it has (idle at the end); NULL when it has none. */
const hd_anim *hd_skin_anim(const hd_skin *sk, int32_t state);

#endif
