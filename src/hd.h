/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * go-link HD: the screen, the whole game state and the calls between the
 * engine's parts (art, level, game, draw, audio). The libretro API lives in
 * libretro.c and only talks to the engine through these calls.
 *
 * The state rule: every field of hd_state (and of the structs inside it) is
 * a 32-bit integer (int32_t or uint32_t), so the struct has no padding and a
 * save state is the struct written word by word in little endian. Nothing
 * outside hd_state changes while the game runs: art, level and sounds are
 * built once by hd_static_init() and only read afterwards.
 */
#ifndef HD_ENGINE_H
#define HD_ENGINE_H

#include <stdint.h>
#include "fixed.h"

#define HD_VERSION "0.1.0"
/*
 * The save state's layout version. Bump it whenever hd_state changes; a
 * save state of another version is refused cleanly, never misread.
 */
#define HD_STATE_VERSION 1

/* The logical screen: 16:9, scaled x3 to 1080p and x6 to 4K. */
#define HD_W 640
#define HD_H 360
#define HD_FPS 60
#define HD_RATE 48000
#define HD_SAMPLES_PER_FRAME (HD_RATE / HD_FPS)

#define TILE 16
#define MAP_W 224
#define MAP_H 24

#define MAX_PLAYERS 4
#define MAX_ENEMIES 48
#define MAX_PARTICLES 256
#define MAX_CHANNELS 32
/* Channels 0 and 1 belong to the music, the rest to sound effects. */
#define MUSIC_CHANNELS 2

/* A player's buttons for one frame. */
enum
{
   PAD_UP = 1 << 0,
   PAD_DOWN = 1 << 1,
   PAD_LEFT = 1 << 2,
   PAD_RIGHT = 1 << 3,
   PAD_JUMP = 1 << 4,
   PAD_RUN = 1 << 5,
   PAD_START = 1 << 6,
   PAD_SELECT = 1 << 7
};

/* The level's cells. */
enum
{
   T_EMPTY = 0,
   T_GROUND,
   T_BRICK,
   T_PLATFORM, /* one way: stood on from above, passed through from below */
   T_COIN,
   T_CHECK,
   T_FLAG,
   T_ENEMY /* where an enemy starts; empty once the level is built */
};

enum { PH_TITLE = 0, PH_PLAY, PH_CLEAR };

enum
{
   SFX_JUMP = 0,
   SFX_COIN,
   SFX_STOMP,
   SFX_HURT,
   SFX_JOIN,
   SFX_CHECK,
   SFX_CLEAR,
   SFX_PAUSE,
   SFX_COUNT
};
/* Every sample the mixer knows: the effects and the music's two waves. */
#define HD_SAMPLES (SFX_COUNT + 2)

/* The player's hitbox inside its 16 x 24 picture. */
#define PW 10
#define PH 22
/* An enemy's hitbox inside its 16 x 16 picture. */
#define EW 14
#define EH 12

/* A hurt player blinks for HURT_FRAMES and flashes white while hurt > HURT_FLASH. */
#define HURT_FRAMES 90
#define HURT_FLASH 84

typedef struct
{
   int32_t active;
   int32_t x, y;   /* hitbox top-left, 16.16 */
   int32_t vx, vy; /* 16.16 per frame */
   int32_t ground, facing, anim;
   int32_t coyote, buffer, jumping, drop;
   int32_t coins, hurt, respawn;
   uint32_t pad, prev;
   int32_t check_x, check_y; /* where it comes back, in pixels */
   int32_t landed;           /* frames since it touched the ground */
} hd_player;

typedef struct
{
   int32_t alive; /* 0 gone, 1 walking, 2 squashed */
   int32_t x, y, vx, vy;
   int32_t squash, anim;
   int32_t awake;
} hd_enemy;

typedef struct
{
   int32_t life, max;
   int32_t x, y, vx, vy;
   uint32_t color;
   int32_t flags; /* 1 = falls, 2 = additive glow */
} hd_particle;

typedef struct
{
   int32_t sample; /* -1 = idle */
   int32_t pos;  /* whole samples played */
   int32_t frac; /* and the fraction of the next one, 0..65535 */
   int32_t step; /* 16.16 samples per output sample (the pitch) */
   int32_t vol, decay, pan;
} hd_channel;

typedef struct
{
   int32_t frame;
   uint32_t rng;
   int32_t phase, phase_t, paused, hitstop;
   int32_t shake, shake_x, shake_y;
   int32_t cam_x, cam_y; /* 16.16, top-left of the screen in the level */
   int32_t music_row, music_tick;
   int32_t part_next, sfx_next;
   uint32_t taken[(MAP_W * MAP_H + 31) / 32]; /* coins taken, checkpoints reached */
   hd_player p[MAX_PLAYERS];
   hd_enemy e[MAX_ENEMIES];
   hd_particle part[MAX_PARTICLES];
   hd_channel ch[MAX_CHANNELS];
} hd_state;

/* Pictures are 0xAARRGGBB; alpha 0 is not drawn. */
typedef struct
{
   int32_t w, h;
   uint32_t *px;
} hd_image;

/* art.c */
enum { HERO_IDLE = 0, HERO_WALK1, HERO_WALK2, HERO_JUMP, HERO_FRAMES };
enum { ENEMY_WALK1 = 0, ENEMY_WALK2, ENEMY_SQUASHED, ENEMY_FRAMES };
enum { TL_GROUND_TOP = 0, TL_GROUND, TL_BRICK, TL_PLATFORM, TL_COUNT };
#define COIN_FRAMES 4

extern hd_image hd_hero[MAX_PLAYERS][HERO_FRAMES];
extern hd_image hd_enemy_img[ENEMY_FRAMES];
extern hd_image hd_tiles[TL_COUNT];
extern hd_image hd_coin[COIN_FRAMES];
extern hd_image hd_check[2]; /* not reached, reached */
extern hd_image hd_flag;
extern const uint32_t hd_player_color[MAX_PLAYERS];
/* Returns 0 when a drawing in the source has a wrong size. */
int hd_art_build(void);

/* level.c */
extern uint8_t hd_map[MAP_H][MAP_W];
extern int32_t hd_enemy_start[MAX_ENEMIES][2]; /* pixels; count in hd_enemy_count */
extern int32_t hd_enemy_count;
extern int32_t hd_start_x, hd_start_y;
void hd_level_build(void);

/* game.c */
void hd_static_init(void);
void hd_reset(hd_state *s);
void hd_step(hd_state *s, const uint32_t pads[MAX_PLAYERS]);
int hd_cell(int32_t tx, int32_t ty);

/* draw.c */
void hd_draw(const hd_state *s, uint32_t *fb);

/* audio.c */
void hd_audio_build(void);
void hd_play(hd_state *s, int32_t sfx, int32_t screen_x);
void hd_music_step(hd_state *s);
/* Mixes one frame (HD_SAMPLES_PER_FRAME stereo samples) into out. */
void hd_mix(hd_state *s, int16_t *out, int music_on);

/* save.c */
#define HD_SAVE_SIZE (16 + sizeof(hd_state))
/* Writes a save state of HD_SAVE_SIZE bytes. */
void hd_save(const hd_state *s, uint8_t *out);
/* Reads one back; returns 0 (and leaves s alone) when it is not ours or of another version. */
int hd_load(hd_state *s, const uint8_t *in, uint32_t size);

#endif
