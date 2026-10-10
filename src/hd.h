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
#define HD_STATE_VERSION 7

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
/*
 * The drawing's scale (format 3's "resolution"): the game's rules stay on
 * the logical screen above (its pixels, cells of 16), and the picture is
 * drawn hd_res times bigger with pictures made for that size: 1 (360p, the
 * default), 2 (720p, 1280 x 720) or 3 (1080p, 1920 x 1080).
 */
extern int32_t hd_res;
#define HD_RES_MAX 3
extern int32_t hd_res_host; /* the host's choice for the next package, 0: the package's own */
#define HD_ART_MAX 6        /* format 3's art_scale: pictures up to 2160p (4K) */
#define HD_OUT_W (hd_w * hd_res)
#define HD_OUT_H (hd_h * hd_res)
#define HD_OUT_MAX_W (HD_MAX_W * HD_RES_MAX)
#define HD_OUT_MAX_H (HD_MAX_H * HD_RES_MAX)
#define HD_FPS 60
#define HD_RATE 48000
#define HD_SAMPLES_PER_FRAME (HD_RATE / HD_FPS)

#define TILE 16
/* The level's size in cells: the loaded game's, within these limits. */
#define MAP_MAX_W 1792 /* 28672 pixels: positions are 16.16, so a level stays well inside 32767 */
#define MAP_MAX_H 64
#define MAP_MIN_W (HD_W / TILE)
#define MAP_MIN_H ((HD_H + TILE - 1) / TILE)
#define MAP_W hd_map_w
#define MAP_H hd_map_h

/* Up to 8 players; the game says how many it takes (hd_players). */
#define MAX_PLAYERS GOLINKHD_MAX_PLAYERS
#define DEFAULT_PLAYERS 4
extern int32_t hd_players;
#define MAX_ENEMIES 256
/* a level's own enemies: the rest is kept for what a boss lets out */
#define MAX_LEVEL_ENEMIES (MAX_ENEMIES - 32)
#define MAX_PARTICLES 256
#define MAX_SHOTS 48
#define MAX_BOLTS 48 /* the enemies' own shots: spit, a boss's attacks */
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

enum { PH_TITLE = 0, PH_PLAY, PH_CLEAR, PH_INTRO, PH_OVER, PH_CREDITS };

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
   SFX_SHOOT, /* a weapon fires (format 3's "weapon") */
   SFX_HIT,   /* a shot hits an enemy that does not die */
   SFX_KO,    /* a player runs out of health (format 3's "health") */
   SFX_SUPER, /* a super attack (format 3's "super") */
   SFX_YAWN,  /* a player standing still yawns (a puppet's yawn, a skin's bored animation) */
   SFX_SPIT,  /* an enemy spits (format 3's spitter, a boss's spit) */
   SFX_DASH,  /* a player dashes (format 3's "dash") */
   SFX_ROAR,  /* a boss wakes up, and again when it gets angry */
   SFX_BOSS_HIT,  /* a boss takes a hit */
   SFX_BOSS_DOWN, /* a boss is defeated */
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
   int32_t ew, eh; /* an enemy's hitbox */
   int32_t walk_max, run_max, accel_ground, accel_air, friction_ground, friction_air;
   int32_t gravity, gravity_hold, fall_max, jump_speed, jump_cut, bounce, bounce_held;
} hd_physics;
extern hd_physics hd_phys;
void hd_physics_default(void);

/*
 * A package's weapon (format 3's "weapon"; off in the built-in game): a
 * button fires shots that fly straight ahead, stop at walls and hurt
 * enemies, which take `enemy_health` hits.
 */
typedef struct
{
   int32_t on;
   uint32_t button;        /* the pad bit that fires (held: it fires again every `rate` frames) */
   int32_t rate;           /* frames between two shots */
   int32_t speed;          /* 16.16 pixels a frame */
   int32_t life;           /* frames a shot flies before it fades (its range over its speed) */
   int32_t muzzle_x, muzzle_y; /* where shots start: pixels in front of the hitbox's middle, and from its feet (up is negative) */
   int32_t enemy_health;   /* hits an enemy takes */
   /* the super attack (format 3's "super"): charged by the shots' hits, it throws a fan of granules */
   int32_t super_on;
   uint32_t super_button;
   int32_t super_charge;   /* hits that fill it */
   int32_t super_count;    /* granules thrown */
   int32_t super_spread;   /* the fan's width, in the engine's angle units (4096 a turn) */
   int32_t super_speed;    /* 16.16 pixels a frame */
   int32_t super_life;     /* frames a granule flies */
   int32_t super_damage;   /* hits a granule costs an enemy */
   int32_t super_frames;   /* the whole attack, in frames (the player stands still and cannot be hurt) */
   int32_t super_release;  /* frames into it when the granules leave */
} hd_weapon_config;
extern hd_weapon_config hd_weapon;
void hd_weapon_default(void);

/*
 * The players' health (format 3's "health"; off in the built-in game, where
 * a hit costs coins): a hit costs one of `hits`, the last one knocks the
 * player out (`knockout` frames, then back at the checkpoint with all of
 * them); with `worn` or fewer left a puppet shows its worn body. After a hit
 * the player cannot be hurt for `invulnerable` frames. With `lives`, each
 * knockout costs one; at none the player gets `cont` seconds to continue
 * with start (all its lives back) or leaves the game (0 lives: unlimited).
 */
typedef struct
{
   int32_t on, hits, worn, knockout;
   int32_t invulnerable, lives, cont;
} hd_health_config;
extern hd_health_config hd_health;

/*
 * The players' dash (format 3's "dash"; off in the built-in game): a button
 * throws the player forward at `speed` for `frames`, gravity off and
 * enemies passed through unhurt, then `cooldown` frames before the next;
 * in the air once until it lands.
 */
typedef struct
{
   int32_t on;
   uint32_t button;
   int32_t speed, frames, cooldown;
} hd_dash_config;
extern hd_dash_config hd_dash;

/*
 * The enemies' kinds. A walker is the built-in game's; the others come with
 * format 3's "enemies" (and a level's "boss"): a spore flies, bobbing, and
 * goes after the nearest player; a spitter stands and spits arcs at
 * players in front of it; a boss is a level's big enemy with a health bar,
 * attacks in turn and a brood of small ones (minions) it lets out now and
 * then; the level's goal opens when it is beaten. A roller rolls at the
 * players and on, turning at walls; a hopper leaps at them; a puffer stands
 * and puffs a fan of spores up into the air; a splitter crawls at them and,
 * beaten, splits into two smaller ones (seq 1) that are beaten for good.
 */
enum { EK_WALKER = 0, EK_SPORE, EK_SPITTER, EK_BOSS, EK_MINION, EK_ROLLER, EK_HOPPER, EK_PUFFER, EK_SPLITTER, EK_COUNT };
typedef struct
{
   int32_t w, h;   /* hitbox */
   int32_t health; /* hits it takes (a walker: the weapon's enemy_health) */
   int32_t speed;  /* 16.16 pixels a frame */
   int32_t rate;   /* frames between two attacks */
   int32_t shot_speed; /* 16.16 pixels a frame */
   int32_t range;  /* pixels: how near a player must be */
   int32_t bob;    /* a spore's bobbing, in pixels */
   int32_t jump;   /* a hopper's leap, 16.16 pixels a frame up */
   int32_t count;  /* a puffer's spores a puff */
} hd_enemy_kind;
extern hd_enemy_kind hd_kinds[EK_COUNT];

/* A boss's attacks, done in the order the level lists them. */
enum { BA_JUMP = 0, BA_CHARGE, BA_SPIT, BA_BROOD, BA_ADVANCE, BA_COUNT };
#define BOSS_ATTACKS_MAX 8
#define BOSS_NAME_MAX 32
typedef struct
{
   int32_t on;
   int32_t attacks, attack[BOSS_ATTACKS_MAX];
   int32_t brood;     /* minions let out at a time (more when angry) */
   int32_t spit;      /* shots in its spit's fan */
   int32_t rest;      /* frames between two attacks (fewer when angry) */
   char name[BOSS_NAME_MAX];
} hd_boss_config;
extern hd_boss_config hd_boss;
/* The player's hitbox (inside its 16 x 24 picture in the built-in game). */
#define PW (hd_phys.pw)
#define PH (hd_phys.ph)
/* An enemy's hitbox (inside its 16 x 16 picture in the built-in game). */
#define EW (hd_phys.ew)
#define EH (hd_phys.eh)

/* A hurt player blinks for HURT_FRAMES and flashes white while hurt > HURT_FLASH. */
#define HURT_FRAMES (hd_health.invulnerable)
#define HURT_FLASH (HURT_FRAMES - 6)
/* frames a game over shows before the title */
#define OVER_FRAMES 240

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
   int32_t shot_wait;        /* frames before the weapon fires again */
   int32_t aim;              /* frames left of the shooting pose */
   int32_t hp;               /* hits it still takes (format 3's health) */
   int32_t ko;               /* frames left of a knockout */
   int32_t charge;           /* the super attack's charge: its shots' hits */
   int32_t super_t;          /* frames into a super attack (0: none) */
   int32_t dash_t;           /* frames into a dash (0: none) */
   int32_t dash_wait;        /* frames before the next dash */
   int32_t dash_air;         /* dashed in the air: no other until it lands */
   int32_t lives;            /* lives left (format 3's health with lives) */
   int32_t cont;             /* frames left to continue after the last life (0: none) */
} hd_player;

typedef struct
{
   int32_t alive; /* 0 gone, 1 walking, 2 squashed */
   int32_t x, y, vx, vy;
   int32_t squash, anim;
   int32_t awake;
   int32_t hp;    /* hits it still takes (format 3's weapon) */
   int32_t flash; /* frames it shows white after a hit */
   int32_t kind;  /* EK_* */
   int32_t t;     /* a timer of its own (a spore's bobbing, a spitter's or a boss's next attack) */
   int32_t home_x, home_y; /* where it started, in pixels */
   int32_t act, act_t;     /* a boss's or a spitter's attack (-1: resting) and frames into it */
   int32_t face;           /* -1 left, 1 right */
   int32_t ground;         /* standing on something */
   int32_t seq;            /* a boss's attacks done so far (the next in its list); a splitter's half: 1 */
} hd_enemy;

/* An enemy's shot: flying (life > 0), falling in an arc when it has gravity. */
typedef struct
{
   int32_t life;
   int32_t x, y, vx, vy; /* its middle, 16.16 */
   int32_t gravity;      /* 16.16 pixels a frame squared */
   int32_t age;
   int32_t big;          /* a boss's (drawn bigger) */
   int32_t puff;         /* a puffer's spore (its own picture) */
} hd_bolt;

/* A weapon's shot: flying (hit 0) or bursting where it hit (hit > 0, frames left). */
#define SHOT_HIT_FRAMES 18
#define SHOT_GRACE 8 /* pixels above or below an enemy a shot still hits */
typedef struct
{
   int32_t life; /* frames left flying; 0 with hit 0: unused */
   int32_t x, y, vx; /* its middle, 16.16 */
   int32_t owner;    /* the player who fired it */
   int32_t hit;
   int32_t age;
   int32_t vy;       /* a granule falls in an arc; a shot flies straight (0) */
   int32_t damage;   /* hits it costs an enemy */
   int32_t granule;  /* 1: a super attack's granule */
} hd_shot;

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
   int32_t skip_hold;   /* frames the jump button has been held on a level's intro (format 3's screens) */
   uint32_t intro_join; /* the players who pressed start on the title, joined when the intro ends */
   int32_t shake, shake_x, shake_y;
   int32_t cam_x, cam_y; /* 16.16, top-left of the screen in the level */
   int32_t music_row, music_tick;
   int32_t music_pos; /* a package's music (format 3): the stereo sample it plays next */
   int32_t stage;     /* the level played, of a package of several (format 3's levels) */
   int32_t part_next, sfx_next;
   uint32_t taken[(MAP_MAX_W * MAP_MAX_H + 31) / 32]; /* coins taken, checkpoints reached */
   hd_player p[MAX_PLAYERS];
   hd_enemy e[MAX_ENEMIES];
   hd_particle part[MAX_PARTICLES];
   hd_shot shot[MAX_SHOTS];
   int32_t shot_next;
   hd_bolt bolt[MAX_BOLTS];
   int32_t bolt_next;
   int32_t boss;         /* the level's boss: its enemy's index + 1 once it woke up, 0 before */
   int32_t boss_max;     /* its health when it woke up (the bar) */
   int32_t boss_angry;   /* it lost half its health */
   int32_t boss_beaten;  /* the goal is open */
   int32_t boss_form;    /* the boss's form in play: 0 the level's, 1 the one it evolved into */
   int32_t arena;        /* the camera's left edge while the boss fights (pixels), 0: none */
   int32_t music_boss;   /* the level's boss music plays (format 3's levels) */
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
extern int32_t hd_enemy_kind_of[MAX_ENEMIES];  /* EK_* of each */
extern int32_t hd_enemy_count;
extern int32_t hd_start_x, hd_start_y;
void hd_level_build(void);

/* content.c: what the loaded game is */
extern char hd_title[64];
/*
 * Format 3's "credits": lines that roll up the screen after the last
 * level's ending, before the title (start or jump skips them after two
 * seconds). A line starting with "# " is a heading; an empty one a gap.
 */
#define CREDITS_MAX 200
#define CREDIT_LEN 64
#define CREDIT_LINE 20 /* logical pixels a line */
extern char hd_credits[CREDITS_MAX][CREDIT_LEN];
extern int32_t hd_credit_count;
/* frames the credits roll: from below the screen until the last line is gone above it, a pixel each two frames */
#define CREDITS_FRAMES ((HD_H + hd_credit_count * CREDIT_LINE + 20) * 2)
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
/* Whether the level's goal is open: always, but in a level with a boss only once it is beaten. */
int hd_goal_open(const hd_state *s);
/* Shows boss form f (0 the level's, 1 its evolved one) in hd_boss, hd_kinds[EK_BOSS] and the boss's pictures. */
void hd_boss_form_use(int32_t f);
int32_t hd_boss_form_now(void);
#define SPIT_AT 24  /* frames into a spitter's attack when its shot leaves */
#define SPIT_END 44 /* and when the attack ends */
#define BOSS_WINDUP(s) ((s)->boss_angry ? 20 : 30) /* a boss's windup before each attack */

/* draw.c */
void hd_draw(const hd_state *s, uint32_t *fb);
/* The zoom's buffer: the screen at 0.5x, the largest it draws. */
#define ZOOM_MAX_W (2 * HD_OUT_MAX_W)
#define ZOOM_MAX_H (2 * HD_OUT_MAX_H)
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
/* A package's own effect in place of a built-in one (mono, HD_RATE). */
void hd_audio_sample(int32_t sfx, const int16_t *data, int32_t len);
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
