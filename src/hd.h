/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * go-link HD: the screen, the whole game state and the calls between the
 * engine's parts (art, level, game, draw, audio). Hosts never see these:
 * they use the API in include/golink_hd.h (api.c).
 *
 * The state rule: every field of hd_state (and of the structs inside it) is
 * a 32-bit integer (int32_t or uint32_t), so the struct has no padding and a
 * save state is the struct written word by word in little endian. Nothing
 * outside hd_state changes while the game runs: art, level and sounds are
 * built once by hd_static_init() and only read afterwards.
 */
#ifndef HD_ENGINE_H
#define HD_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include "fixed.h"
#include "golink_hd.h"

#define HD_VERSION "0.2.0"
/*
 * The save state's layout version. Bump it whenever hd_state changes; a
 * save state of another version is refused cleanly, never misread.
 */
#define HD_STATE_VERSION 5

/*
 * The logical screen, chosen by the game: 640 x 360 (16:9, scaled x3 to
 * 1080p and x6 to 4K, the default), 480 x 360 (4:3) or 360 x 640 (9:16,
 * vertical). Buffers are sized for the largest.
 */
#define HD_MAX_W 640
#define HD_MAX_H 640
#define HD_W hd_w
#define HD_H hd_h
extern int32_t hd_w, hd_h;
#define HD_FPS 60
#define HD_RATE 48000
#define HD_SAMPLES_PER_FRAME (HD_RATE / HD_FPS)

#define TILE 16
/* The level's size in cells: the loaded game's, within these limits. */
#define MAP_MAX_W 1024
#define MAP_MAX_H 64
#define MAP_MIN_W (HD_W / TILE)
#define MAP_MIN_H ((HD_H + TILE - 1) / TILE)
#define MAP_W hd_map_w
#define MAP_H hd_map_h

/* Up to 8 players; the game says how many it takes (hd_players). */
#define MAX_PLAYERS GOLINKHD_MAX_PLAYERS
#define DEFAULT_PLAYERS 4
extern int32_t hd_players;
#define MAX_ENEMIES 48
#define MAX_PARTICLES 256
#define MAX_CHANNELS 32
/* Channels 0 and 1 belong to the music, the rest to sound effects. */
#define MUSIC_CHANNELS 2
/* The longest echo: 0.3 s. */
#define ECHO_MAX (HD_RATE * 3 / 10)

/* A player's buttons for one frame: the API's (golink_hd.h), by shorter names. */
enum
{
   PAD_UP = GOLINKHD_UP,
   PAD_DOWN = GOLINKHD_DOWN,
   PAD_LEFT = GOLINKHD_LEFT,
   PAD_RIGHT = GOLINKHD_RIGHT,
   PAD_JUMP = GOLINKHD_JUMP,
   PAD_RUN = GOLINKHD_RUN,
   PAD_START = GOLINKHD_START,
   PAD_SELECT = GOLINKHD_SELECT,
   PAD_L = GOLINKHD_L,
   PAD_R = GOLINKHD_R,
   PAD_L2 = GOLINKHD_L2,
   PAD_R2 = GOLINKHD_R2,
   PAD_L3 = GOLINKHD_L3,
   PAD_R3 = GOLINKHD_R3,
   PAD_A = GOLINKHD_A,
   PAD_B = GOLINKHD_B,
   PAD_X = GOLINKHD_X,
   PAD_Y = GOLINKHD_Y
};

/* One player's controller for one frame: the API's pad (golink_hd.h). */
typedef golinkhd_pad hd_input;

/* How far a stick must lean to count as a direction. */
#define STICK_DEAD 16384

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

/*
 * The players' body and movement: the built-in game's by default, a
 * package's own with format 3's "physics" (a bigger hero drawn from bigger
 * pictures). Speeds and accelerations are 16.16 pixels per frame (squared).
 */
typedef struct
{
   int32_t pw, ph; /* the hitbox, in pixels */
   int32_t walk_max, run_max, accel_ground, accel_air, friction_ground, friction_air;
   int32_t gravity, gravity_hold, fall_max, jump_speed, jump_cut, bounce, bounce_held;
} hd_physics;
extern hd_physics hd_phys;
void hd_physics_default(void);
/* The player's hitbox (inside its 16 x 24 picture in the built-in game). */
#define PW (hd_phys.pw)
#define PH (hd_phys.ph)
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
   int32_t lx, ly;           /* the left stick this frame */
   int32_t check_x, check_y; /* where it comes back, in pixels */
   int32_t landed;           /* frames since it touched the ground */
   int32_t still;            /* frames standing still with no button held (a sprite's bored animation) */
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

/* The showcase demo's own state (showcase.c). */
#define SHOW_BATS 4
typedef struct
{
   int32_t on, scene, t, trans, next;   /* trans: frames left of the change of scene */
   int32_t grade, zoom, blur, bloom;     /* the colors scene */
   int32_t kx, ky, ka, kv;              /* the kart (Mode 7), 16.16 */
   int32_t rpos, rx, rv;                /* the car on the road */
   int32_t hx, hy, hvx, hvy, hground, hface, hwalk, hjump; /* the boned hero, 16.16 */
   int32_t bat_x[SHOW_BATS], bat_y[SHOW_BATS], bat_tx[SHOW_BATS], bat_ty[SHOW_BATS], bat_hit; /* 16.16 and target cells */
   int32_t dlg, dlg_chars;
} hd_show;

typedef struct
{
   int32_t frame;
   uint32_t rng;
   int32_t phase, phase_t, paused, hitstop;
   int32_t shake, shake_x, shake_y;
   int32_t cam_x, cam_y; /* 16.16, top-left of the screen in the level */
   int32_t music_row, music_tick;
   int32_t part_next, sfx_next;
   uint32_t taken[(MAP_MAX_W * MAP_MAX_H + 31) / 32]; /* coins taken, checkpoints reached */
   hd_player p[MAX_PLAYERS];
   hd_enemy e[MAX_ENEMIES];
   hd_particle part[MAX_PARTICLES];
   hd_channel ch[MAX_CHANNELS];
   /* the sound's effects on the whole mix (underwater, caves) */
   int32_t lowpass;              /* 0 off; else 1..256, how much of each new sample passes */
   int32_t echo, echo_feedback, echo_mix; /* echo: delay in samples (0 off), 0..256, 0..256 */
   int32_t lp_l, lp_r, echo_pos;
   int32_t echo_buf[ECHO_MAX * 2];
   int32_t zoom;                 /* the camera's zoom, 256 = 1x (128 shows twice as much, 512 half) */
   int32_t dlg, dlg_chars;       /* the dialog on screen (index + 1, 0 = none) and its letters shown */
   uint32_t dlg_done;            /* dialogs already shown, a bit each */
   hd_show show;                 /* the showcase demo (showcase.c) */
} hd_state;

/* Effects of a game's level (package format 2, "effects"; all off in the built-in demo). */
#define FX_LIGHTS_MAX 32
#define DIALOGS_MAX 16
#define LANGS 3 /* English, Spanish, Portuguese */
typedef struct
{
   int32_t darkness;            /* 0..256 */
   int32_t player_light;        /* the light each player carries: radius in pixels, 0 = none */
   uint32_t player_light_color; /* 0xRRGGBB */
   int32_t lights;
   int32_t light_x[FX_LIGHTS_MAX], light_y[FX_LIGHTS_MAX], light_r[FX_LIGHTS_MAX], light_flicker[FX_LIGHTS_MAX];
   uint32_t light_color[FX_LIGHTS_MAX];
   int32_t grade, grade_amount;  /* GRADE_* (gfx.h), 0..256 */
   int32_t bloom, bloom_threshold;
   int32_t waves_amp, waves_len, waves_y; /* below the level's row waves_y (pixels), 0 amplitude = off */
   int32_t zoom_auto;           /* zoom out when players spread apart */
   uint32_t outline;            /* 0xAARRGGBB around the characters, 0 = none */
   int32_t shadows;
   int32_t lowpass, echo_ms;    /* the mix: 0 = off (see hd_audio_effects) */
   int32_t dialogs;
   int32_t dialog_col[DIALOGS_MAX];
   char dialog_name[DIALOGS_MAX][24];
   char dialog_text[DIALOGS_MAX][LANGS][200];
} hd_fx_config;
extern hd_fx_config hd_fx;
extern int32_t hd_lang; /* 0 English, 1 Spanish, 2 Portuguese (golinkhd_set_language) */

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

/* level.c: the built-in demo's level; content.c loads a package's */
extern uint8_t hd_map[MAP_MAX_H][MAP_MAX_W];
extern int32_t hd_map_w, hd_map_h;
extern int32_t hd_enemy_start[MAX_ENEMIES][2]; /* pixels; count in hd_enemy_count */
extern int32_t hd_enemy_count;
extern int32_t hd_start_x, hd_start_y;
void hd_level_build(void);

/* content.c: what the loaded game is */
extern char hd_title[64];
extern uint32_t hd_sky_top, hd_sky_bottom; /* 0xRRGGBB */
extern uint8_t hd_content_id[32];         /* SHA-256 of the package; zeros for the built-in demo */
extern int32_t hd_content_gen;            /* changes whenever the content does */
/* The built-in demo. */
void hd_content_builtin(void);
/*
 * A game package (.glhd). Returns 1, or 0 with a readable *err (and the
 * built-in demo back in place).
 */
int hd_content_load(const uint8_t *data, size_t size, const char **err);
/* The package format this engine reads (manifest "format"). */
#define HD_PACKAGE_FORMAT 3

/* game.c */
void hd_static_init(void);
void hd_reset(hd_state *s);
void hd_step(hd_state *s, const hd_input in[MAX_PLAYERS]);
int hd_cell(int32_t tx, int32_t ty);

/* draw.c */
void hd_draw(const hd_state *s, uint32_t *fb);
/* The zoom's buffer: the screen at 0.5x, the largest it draws. */
#define ZOOM_MAX_W (2 * HD_MAX_W)
#define ZOOM_MAX_H (2 * HD_MAX_H)
/* An optional picture for dialogs (the package's "portrait"). */
extern hd_image hd_portrait;

/* showcase.c: the demo of every effect (golinkhd_load_demo(e, 1)) */
void hd_show_start(hd_state *s);
void hd_show_step(hd_state *s, const hd_input in[MAX_PLAYERS]);
void hd_show_draw(const hd_state *s, void *screen);
void hd_show_build(void);
void hd_show_sanitize(hd_show *w);

/* audio.c */
void hd_audio_build(void);
void hd_play(hd_state *s, int32_t sfx, int32_t screen_x);
void hd_music_step(hd_state *s);
/* Mixes one frame (HD_SAMPLES_PER_FRAME stereo samples) into out. */
void hd_mix(hd_state *s, int16_t *out, int music_on);
/* The mix's effects: low pass 0 (off) or 1..256, an echo of echo_ms (0 = off, up to 300) with its feedback and mix (0..256). */
void hd_audio_effects(hd_state *s, int32_t lowpass, int32_t echo_ms, int32_t feedback, int32_t mix);

/* save.c: a 48 byte header (see save.c), then the state */
#define HD_SAVE_HEADER 48
#define HD_SAVE_SIZE (HD_SAVE_HEADER + sizeof(hd_state))
/* Writes a save state of HD_SAVE_SIZE bytes. */
void hd_save(const hd_state *s, uint8_t *out);
/* Reads one back; returns 0 (and leaves s alone) when it is not ours or of another version. */
int hd_load(hd_state *s, const uint8_t *in, uint32_t size);

#endif
