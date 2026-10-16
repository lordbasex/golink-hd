/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * TELETRABAJO: CÁMARA APAGADA (golinkhd_load_demo(e, 3), hdrun --demo
 * remote; the design is docs/game-remote-work.md). A remote worker's day
 * in the house of room3d.c, played in third person.
 *
 * Phase 1, walking: the worker walks and runs where the walk grid lets it
 * (play3d), turns toward where it goes, and a follow camera turned with the
 * right stick or L and R stays behind it, risen over walls.
 *
 * Phase 2, doing things: near an activity's place a prompt says what the
 * main button does; pressing it takes the worker there in the activity's
 * pose until it ends or the button is pressed again. Work at the desk adds
 * hours and costs energy; the rest gives energy (most of it) and points,
 * less after the second time. The desk's screens scroll code while working.
 *
 * Phase 3, the day: a clock from 09:00 to 18:00, the boss's suspicion
 * (away from the desk, asleep on the keyboard, events missed), the boss's
 * events (the worker must be at the desk in time, then stays on the call
 * while the boss hears the house), the house's events (a delivery, the cat
 * on the keyboard, a drill, no internet, the flatmate home early), the cat
 * wandering the house, the phone with the last messages, and the day's end:
 * fired, hours missed or survived, with its score.
 */
#include <stdio.h>
#include <string.h>
#include "remote.h"
#include "gfx.h"
#include "text.h"
#include "r3d.h"
#include "actor3d.h"
#include "pet3d.h"
#include "play3d.h"
#include "room3d.h"

#define CM(n) FX_FRAC(n, 100)
#define FPM REMOTE_FRAMES_PER_MINUTE

HD_GAME3D_FITS(hd_remote);

/* ---- the activities ---- */

enum { P_STAND = 0, P_SIT = 1, P_TYPE = 2, P_HOLD = 4, P_CROUCH = 8, P_LIE = 16 };

/* Facing the four ways (actor3d's yaw: the face looks along its own -z). */
enum { FACE_SOUTH = 0, FACE_WEST = 1024, FACE_NORTH = 2048, FACE_EAST = 3072 };

typedef struct
{
   int32_t ux, uz;           /* where the worker stands to start it (centimeters of the plan) */
   int32_t ax, ay, az, ayaw; /* where its body is while doing it, and where it faces */
   int32_t pose;             /* P_* */
   int32_t minutes;          /* game minutes it takes, 0: until it is stopped */
   int32_t energy, points;   /* what it gives when done whole */
} activity;

static const activity activities[RA_COUNT] = {
   [RA_WORK] = { 950, 1035, 950, 0, 1110, FACE_NORTH + 200, P_SIT | P_TYPE, 0, 0, 0 },
   [RA_NAP] = { 255, 1010, 115, 58, 1010, FACE_EAST, P_LIE, 30, 400, 300 },
   [RA_COFFEE] = { 1495, 105, 1495, 0, 105, FACE_EAST, P_HOLD, 3, 120, 30 },
   [RA_COOK] = { 1450, 108, 1450, 0, 108, FACE_SOUTH, P_HOLD, 25, 300, 250 },
   [RA_FRIDGE] = { 1485, 520, 1485, 0, 520, FACE_EAST, P_HOLD, 5, 80, 60 },
   [RA_EAT] = { 971, 300, 971, 0, 236, FACE_SOUTH, P_SIT | P_HOLD, 20, 250, 200 },
   [RA_SERIES] = { 605, 130, 515, 0, 130, FACE_EAST, P_SIT, 25, 100, 250 },
   [RA_CONSOLE] = { 1375, 950, 1375, 0, 1030, FACE_NORTH, P_SIT | P_HOLD, 20, -50, 350 },
   [RA_GUITAR] = { 1470, 1170, 1470, 0, 1170, FACE_SOUTH, P_HOLD, 15, 50, 220 },
   [RA_SUN] = { 650, -60, 640, 38, -138, FACE_WEST, P_LIE, 15, 150, 200 },
   [RA_PLANTS] = { 440, -135, 440, 0, -135, FACE_SOUTH, P_HOLD, 5, 0, 80 },
   [RA_SHOWER] = { 590, 1060, 638, 30, 1204, FACE_NORTH, P_STAND, 15, 200, 150 },
   [RA_WASHING] = { 120, 700, 120, 0, 700, FACE_WEST, P_CROUCH, 5, 0, 100 },
   [RA_DOOR] = { 1540, 730, 1540, 0, 730, FACE_WEST, P_HOLD, 2, 0, 100 },
   [RA_CAT] = { 0, 0, 0, 0, 0, 0, P_CROUCH, 3, 60, 50 },   /* where the cat is */
   [RA_PHONES] = { 880, 1100, 880, 0, 1100, FACE_NORTH, P_HOLD, 1, 0, 0 },
};
#define REACH CM(75) /* how near the place the prompt shows */

/* ---- the boss's events: limit to answer (real seconds; the meeting waits until 10:00), the call's length, suspicion ---- */

static const struct
{
   int32_t seconds, minutes, missed, answered;
} boss_events[RE_BOSS_KINDS] = {
   [RE_MEETING] = { 0, 15, 250, 80 },
   [RE_CALL] = { 45, 10, 300, 100 },
   [RE_MESSAGE] = { 30, 0, 150, 50 },
   [RE_SHARE] = { 20, 5, 200, 80 },
   [RE_HOTFIX] = { 40, 20, 350, 60 },  /* then deploying at the desk; done, it pays REMOTE_HOTFIX_POINTS */
   [RE_WEEKEND] = { 30, 0, 150, 120 }, /* answered "of course" (calm, the weekend sold) or "I have plans" (REMOTE_WEEKEND_NO) */
   [RE_CAMERA] = { 30, 8, 300, 80 },   /* the boss sees the cat, the flatmate, the worker asleep */
};
/*
 * ---- the answers: every ask of the boss is a dialogue of four answers ----
 * A tone says what an answer does: a good one pleases the boss (points, and
 * the calm an answer always brought), an honest one half of that, a joke
 * pleases it more while it is calm and costs when it already suspects, a
 * bad one costs points and raises suspicion. What follows: the event goes
 * on as before (its call), no call, the boss comes back later, a question
 * follows, or the weekend sold, half sold or kept.
 */
enum { OUT_GO = 0, OUT_SKIP, OUT_LATER, OUT_FOLLOW, OUT_WEEKEND, OUT_HALF, OUT_FREE };
enum { DLG_HALF = RE_BOSS_KINDS * 2, DLG_SMALL }; /* the questions that follow "yes, but half time" and "only if it's small" */

/* Each dialogue's answers: their tone and what they lead to (texts: RT_DLG_ANSWER, RT_DLG_REPLY, RT_DLG_FUNNY_BAD). */
static const struct
{
   int8_t tone, out;
} dialogues[REMOTE_DLG_COUNT][REMOTE_DLG_ANSWERS] = {
   /* the daily (a) */
   { { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_FUNNY, OUT_GO }, { REMOTE_TONE_HONEST, OUT_GO }, { REMOTE_TONE_BAD, OUT_GO } },
   /* the daily (b) */
   { { REMOTE_TONE_HONEST, OUT_GO }, { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_BAD, OUT_GO }, { REMOTE_TONE_FUNNY, OUT_GO } },
   /* a call (a) */
   { { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_HONEST, OUT_LATER }, { REMOTE_TONE_FUNNY, OUT_GO }, { REMOTE_TONE_BAD, OUT_GO } },
   /* a call (b) */
   { { REMOTE_TONE_FUNNY, OUT_GO }, { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_BAD, OUT_LATER }, { REMOTE_TONE_HONEST, OUT_GO } },
   /* yesterday's thing */
   { { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_BAD, OUT_GO }, { REMOTE_TONE_HONEST, OUT_GO }, { REMOTE_TONE_FUNNY, OUT_GO } },
   /* the urgent report */
   { { REMOTE_TONE_HONEST, OUT_GO }, { REMOTE_TONE_FUNNY, OUT_GO }, { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_BAD, OUT_GO } },
   /* share your screen */
   { { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_HONEST, OUT_GO }, { REMOTE_TONE_BAD, OUT_SKIP }, { REMOTE_TONE_FUNNY, OUT_GO } },
   /* show me the code */
   { { REMOTE_TONE_FUNNY, OUT_GO }, { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_HONEST, OUT_GO }, { REMOTE_TONE_BAD, OUT_SKIP } },
   /* production down (a) */
   { { REMOTE_TONE_HONEST, OUT_GO }, { REMOTE_TONE_BAD, OUT_GO }, { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_FUNNY, OUT_GO } },
   /* production down (b) */
   { { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_FUNNY, OUT_GO }, { REMOTE_TONE_BAD, OUT_GO }, { REMOTE_TONE_HONEST, OUT_GO } },
   /* the weekend (a) */
   { { REMOTE_TONE_GOOD, OUT_WEEKEND }, { REMOTE_TONE_HONEST, OUT_FOLLOW }, { REMOTE_TONE_FUNNY, OUT_FREE }, { REMOTE_TONE_BAD, OUT_FREE } },
   /* the weekend (b) */
   { { REMOTE_TONE_FUNNY, OUT_FREE }, { REMOTE_TONE_HONEST, OUT_FOLLOW }, { REMOTE_TONE_BAD, OUT_FREE }, { REMOTE_TONE_GOOD, OUT_WEEKEND } },
   /* camera on (a) */
   { { REMOTE_TONE_HONEST, OUT_GO }, { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_FUNNY, OUT_GO }, { REMOTE_TONE_BAD, OUT_GO } },
   /* camera on (b) */
   { { REMOTE_TONE_BAD, OUT_GO }, { REMOTE_TONE_FUNNY, OUT_GO }, { REMOTE_TONE_GOOD, OUT_GO }, { REMOTE_TONE_HONEST, OUT_GO } },
   /* half the weekend: which day */
   { { REMOTE_TONE_GOOD, OUT_HALF }, { REMOTE_TONE_HONEST, OUT_HALF }, { REMOTE_TONE_FUNNY, OUT_HALF }, { REMOTE_TONE_BAD, OUT_FREE } },
   /* a small deploy: two hours */
   { { REMOTE_TONE_HONEST, OUT_HALF }, { REMOTE_TONE_BAD, OUT_FREE }, { REMOTE_TONE_GOOD, OUT_HALF }, { REMOTE_TONE_FUNNY, OUT_HALF } },
};

#define MEETING_WARN 40 /* 09:40: the meeting is announced */
#define MEETING_AT 60   /* 10:00 */

/* ---- the cat's spots: the floor next to it, where it rests (y over the floor), its facing and whether it lies ---- */

enum { SPOT_RUG = 0, SPOT_BED, SPOT_SUN, SPOT_KITCHEN, SPOT_GAMES, SPOT_WANDER, SPOT_KEYS = SPOT_WANDER, SPOT_COUNT };
static const struct
{
   int32_t fx, fz, rx, ry, rz, yaw, lie;
} cat_spots[SPOT_COUNT] = {
   [SPOT_RUG] = { 760, 200, 760, 0, 200, FACE_EAST, 1 },
   [SPOT_BED] = { 250, 1060, 160, 60, 1050, FACE_EAST + 300, 1 },
   [SPOT_SUN] = { 330, -100, 330, 0, -100, FACE_SOUTH, 1 },
   [SPOT_KITCHEN] = { 1300, 520, 1300, 0, 520, FACE_EAST, 0 },
   [SPOT_GAMES] = { 1275, 1100, 1275, 0, 1100, FACE_SOUTH, 1 },
   [SPOT_KEYS] = { 880, 1070, 940, 79, 1192, FACE_SOUTH, 1 },
};
#define CAT_HOP 20                 /* frames of a jump up or down */
#define CAT_SPEED FX_FRAC(11, 1000) /* 0.66 m/s */

/* ---- the flatmate's spots ---- */

static const int32_t mate_spots[][2] = { { 1300, 550 }, { 690, 250 }, { 800, 725 }, { 900, 990 }, { 1300, 960 } };
#define MATE_SPOTS ((int32_t)(sizeof mate_spots / sizeof mate_spots[0]))
#define MATE_SPEED FX_FRAC(20, 1000) /* 1.2 m/s */
#define MATE_ROOM CM(55)             /* the worker cannot walk through it */

int32_t remote_answer_tone(int32_t d, int32_t i)
{
   if (d < 0 || d >= REMOTE_DLG_COUNT || i < 0 || i >= REMOTE_DLG_ANSWERS)
      return -1;
   return dialogues[d][i].tone;
}

int32_t remote_place(int32_t kind, int32_t a, int32_t *x, int32_t *z)
{
   if (kind == REMOTE_PLACE_ACTIVITY && a >= 0 && a < RA_COUNT && a != RA_CAT)
      *x = activities[a].ux, *z = activities[a].uz;
   else if (kind == REMOTE_PLACE_ACTIVITY && a == RA_CAT)
      *x = cat_spots[SPOT_RUG].fx, *z = cat_spots[SPOT_RUG].fz; /* it moves: one of its spots */
   else if (kind == REMOTE_PLACE_CAT && a >= 0 && a < SPOT_COUNT)
      *x = cat_spots[a].fx, *z = cat_spots[a].fz;
   else if (kind == REMOTE_PLACE_MATE && a >= 0 && a < MATE_SPOTS)
      *x = mate_spots[a][0], *z = mate_spots[a][1];
   else
      return 0;
   return 1;
}

/* Where the worker can walk: the house and its terrace, never out of the front door (the grid ends at the outer walls). */
static void ready(void)
{
   static int32_t built = -1;
   r3d_vec lo, hi;
   if (built == room3d_builds())
      return; /* the flat as it was (a package's furniture builds it again) */
   lo.x = ROOM3D_AT(0), lo.y = 0, lo.z = ROOM3D_AT(-210);
   hi.x = ROOM3D_AT(1600), hi.y = 0, hi.z = ROOM3D_AT(1250);
   play3d_grid(lo, hi, 0);
   built = room3d_builds();
}

/* The point the camera follows: the worker's chest. */
static r3d_vec target(const hd_remote *g)
{
   r3d_vec t;
   t.x = g->x;
   t.y = CM(120);
   t.z = g->z;
   return t;
}

/* Where the chest will be in half a second at this speed: the camera rises before a wall comes between. */
static r3d_vec ahead(const hd_remote *g)
{
   r3d_vec t = target(g);
   t.x -= (int32_t)(((int64_t)g->speed * 30 * hd_sin(g->yaw)) >> 14);
   t.z -= (int32_t)(((int64_t)g->speed * 30 * hd_cos(g->yaw)) >> 14);
   return t;
}

static int64_t dist2(int32_t ax, int32_t az, int32_t bx, int32_t bz)
{
   int64_t dx = (int64_t)ax - bx, dz = (int64_t)az - bz;
   return dx * dx + dz * dz;
}

/* Where the worker stands to pet the cat: beside it, or by the furniture it rests on. */
static void cat_floor(const hd_remote *g, int32_t *x, int32_t *z)
{
   if (g->cat_y > 0)
      *x = ROOM3D_AT(cat_spots[g->cat_spot].fx), *z = ROOM3D_AT(cat_spots[g->cat_spot].fz);
   else
      *x = g->cat_x, *z = g->cat_z;
}

/* Whether an activity can be started now (some only exist while their event lasts). */
static int available(const hd_remote *g, int32_t a)
{
   switch (a)
   {
   case RA_DOOR:
      return g->bell_t > 0;
   case RA_CAT:
      return g->cat_mode == CAT_REST && !g->cat_keys;
   case RA_PHONES:
      return g->drill_t > 0 && !g->headphones;
   default:
      return 1;
   }
}

/* The activity whose place is nearest within reach, or -1. */
static int32_t in_reach(const hd_remote *g)
{
   int32_t i, best = -1;
   int64_t best_d = (int64_t)REACH * REACH;
   for (i = 0; i < RA_COUNT; i++)
   {
      int32_t ux = ROOM3D_AT(activities[i].ux), uz = ROOM3D_AT(activities[i].uz);
      int64_t d;
      if (!available(g, i))
         continue;
      if (i == RA_CAT)
         cat_floor(g, &ux, &uz);
      d = dist2(g->x, g->z, ux, uz);
      if (d <= best_d)
         best_d = d, best = i;
   }
   return best;
}

static void toast(hd_remote *g, int32_t text, int32_t points)
{
   g->toast = text + 1;
   g->toast_t = REMOTE_TOAST_FRAMES;
   g->toast_points = points;
}

/* A message on the phone. */
static void phone(hd_remote *g, int32_t who, int32_t text)
{
   int32_t i = g->phone_n % REMOTE_PHONE;
   g->phone_text[i] = text;
   g->phone_who[i] = who;
   g->phone_min[i] = hd_min(g->t / FPM, REMOTE_DAY_MINUTES);
   g->phone_n = hd_min(g->phone_n + 1, 1 << 30);
   g->phone_new_t = 240;
}

static void suspect(hd_remote *g, int32_t by)
{
   g->suspicion = hd_clamp(g->suspicion + by, 0, REMOTE_SUSPICION_MAX);
}

static int at_desk(const hd_remote *g)
{
   return g->act == RA_WORK + 1;
}

static void start(hd_remote *g, int32_t a)
{
   const activity *v = &activities[a];
   g->act = a + 1;
   g->act_t = 0;
   g->act_gain = 0;
   g->speed = 0;
   g->walk = 0;
   if (a == RA_CAT)
   {
      /* right where the worker stands, facing the cat */
      g->yaw = (play3d_angle(g->cat_x - g->x, g->cat_z - g->z) + ANGLE_FULL / 2) & (ANGLE_FULL - 1);
      return;
   }
   if (a == RA_DOOR)
      g->bell_t = 0, g->bell_owed = 0;
   g->x = ROOM3D_AT(v->ax);
   g->z = ROOM3D_AT(v->az);
   g->yaw = v->ayaw & (ANGLE_FULL - 1);
}

/* The activity ends: whole (it ran its time) or stopped early (a share of its points). */
static void finish(hd_remote *g, int whole)
{
   int32_t a = g->act - 1;
   const activity *v = &activities[a];
   if (a == RA_WORK)
   {
      if (g->act_t >= 30 * FPM)
         toast(g, RT_DONE + a, 0);
   }
   else
   {
      int32_t total = v->minutes * FPM;
      int32_t pts = whole ? v->points : (int32_t)((int64_t)v->points * g->act_t / hd_max(1, total));
      int32_t again = g->done[a] >= REMOTE_REPEAT_FULL;
      if (again)
         pts /= 5;
      g->points += pts;
      if (whole || g->act_t * 2 >= total)
         g->done[a]++;
      toast(g, again && pts ? RT_AGAIN : RT_DONE + a, pts);
      if (whole && a == RA_WASHING)
      {
         g->washer_t = REMOTE_WASHER_MINUTES * FPM;
         phone(g, RW_HOUSE, RT_WASHER);
      }
      if (whole && a == RA_PHONES)
         g->headphones = 1;
   }
   g->act = 0;
   if (a != RA_CAT)
   {
      g->x = ROOM3D_AT(v->ux);
      g->z = ROOM3D_AT(v->uz);
   }
}

/* ---- the cat ---- */

static void cat_leave(hd_remote *g, int32_t goal)
{
   g->cat_goal = goal;
   g->cat_mode = g->cat_y > 0 ? CAT_DOWN : CAT_WALK;
   g->cat_t = CAT_HOP;
}

/* The worker takes the cat off the keyboard: it jumps down, offended, and goes somewhere else. */
static void cat_off(hd_state *s, hd_remote *g)
{
   g->cat_keys = 0;
   g->cat_keys_t = 0;
   cat_leave(g, rng_range(&s->rng, SPOT_WANDER));
   toast(g, RT_CAT_MOVED, 0);
}

static void cat_step(hd_state *s, hd_remote *g)
{
   int32_t gx = ROOM3D_AT(cat_spots[g->cat_goal].fx), gz = ROOM3D_AT(cat_spots[g->cat_goal].fz);
   switch (g->cat_mode)
   {
   case CAT_REST:
      g->cat_walk = 0;
      if (g->cat_keys == 2)
      {
         /* on the keyboard: after a while it types something to the boss and logs the worker off */
         if (++g->cat_keys_t == REMOTE_CAT_LOGOFF && !g->cat_logged)
         {
            g->cat_logged = 1;
            phone(g, RW_CAT, RT_CAT_TYPED);
            phone(g, RW_BOSS, RT_CAT_LOGOFF);
            suspect(g, REMOTE_NOISE);
         }
      }
      else if (g->act == RA_CAT + 1)
         break; /* being petted: it stays */
      else if (g->cat_keys == 1)
         cat_leave(g, SPOT_KEYS); /* the keyboard calls */
      else if (--g->cat_t <= 0)
      {
         int32_t next = rng_range(&s->rng, SPOT_WANDER - 1);
         cat_leave(g, next >= g->cat_spot ? next + 1 : next);
      }
      break;
   case CAT_UP:
   case CAT_DOWN:
   {
      /* a jump between the floor beside a spot and the spot: an arc 30 cm over the higher end */
      int32_t sp = g->cat_mode == CAT_UP ? g->cat_goal : g->cat_spot;
      int32_t f = (CAT_HOP - g->cat_t) * 256 / CAT_HOP, k = g->cat_mode == CAT_UP ? f : 256 - f;
      int32_t fx = ROOM3D_AT(cat_spots[sp].fx), fz = ROOM3D_AT(cat_spots[sp].fz), rx = ROOM3D_AT(cat_spots[sp].rx), rz = ROOM3D_AT(cat_spots[sp].rz);
      g->cat_x = fx + (int32_t)((int64_t)(rx - fx) * k / 256);
      g->cat_z = fz + (int32_t)((int64_t)(rz - fz) * k / 256);
      g->cat_y = (int32_t)((int64_t)CM(cat_spots[sp].ry) * k / 256) + (int32_t)((int64_t)CM(30) * f * (256 - f) / 16384);
      g->cat_yaw = (play3d_angle(rx - fx, rz - fz) + (g->cat_mode == CAT_UP ? ANGLE_FULL / 2 : 0)) & (ANGLE_FULL - 1);
      if (--g->cat_t > 0)
         break;
      if (g->cat_mode == CAT_DOWN)
      {
         g->cat_y = 0;
         g->cat_mode = CAT_WALK;
         break;
      }
      g->cat_spot = g->cat_goal;
      g->cat_y = CM(cat_spots[sp].ry);
      g->cat_mode = CAT_REST;
      g->cat_yaw = cat_spots[sp].yaw & (ANGLE_FULL - 1);
      g->cat_t = (20 + rng_range(&s->rng, 25)) * 60;
      if (g->cat_goal == SPOT_KEYS && g->cat_keys == 1)
      {
         g->cat_keys = 2;
         g->cat_keys_t = 0;
         phone(g, RW_HOUSE, RT_CAT_KEYS);
      }
      break;
   }
   default:
   {
      int32_t dx, dz;
      if (g->cat_keys == 1 && g->cat_goal != SPOT_KEYS)
      {
         g->cat_goal = SPOT_KEYS;
         gx = ROOM3D_AT(cat_spots[SPOT_KEYS].fx), gz = ROOM3D_AT(cat_spots[SPOT_KEYS].fz);
      }
      if (dist2(g->cat_x, g->cat_z, gx, gz) < (int64_t)CM(8) * CM(8) || !play3d_route(0, g->cat_x, g->cat_z, gx, gz, &dx, &dz))
      {
         int32_t sp = g->cat_goal;
         g->cat_x = gx, g->cat_z = gz;
         g->cat_walk = 0;
         if (cat_spots[sp].ry || cat_spots[sp].rx != cat_spots[sp].fx || cat_spots[sp].rz != cat_spots[sp].fz)
         {
            g->cat_mode = CAT_UP;
            g->cat_t = CAT_HOP;
         }
         else
         {
            g->cat_mode = CAT_UP; /* the same arrival, without a jump */
            g->cat_t = 1;
         }
         break;
      }
      g->cat_yaw = play3d_turn(g->cat_yaw, (play3d_angle(dx, dz) + ANGLE_FULL / 2) & (ANGLE_FULL - 1), 160);
      play3d_move(&g->cat_x, &g->cat_z, (int32_t)(((int64_t)CAT_SPEED * dx) >> 14), (int32_t)(((int64_t)CAT_SPEED * dz) >> 14), CM(12));
      g->cat_walk = 16384;
      g->cat_phase = (g->cat_phase + 150) & (ANGLE_FULL - 1);
      break;
   }
   }
}

/* ---- the flatmate ---- */

static void mate_step(hd_state *s, hd_remote *g)
{
   int32_t gx = ROOM3D_AT(mate_spots[g->mate_goal][0]), gz = ROOM3D_AT(mate_spots[g->mate_goal][1]), dx, dz;
   int64_t near = dist2(g->mate_x, g->mate_z, g->x, g->z);
   if (g->mate_talk_t > 0)
      g->mate_talk_t--;
   else if (near < (int64_t)FX(2) * FX(2))
   {
      /* it talks to the worker whenever it is near */
      phone(g, RW_MATE, RT_MATE_TALK + rng_range(&s->rng, 4));
      g->mate_talk_t = 20 * 60;
   }
   g->mate_walk = 0;
   if (g->mate_t > 0)
   {
      g->mate_t--;
      return;
   }
   if (dist2(g->mate_x, g->mate_z, gx, gz) < (int64_t)CM(10) * CM(10) || !play3d_route(1, g->mate_x, g->mate_z, gx, gz, &dx, &dz))
   {
      int32_t next = rng_range(&s->rng, MATE_SPOTS - 1);
      g->mate_goal = next >= g->mate_goal ? next + 1 : next;
      g->mate_t = (15 + rng_range(&s->rng, 20)) * 60;
      return;
   }
   g->mate_yaw = play3d_turn(g->mate_yaw, (play3d_angle(dx, dz) + ANGLE_FULL / 2) & (ANGLE_FULL - 1), 140);
   if (near < (int64_t)(MATE_ROOM + CM(10)) * (MATE_ROOM + CM(10)))
      return; /* the worker is in the way: it waits */
   play3d_move(&g->mate_x, &g->mate_z, (int32_t)(((int64_t)MATE_SPEED * dx) >> 14), (int32_t)(((int64_t)MATE_SPEED * dz) >> 14), PLAY3D_ROUTE_RADIUS);
   g->mate_walk = 16384;
   g->mate_phase = (g->mate_phase + 72) & (ANGLE_FULL - 1);
}

/* ---- the day ---- */

static void end_day(hd_remote *g, int32_t how)
{
   int32_t worked = g->work / REMOTE_WORK_UNIT;
   g->over = how;
   g->over_t = 0;
   if (how == REND_FIRED)
   {
      g->score = g->points;
      phone(g, RW_BOSS, RT_END_FIRED_MSG);
   }
   else if (how == REND_MISSED)
      g->score = g->points / 2;
   else
      g->score = g->points + 1000 + (REMOTE_SUSPICION_MAX - g->suspicion) - 2 * hd_max(0, worked - REMOTE_OVERTIME);
   if (g->weekend == 1 && how != REND_FIRED)
      g->score -= REMOTE_WEEKEND_COST; /* the weekend sold */
   if (g->weekend == 3 && how != REND_FIRED)
      g->score -= REMOTE_WEEKEND_HALF_COST; /* half of it */
   g->screen = RSC_END;
   g->screen_t = 0;
   g->best_new = g->score > g->best && !g->attract;
   if (g->best_new)
      g->best = g->score;
}

/* The day's events, from the seed: the meeting, 3 to 5 more from the boss and 2 to 4 from the house, spread from 10:15 to 17:35. */
static void plan_day(hd_state *s, hd_remote *g)
{
   int32_t kinds[REMOTE_EVENTS_MAX], house[RE_KINDS - RE_BOSS_KINDS], nb, nr, n, i, span;
   nb = 3 + rng_range(&s->rng, 3);
   nr = 2 + rng_range(&s->rng, 3);
   for (i = 0; i < RE_KINDS - RE_BOSS_KINDS; i++)
      house[i] = RE_BOSS_KINDS + i;
   for (i = RE_KINDS - RE_BOSS_KINDS - 1; i > 0; i--)
   {
      int32_t j = rng_range(&s->rng, i + 1), t = house[i];
      house[i] = house[j], house[j] = t;
   }
   n = 0;
   for (i = 0; i < nb; i++)
   {
      int32_t k = RE_CALL + rng_range(&s->rng, RE_BOSS_KINDS - 1), j;
      for (j = 0; j < n; j++)
         if (kinds[j] == k && (k == RE_WEEKEND || k == RE_HOTFIX))
            k = RE_CALL; /* the weekend is asked once, production falls once */
      kinds[n++] = k;
   }
   for (i = 0; i < nr; i++)
      kinds[n++] = house[i];
   for (i = n - 1; i > 0; i--)
   {
      int32_t j = rng_range(&s->rng, i + 1), t = kinds[i];
      kinds[i] = kinds[j], kinds[j] = t;
   }
   g->ev_kind[0] = RE_MEETING;
   g->ev_at[0] = MEETING_WARN;
   span = (515 - 75) / n;
   for (i = 0; i < n; i++)
   {
      g->ev_kind[i + 1] = kinds[i];
      g->ev_at[i + 1] = 75 + i * span + rng_range(&s->rng, span - 10);
   }
   g->ev_count = n + 1;
}

static void fire(hd_state *s, hd_remote *g, int32_t kind)
{
   int32_t ask;
   if (kind < RE_BOSS_KINDS)
   {
      g->boss = kind + 1;
      g->boss_call = 0;
      g->boss_t = kind == RE_MEETING ? hd_max(10 * 60, (MEETING_AT - g->t / FPM) * FPM) : boss_events[kind].seconds * 60;
      g->boss_limit = g->boss_t;
      ask = kind * 2 + rng_range(&s->rng, 2);
      g->ask = ask;
      phone(g, RW_BOSS, RT_BOSS_ASK + ask);
      /* the worker reads it, then thinks what it really thinks of it */
      g->muse = RT_BOSS_MUSE + ask + 1;
      g->muse_t = REMOTE_MUSE_FRAMES;
      return;
   }
   switch (kind)
   {
   case RE_DELIVERY:
      g->bell_t = REMOTE_BELL_FRAMES;
      phone(g, RW_HOUSE, RT_BELL);
      break;
   case RE_CAT:
      g->cat_keys = 1; /* it goes as soon as it can (cat_step) */
      g->cat_logged = 0;
      break;
   case RE_DRILL:
      g->drill_t = REMOTE_DRILL_MINUTES * FPM;
      phone(g, RW_HOUSE, RT_DRILL);
      break;
   case RE_NET:
      g->net_t = REMOTE_NET_MINUTES * FPM;
      phone(g, RW_HOUSE, RT_NET);
      break;
   default:
      g->mate = 1;
      g->mate_x = ROOM3D_AT(1540), g->mate_z = ROOM3D_AT(730);
      g->mate_yaw = FACE_WEST;
      g->mate_goal = 2;
      g->mate_t = 0;
      g->mate_talk_t = 0;
      phone(g, RW_MATE, RT_MATE_HOME);
      break;
   }
}

/* What the boss hears on a call, once each: the house's noises (and snoring). */
static void on_call(hd_remote *g)
{
   static const int32_t rise[REMOTE_HEARD] = { REMOTE_NOISE, REMOTE_NOISE, REMOTE_NOISE, 0, REMOTE_NOISE, REMOTE_NOISE, REMOTE_NOISE, REMOTE_NOISE };
   int32_t heard[REMOTE_HEARD], i, camera = g->boss == RE_CAMERA + 1;
   heard[0] = g->washer_t > 0;
   heard[1] = g->bell_owed;
   heard[2] = g->mate && g->mate_x > ROOM3D_AT(750) && g->mate_x < ROOM3D_AT(1150) && g->mate_z > ROOM3D_AT(800);
   heard[3] = g->drill_t > 0 && !g->headphones;
   heard[4] = g->energy < REMOTE_WORK_ASLEEP;
   /* with the camera on the boss also sees: the cat on the desk, the flatmate behind (anywhere in the office), the worker asleep */
   heard[5] = camera && (g->cat_keys == 2 || (g->cat_spot == SPOT_KEYS && g->cat_mode == CAT_REST && g->cat_y > 0));
   heard[6] = camera && g->mate && g->mate_x > ROOM3D_AT(750) && g->mate_x < ROOM3D_AT(1150) && g->mate_z > ROOM3D_AT(800);
   heard[7] = camera && g->energy < REMOTE_WORK_ASLEEP;
   for (i = 0; i < REMOTE_HEARD; i++)
      if (heard[i] && !(g->boss_noise & (1 << i)))
      {
         g->boss_noise |= 1 << i;
         if (i == 1)
         {
            phone(g, RW_HOUSE, RT_BELL);
            g->bell_owed = 0;
         }
         phone(g, RW_BOSS, RT_HEARD + i);
         suspect(g, rise[i]);
      }
}

/* How many rows the day's end counts up (the points, what the answers gave, the hours, the weekend...). */
static int32_t end_rows(const hd_remote *g)
{
   int32_t worked = g->work / REMOTE_WORK_UNIT;
   return 1 + (g->dlg_points != 0) + (g->over == REND_MISSED) + (g->over == REND_SURVIVED ? 2 + (worked > REMOTE_OVERTIME) : 0) +
          ((g->weekend == 1 || g->weekend == 3) && g->over != REND_FIRED);
}

/* The boss's ask is answered at the desk, awake: its dialogue opens there. */
static int can_answer(const hd_remote *g)
{
   return g->boss && !g->boss_call && !g->dlg && at_desk(g) && g->energy >= REMOTE_WORK_ASLEEP;
}

/* The boss comes back at `minute` with the same kind of ask (put off): in the day's events, in time order. */
static void put_off(hd_remote *g, int32_t kind, int32_t minute)
{
   int32_t i;
   if (g->ev_count >= REMOTE_EVENTS_MAX)
      return; /* no room: forgotten */
   for (i = g->ev_count; i > g->ev_next && g->ev_at[i - 1] > minute; i--)
   {
      g->ev_at[i] = g->ev_at[i - 1];
      g->ev_kind[i] = g->ev_kind[i - 1];
   }
   g->ev_at[i] = minute;
   g->ev_kind[i] = kind;
   g->ev_count++;
}

/* Answer `i` of dialogue `d` is chosen: said out loud, the boss replies, and what it leads to. */
static void answer(hd_remote *g, int32_t d, int32_t i)
{
   int32_t kind = g->boss - 1, tone = dialogues[d][i].tone, out = dialogues[d][i].out, reply = RT_DLG_REPLY + d * REMOTE_DLG_ANSWERS + i;
   int32_t calm = boss_events[kind].answered * REMOTE_ANSWER_CALM / 100, points = 0;
   if (out == OUT_LATER && (g->postponed & (1 << kind)))
   {
      /* put off once already: the boss calls now, and minds it */
      out = OUT_GO;
      tone = REMOTE_TONE_BAD;
      reply = RT_DLG_NOW;
   }
   if (tone == REMOTE_TONE_FUNNY && g->suspicion >= REMOTE_FUNNY_LIMIT)
   {
      /* the boss is not in the mood for jokes */
      points = REMOTE_DLG_FUNNY_BAD;
      suspect(g, REMOTE_DLG_FUNNY_RISE);
      reply = RT_DLG_FUNNY_BAD + d;
   }
   else if (tone == REMOTE_TONE_GOOD || tone == REMOTE_TONE_FUNNY)
   {
      points = tone == REMOTE_TONE_GOOD ? REMOTE_DLG_GOOD : REMOTE_DLG_FUNNY;
      suspect(g, -calm);
   }
   else if (tone == REMOTE_TONE_HONEST)
   {
      points = REMOTE_DLG_HONEST;
      suspect(g, -calm / 2);
   }
   else
   {
      points = REMOTE_DLG_BAD;
      suspect(g, REMOTE_DLG_BAD_RISE);
   }
   g->points += points;
   g->dlg_points += points;
   phone(g, RW_YOU, RT_DLG_ANSWER + d * REMOTE_DLG_ANSWERS + i);
   phone(g, RW_BOSS, reply);
   g->dlg = 0;
   switch (out)
   {
   case OUT_FOLLOW:
      /* a question follows: another dialogue, the same ask still under way */
      g->dlg = (d == RE_WEEKEND * 2 ? DLG_HALF : DLG_SMALL) + 1;
      g->dlg_sel = 0;
      g->dlg_t = 0;
      return;
   case OUT_WEEKEND:
   case OUT_HALF:
   case OUT_FREE:
      g->weekend = out == OUT_WEEKEND ? 1 : out == OUT_HALF ? 3 : 2;
      g->boss = 0;
      return;
   case OUT_LATER:
      g->postponed |= 1 << kind;
      put_off(g, kind, g->t / FPM + REMOTE_LATER_MINUTES);
      g->boss = 0;
      return;
   case OUT_SKIP:
      g->boss = 0;
      return;
   default:
      if (boss_events[kind].minutes)
      {
         g->boss_call = 1;
         g->boss_t = boss_events[kind].minutes * FPM;
         g->boss_noise = 0;
      }
      else
         g->boss = 0;
   }
}

static void day(hd_state *s, hd_remote *g, int32_t choice)
{
   int minute = (g->t % FPM) == FPM - 1;
   /* the events whose time came; the boss's wait while another one is under way or the internet is down */
   if (g->ev_next < g->ev_count && g->t >= g->ev_at[g->ev_next] * FPM)
   {
      int32_t kind = g->ev_kind[g->ev_next];
      if (kind < RE_BOSS_KINDS && (g->boss || g->net_t))
         g->ev_at[g->ev_next] += 5;
      else
      {
         fire(s, g, kind);
         g->ev_next++;
      }
   }
   /* the boss event under way: waiting for the worker at the desk, then the call */
   if (g->boss)
   {
      int32_t kind = g->boss - 1;
      if (!g->boss_call)
      {
         if (g->dlg)
         {
            /* the dialogue is open: the boss waits for the answer (its time stops) */
            g->dlg_t = hd_min(g->dlg_t + 1, 1 << 20);
            if (choice)
               answer(g, g->dlg - 1, g->dlg_sel);
         }
         else if (can_answer(g))
         {
            g->dlg = g->ask + 1;
            g->dlg_sel = 0;
            g->dlg_t = 0;
         }
         else if (--g->boss_t <= 0)
         {
            if (kind == RE_WEEKEND)
               g->weekend = 1; /* no answer counts as a yes */
            suspect(g, boss_events[kind].missed);
            phone(g, RW_BOSS, RT_BOSS_MISS + kind);
            g->boss = 0;
         }
      }
      else
      {
         on_call(g);
         if (--g->boss_t <= 0)
         {
            if (kind == RE_HOTFIX)
            {
               /* deployed: it works, nobody knows why */
               g->points += REMOTE_HOTFIX_POINTS;
               g->hotfixes++;
               phone(g, RW_BOSS, RT_HOTFIX_DONE);
               toast(g, RT_HOTFIX_DONE, REMOTE_HOTFIX_POINTS);
            }
            g->boss = 0, g->boss_call = 0;
         }
      }
   }
   /* the house's timers */
   if (g->bell_t > 0 && --g->bell_t == 0)
   {
      g->bell_owed = 1;
      phone(g, RW_HOUSE, RT_BELL_GONE);
   }
   if (g->drill_t > 0 && --g->drill_t == 0)
      phone(g, RW_HOUSE, RT_DRILL_END);
   if (g->net_t > 0 && --g->net_t == 0)
      phone(g, RW_HOUSE, RT_NET_BACK);
   if (g->washer_t > 0)
      g->washer_t--;
   /* suspicion: away from the desk, asleep on it, or working (nobody watches while the internet is down) */
   g->away_t = at_desk(g) ? 0 : hd_min(g->away_t + 1, 1 << 30);
   if (minute && !g->net_t)
   {
      if (at_desk(g))
      {
         if (g->energy < REMOTE_WORK_ASLEEP)
            suspect(g, REMOTE_ASLEEP_RISE);
         else if ((g->t / FPM) % REMOTE_WORK_CALM_EVERY == 0)
            suspect(g, -REMOTE_WORK_CALM);
      }
      else if (g->away_t > REMOTE_AWAY_GRACE * FPM)
         suspect(g, REMOTE_AWAY_RISE);
   }
   /* now and then the boss writes something that needs no answer */
   if (!g->boss && !g->net_t && minute && g->t / FPM >= g->chatter_at)
   {
      phone(g, RW_BOSS, RT_CHATTER + rng_range(&s->rng, REMOTE_CHATTER));
      g->chatter_at = g->t / FPM + 40 + rng_range(&s->rng, 40);
   }
   cat_step(s, g);
   if (g->mate)
      mate_step(s, g);
   if (g->phone_new_t > 0)
      g->phone_new_t--;
   /* the end: fired at once, or 18:00 */
   if (g->suspicion >= REMOTE_SUSPICION_MAX)
      end_day(g, REND_FIRED);
   else if (g->t + 1 >= REMOTE_DAY_MINUTES * FPM)
      end_day(g, g->work / REMOTE_WORK_UNIT >= REMOTE_WORK_GOAL ? REND_SURVIVED : REND_MISSED);
}

/* The chat status the boss sees now. */
static int32_t status(const hd_remote *g)
{
   if (g->net_t)
      return RS_OFFLINE;
   if (g->boss && g->boss_call)
      return g->boss == RE_MEETING + 1 ? RS_MEETING : g->boss == RE_HOTFIX + 1 ? RS_DEPLOY : RS_CALL;
   if (!at_desk(g) && g->away_t > REMOTE_AWAY_GRACE * FPM)
      return RS_AWAY;
   return RS_ONLINE;
}

/* One frame of an activity under way. */
static void doing(hd_state *s, hd_remote *g, int pressed)
{
   int32_t a = g->act - 1;
   const activity *v = &activities[a];
   int minute = (g->t % FPM) == FPM - 1;
   g->act_t++;
   if (a == RA_WORK)
   {
      /* full speed with energy, half when tired, nothing asleep on the keyboard, with the cat on it or without internet; the drill halves it */
      int32_t rate = g->energy > REMOTE_WORK_TIRED ? 2 : g->energy >= REMOTE_WORK_ASLEEP ? 1 : 0;
      if (g->cat_keys == 2 || g->net_t)
         rate = 0;
      if (g->drill_t && !g->headphones)
         rate /= 2;
      g->work += rate;
      g->screen_scroll += rate;
      if (minute)
         g->energy = hd_max(0, g->energy - REMOTE_WORK_DRAIN);
      if (pressed)
      {
         if (g->cat_keys == 2)
            cat_off(s, g);
         else if (!g->boss_call)
            finish(g, 0);
      }
      return;
   }
   {
      /* the energy comes as it goes, so stopping early keeps what was had */
      int32_t total = v->minutes * FPM;
      int32_t want = (int32_t)((int64_t)v->energy * hd_min(g->act_t, total) / total);
      g->energy = hd_clamp(g->energy + want - g->act_gain, 0, REMOTE_ENERGY_MAX);
      g->act_gain = want;
      if (g->act_t >= total)
      {
         finish(g, 1);
         return;
      }
   }
   if (pressed)
      finish(g, 0);
}

/* ---- a day's start ---- */

/* Everything of a new day, the session's record kept; the worker in the hall. */
static void new_day(hd_state *s, hd_remote *g)
{
   int32_t best = g->best, hero = g->hero;
   memset(g, 0, sizeof *g);
   g->best = best;
   g->hero = hero;
   g->screen = RSC_DAY;
   /* in the hall, facing east toward the front door, the camera behind */
   g->x = ROOM3D_AT(450);
   g->z = ROOM3D_AT(725);
   g->yaw = FACE_EAST;
   g->cam_yaw = 1024;
   g->cam_pitch = 620;
   g->cam_dist = REMOTE_CAM_DIST;
   g->energy = REMOTE_ENERGY_START;
   /* the cat starts the day on the bed */
   g->cat_spot = g->cat_goal = SPOT_BED;
   g->cat_x = ROOM3D_AT(cat_spots[SPOT_BED].rx);
   g->cat_z = ROOM3D_AT(cat_spots[SPOT_BED].rz);
   g->cat_y = CM(cat_spots[SPOT_BED].ry);
   g->cat_yaw = cat_spots[SPOT_BED].yaw;
   g->cat_mode = CAT_REST;
   g->cat_t = 40 * 60;
   plan_day(s, g);
   g->chatter_at = 30 + rng_range(&s->rng, 30);
   phone(g, RW_BOSS, RT_GREETING);
   g->phone_new_t = 0;
   play3d_follow(target(g), ahead(g), g->cam_yaw, g->cam_pitch, REMOTE_CAM_DIST, &g->cam_dist, &g->cam_lift);
}

/* The morning: a new day from a seed mixed with how long the player took (so every day is different), the worker in bed. */
static void start_morning(hd_state *s, hd_remote *g, uint32_t mix)
{
   int32_t i;
   s->rng ^= mix * 2654435761u;
   if (!s->rng)
      s->rng = 1;
   new_day(s, g);
   g->screen = RSC_MORNING;
   g->screen_t = 0;
   g->buttons = PAD_JUMP | PAD_START; /* the press that started it does not skip the morning */
   /* where the day starts: by the bed, facing the bedroom's door, the camera settled behind (the morning flies to it) */
   g->x = ROOM3D_AT(activities[RA_NAP].ux);
   g->z = ROOM3D_AT(activities[RA_NAP].uz);
   g->yaw = FACE_SOUTH;
   g->cam_yaw = 2048;
   g->cam_lift = 0;
   for (i = 0; i < 60; i++)
      play3d_follow(target(g), ahead(g), g->cam_yaw, g->cam_pitch, REMOTE_CAM_DIST, &g->cam_dist, &g->cam_lift);
}

/* The morning is over: the day starts (start_morning already put the worker and the camera in their places). */
static void wake_up(hd_remote *g)
{
   g->screen = RSC_DAY;
   g->screen_t = 0;
}

void hd_remote_new_day(hd_state *s)
{
   hd_remote *g = HD_GAME3D(s, hd_remote);
   ready();
   memset(&s->show, 0, sizeof s->show);
   s->show.on = 3;
   memset(g, 0, sizeof *g);
   new_day(s, g);
}

/* What the host keeps between sessions: "RWK1", the record (4 bytes, little end first), the main character. */
#define KEEP_SIZE 9
static int32_t kept_best, kept_hero;

size_t hd_remote_keep(const hd_state *s, uint8_t *out, size_t size)
{
   const hd_remote *g = HD_GAME3D_C(s, hd_remote);
   uint32_t best = (uint32_t)hd_max(g->best, kept_best);
   if (!out || size < KEEP_SIZE)
      return 0;
   memcpy(out, "RWK1", 4);
   out[4] = (uint8_t)best, out[5] = (uint8_t)(best >> 8), out[6] = (uint8_t)(best >> 16), out[7] = (uint8_t)(best >> 24);
   out[8] = (uint8_t)hd_clamp(g->hero, 0, 1);
   return KEEP_SIZE;
}

int hd_remote_keep_load(hd_state *s, const uint8_t *in, size_t size)
{
   hd_remote *g = HD_GAME3D(s, hd_remote);
   kept_best = kept_hero = 0;
   if (!in || size < KEEP_SIZE || memcmp(in, "RWK1", 4))
      return 0;
   kept_best = hd_clamp((int32_t)((uint32_t)in[4] | (uint32_t)in[5] << 8 | (uint32_t)in[6] << 16 | (uint32_t)in[7] << 24), 0, 1 << 30);
   kept_hero = in[8] ? 1 : 0;
   if (g->screen == RSC_TITLE)
   {
      g->best = hd_max(g->best, kept_best);
      g->hero = kept_hero;
   }
   return 1;
}

void hd_remote_start(hd_state *s)
{
   hd_remote *g = HD_GAME3D(s, hd_remote);
   ready();
   memset(&s->show, 0, sizeof s->show);
   s->show.on = 3;
   memset(g, 0, sizeof *g);
   g->screen = RSC_TITLE;
   g->best = kept_best;
   g->hero = kept_hero;
}

static void to_title(hd_remote *g)
{
   int32_t best = g->best, hero = g->hero;
   memset(g, 0, sizeof *g);
   g->best = best;
   g->hero = hero;
   g->screen = RSC_TITLE;
   g->buttons = PAD_JUMP | PAD_START;
}

/* ---- the demo: a bot walks to activities and does a little of each ---- */

static const int32_t attract_plan[] = { RA_COFFEE, RA_WORK, RA_SERIES, RA_FRIDGE, RA_WORK, RA_CONSOLE, RA_GUITAR, RA_WORK };
#define ATTRACT_PLAN ((int32_t)(sizeof attract_plan / sizeof attract_plan[0]))

static void attract_input(hd_remote *g, hd_input *in)
{
   int32_t want = attract_plan[g->attract_goal % ATTRACT_PLAN], tx, tz, dx, dz;
   memset(in, 0, sizeof *in);
   if (g->dlg)
   {
      /* the boss asks: the first answer, after reading them */
      if (g->dlg_t > 150 && !(g->t & 1))
         in->buttons = PAD_JUMP;
      return;
   }
   if (g->act)
   {
      /* a little of each, a little more of work */
      if (++g->attract_t > (g->act == RA_WORK + 1 ? 420 : 300) && !g->boss_call && !(g->t & 1))
         in->buttons = PAD_JUMP;
      return;
   }
   g->attract_t = 0;
   if (g->prompt == want + 1)
   {
      if (!(g->t & 1))
      {
         in->buttons = PAD_JUMP;
         g->attract_goal++;
      }
      return;
   }
   tx = ROOM3D_AT(activities[want].ux), tz = ROOM3D_AT(activities[want].uz);
   if (!play3d_route(2, g->x, g->z, tx, tz, &dx, &dz))
   {
      int32_t l = play3d_sqrt(dist2(g->x, g->z, tx, tz));
      if (!l)
         return;
      dx = (int32_t)((int64_t)(tx - g->x) * 16384 / l), dz = (int32_t)((int64_t)(tz - g->z) * 16384 / l);
   }
   /* the way as the camera sees it: forward (sin y, cos y), right (cos y, -sin y) */
   in->lx = hd_clamp((int32_t)(((int64_t)dx * hd_cos(g->cam_yaw) - (int64_t)dz * hd_sin(g->cam_yaw)) >> 14) * 2, -32767, 32767);
   in->ly = hd_clamp(-(int32_t)(((int64_t)dx * hd_sin(g->cam_yaw) + (int64_t)dz * hd_cos(g->cam_yaw)) >> 14) * 2, -32767, 32767);
   if (dist2(g->x, g->z, tx, tz) > (int64_t)FX(1) * FX(1))
      in->buttons |= PAD_RUN;
   /* the camera turns slowly behind where the worker goes */
   if (g->speed > 0)
   {
      int32_t d = ((((g->yaw + 2048) & 4095) - g->cam_yaw + 2048) & 4095) - 2048;
      if (d > 40)
         in->buttons |= PAD_L;
      else if (d < -40)
         in->buttons |= PAD_R;
   }
}

/* ---- the day ---- */

static void day_step(hd_state *s, hd_remote *g, const hd_input *in)
{
   uint32_t b = in->buttons;
   int pressed = (b & ~g->buttons & PAD_JUMP) != 0, other = (b & ~g->buttons & PAD_RUN) != 0;
   int32_t dx, dz, tilt, want, top, choice = 0;
   g->buttons = b;
   if (g->dlg)
   {
      /* a dialogue at the desk: up and down choose an answer, A says it (and the worker stays seated) */
      uint32_t nav = b & (PAD_UP | PAD_DOWN), fresh;
      if (in->ly < -16000)
         nav |= PAD_UP;
      if (in->ly > 16000)
         nav |= PAD_DOWN;
      fresh = nav & ~(uint32_t)g->nav;
      g->nav = (int32_t)nav;
      if (fresh & PAD_UP)
         g->dlg_sel = (g->dlg_sel + REMOTE_DLG_ANSWERS - 1) % REMOTE_DLG_ANSWERS;
      if (fresh & PAD_DOWN)
         g->dlg_sel = (g->dlg_sel + 1) % REMOTE_DLG_ANSWERS;
      choice = pressed;
      pressed = other = 0;
   }
   /* the camera: L and R or the right stick turn it, the right stick's up and down tilt it */
   if (b & PAD_L)
      g->cam_yaw += 28;
   if (b & PAD_R)
      g->cam_yaw -= 28;
   if (hd_abs(in->rx) > 6000)
      g->cam_yaw -= in->rx * 36 / 32767;
   if (hd_abs(in->ry) > 6000)
      g->cam_pitch = hd_clamp(g->cam_pitch - in->ry * 14 / 32767, REMOTE_CAM_PITCH_MIN, REMOTE_CAM_PITCH_MAX);
   g->cam_yaw &= ANGLE_FULL - 1;
   if (g->toast_t > 0)
      g->toast_t--;
   if (g->muse_t > 0)
      g->muse_t--;
   if (g->act)
   {
      g->prompt = 0;
      doing(s, g, pressed);
   }
   else
   {
      /* the walk: toward where the stick points as the camera sees it, turning on the way */
      play3d_stick(in->lx, in->ly, b, g->cam_yaw, &dx, &dz);
      tilt = play3d_sqrt((int64_t)dx * dx + (int64_t)dz * dz);
      top = (b & PAD_RUN) ? REMOTE_RUN : REMOTE_WALK;
      want = (int32_t)((int64_t)top * hd_min(tilt, 16384) / 16384);
      if (tilt)
         g->yaw = play3d_turn(g->yaw, (play3d_angle(dx, dz) + ANGLE_FULL / 2) & (ANGLE_FULL - 1), REMOTE_TURN);
      g->speed += hd_clamp(want - g->speed, -REMOTE_ACCEL * 2, REMOTE_ACCEL);
      if (g->speed > 0)
      {
         /* along where the worker faces (its face looks along -z of its own parts); the flatmate is in the way */
         int32_t mx = (int32_t)(((int64_t)-g->speed * hd_sin(g->yaw)) >> 14);
         int32_t mz = (int32_t)(((int64_t)-g->speed * hd_cos(g->yaw)) >> 14);
         int32_t ox = g->x, oz = g->z;
         int moved = play3d_move(&g->x, &g->z, mx, mz, REMOTE_RADIUS);
         if (moved && g->mate && dist2(g->x, g->z, g->mate_x, g->mate_z) < (int64_t)MATE_ROOM * MATE_ROOM &&
             dist2(g->x, g->z, g->mate_x, g->mate_z) < dist2(ox, oz, g->mate_x, g->mate_z))
            g->x = ox, g->z = oz, moved = 0;
         if (!moved)
            g->speed /= 2; /* against a wall: no walking on the spot */
      }
      /* the legs follow the speed: a full walk at walking speed, longer strides running */
      g->walk = hd_min((int32_t)((int64_t)g->speed * 16384 / REMOTE_WALK), 22000);
      g->phase = (g->phase + (int32_t)((int64_t)g->speed * 60 / REMOTE_WALK)) & (ANGLE_FULL - 1);
      g->prompt = in_reach(g) + 1;
      if (pressed && g->prompt)
      {
         if (g->prompt == RA_WORK + 1 && g->cat_keys == 2)
            cat_off(s, g);
         else
            start(g, g->prompt - 1);
      }
   }
   day(s, g, choice);
   play3d_follow(target(g), ahead(g), g->cam_yaw, g->cam_pitch, REMOTE_CAM_DIST, &g->cam_dist, &g->cam_lift);
   g->t++;
}

/* ---- the menus ---- */

/* A menu's line moved by the D-pad or the stick; returns 1 when the line was chosen (A or START), and in *side -1 or 1 when left or right was pressed. */
static int menu_step(hd_remote *g, const hd_input *in, int32_t lines, int32_t *side)
{
   uint32_t b = in->buttons, nav = 0, fresh;
   int chosen;
   if (in->ly < -16000)
      nav |= PAD_UP;
   if (in->ly > 16000)
      nav |= PAD_DOWN;
   if (in->lx < -16000)
      nav |= PAD_LEFT;
   if (in->lx > 16000)
      nav |= PAD_RIGHT;
   nav |= b & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
   fresh = nav & ~(uint32_t)g->nav;
   g->nav = (int32_t)nav;
   if (fresh & PAD_UP)
      g->menu = (g->menu + lines - 1) % lines;
   if (fresh & PAD_DOWN)
      g->menu = (g->menu + 1) % lines;
   *side = (fresh & PAD_LEFT) ? -1 : (fresh & PAD_RIGHT) ? 1 : 0;
   chosen = (b & ~g->buttons & (PAD_JUMP | PAD_START)) != 0;
   g->buttons = b;
   return chosen;
}

static int any_input(const hd_input *in)
{
   return in->buttons || hd_abs(in->lx) > 16000 || hd_abs(in->ly) > 16000;
}

/* ---- the credits: the package's (in the texts' language), or the game's name and its author ---- */

static const char *const credits_builtin[] = { "# TELETRABAJO: CÁMARA APAGADA", "", "Federico Pereira", "", "# go-link HD" };
#define CREDITS_BUILTIN ((int32_t)(sizeof credits_builtin / sizeof credits_builtin[0]))

static int32_t credits_count(void)
{
   return hd_credit_lines() ? hd_credit_lines() : CREDITS_BUILTIN;
}

static const char *credits_line(int32_t i)
{
   return hd_credit_lines() ? hd_credit_line(i) : i < CREDITS_BUILTIN ? credits_builtin[i] : "";
}

/* Frames they roll: from under the screen until the last line is gone, a logical pixel each two frames (the longest language's, so the same in every one). */
static int32_t credits_frames(void)
{
   return (360 + (hd_credit_rows ? hd_credit_rows : CREDITS_BUILTIN) * CREDIT_LINE + 20) * 2;
}

/* ---- the sound: what changed this frame, heard (docs/game-remote-work.md, section 8) ---- */

/*
 * The package's effects by name ("audio" in its manifest). A day without a
 * package (the built-in demo) is silent but for a few built-in blips.
 */
enum
{
   RF_ALARM = 0, RF_RING, RF_MESSAGE, RF_BOSS_MESSAGE, RF_ANSWER, RF_MISSED, RF_HEARD, RF_DOORBELL, RF_DOOR,
   RF_DRILL, RF_WASHER, RF_MEOW, RF_PURR, RF_CAT_KEYS, RF_STEP, RF_TYPING, RF_SNORE,
   RF_COFFEE, RF_COOK, RF_FRIDGE, RF_EAT, RF_TV, RF_CONSOLE, RF_GUITAR, RF_BIRDS, RF_WATER, RF_SHOWER,
   RF_NET_DOWN, RF_NET_UP, RF_HEADPHONES, RF_TICK, RF_HEARTBEAT, RF_DONE,
   RF_UI_MOVE, RF_UI_SELECT, RF_UI_BACK, RF_PAUSE, RF_COUNT_UP, RF_TYPEWRITER, RF_FIRED, RF_MISSED_HOURS, RF_SURVIVED, RF_RECORD,
   RF_COUNT
};
static const char *const effect_names[RF_COUNT] = {
   "alarm", "phone_ring", "message", "boss_message", "call_answer", "call_missed", "heard", "doorbell", "door",
   "drill", "washer", "meow", "purr", "cat_keys", "step", "typing", "snore",
   "coffee", "cook", "fridge", "eat", "tv", "console", "guitar", "birds", "water", "shower",
   "net_down", "net_up", "headphones", "tick", "heartbeat", "done",
   "ui_move", "ui_select", "ui_back", "pause", "count", "typewriter", "fired", "missed_hours", "survived", "record"
};
/* without the package's own: a built-in blip for what must be heard */
static const int8_t effect_builtin[RF_COUNT] = {
   [RF_UI_MOVE] = SFX_PAUSE + 1, [RF_UI_SELECT] = SFX_JOIN + 1, [RF_UI_BACK] = SFX_PAUSE + 1, [RF_PAUSE] = SFX_PAUSE + 1,
   [RF_MESSAGE] = SFX_CHECK + 1, [RF_BOSS_MESSAGE] = SFX_CHECK + 1, [RF_DONE] = SFX_COIN + 1, [RF_RECORD] = SFX_CLEAR + 1,
};
/* the loop each activity makes while it goes on (-1: none) */
static const int8_t activity_loop[RA_COUNT] = {
   [RA_WORK] = -1, [RA_NAP] = RF_SNORE, [RA_COFFEE] = RF_COFFEE, [RA_COOK] = RF_COOK, [RA_FRIDGE] = RF_FRIDGE,
   [RA_EAT] = RF_EAT, [RA_SERIES] = RF_TV, [RA_CONSOLE] = RF_CONSOLE, [RA_GUITAR] = RF_GUITAR, [RA_SUN] = RF_BIRDS,
   [RA_PLANTS] = RF_WATER, [RA_SHOWER] = RF_SHOWER, [RA_WASHING] = -1, [RA_DOOR] = -1, [RA_CAT] = RF_PURR, [RA_PHONES] = -1,
};
/* the loops, each driven on every frame (hd_loop) */
static const int8_t loops[] = { RF_ALARM, RF_RING, RF_DRILL, RF_WASHER, RF_PURR, RF_CAT_KEYS, RF_TYPING, RF_SNORE, RF_COFFEE, RF_COOK,
                                RF_FRIDGE, RF_EAT, RF_TV, RF_CONSOLE, RF_GUITAR, RF_BIRDS, RF_WATER, RF_SHOWER, RF_HEARTBEAT };
#define LOOPS ((int32_t)(sizeof loops / sizeof loops[0]))

enum { TRACK_TITLE = 0, TRACK_DAY, TRACK_CALL, TRACK_END, TRACKS };
static const char *const track_names[TRACKS] = { "title", "day", "call", "end" };

static int32_t effect_sample[RF_COUNT], track_value[TRACKS];

/*
 * The worker's thoughts read aloud: the lines shown on screen that the
 * worker thinks (what an activity left, the answers to the boss, what
 * happens in the house), each the package's recording in every language
 * and for both main characters, named "voice_<m|w>_<en|es|pt>_<key>"
 * (examples/remote/voices.py reads this table and remote_text.c to make
 * them).
 */
static const struct
{
   int16_t text;
   const char *key;
} voiced[] = {
   { RT_DONE + RA_WORK, "done_work" }, { RT_DONE + RA_NAP, "done_nap" }, { RT_DONE + RA_COFFEE, "done_coffee" },
   { RT_DONE + RA_COOK, "done_cook" }, { RT_DONE + RA_FRIDGE, "done_fridge" }, { RT_DONE + RA_EAT, "done_eat" },
   { RT_DONE + RA_SERIES, "done_series" }, { RT_DONE + RA_CONSOLE, "done_console" }, { RT_DONE + RA_GUITAR, "done_guitar" },
   { RT_DONE + RA_SUN, "done_sun" }, { RT_DONE + RA_PLANTS, "done_plants" }, { RT_DONE + RA_SHOWER, "done_shower" },
   { RT_DONE + RA_WASHING, "done_washing" }, { RT_DONE + RA_DOOR, "done_door" }, { RT_DONE + RA_CAT, "done_cat" },
   { RT_DONE + RA_PHONES, "done_phones" }, { RT_AGAIN, "again" }, { RT_CAT_MOVED, "cat_moved" }, { RT_HOTFIX_DONE, "hotfix_done" },
   { RT_DLG_ANSWER + 0, "ans_00_0" }, { RT_DLG_ANSWER + 1, "ans_00_1" }, { RT_DLG_ANSWER + 2, "ans_00_2" }, { RT_DLG_ANSWER + 3, "ans_00_3" },
   { RT_DLG_ANSWER + 4, "ans_01_0" }, { RT_DLG_ANSWER + 5, "ans_01_1" }, { RT_DLG_ANSWER + 6, "ans_01_2" }, { RT_DLG_ANSWER + 7, "ans_01_3" },
   { RT_DLG_ANSWER + 8, "ans_02_0" }, { RT_DLG_ANSWER + 9, "ans_02_1" }, { RT_DLG_ANSWER + 10, "ans_02_2" }, { RT_DLG_ANSWER + 11, "ans_02_3" },
   { RT_DLG_ANSWER + 12, "ans_03_0" }, { RT_DLG_ANSWER + 13, "ans_03_1" }, { RT_DLG_ANSWER + 14, "ans_03_2" }, { RT_DLG_ANSWER + 15, "ans_03_3" },
   { RT_DLG_ANSWER + 16, "ans_04_0" }, { RT_DLG_ANSWER + 17, "ans_04_1" }, { RT_DLG_ANSWER + 18, "ans_04_2" }, { RT_DLG_ANSWER + 19, "ans_04_3" },
   { RT_DLG_ANSWER + 20, "ans_05_0" }, { RT_DLG_ANSWER + 21, "ans_05_1" }, { RT_DLG_ANSWER + 22, "ans_05_2" }, { RT_DLG_ANSWER + 23, "ans_05_3" },
   { RT_DLG_ANSWER + 24, "ans_06_0" }, { RT_DLG_ANSWER + 25, "ans_06_1" }, { RT_DLG_ANSWER + 26, "ans_06_2" }, { RT_DLG_ANSWER + 27, "ans_06_3" },
   { RT_DLG_ANSWER + 28, "ans_07_0" }, { RT_DLG_ANSWER + 29, "ans_07_1" }, { RT_DLG_ANSWER + 30, "ans_07_2" }, { RT_DLG_ANSWER + 31, "ans_07_3" },
   { RT_DLG_ANSWER + 32, "ans_08_0" }, { RT_DLG_ANSWER + 33, "ans_08_1" }, { RT_DLG_ANSWER + 34, "ans_08_2" }, { RT_DLG_ANSWER + 35, "ans_08_3" },
   { RT_DLG_ANSWER + 36, "ans_09_0" }, { RT_DLG_ANSWER + 37, "ans_09_1" }, { RT_DLG_ANSWER + 38, "ans_09_2" }, { RT_DLG_ANSWER + 39, "ans_09_3" },
   { RT_DLG_ANSWER + 40, "ans_10_0" }, { RT_DLG_ANSWER + 41, "ans_10_1" }, { RT_DLG_ANSWER + 42, "ans_10_2" }, { RT_DLG_ANSWER + 43, "ans_10_3" },
   { RT_DLG_ANSWER + 44, "ans_11_0" }, { RT_DLG_ANSWER + 45, "ans_11_1" }, { RT_DLG_ANSWER + 46, "ans_11_2" }, { RT_DLG_ANSWER + 47, "ans_11_3" },
   { RT_DLG_ANSWER + 48, "ans_12_0" }, { RT_DLG_ANSWER + 49, "ans_12_1" }, { RT_DLG_ANSWER + 50, "ans_12_2" }, { RT_DLG_ANSWER + 51, "ans_12_3" },
   { RT_DLG_ANSWER + 52, "ans_13_0" }, { RT_DLG_ANSWER + 53, "ans_13_1" }, { RT_DLG_ANSWER + 54, "ans_13_2" }, { RT_DLG_ANSWER + 55, "ans_13_3" },
   { RT_DLG_ANSWER + 56, "ans_14_0" }, { RT_DLG_ANSWER + 57, "ans_14_1" }, { RT_DLG_ANSWER + 58, "ans_14_2" }, { RT_DLG_ANSWER + 59, "ans_14_3" },
   { RT_DLG_ANSWER + 60, "ans_15_0" }, { RT_DLG_ANSWER + 61, "ans_15_1" }, { RT_DLG_ANSWER + 62, "ans_15_2" }, { RT_DLG_ANSWER + 63, "ans_15_3" },
   { RT_BOSS_ASK + RE_MEETING * 2, "read_meeting_a" }, { RT_BOSS_ASK + RE_MEETING * 2 + 1, "read_meeting_b" },
   { RT_BOSS_ASK + RE_CALL * 2, "read_call_a" }, { RT_BOSS_ASK + RE_CALL * 2 + 1, "read_call_b" },
   { RT_BOSS_ASK + RE_MESSAGE * 2, "read_message_a" }, { RT_BOSS_ASK + RE_MESSAGE * 2 + 1, "read_message_b" },
   { RT_BOSS_ASK + RE_SHARE * 2, "read_share_a" }, { RT_BOSS_ASK + RE_SHARE * 2 + 1, "read_share_b" },
   { RT_BOSS_ASK + RE_HOTFIX * 2, "read_hotfix_a" }, { RT_BOSS_ASK + RE_HOTFIX * 2 + 1, "read_hotfix_b" },
   { RT_BOSS_ASK + RE_WEEKEND * 2, "read_weekend_a" }, { RT_BOSS_ASK + RE_WEEKEND * 2 + 1, "read_weekend_b" },
   { RT_BOSS_ASK + RE_CAMERA * 2, "read_camera_a" }, { RT_BOSS_ASK + RE_CAMERA * 2 + 1, "read_camera_b" },
   { RT_BOSS_MUSE + RE_MEETING * 2, "muse_meeting_a" }, { RT_BOSS_MUSE + RE_MEETING * 2 + 1, "muse_meeting_b" },
   { RT_BOSS_MUSE + RE_CALL * 2, "muse_call_a" }, { RT_BOSS_MUSE + RE_CALL * 2 + 1, "muse_call_b" },
   { RT_BOSS_MUSE + RE_MESSAGE * 2, "muse_message_a" }, { RT_BOSS_MUSE + RE_MESSAGE * 2 + 1, "muse_message_b" },
   { RT_BOSS_MUSE + RE_SHARE * 2, "muse_share_a" }, { RT_BOSS_MUSE + RE_SHARE * 2 + 1, "muse_share_b" },
   { RT_BOSS_MUSE + RE_HOTFIX * 2, "muse_hotfix_a" }, { RT_BOSS_MUSE + RE_HOTFIX * 2 + 1, "muse_hotfix_b" },
   { RT_BOSS_MUSE + RE_WEEKEND * 2, "muse_weekend_a" }, { RT_BOSS_MUSE + RE_WEEKEND * 2 + 1, "muse_weekend_b" },
   { RT_BOSS_MUSE + RE_CAMERA * 2, "muse_camera_a" }, { RT_BOSS_MUSE + RE_CAMERA * 2 + 1, "muse_camera_b" },
   { RT_BELL, "bell" }, { RT_BELL_GONE, "bell_gone" }, { RT_CAT_KEYS, "cat_keys" }, { RT_DRILL, "drill" }, { RT_DRILL_END, "drill_end" },
   { RT_NET, "net" }, { RT_NET_BACK, "net_back" }, { RT_WASHER, "washer" },
};
#define VOICED ((int32_t)(sizeof voiced / sizeof voiced[0]))
static const char *const voice_langs[LANGS] = { "en", "es", "pt" };
static int32_t voice_sample[2][LANGS][VOICED];

/* The samples and tracks of the content loaded now (looked up again when it changes). */
static void sound_names(void)
{
   static int32_t gen = -1;
   int32_t i;
   if (gen == hd_content_gen)
      return;
   gen = hd_content_gen;
   for (i = 0; i < RF_COUNT; i++)
   {
      effect_sample[i] = hd_effect(effect_names[i]);
      if (effect_sample[i] < 0 && effect_builtin[i])
         effect_sample[i] = effect_builtin[i] - 1;
   }
   for (i = 0; i < TRACKS; i++)
      track_value[i] = hd_track(track_names[i]);
   for (i = 0; i < 2 * LANGS * VOICED; i++)
   {
      char name[48];
      int32_t who = i / (LANGS * VOICED), lang = i / VOICED % LANGS;
      snprintf(name, sizeof name, "voice_%c_%s_%s", who ? 'w' : 'm', voice_langs[lang], voiced[i % VOICED].key);
      voice_sample[who][lang][i % VOICED] = hd_effect(name);
   }
}

/* The recording of a line in the language of the texts and the main character's voice, -1 when there is none. */
static int32_t voice_of(const hd_remote *g, int32_t text)
{
   int32_t i;
   for (i = 0; i < VOICED; i++)
      if (voiced[i].text == text)
         return voice_sample[hd_clamp(g->hero, 0, 1)][hd_clamp(hd_lang, 0, LANGS - 1)][i];
   return -1;
}

/* Output samples until every line said now (and waiting) is over. */
static int32_t voice_left(const hd_state *s)
{
   const int32_t *all = &voice_sample[0][0][0];
   int32_t c, k, left = 0;
   for (c = 0; c < MAX_CHANNELS; c++)
      for (k = 0; k < 2 * LANGS * VOICED; k++)
         if (s->ch[c].sample >= 0 && s->ch[c].sample == all[k])
            left = hd_max(left, hd_sample_len(all[k]) - s->ch[c].pos);
   return left;
}

/* Lines said one after another: a short breath between two. */
#define VOICE_GAP (HD_RATE * 2 / 5)

/*
 * A line said: cutting short the ones before (and those still waiting), or,
 * `queued`, once they are over; `wait` output samples of silence first.
 * Returns how long it lasts, 0 when it has no recording.
 */
static int32_t say(hd_state *s, const hd_remote *g, int32_t text, int32_t vol, int32_t wait, int queued)
{
   const int32_t *all = &voice_sample[0][0][0];
   int32_t k, c, sample = voice_of(g, text);
   if (sample < 0)
      return 0;
   if (queued)
   {
      int32_t left = voice_left(s);
      wait = left ? left + VOICE_GAP : wait;
   }
   else if (!wait)
      for (c = 0; c < MAX_CHANNELS; c++)
         for (k = 0; k < 2 * LANGS * VOICED; k++)
            if (s->ch[c].sample >= 0 && s->ch[c].sample == all[k])
               s->ch[c].sample = -1;
   c = hd_play_at(s, sample, vol, 0, FX_ONE);
   if (c >= 0)
      s->ch[c].pos = -wait;
   return c >= 0 ? hd_sample_len(sample) : 0;
}

/* A thought, cutting short the line before. */
static void think(hd_state *s, const hd_remote *g, int32_t text)
{
   say(s, g, text, 240, 0, 0);
}

/*
 * How loud and where a sound at (x, z) is heard: the worker is the ear (a
 * listener beside the camera), full within 2 meters and fading to `far` at
 * 10; left or right as the camera sees it.
 */
static void heard_at(const hd_remote *g, int32_t x, int32_t z, int32_t vol, int32_t far, int32_t *out_vol, int32_t *pan)
{
   int32_t dx = x - g->x, dz = z - g->z, d = play3d_sqrt((int64_t)dx * dx + (int64_t)dz * dz), k;
   int32_t right = (int32_t)(((int64_t)dx * hd_cos(g->cam_yaw) - (int64_t)dz * hd_sin(g->cam_yaw)) >> 14);
   k = 256 - (int32_t)((int64_t)(256 - far) * hd_clamp(d - FX(2), 0, FX(8)) / FX(8));
   *out_vol = vol * k / 256;
   *pan = d > FX_FRAC(1, 2) ? hd_clamp((int32_t)((int64_t)right * 200 / d), -200, 200) : 0;
}

static void play(hd_state *s, int32_t effect, int32_t vol, int32_t pan, int32_t step)
{
   hd_play_at(s, effect_sample[effect], vol, pan, step);
}

static void play_at(hd_state *s, const hd_remote *g, int32_t effect, int32_t x, int32_t z, int32_t vol, int32_t far)
{
   int32_t v, pan;
   heard_at(g, x, z, vol, far, &v, &pan);
   play(s, effect, v, pan, FX_ONE);
}

/* The music the screen wants: the title's, the day's (a call's while on one), the end's; the day's goes on where it was after a call. */
static void sound_music(hd_state *s, hd_remote *g)
{
   int32_t want;
   if (g->screen == RSC_TITLE || (g->screen == RSC_HELP && g->help_back == RSC_TITLE))
      want = track_value[TRACK_TITLE];
   else if (g->screen == RSC_MORNING)
      want = 0; /* only the alarm */
   else if (g->screen == RSC_END)
      want = g->over_t < 150 ? 0 : track_value[TRACK_END]; /* after the end's jingle */
   else if (g->screen == RSC_CREDITS)
      want = track_value[TRACK_END];
   else if (g->boss && g->boss_call && track_value[TRACK_CALL])
      want = track_value[TRACK_CALL];
   else
      want = track_value[TRACK_DAY];
   if (want == s->music_track)
      return;
   if (s->music_track && s->music_track == track_value[TRACK_DAY])
      g->music_resume = s->music_pos;
   s->music_pos = want && want == track_value[TRACK_DAY] && g->screen == RSC_DAY && g->t > 0 ? g->music_resume : 0;
   if (want == track_value[TRACK_DAY] && (g->screen != RSC_DAY || g->t == 0))
      g->music_resume = 0;
   s->music_track = want;
}

/* The loops: what goes on for as long as it lasts, each at its place. */
static void sound_loops(hd_state *s, const hd_remote *g)
{
   int32_t want[RF_COUNT], pan[RF_COUNT], i, day = g->screen == RSC_DAY && !g->paused && !g->over;
   memset(want, 0, sizeof want);
   memset(pan, 0, sizeof pan);
   if (g->screen == RSC_MORNING && g->screen_t < 170)
      want[RF_ALARM] = 220;
   if (day)
   {
      int32_t a = g->act - 1, desk_x = ROOM3D_AT(activities[RA_WORK].ax), desk_z = ROOM3D_AT(activities[RA_WORK].az);
      /* the computer rings until the worker sits down: heard all over the house */
      if (g->boss && !g->boss_call && !g->dlg && g->boss != RE_MESSAGE + 1 && g->boss != RE_WEEKEND + 1 && (g->boss != RE_MEETING + 1 || g->boss_t < 15 * 60))
         heard_at(g, desk_x, desk_z, 210, 110, &want[RF_RING], &pan[RF_RING]);
      /* the neighbor's drill comes and goes; the headphones take most of it away */
      if (g->drill_t > 0 && (g->t / 80) % 4 != 3)
         want[RF_DRILL] = g->headphones ? 45 : 190;
      if (g->washer_t > 0)
         heard_at(g, ROOM3D_AT(activities[RA_WASHING].ax), ROOM3D_AT(activities[RA_WASHING].az), 170, 60, &want[RF_WASHER], &pan[RF_WASHER]);
      if (g->cat_keys == 2)
         heard_at(g, desk_x, desk_z, 150, 40, &want[RF_CAT_KEYS], &pan[RF_CAT_KEYS]);
      if (a == RA_WORK && g->energy >= REMOTE_WORK_ASLEEP && !g->net_t && g->cat_keys != 2)
         want[RF_TYPING] = 130;
      if (a == RA_WORK && g->energy < REMOTE_WORK_ASLEEP)
         want[RF_SNORE] = 170;
      if (a >= 0 && activity_loop[a] >= 0)
         want[activity_loop[a]] = 190;
      /* close to being fired: a heartbeat, louder the closer */
      if (g->suspicion > 800)
         want[RF_HEARTBEAT] = 90 + (g->suspicion - 800) * 3 / 4;
   }
   for (i = 0; i < LOOPS; i++)
      hd_loop(s, effect_sample[(int32_t)loops[i]], want[(int32_t)loops[i]], pan[(int32_t)loops[i]]);
}

/* The newest message on the phone, and who wrote it. */
static int32_t newest(const hd_remote *g, int32_t *who)
{
   int32_t k = (g->phone_n + REMOTE_PHONE - 1) % REMOTE_PHONE;
   *who = g->phone_who[k];
   return g->phone_text[k];
}

/* What happened in the day this frame. */
static void sound_day(hd_state *s, const hd_remote *was, const hd_remote *g)
{
   int32_t who, text, k;
   int32_t door_x = ROOM3D_AT(activities[RA_DOOR].ax), door_z = ROOM3D_AT(activities[RA_DOOR].az);
   if (g->phone_n > was->phone_n)
   {
      text = newest(g, &who);
      if (text >= RT_BOSS_MISS && text < RT_BOSS_MISS + RE_BOSS_KINDS)
         play(s, RF_MISSED, 220, 0, FX_ONE);
      else if (who == RW_YOU)
         play(s, RF_ANSWER, 200, 0, FX_ONE);
      else if (text >= RT_HEARD && text < RT_HEARD + REMOTE_HEARD)
         play(s, RF_HEARD, 230, 0, FX_ONE);
      else if (text == RT_BELL && was->boss_call)
         play_at(s, g, RF_DOORBELL, door_x, door_z, 240, 120); /* the delivery, back during the call */
      else if (who != RW_MATE)
         play(s, who == RW_BOSS ? RF_BOSS_MESSAGE : RF_MESSAGE, 170, 0, FX_ONE);
   }
   /* the voice: every message of the frame (an answer and the boss's reply to it come together) */
   for (k = hd_max(was->phone_n, g->phone_n - REMOTE_PHONE); k < g->phone_n; k++)
   {
      text = g->phone_text[k % REMOTE_PHONE];
      who = g->phone_who[k % REMOTE_PHONE];
      if (who == RW_YOU)
         say(s, g, text, 230, 0, 1); /* the answer to the boss, once the ask was read and thought over */
      else if (who == RW_HOUSE)
         think(s, g, text);
      else if (text >= RT_BOSS_ASK && text < RT_BOSS_ASK + RE_BOSS_KINDS * 2)
      {
         /* the boss's ask read aloud, then what the worker thinks of it */
         int32_t read = say(s, g, text, 220, 0, 0);
         if (read)
            say(s, g, RT_BOSS_MUSE + text - RT_BOSS_ASK, 240, read + VOICE_GAP, 0);
      }
   }
   /* an urgent message pings again every two and a half seconds until it is answered */
   if ((g->boss == RE_MESSAGE + 1 || g->boss == RE_WEEKEND + 1) && !g->boss_call && !g->dlg && was->boss == g->boss && g->boss_t % 150 == 0)
      play(s, RF_BOSS_MESSAGE, 170, 0, FX_ONE);
   /* the last seconds to answer tick */
   if (g->boss && !g->boss_call && !g->dlg && g->boss_t <= 5 * 60 && g->boss_t > 0 && g->boss_t % 60 == 0)
      play(s, RF_TICK, 200, 0, FX_ONE);
   /* the answers: a blip when the dialogue opens and when the choice moves */
   if (g->dlg && (!was->dlg || was->dlg_sel != g->dlg_sel))
      play(s, RF_UI_MOVE, 160, 0, FX_ONE);
   /* the bell: when it starts, and again every four seconds while the delivery waits */
   if (g->bell_t > 0 && (REMOTE_BELL_FRAMES - g->bell_t) % 240 == 0)
      play_at(s, g, RF_DOORBELL, door_x, door_z, 240, 120);
   if (g->act == RA_DOOR + 1 && was->act != g->act)
      play_at(s, g, RF_DOOR, door_x, door_z, 200, 80);
   if (g->mate && !was->mate)
      play_at(s, g, RF_DOOR, door_x, door_z, 200, 80);
   if (g->net_t && !was->net_t)
      play(s, RF_NET_DOWN, 200, 0, FX_ONE);
   if (!g->net_t && was->net_t)
      play(s, RF_NET_UP, 200, 0, FX_ONE);
   if (g->headphones && !was->headphones)
      play(s, RF_HEADPHONES, 200, 0, FX_ONE);
   if (g->toast_points > 0 && g->toast_t == REMOTE_TOAST_FRAMES)
      play(s, RF_DONE, 170, 0, FX_ONE);
   if (g->toast && g->toast_t == REMOTE_TOAST_FRAMES)
      think(s, g, g->toast - 1);
   /* the cat: a meow when the keyboard calls it, a cross one when it is taken off, now and then one when it arrives somewhere */
   if (g->cat_keys == 1 && was->cat_keys == 0)
      play_at(s, g, RF_MEOW, g->cat_x, g->cat_z, 220, 90);
   else if (g->cat_keys == 0 && was->cat_keys == 2)
      play(s, RF_MEOW, 230, 0, FX_FRAC(85, 100));
   else if (g->cat_mode == CAT_REST && was->cat_mode == CAT_UP && g->cat_spot % 2 == 0)
      play_at(s, g, RF_MEOW, g->cat_x, g->cat_z, 160, 50);
   /* footsteps: one each half of the walk's cycle, louder running, never two alike */
   if (!g->act && g->walk > 3000 && ((was->phase ^ g->phase) & 2048))
      play(s, RF_STEP, 70 + g->walk / 200, ((g->phase >> 11) & 1) ? -40 : 40, FX_FRAC(92 + (g->t * 7) % 17, 100));
   if (g->mate && g->mate_walk && ((was->mate_phase ^ g->mate_phase) & 2048))
   {
      int32_t v, pan;
      heard_at(g, g->mate_x, g->mate_z, 110, 20, &v, &pan);
      play(s, RF_STEP, v, pan, FX_FRAC(80 + (g->t * 5) % 11, 100));
   }
}

/* The end: the boss's last words typed, the rows counting, then the verdict and the record (the timing of end_hud). */
static void sound_end(hd_state *s, const hd_remote *g)
{
   int32_t start = g->over == REND_FIRED ? 150 : 30, k = g->over_t - start, rows, total_at;
   if (g->over == REND_FIRED && k < 0)
   {
      if (g->over_t % 4 == 0 && g->over_t / 2 <= text_glyphs(remote_text(RT_END_FIRED_MSG)))
         play(s, RF_TYPEWRITER, 150, 0, FX_FRAC(90 + (g->over_t * 13) % 21, 100));
      return;
   }
   rows = end_rows(g);
   total_at = rows * 30;
   if (k >= 0 && k < total_at + 40 && k % 3 == 0)
      play(s, RF_COUNT_UP, 110, 0, FX_ONE + (k < total_at ? (k % 30) * 600 : (k - total_at) * 500));
   if (k == total_at + 50)
      play(s, g->over == REND_FIRED ? RF_FIRED : g->over == REND_MISSED ? RF_MISSED_HOURS : RF_SURVIVED, 230, 0, FX_ONE);
   if (k == total_at + 70 && g->best_new)
      play(s, RF_RECORD, 230, 0, FX_ONE);
}

static void remote_sound(hd_state *s, const hd_remote *was, hd_remote *g, int32_t lang_was)
{
   sound_names();
   sound_music(s, g);
   sound_loops(s, g);
   /* the menus */
   if ((g->screen == RSC_TITLE || g->paused) && g->screen == was->screen && g->paused == was->paused && (g->menu != was->menu || hd_lang != lang_was))
      play(s, RF_UI_MOVE, 160, 0, FX_ONE);
   if (g->screen == RSC_HELP && (was->screen != RSC_HELP || g->help_page != was->help_page))
      play(s, RF_UI_SELECT, 160, 0, FX_ONE);
   if ((g->screen == RSC_MORNING || g->screen == RSC_CREDITS) && was->screen != g->screen)
      play(s, RF_UI_SELECT, 180, 0, FX_ONE);
   if (g->paused && !was->paused)
      play(s, RF_PAUSE, 180, 0, FX_ONE);
   else if (!g->paused && was->paused && g->screen == RSC_DAY)
      play(s, RF_UI_BACK, 160, 0, FX_ONE);
   if (g->screen == RSC_TITLE && was->screen != RSC_TITLE && was->screen != RSC_HELP)
      play(s, RF_UI_BACK, 160, 0, FX_ONE);
   if (g->screen == RSC_DAY && was->screen == RSC_DAY && !g->paused)
      sound_day(s, was, g);
   if (g->screen == RSC_END && !g->attract)
      sound_end(s, g);
}

void hd_remote_step(hd_state *s, const hd_input in[MAX_PLAYERS])
{
   hd_remote *g = HD_GAME3D(s, hd_remote);
   hd_remote was = *g;
   uint32_t b = in[0].buttons;
   int32_t side, lang = hd_lang;
   ready();
   remote_text_hero = g->hero;
   g->screen_t = hd_min(g->screen_t + 1, 1 << 30);
   switch (g->screen)
   {
   case RSC_TITLE:
      if (menu_step(g, &in[0], 5, &side))
      {
         if (g->menu == 0)
            start_morning(s, g, (uint32_t)g->screen_t);
         else if (g->menu == 2)
            hd_lang = (hd_lang + 1) % LANGS; /* the texts' language: the host's by default, the player's from here */
         else if (g->menu == 3)
            g->hero ^= 1;
         else if (g->menu == 4)
         {
            g->screen = RSC_CREDITS;
            g->screen_t = 0;
         }
         else
         {
            g->screen = RSC_HELP;
            g->help_page = 0;
            g->help_back = RSC_TITLE;
         }
      }
      else if (g->menu == 2 && side)
         hd_lang = (hd_lang + LANGS + side) % LANGS;
      else if (g->menu == 3 && side)
         g->hero ^= 1;
      if (g->screen != RSC_TITLE)
         break;
      if (any_input(&in[0]))
         g->screen_t = 0;
      else if (g->screen_t > REMOTE_ATTRACT_AFTER)
      {
         /* nobody plays: the demo, a short stretch of a day */
         new_day(s, g);
         g->attract = 1;
         g->buttons = 0;
      }
      break;
   case RSC_HELP:
      if (b & ~g->buttons & (PAD_JUMP | PAD_START))
      {
         if (++g->help_page >= REMOTE_HELP_PAGES || (b & ~g->buttons & PAD_START))
         {
            g->screen = g->help_back;
            g->menu = 0;
         }
      }
      g->buttons = b;
      break;
   case RSC_CREDITS:
      /* they roll up to the end, or A or START after two seconds; then the title */
      if (g->screen_t >= credits_frames() || (g->screen_t > 120 && (b & ~g->buttons & (PAD_JUMP | PAD_START))))
         to_title(g);
      else
         g->buttons = b;
      break;
   case RSC_MORNING:
      if ((b & ~g->buttons & (PAD_JUMP | PAD_START)) || g->screen_t >= REMOTE_MORNING_FRAMES)
         wake_up(g);
      g->buttons = b;
      break;
   case RSC_END:
      g->over_t = hd_min(g->over_t + 1, 1 << 30);
      if (g->attract)
         to_title(g);
      else if (g->over_t > 90 && (b & ~g->buttons & PAD_START))
         start_morning(s, g, (uint32_t)g->over_t);
      else if (g->over_t > 90 && (b & ~g->buttons & PAD_JUMP) && g->over == REND_SURVIVED)
      {
         /* the day was won: the credits roll, then the title */
         g->screen = RSC_CREDITS;
         g->screen_t = 0;
         g->buttons = b;
      }
      else if (g->over_t > 90 && (b & ~g->buttons & PAD_JUMP))
         to_title(g);
      else
         g->buttons = b;
      break;
   default:
      if (g->attract)
      {
         /* the demo: any button back to the title, or its time is up */
         hd_input bot;
         if (any_input(&in[0]) || g->t >= REMOTE_ATTRACT_FRAMES)
         {
            to_title(g);
            break;
         }
         attract_input(g, &bot);
         day_step(s, g, &bot);
         break;
      }
      if (g->paused)
      {
         if (menu_step(g, &in[0], 4, &side))
         {
            if (g->menu == 0)
               g->paused = 0;
            else if (g->menu == 1)
            {
               g->screen = RSC_HELP;
               g->help_page = 0;
               g->help_back = RSC_DAY;
            }
            else if (g->menu == 2)
               start_morning(s, g, (uint32_t)g->t);
            else
               to_title(g);
         }
         break;
      }
      if (b & ~g->buttons & PAD_START)
      {
         g->paused = 1;
         g->menu = 0;
         g->buttons = b;
         break;
      }
      day_step(s, g, &in[0]);
      break;
   }
   remote_sound(s, &was, g, lang);
}

/* ---- drawing ---- */

/* A see-through panel: color over what is there, alpha 0..256. */
static void panel(hd_surface *sf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c, int32_t alpha)
{
   int32_t i, j;
   for (j = hd_max(y, 0); j < hd_min(y + h, sf->h); j++)
      for (i = hd_max(x, 0); i < hd_min(x + w, sf->w); i++)
         sf->px[j * sf->w + i] = gfx_mix(sf->px[j * sf->w + i], c, alpha);
}

/* A bar of `full` 0..256, with its label above it. */
static void bar(hd_surface *sf, int32_t x, int32_t y, int32_t w, int32_t full, uint32_t c, const char *label, int32_t scale)
{
   text_draw(sf, label, x, y, scale, 0xffffff, 1, -1);
   y += 10 * scale;
   panel(sf, x, y, w, 6 * scale, 0x000000, 140);
   gfx_fill(sf, x + scale, y + scale, (int32_t)((int64_t)(w - 2 * scale) * hd_clamp(full, 0, 256) / 256), 4 * scale, c);
}

/* The byte where a line of at most `cols` letters ends: at the last space that fits, or `cols` letters in when a word is longer. */
static const char *line_end(const char *p, int32_t cols)
{
   const char *q = p, *space = NULL;
   int32_t n = 0;
   while (*q && n < cols)
   {
      if (*q == ' ')
         space = q;
      q++;
      while ((*q & 0xc0) == 0x80)
         q++; /* the rest of a UTF-8 letter */
      n++;
   }
   if (!*q || *q == ' ' || !space)
      return q;
   return space;
}

/* A text wrapped to `cols` letters a line from (x, y); draws only when `draw`; returns how many lines it takes. */
static int32_t wrap(hd_surface *sf, const char *text, int32_t x, int32_t y, int32_t cols, int32_t scale, uint32_t color, int draw)
{
   int32_t lines = 0;
   char buf[160];
   while (*text)
   {
      const char *e = line_end(text, cols);
      size_t len = hd_min((int32_t)(e - text), (int32_t)sizeof buf - 1);
      if (draw)
      {
         memcpy(buf, text, len);
         buf[len] = 0;
         text_draw(sf, buf, x, y + lines * 9 * scale, scale, color, 1, -1);
      }
      lines++;
      text = e;
      while (*text == ' ')
         text++;
   }
   return lines;
}

static const uint32_t who_color[RW_COUNT] = { 0xff8fab, 0x8ecae6, 0xffb703, 0x80ed99, 0xbfc7d5 };
static const uint32_t status_color[RS_COUNT] = { 0x57cc99, 0xf6bd60, 0xe63946, 0xe63946, 0x8d99ae, 0xff9f1c };

/* The phone on the right: the last messages, newest at the bottom, and the countdown when the boss waits. */
static void phone_hud(hd_surface *sf, const hd_remote *g, int32_t scale)
{
   const int32_t w = 186 * scale, x = HD_OUT_W - w - 6 * scale, cols = (w - 14 * scale) / (6 * scale);
   int32_t shown = hd_min(g->phone_n, 3), i, h = 8 * scale, y;
   char line[64];
   if (!shown && !g->boss)
      return;
   for (i = 0; i < shown; i++)
      h += (10 + 9 * wrap(sf, remote_text(g->phone_text[(g->phone_n - shown + i) % REMOTE_PHONE]), 0, 0, cols, scale, 0, 0) + 4) * scale;
   if (g->boss && !g->boss_call && !g->dlg)
      h += 22 * scale;
   y = 26 * scale;
   panel(sf, x, y, w, h, 0x101418, 170);
   if (g->phone_new_t > 0 && (g->phone_new_t / 8) & 1)
      gfx_fill(sf, x, y, w, scale, 0xffe66d); /* a new message blinks the phone's edge */
   y += 6 * scale;
   for (i = 0; i < shown; i++)
   {
      int32_t k = (g->phone_n - shown + i) % REMOTE_PHONE, who = hd_clamp(g->phone_who[k], 0, RW_COUNT - 1), m = g->phone_min[k] + 9 * 60;
      int newest = i == shown - 1 && g->phone_new_t > 0;
      snprintf(line, sizeof line, "%s  %02d:%02d", remote_text(RT_WHO + who), (int)(m / 60), (int)(m % 60));
      text_draw(sf, line, x + 7 * scale, y, scale, who_color[who], 1, -1);
      y += 10 * scale;
      y += (9 * wrap(sf, remote_text(g->phone_text[k]), x + 7 * scale, y, cols, scale, newest ? 0xffffff : 0xd8dee9, 1) + 4) * scale;
   }
   if (g->boss && !g->boss_call && !g->dlg)
   {
      /* to the desk before the bar runs out */
      int32_t left = (g->boss_t + 59) / 60;
      snprintf(line, sizeof line, "%s %d:%02d", remote_text(RT_ANSWER), (int)(left / 60), (int)(left % 60));
      text_draw(sf, line, x + 7 * scale, y, scale, (g->t / 15) & 1 ? 0xff4d6d : 0xffffff, 1, -1);
      panel(sf, x + 7 * scale, y + 10 * scale, w - 14 * scale, 4 * scale, 0x000000, 160);
      gfx_fill(sf, x + 7 * scale, y + 10 * scale, (int32_t)((int64_t)(w - 14 * scale) * g->boss_t / hd_max(1, g->boss_limit)), 4 * scale, 0xe63946);
   }
}

/* The end of the day: the reason (fired: the screen goes black with the boss's last message), the numbers counting up one by one, the total, the title by score, the record. */
static void end_hud(hd_surface *sf, const hd_remote *g, int32_t scale)
{
   int32_t worked = g->work / REMOTE_WORK_UNIT, title, y = 40 * scale, lx = HD_OUT_W / 2 - 150 * scale, rx = HD_OUT_W / 2 + 150 * scale;
   int32_t start = g->over == REND_FIRED ? 150 : 30, k = g->over_t - start, rows = 0, labels[8], values[8], colors[8], i, total_at;
   char line[64];
   if (g->over == REND_FIRED)
   {
      const char *msg = remote_text(RT_END_FIRED_MSG);
      panel(sf, 0, 0, HD_OUT_W, HD_OUT_H, 0x000000, hd_min(256, g->over_t * 8));
      if (k < 0)
      {
         text_draw(sf, msg, (HD_OUT_W - text_width(msg, 2 * scale)) / 2, HD_OUT_H / 2 - 8 * scale, 2 * scale, 0xff8fab, 1, g->over_t / 2);
         return;
      }
   }
   else
      panel(sf, 0, 0, HD_OUT_W, HD_OUT_H, 0x0b0d12, hd_min(210, g->over_t * 6));
   if (k < 0)
      return;
   if (g->over == REND_FIRED)
      title = 0;
   else if (g->over == REND_MISSED)
      title = 1;
   else
      title = g->score < 1500 ? 2 : g->score <= 3000 ? 3 : 4;
   text_center(sf, remote_text(g->over == REND_FIRED ? RT_END_FIRED_MSG : RT_END_TIME), y, scale, 0xbfc7d5);
   /* the rows, one every half second, each counting up */
   labels[rows] = RT_END_POINTS, values[rows] = g->points - g->dlg_points, colors[rows++] = 0xffe66d;
   if (g->dlg_points)
      labels[rows] = RT_END_ANSWERS, values[rows] = g->dlg_points, colors[rows++] = g->dlg_points > 0 ? 0x57cc99 : 0xe63946;
   if (g->over == REND_MISSED)
      labels[rows] = RT_END_HALF, values[rows] = -(g->points - g->points / 2), colors[rows++] = 0xe63946;
   if (g->over == REND_SURVIVED)
   {
      labels[rows] = RT_END_SURVIVED, values[rows] = 1000, colors[rows++] = 0x57cc99;
      labels[rows] = RT_END_DISCRETION, values[rows] = REMOTE_SUSPICION_MAX - g->suspicion, colors[rows++] = 0x57cc99;
      if (worked > REMOTE_OVERTIME)
         labels[rows] = RT_END_OVERTIME, values[rows] = -2 * (worked - REMOTE_OVERTIME), colors[rows++] = 0xe63946;
   }
   if (g->weekend == 1 && g->over != REND_FIRED)
      labels[rows] = RT_END_WEEKEND, values[rows] = -REMOTE_WEEKEND_COST, colors[rows++] = 0xe63946;
   if (g->weekend == 3 && g->over != REND_FIRED)
      labels[rows] = RT_END_WEEKEND_HALF, values[rows] = -REMOTE_WEEKEND_HALF_COST, colors[rows++] = 0xe63946;
   y = 112 * scale;
   for (i = 0; i < rows && k >= i * 30; i++)
   {
      snprintf(line, sizeof line, "%+d", (int)((int64_t)values[i] * hd_min(k - i * 30, 30) / 30));
      text_draw(sf, remote_text(labels[i]), lx, y, scale, 0xd8dee9, 1, -1);
      text_draw(sf, line, rx - text_width(line, scale), y, scale, colors[i], 1, -1);
      y += 14 * scale;
   }
   total_at = rows * 30;
   if (k < total_at)
      return;
   gfx_fill(sf, lx, y, rx - lx, scale, 0x5c677d);
   y += 6 * scale;
   snprintf(line, sizeof line, "%d", (int)((int64_t)g->score * hd_min(k - total_at, 40) / 40));
   text_draw(sf, remote_text(RT_END_TOTAL), lx, y, 2 * scale, 0xffffff, 1, -1);
   text_draw(sf, line, rx - text_width(line, 2 * scale), y, 2 * scale, 0xffe66d, 1, -1);
   /* then the title, and the record */
   if (k >= total_at + 50)
      text_center(sf, remote_text(RT_END_TITLE + title), 62 * scale, 3 * scale, title == 0 ? 0xe63946 : title == 1 ? 0xf6bd60 : 0xffe66d);
   if (k >= total_at + 70)
   {
      if (g->best_new)
         text_center(sf, remote_text(RT_NEW_RECORD), y + 30 * scale, 2 * scale, (k / 8) & 1 ? 0xffe66d : 0xff8fab);
      else if (g->best > 0)
      {
         snprintf(line, sizeof line, "%s: %d", remote_text(RT_RECORD), (int)g->best);
         text_center(sf, line, y + 32 * scale, scale, 0xbfc7d5);
      }
   }
   if (g->over_t > 90 && k >= total_at + 70 && (g->over_t / 30) & 1)
   {
      snprintf(line, sizeof line, "%s     %s", remote_text(RT_END_AGAIN), remote_text(RT_END_MENU));
      text_center(sf, line, HD_OUT_H - 30 * scale, scale, 0xffffff);
   }
}

/*
 * The call's window at the top: the boss talking in a little video, the
 * time left; with the camera on the worker's own picture beside it with a
 * red dot, else "your camera: off". A hotfix shows the deploy instead.
 */
/*
 * The dialogue at the bottom, like the adventures of the 90s: the boss's
 * question, the four answers (the chosen one bright, with an arrow) and how
 * to choose.
 */
static void dialogue_hud(hd_surface *sf, const hd_remote *g, int32_t scale)
{
   int32_t d = g->dlg - 1, i, w, h, x, y;
   int32_t question = d == DLG_HALF ? RT_DLG_REPLY + RE_WEEKEND * 2 * REMOTE_DLG_ANSWERS + 1
                      : d == DLG_SMALL ? RT_DLG_REPLY + (RE_WEEKEND * 2 + 1) * REMOTE_DLG_ANSWERS + 1
                                       : RT_BOSS_ASK + d;
   char line[160];
   w = hd_max(text_width(remote_text(question), scale), text_width(remote_text(RT_DLG_HINT), scale));
   for (i = 0; i < REMOTE_DLG_ANSWERS; i++)
   {
      snprintf(line, sizeof line, "> %s", remote_text(RT_DLG_ANSWER + d * REMOTE_DLG_ANSWERS + i));
      w = hd_max(w, text_width(line, scale));
   }
   w += 20 * scale;
   h = (16 + REMOTE_DLG_ANSWERS * 11 + 14) * scale;
   x = (HD_OUT_W - w) / 2, y = HD_OUT_H - h - 8 * scale;
   panel(sf, x, y, w, h, 0x101418, 205);
   text_draw(sf, remote_text(question), x + 10 * scale, y + 5 * scale, scale, 0xff8fa3, 1, -1);
   for (i = 0; i < REMOTE_DLG_ANSWERS; i++)
   {
      int on = i == g->dlg_sel;
      snprintf(line, sizeof line, "%s%s", on ? "> " : "  ", remote_text(RT_DLG_ANSWER + d * REMOTE_DLG_ANSWERS + i));
      text_draw(sf, line, x + 10 * scale, y + (18 + i * 11) * scale, scale, on ? 0xffe66d : 0xbfc7d5, 1, -1);
   }
   text_center(sf, remote_text(RT_DLG_HINT), y + h - 11 * scale, scale, 0x8a93a6);
}

static void call_hud(hd_surface *sf, const hd_remote *g, int32_t scale)
{
   int32_t kind = g->boss - 1, w = 230 * scale, h = 86 * scale, x = (HD_OUT_W - w) / 2, y = 8 * scale, left = (g->boss_t + 59) / 60;
   int32_t bx = x + 8 * scale, by = y + 8 * scale, bw = 92 * scale, bh = 62 * scale, talk = (g->t / 7) % 3;
   char line[48];
   panel(sf, x, y, w, h, 0x0b0d12, 210);
   if (kind == RE_HOTFIX)
   {
      /* the deploy: a bar filling and the log scrolling */
      static const char *const steps[4] = { "git push -f", "npm run build", "kubectl apply", "pray.sh" };
      int32_t total = boss_events[RE_HOTFIX].minutes * FPM, done = total - g->boss_t;
      text_draw(sf, remote_text(RT_DEPLOYING), x + 10 * scale, y + 8 * scale, scale, 0xff9f1c, 1, -1);
      panel(sf, x + 10 * scale, y + 22 * scale, w - 20 * scale, 6 * scale, 0x000000, 200);
      gfx_fill(sf, x + 11 * scale, y + 23 * scale, (int32_t)((int64_t)(w - 22 * scale) * done / hd_max(1, total)), 4 * scale, 0x57cc99);
      snprintf(line, sizeof line, "$ %s", steps[hd_clamp(done * 4 / hd_max(1, total), 0, 3)]);
      text_draw(sf, line, x + 10 * scale, y + 36 * scale, scale, 0x80ed99, 1, -1);
      snprintf(line, sizeof line, "%d%%", (int)(done * 100 / hd_max(1, total)));
      text_draw(sf, line, x + w - 10 * scale - text_width(line, scale), y + 36 * scale, scale, 0xffffff, 1, -1);
      return;
   }
   /* the boss: a face in a frame, the mouth moving while it talks */
   panel(sf, bx, by, bw, bh, 0x3d5a80, 255);
   gfx_fill(sf, bx + 30 * scale, by + 10 * scale, 32 * scale, 36 * scale, 0xe0ac69);   /* the face */
   gfx_fill(sf, bx + 28 * scale, by + 6 * scale, 36 * scale, 8 * scale, 0x6c757d);     /* grey hair */
   gfx_fill(sf, bx + 36 * scale, by + 22 * scale, 6 * scale, 4 * scale, 0x1d1d1d);     /* eyes */
   gfx_fill(sf, bx + 50 * scale, by + 22 * scale, 6 * scale, 4 * scale, 0x1d1d1d);
   gfx_fill(sf, bx + 40 * scale, by + 34 * scale, 12 * scale, (2 + talk * 2) * scale, 0x7a2e2e); /* the mouth */
   gfx_fill(sf, bx + 22 * scale, by + 46 * scale, 48 * scale, 16 * scale, 0x1d3557);   /* a suit */
   gfx_fill(sf, bx + 43 * scale, by + 46 * scale, 6 * scale, 12 * scale, 0xe63946);    /* a tie */
   text_draw(sf, remote_text(RT_CALL_BOSS), bx + 3 * scale, by + bh - 10 * scale, scale, 0xffffff, 1, -1);
   /* the worker's side */
   bx += bw + 8 * scale;
   if (kind == RE_CAMERA)
   {
      int32_t asleep = g->energy < REMOTE_WORK_ASLEEP;
      panel(sf, bx, by, bw, bh, 0x6f9fd0, 255);
      gfx_fill(sf, bx + 30 * scale, by + 14 * scale, 32 * scale, 34 * scale, 0xe0ac69);
      gfx_fill(sf, bx + 28 * scale, by + 8 * scale, 36 * scale, 10 * scale, 0x3d2817);
      gfx_fill(sf, bx + 34 * scale, by + 26 * scale, 24 * scale, 4 * scale, 0x222222); /* glasses */
      gfx_fill(sf, bx + 40 * scale, by + 38 * scale, 12 * scale, asleep ? scale : 3 * scale, 0x9c4a3a);
      gfx_fill(sf, bx + 22 * scale, by + 48 * scale, 48 * scale, 14 * scale, 0xf4a261);
      if (g->cat_keys == 2)
         gfx_fill(sf, bx + 60 * scale, by + 40 * scale, 30 * scale, 20 * scale, 0xc0601c); /* the cat in the picture */
      gfx_fill(sf, bx + 4 * scale, by + 4 * scale, 5 * scale, 5 * scale, (g->t / 20) & 1 ? 0xe63946 : 0x7a1c22);
      text_draw(sf, remote_text(RT_CAM_ON), x + 8 * scale, y + h - 12 * scale, scale, 0xff4d6d, 1, -1);
   }
   else
   {
      panel(sf, bx, by, bw, bh, 0x1d1d1d, 255);
      text_draw(sf, remote_text(RT_CAM_OFF), x + 8 * scale, y + h - 12 * scale, scale, 0xbfc7d5, 1, -1);
   }
   snprintf(line, sizeof line, "%d:%02d", (int)(left / 60), (int)(left % 60));
   text_draw(sf, line, x + w - 8 * scale - text_width(line, scale), y + h - 12 * scale, scale, 0xffffff, 1, -1);
}

static void hud(hd_surface *sf, const hd_remote *g)
{
   int32_t scale = hd_res, minutes = 9 * 60 + hd_min(g->t / FPM, REMOTE_DAY_MINUTES), worked = g->work / REMOTE_WORK_UNIT;
   int32_t e = g->energy * 256 / REMOTE_ENERGY_MAX, su = g->suspicion * 256 / REMOTE_SUSPICION_MAX, st = status(g), shake = 0;
   char line[96];
   if (g->over)
   {
      end_hud(sf, g, scale);
      return;
   }
   /* top left: the clock and the status, the work done, the energy and the suspicion */
   panel(sf, 6 * scale, 6 * scale, 156 * scale, 100 * scale, 0x101418, 150);
   snprintf(line, sizeof line, "%02d:%02d", (int)(minutes / 60), (int)(minutes % 60));
   text_draw(sf, line, 12 * scale, 11 * scale, 2 * scale, 0xffffff, 1, -1);
   gfx_fill(sf, 12 * scale, 27 * scale, (int32_t)((int64_t)144 * scale * hd_min(g->t, REMOTE_DAY_MINUTES * FPM) / (REMOTE_DAY_MINUTES * FPM)), scale, 0x5c677d);
   gfx_fill(sf, 80 * scale, 14 * scale, 5 * scale, 5 * scale, status_color[st]);
   text_draw(sf, remote_text(RT_STATUS + st), 88 * scale, 13 * scale, scale, status_color[st], 1, -1);
   snprintf(line, sizeof line, "%s %d h %02d / 6 h", remote_text(RT_WORK), (int)(worked / 60), (int)(worked % 60));
   bar(sf, 12 * scale, 32 * scale, 144 * scale, worked * 256 / REMOTE_WORK_GOAL, worked >= REMOTE_WORK_GOAL ? 0x57cc99 : 0x4ea8de, line, scale);
   bar(sf, 12 * scale, 56 * scale, 144 * scale, e, e > 77 ? 0xf6bd60 : 0xe63946, remote_text(RT_ENERGY), scale);
   if (g->suspicion > 800)
      shake = ((g->t / 2) & 1) ? scale : -scale; /* close to the end: it shakes */
   bar(sf, 12 * scale + shake, 80 * scale, 144 * scale, su, su < 100 ? 0x57cc99 : su < 180 ? 0xf6bd60 : 0xe63946, remote_text(RT_SUSPICION), scale);
   /* top right: the points, the phone under them */
   snprintf(line, sizeof line, "%s %d", remote_text(RT_POINTS), (int)g->points);
   panel(sf, HD_OUT_W - text_width(line, scale) - 18 * scale, 6 * scale, text_width(line, scale) + 12 * scale, 16 * scale, 0x101418, 150);
   text_draw(sf, line, HD_OUT_W - text_width(line, scale) - 12 * scale, 10 * scale, scale, 0xffe66d, 1, -1);
   phone_hud(sf, g, scale);
   if (g->boss && g->boss_call)
      call_hud(sf, g, scale);
   /* bottom: what the button does, or what is being done and how far it went */
   if (g->dlg)
      dialogue_hud(sf, g, scale);
   else if (g->act)
   {
      int32_t a = g->act - 1, total = activities[a].minutes * FPM;
      const char *what = a == RA_WORK && g->energy < REMOTE_WORK_ASLEEP ? remote_text(RT_ASLEEP) : remote_text(RT_DOING + a);
      const char *stop = remote_text(a == RA_WORK && g->cat_keys == 2 ? RT_CAT_OFF : a == RA_WORK && g->boss_call ? RT_ON_CALL : RT_STOP);
      int32_t w = hd_max(text_width(what, scale), text_width(stop, scale)) + 24 * scale, x = (HD_OUT_W - w) / 2, y = HD_OUT_H - 46 * scale;
      panel(sf, x, y, w, 38 * scale, 0x101418, 170);
      text_center(sf, what, y + 6 * scale, scale, 0xffffff);
      if (total > 0)
      {
         panel(sf, x + 8 * scale, y + 18 * scale, w - 16 * scale, 4 * scale, 0x000000, 160);
         gfx_fill(sf, x + 8 * scale, y + 18 * scale, (int32_t)((int64_t)(w - 16 * scale) * hd_min(g->act_t, total) / total), 4 * scale, 0x57cc99);
      }
      text_center(sf, stop, y + 26 * scale, scale, g->cat_keys == 2 && a == RA_WORK ? 0xffe66d : 0xbfc7d5);
   }
   else if (g->prompt)
   {
      const char *what = remote_text(g->prompt == RA_WORK + 1 && g->cat_keys == 2 ? RT_CAT_OFF : RT_PROMPT + g->prompt - 1);
      int32_t w = text_width(what, scale) + 24 * scale, y = HD_OUT_H - 34 * scale;
      panel(sf, (HD_OUT_W - w) / 2, y, w, 20 * scale, 0x101418, 170);
      text_center(sf, what, y + 6 * scale, scale, 0xffe66d);
   }
   else if (g->t < 20 * 60)
      text_center(sf, remote_text(RT_HINT_WALK), HD_OUT_H - 20 * scale, scale, 0xffffff);
   /* a message when something ends, with its points */
   if (g->toast && g->toast_t > 0)
   {
      const char *msg = remote_text(g->toast - 1);
      int32_t w = text_width(msg, scale) + 24 * scale, y = HD_OUT_H - 84 * scale;
      panel(sf, (HD_OUT_W - w) / 2, y, w, (g->toast_points ? 30 : 18) * scale, 0x101418, 170);
      text_center(sf, msg, y + 5 * scale, scale, 0xffffff);
      if (g->toast_points)
      {
         snprintf(line, sizeof line, "+%d", (int)g->toast_points);
         text_center(sf, line, y + 17 * scale, scale, 0xffe66d);
      }
   }
   /* what the worker thinks of the boss's ask, once it was read: a cloud over the messages, its little puffs under it */
   if (g->muse && g->muse_t > 0 && g->muse_t <= REMOTE_MUSE_FRAMES - REMOTE_MUSE_WAIT)
   {
      const char *msg = remote_text(g->muse - 1);
      int32_t cols = 46, lines = wrap(sf, msg, 0, 0, cols, scale, 0, 0);
      int32_t w = lines > 1 ? (cols * 6 + 16) * scale : text_width(msg, scale) + 16 * scale;
      int32_t h = (lines * 9 + 9) * scale, x = (HD_OUT_W - w) / 2, y = HD_OUT_H - 84 * scale - h - 16 * scale;
      panel(sf, x, y, w, h, 0x2b2346, 190);
      panel(sf, x + 18 * scale, y + h + 3 * scale, 6 * scale, 5 * scale, 0x2b2346, 190);
      panel(sf, x + 12 * scale, y + h + 10 * scale, 4 * scale, 3 * scale, 0x2b2346, 190);
      wrap(sf, msg, x + 8 * scale, y + 5 * scale, cols, scale, 0xd9ccff, 1);
   }
}

#define GUITAR_ROLL -260 /* the guitar's neck up, toward the hand along it */
/* What the worker holds in each activity (-1: nothing). */
static const int8_t activity_prop[RA_COUNT] = {
   [RA_WORK] = -1, [RA_NAP] = -1, [RA_COFFEE] = ROOM3D_PROP_MUG, [RA_COOK] = ROOM3D_PROP_PAN, [RA_FRIDGE] = ROOM3D_PROP_CAN,
   [RA_EAT] = ROOM3D_PROP_BOWL, [RA_SERIES] = -1, [RA_CONSOLE] = ROOM3D_PROP_PAD, [RA_GUITAR] = ROOM3D_PROP_GUITAR, [RA_SUN] = -1,
   [RA_PLANTS] = ROOM3D_PROP_WATERING, [RA_SHOWER] = -1, [RA_WASHING] = -1, [RA_DOOR] = ROOM3D_PROP_PARCEL, [RA_CAT] = -1,
   [RA_PHONES] = ROOM3D_PROP_HEADPHONES,
};

/*
 * The top of the seat under a sitting person whose feet are at `at` (over
 * them, 16.16 meters), 0 when there is none: what the thighs rest on. Kept
 * for each place (`slot`) until the flat is built again.
 */
static int32_t seat_under(int32_t slot, r3d_vec at)
{
   static int32_t built = -1, seat[RA_COUNT + 1], found[RA_COUNT + 1];
   r3d_vec p = at;
   int32_t y;
   if (built != room3d_builds())
   {
      memset(found, 0, sizeof found);
      built = room3d_builds();
   }
   if (!found[slot])
   {
      p.y += CM(75);
      y = r3d_surface_below(p);
      seat[slot] = y == R3D_NOTHING || y < at.y + CM(20) ? 0 : y - at.y;
      found[slot] = 1;
   }
   return seat[slot];
}
#define SEAT_MORNING RA_COUNT /* seat_under's slot for the bed's edge in the morning */

/* The worker's pose from the state: walking, or the activity's pose. */
static void worker_pose(const hd_remote *g, actor3d_pose *pose)
{
   memset(pose, 0, sizeof *pose);
   pose->at.x = g->x;
   pose->at.z = g->z;
   pose->yaw = g->yaw;
   pose->walk = g->walk;
   pose->phase = g->phase;
   pose->clock = g->screen_t;
   if (g->act)
   {
      const activity *a = &activities[g->act - 1];
      pose->at.y = CM(a->ay);
      pose->floor_y = CM(a->ay);
      pose->sit = (a->pose & P_SIT) != 0;
      pose->type = (a->pose & P_TYPE) != 0;
      pose->hold = (a->pose & P_HOLD) != 0;
      pose->crouch = (a->pose & P_CROUCH) != 0;
      pose->lie = (a->pose & P_LIE) != 0;
      if (pose->sit)
         pose->seat = seat_under(g->act - 1, pose->at);
      if (activity_prop[g->act - 1] >= 0)
      {
         int32_t prop = activity_prop[g->act - 1];
         pose->prop = room3d_prop(prop);
         pose->small = prop == ROOM3D_PROP_MUG || prop == ROOM3D_PROP_CAN || prop == ROOM3D_PROP_BOWL || prop == ROOM3D_PROP_PAD;
      }
      if (g->act == RA_GUITAR + 1)
      {
         pose->hold = 0;
         pose->strum = 1;
         pose->prop_roll = GUITAR_ROLL; /* the neck up */
      }
      pose->phase = (g->t * 40) & 4095;
      if (g->act == RA_WORK + 1)
      {
         pose->nod = g->energy < REMOTE_WORK_ASLEEP ? -420 : -80; /* asleep: the head down on the desk */
         pose->type = g->energy >= REMOTE_WORK_ASLEEP;
         pose->look = ((g->t / 240) & 1) ? 180 : 0;
      }
   }
}

/* A person: the package's model when it has one, else the built one. */
static int32_t place_person(int32_t who, const actor3d_pose *pose, r3d_instance *out)
{
   model3d_body *body = room3d_person_model(who);
   if (!body && who == ROOM3D_WORKER_WOMAN)
      body = room3d_person_model(ROOM3D_WORKER); /* a package with no woman: the man stands in */
   if (body)
      return actor3d_place_model(body, pose, room3d_shadow(), out);
   return actor3d_place(room3d_person(who), pose, out);
}

/* The house seen from `v` with the worker (in `worker`'s pose), the flatmate and the cat. */
/*
 * The 3D picture: drawn at 720p at most, and at 1080p made 1.5 times bigger
 * (bilinear, in integers, on every core), so the game keeps 60 frames a
 * second there; the HUD's texts are drawn over it at the full size, sharp.
 */
#define LOW_W 1280
#define LOW_H 720
static uint32_t low_px[LOW_W * LOW_H];

typedef struct
{
   const hd_surface *src;
   hd_surface *dst;
   const int32_t *col; /* each destination column's place in the source, 16.16 */
} enlarge_job;

static void enlarge_rows(void *ctx, int32_t y0, int32_t y1)
{
   const enlarge_job *j = (const enlarge_job *)ctx;
   const hd_surface *a = j->src;
   hd_surface *b = j->dst;
   int32_t x, y;
   for (y = y0; y < y1; y++)
   {
      /* the destination pixel's center in the source, 16.16 */
      int32_t sy = (int32_t)hd_clamp((int32_t)((((int64_t)y * 2 + 1) * a->h << 16) / (2 * b->h)) - 32768, 0, (a->h - 1) << 16);
      int32_t y0s = sy >> 16, y1s = hd_min(y0s + 1, a->h - 1), fy = (sy >> 8) & 255;
      const uint32_t *r0 = a->px + (size_t)y0s * a->w, *r1 = a->px + (size_t)y1s * a->w;
      uint32_t *out = b->px + (size_t)y * b->w;
      for (x = 0; x < b->w; x++)
      {
         int32_t sx = j->col[x];
         int32_t x0s = sx >> 16, x1s = hd_min(x0s + 1, a->w - 1), fx = (sx >> 8) & 255, k;
         uint32_t p00 = r0[x0s], p01 = r0[x1s], p10 = r1[x0s], p11 = r1[x1s], v = 0;
         for (k = 0; k < 24; k += 8)
         {
            int32_t top = (int32_t)((p00 >> k) & 255) * (256 - fx) + (int32_t)((p01 >> k) & 255) * fx;
            int32_t bot = (int32_t)((p10 >> k) & 255) * (256 - fx) + (int32_t)((p11 >> k) & 255) * fx;
            v |= (uint32_t)(((top * (256 - fy) + bot * fy) >> 16) & 255) << k;
         }
         out[x] = v;
      }
   }
}

#define HD_OUT_W_MAX 1920
static void draw3d(hd_surface *sf, const r3d_view *v, const r3d_instance *inst, int32_t n)
{
   static int32_t col[HD_OUT_W_MAX];
   hd_surface low;
   enlarge_job j;
   int32_t x;
   if (sf->w <= LOW_W && sf->h <= LOW_H)
   {
      r3d_draw(sf, v, room3d_scene(), inst, n);
      return;
   }
   low.px = low_px;
   low.w = LOW_W;
   low.h = LOW_H;
   r3d_draw(&low, v, room3d_scene(), inst, n);
   if (sf->w > HD_OUT_W_MAX)
      return;
   for (x = 0; x < sf->w; x++)
      col[x] = hd_clamp((int32_t)((((int64_t)x * 2 + 1) * LOW_W << 16) / (2 * sf->w)) - 32768, 0, (LOW_W - 1) << 16);
   j.src = &low;
   j.dst = sf;
   j.col = col;
   hd_rows(sf->w, sf->h, enlarge_rows, &j);
}

static void scene(hd_surface *sf, const hd_remote *g, r3d_view *v, const actor3d_pose *worker)
{
   r3d_instance inst[ACT_INSTANCES * 2 + PET_INSTANCES + 1];
   actor3d_pose pose;
   pet3d_pose cat;
   int32_t n, typing = g->act == RA_WORK + 1 && g->energy >= REMOTE_WORK_ASLEEP && !g->net_t;
   v->fov = v->fov ? v->fov : 560;
   v->near_z = v->near_z ? v->near_z : CM(10);
   v->sky_top = 0x6f9fd0;
   v->sky_bottom = 0xd6e6f2;
   v->exposure = 256;
   v->smooth = 1;
   v->bloom = 200;
   v->vignette = 110;
   v->light = room3d_light();
   /* the desk's screens: code going up while working, asleep otherwise */
   tex3d_terminal(room3d_desk_screen(0), 11, g->screen_scroll, typing);
   tex3d_terminal(room3d_desk_screen(1), 12, g->screen_scroll / 2 + 40, typing);
   n = place_person(g->hero ? ROOM3D_WORKER_WOMAN : ROOM3D_WORKER, worker, inst);
   /* the flatmate, waving while it talks */
   if (g->mate)
   {
      memset(&pose, 0, sizeof pose);
      pose.at.x = g->mate_x;
      pose.at.z = g->mate_z;
      pose.yaw = g->mate_yaw;
      pose.walk = g->mate_walk;
      pose.phase = g->mate_walk ? g->mate_phase : (g->t * 40) & 4095;
      pose.wave = g->mate_talk_t > 18 * 60;
      pose.clock = g->screen_t + 37;
      n += place_person(ROOM3D_FLATMATE, &pose, inst + n);
   }
   /* the cat */
   memset(&cat, 0, sizeof cat);
   cat.at.x = g->cat_x;
   cat.at.y = g->cat_y;
   cat.at.z = g->cat_z;
   cat.floor_y = g->cat_mode == CAT_UP || g->cat_mode == CAT_DOWN ? 0 : g->cat_y;
   cat.yaw = g->cat_yaw;
   cat.walk = g->cat_walk;
   cat.phase = g->cat_walk ? g->cat_phase : (g->screen_t * 24) & 4095;
   if (g->cat_mode == CAT_REST)
   {
      cat.lie = cat_spots[g->cat_spot].lie;
      cat.sit = !cat.lie;
      cat.look = g->act == RA_CAT + 1 ? 0 : (int32_t)(((int64_t)hd_sin((g->screen_t * 5) & 4095) * 300) >> 14);
   }
   n += pet3d_place(room3d_cat(), &cat, inst + n);
   /* the shower's water while it runs, falling */
   if (g->act == RA_SHOWER + 1)
   {
      r3d_instance *w = &inst[n++];
      memset(w, 0, sizeof *w);
      w->mesh = *room3d_prop(ROOM3D_PROP_SHOWER);
      w->at.x = ROOM3D_AT(ROOM3D_SHOWER_X);
      w->at.y = CM(ROOM3D_SHOWER_Y) - CM((g->t * 7) % 28);
      w->at.z = ROOM3D_AT(ROOM3D_SHOWER_Z);
   }
   draw3d(sf, v, inst, n);
}

/* The title's view: the whole house turning slowly, seen from above like a model, the worker at the desk and the cat on the bed. */
static void title_scene(hd_surface *sf, const hd_remote *g)
{
   hd_remote shown;
   r3d_view v;
   actor3d_pose pose;
   int32_t dist = CM(2000), pitch = 640, yaw = (3600 + g->screen_t * 2) & 4095, c = hd_cos(pitch);
   shown = *g;
   shown.act = RA_WORK + 1;
   shown.x = ROOM3D_AT(activities[RA_WORK].ax);
   shown.z = ROOM3D_AT(activities[RA_WORK].az);
   shown.yaw = activities[RA_WORK].ayaw & (ANGLE_FULL - 1);
   shown.energy = REMOTE_ENERGY_START;
   shown.t = g->screen_t;
   shown.screen_scroll = g->screen_t;
   shown.mate = 0;
   shown.cat_spot = SPOT_BED;
   shown.cat_mode = CAT_REST;
   shown.cat_walk = 0;
   shown.cat_x = ROOM3D_AT(cat_spots[SPOT_BED].rx);
   shown.cat_z = ROOM3D_AT(cat_spots[SPOT_BED].rz);
   shown.cat_y = CM(cat_spots[SPOT_BED].ry);
   shown.cat_yaw = cat_spots[SPOT_BED].yaw;
   memset(&v, 0, sizeof v);
   v.yaw = yaw;
   v.pitch = pitch;
   v.eye.x = ROOM3D_AT(800) - (int32_t)(((((int64_t)dist * c) >> 14) * hd_sin(yaw)) >> 14);
   v.eye.z = ROOM3D_AT(600) - (int32_t)(((((int64_t)dist * c) >> 14) * hd_cos(yaw)) >> 14);
   v.eye.y = CM(80) + (int32_t)(((int64_t)dist * hd_sin(pitch)) >> 14);
   v.fov = 520;
   v.near_z = CM(20);
   worker_pose(&shown, &pose);
   scene(sf, &shown, &v, &pose);
}

/* A menu's lines centered from y, the chosen one bright with a marker. */
static void menu_draw(hd_surface *sf, const int32_t *texts, int32_t lines, int32_t chosen, int32_t y, int32_t scale)
{
   int32_t i;
   for (i = 0; i < lines; i++)
   {
      const char *t = remote_text(texts[i]);
      int32_t w = text_width(t, 2 * scale), x = (HD_OUT_W - w) / 2, yy = y + i * 24 * scale;
      if (i == chosen)
      {
         panel(sf, x - 22 * scale, yy - 5 * scale, w + 44 * scale, 22 * scale, 0xffe66d, 60);
         text_draw(sf, ">", x - 18 * scale, yy, 2 * scale, 0xffe66d, 1, -1);
      }
      text_draw(sf, t, x, yy, 2 * scale, i == chosen ? 0xffe66d : 0xd8dee9, 1, -1);
   }
}

/* The title in two lines: what comes before the colon big, the rest under it. */
static void title_text(hd_surface *sf, int32_t y, int32_t scale)
{
   const char *t = remote_text(RT_TITLE), *colon = strchr(t, ':');
   char first[64];
   size_t n = colon ? (size_t)(colon - t + 1) : strlen(t);
   if (n >= sizeof first)
      n = sizeof first - 1;
   memcpy(first, t, n);
   first[n] = 0;
   panel(sf, 0, y - 14 * scale, HD_OUT_W, 92 * scale, 0x0b0d12, 150);
   text_center(sf, first, y, 5 * scale, 0xffe66d);
   if (colon)
      text_center(sf, colon + 1 + (colon[1] == ' '), y + 44 * scale, 3 * scale, 0xffffff);
}

static void title_hud(hd_surface *sf, const hd_remote *g, int32_t scale)
{
   static const int32_t items[2] = { RT_MENU_PLAY, RT_MENU_HELP }, last[1] = { RT_MENU_CREDITS };
   static const char *const names[LANGS] = { "English", "Español", "Português" };
   char line[64];
   int32_t w, i, y = 172 * scale + 2 * 24 * scale;
   title_text(sf, 40 * scale, scale);
   panel(sf, (HD_OUT_W - 440 * scale) / 2, 158 * scale, 440 * scale, (g->best > 0 ? 170 : 146) * scale, 0x0b0d12, 175);
   menu_draw(sf, items, 2, g->menu, 172 * scale, scale);
   menu_draw(sf, last, 1, g->menu == 4 ? 0 : -1, 172 * scale + 4 * 24 * scale, scale);
   /* the language (its name in itself) and the main character, between arrows, changed with left and right (or A) */
   for (i = 0; i < 2; i++)
   {
      int32_t at = y + i * 24 * scale, on = g->menu == 2 + i;
      if (i == 0)
         snprintf(line, sizeof line, "%s: < %s >", remote_text(RT_MENU_LANGUAGE), names[hd_clamp(hd_lang, 0, LANGS - 1)]);
      else
         snprintf(line, sizeof line, "%s: < %s >", remote_text(RT_MENU_HERO), remote_text(RT_HERO + hd_clamp(g->hero, 0, 1)));
      w = text_width(line, 2 * scale);
      if (on)
      {
         panel(sf, (HD_OUT_W - w) / 2 - 22 * scale, at - 5 * scale, w + 44 * scale, 22 * scale, 0xffe66d, 60);
         text_draw(sf, ">", (HD_OUT_W - w) / 2 - 18 * scale, at, 2 * scale, 0xffe66d, 1, -1);
      }
      text_draw(sf, line, (HD_OUT_W - w) / 2, at, 2 * scale, on ? 0xffe66d : 0xd8dee9, 1, -1);
   }
   if (g->best > 0)
   {
      snprintf(line, sizeof line, "%s: %d", remote_text(RT_RECORD), (int)g->best);
      text_center(sf, line, 302 * scale, scale, 0xbfc7d5);
   }
   if ((g->screen_t / 30) & 1)
      text_center(sf, remote_text(RT_PRESS_START), HD_OUT_H - 30 * scale, scale, 0xffffff);
}

/* How to play: a page over a darkened picture. */
static void help_hud(hd_surface *sf, const hd_remote *g, int32_t scale)
{
   int32_t page = hd_clamp(g->help_page, 0, REMOTE_HELP_PAGES - 1), i, y = 92 * scale, cols = (HD_OUT_W - 160 * scale) / (6 * scale);
   char line[16];
   panel(sf, 0, 0, HD_OUT_W, HD_OUT_H, 0x0b0d12, 200);
   text_center(sf, remote_text(RT_HELP_TITLE + page), 46 * scale, 3 * scale, 0xffe66d);
   for (i = 0; i < 4; i++)
   {
      text_draw(sf, "-", 70 * scale, y, scale, 0xffe66d, 1, -1);
      y += (9 * wrap(sf, remote_text(RT_HELP_LINE + page * 4 + i), 80 * scale, y, cols, scale, 0xffffff, 1) + 10) * scale;
   }
   snprintf(line, sizeof line, "%d / %d", (int)page + 1, REMOTE_HELP_PAGES);
   text_center(sf, line, HD_OUT_H - 46 * scale, scale, 0x8d99ae);
   text_center(sf, remote_text(page == REMOTE_HELP_PAGES - 1 ? RT_HELP_DONE : RT_HELP_NEXT), HD_OUT_H - 30 * scale, scale, 0xffffff);
}

/* The morning: the alarm in bed, sitting up, getting up, and the camera flying to its place behind the worker. */
static void morning_draw(hd_surface *sf, const hd_remote *g, int32_t scale)
{
   int32_t t = g->screen_t, k, shake;
   r3d_view v;
   actor3d_pose pose;
   r3d_vec bed, end, from, to;
   char line[16];
   memset(&pose, 0, sizeof pose);
   if (t < 150)
   {
      /* in bed: the alarm rings */
      pose.at.x = ROOM3D_AT(activities[RA_NAP].ax);
      pose.at.y = CM(activities[RA_NAP].ay);
      pose.at.z = ROOM3D_AT(activities[RA_NAP].az);
      pose.yaw = FACE_EAST;
      pose.lie = 1;
      pose.look = (int32_t)(((int64_t)hd_sin((t * 60) & 4095) * 200) >> 14) * (t > 60);
   }
   else if (t < 195)
   {
      /* sitting on the bed's edge */
      pose.at.x = ROOM3D_AT(195);
      pose.at.z = ROOM3D_AT(1010);
      pose.yaw = FACE_EAST;
      pose.sit = 1;
      pose.seat = seat_under(SEAT_MORNING, pose.at);
      pose.nod = -200;
   }
   else
   {
      pose.at.x = g->x;
      pose.at.z = g->z;
      pose.yaw = FACE_SOUTH;
   }
   /* the camera: a still shot of the bed, then a flight to the follow camera of the day */
   bed.x = ROOM3D_AT(140), bed.y = CM(60), bed.z = ROOM3D_AT(1010);
   from = play3d_eye(bed, 3072 - 350, 560, CM(330));
   end = target(g);
   to = play3d_eye(end, 2048, g->cam_pitch + g->cam_lift, g->cam_dist);
   k = hd_clamp((t - 180) * 256 / 80, 0, 256);
   k = k * k * (768 - 2 * k) / 65536; /* eased in and out */
   memset(&v, 0, sizeof v);
   v.eye.x = from.x + (int32_t)((int64_t)(to.x - from.x) * k / 256);
   v.eye.y = from.y + (int32_t)((int64_t)(to.y - from.y) * k / 256);
   v.eye.z = from.z + (int32_t)((int64_t)(to.z - from.z) * k / 256);
   v.yaw = ((3072 - 350) + (((2048 - (3072 - 350) + 2048) & 4095) - 2048) * k / 256) & 4095;
   v.pitch = 560 + (g->cam_pitch + g->cam_lift - 560) * k / 256;
   scene(sf, g, &v, &pose);
   /* the clock and the alarm */
   if (t < 170)
   {
      shake = ((t / 3) & 1) ? 2 * scale : -2 * scale;
      panel(sf, (HD_OUT_W - 150 * scale) / 2, 30 * scale, 150 * scale, 40 * scale, 0x101418, 170);
      text_center(sf, "08:59", 36 * scale, 4 * scale, (t / 10) & 1 ? 0xff4d6d : 0xffffff);
      text_draw(sf, remote_text(RT_MORNING_RING), (HD_OUT_W - text_width(remote_text(RT_MORNING_RING), 3 * scale)) / 2 + shake, 84 * scale, 3 * scale, 0xffe66d, 1, -1);
   }
   if (t > 50 && t < 240)
      text_center(sf, remote_text(RT_MORNING_LINE), HD_OUT_H - 50 * scale, scale, 0xffffff);
   snprintf(line, sizeof line, "%s", remote_text(RT_SKIP));
   text_draw(sf, line, HD_OUT_W - text_width(line, scale) - 12 * scale, HD_OUT_H - 18 * scale, scale, 0x8d99ae, 1, -1);
}

/* The credits rolling up over the house turning, darkened: headings gold and bigger. */
static void credits_hud(hd_surface *sf, const hd_remote *g, int32_t scale)
{
   int32_t i, y0 = HD_OUT_H + 10 * scale - g->screen_t * scale / 2;
   panel(sf, 0, 0, HD_OUT_W, HD_OUT_H, 0x0b0d12, 200);
   for (i = 0; i < credits_count(); i++)
   {
      const char *t = credits_line(i);
      int32_t y = y0 + i * CREDIT_LINE * scale;
      if (y < -CREDIT_LINE * scale || y > HD_OUT_H)
         continue;
      if (t[0] == '#' && t[1] == ' ')
         text_center(sf, t + 2, y, 3 * scale, 0xffe66d);
      else if (t[0])
         text_center(sf, t, y + 3 * scale, 2 * scale, 0xffffff);
   }
}

static void pause_hud(hd_surface *sf, const hd_remote *g, int32_t scale)
{
   static const int32_t items[4] = { RT_PAUSE_GO_ON, RT_MENU_HELP, RT_PAUSE_RESTART, RT_PAUSE_QUIT };
   panel(sf, 0, 0, HD_OUT_W, HD_OUT_H, 0x0b0d12, 170);
   text_center(sf, remote_text(RT_PAUSE), 60 * scale, 4 * scale, 0xffe66d);
   menu_draw(sf, items, 4, g->menu, 130 * scale, scale);
}

int32_t remote_cover;

void hd_remote_draw(const hd_state *s, void *screen)
{
   hd_surface *sf = (hd_surface *)screen;
   const hd_remote *g = HD_GAME3D_C(s, hd_remote);
   int32_t scale = hd_res;
   r3d_view v;
   actor3d_pose pose;
   remote_text_hero = g->hero;
   ready();
   if (g->screen == RSC_TITLE || g->screen == RSC_CREDITS || (g->screen == RSC_HELP && g->help_back == RSC_TITLE))
   {
      title_scene(sf, g);
      if (g->screen == RSC_TITLE)
         title_hud(sf, g, scale);
      else if (g->screen == RSC_CREDITS)
         credits_hud(sf, g, scale);
      else
         help_hud(sf, g, scale);
      return;
   }
   if (g->screen == RSC_MORNING)
   {
      morning_draw(sf, g, scale);
      return;
   }
   memset(&v, 0, sizeof v);
   v.eye = play3d_eye(target(g), g->cam_yaw, g->cam_pitch + g->cam_lift, g->cam_dist);
   v.yaw = g->cam_yaw;
   v.pitch = g->cam_pitch + g->cam_lift;
   worker_pose(g, &pose);
   scene(sf, g, &v, &pose);
   if (remote_cover)
   {
      /* the game's picture (tools/remotecover): the day's scene and the title, nothing else */
      title_text(sf, 40 * scale, scale);
      return;
   }
   if (g->screen == RSC_HELP)
   {
      help_hud(sf, g, scale);
      return;
   }
   hud(sf, g);
   if (g->attract && g->screen == RSC_DAY)
   {
      /* the demo: a banner over the game */
      panel(sf, (HD_OUT_W - 120 * scale) / 2, 6 * scale, 120 * scale, 30 * scale, 0x101418, 190);
      text_center(sf, remote_text(RT_DEMO), 9 * scale, 2 * scale, 0xffe66d);
      if ((g->t / 30) & 1)
         text_center(sf, remote_text(RT_PRESS_START), 26 * scale, scale, 0xffffff);
   }
   if (g->paused && g->screen == RSC_DAY)
      pause_hud(sf, g, scale);
}

void hd_remote_sanitize(hd_state *s)
{
   hd_remote *g = HD_GAME3D(s, hd_remote);
   int32_t i;
   s->show.on = 3;
   g->hero = hd_clamp(g->hero, 0, 1);
   g->t = hd_clamp(g->t, 0, 1 << 30);
   g->x = hd_clamp(g->x, ROOM3D_AT(0), ROOM3D_AT(1600));
   g->z = hd_clamp(g->z, ROOM3D_AT(-210), ROOM3D_AT(1250));
   g->yaw &= ANGLE_FULL - 1;
   g->speed = hd_clamp(g->speed, 0, REMOTE_RUN);
   g->walk = hd_clamp(g->walk, 0, 22000);
   g->phase &= ANGLE_FULL - 1;
   g->cam_yaw &= ANGLE_FULL - 1;
   g->cam_pitch = hd_clamp(g->cam_pitch, REMOTE_CAM_PITCH_MIN, REMOTE_CAM_PITCH_MAX);
   g->cam_dist = hd_clamp(g->cam_dist, CM(40), REMOTE_CAM_DIST);
   g->cam_lift = hd_clamp(g->cam_lift, 0, PLAY3D_LIFT_TOP - g->cam_pitch);
   g->energy = hd_clamp(g->energy, 0, REMOTE_ENERGY_MAX);
   g->work = hd_clamp(g->work, 0, 1 << 30);
   g->points = hd_clamp(g->points, -1000000, 1000000);
   g->act = hd_clamp(g->act, 0, RA_COUNT);
   g->act_t = hd_clamp(g->act_t, 0, 1 << 30);
   g->act_gain = hd_clamp(g->act_gain, -REMOTE_ENERGY_MAX, REMOTE_ENERGY_MAX);
   g->prompt = hd_clamp(g->prompt, 0, RA_COUNT);
   for (i = 0; i < RA_COUNT; i++)
      g->done[i] = hd_clamp(g->done[i], 0, 1000);
   g->toast = hd_clamp(g->toast, 0, RT_COUNT);
   g->toast_t = hd_clamp(g->toast_t, 0, REMOTE_TOAST_FRAMES);
   g->toast_points = hd_clamp(g->toast_points, -100000, 100000);
   g->muse = hd_clamp(g->muse, 0, RT_COUNT);
   g->muse_t = hd_clamp(g->muse_t, 0, REMOTE_MUSE_FRAMES);
   g->screen_scroll = hd_clamp(g->screen_scroll, 0, 1 << 30);
   g->suspicion = hd_clamp(g->suspicion, 0, REMOTE_SUSPICION_MAX);
   g->away_t = hd_clamp(g->away_t, 0, 1 << 30);
   g->over = hd_clamp(g->over, REND_PLAYING, REND_SURVIVED);
   g->over_t = hd_clamp(g->over_t, 0, 1 << 30);
   g->score = hd_clamp(g->score, -10000000, 10000000);
   g->ev_count = hd_clamp(g->ev_count, 0, REMOTE_EVENTS_MAX);
   g->ev_next = hd_clamp(g->ev_next, 0, g->ev_count);
   for (i = 0; i < REMOTE_EVENTS_MAX; i++)
   {
      g->ev_at[i] = hd_clamp(g->ev_at[i], 0, 1 << 20);
      g->ev_kind[i] = hd_clamp(g->ev_kind[i], 0, RE_KINDS - 1);
   }
   g->boss = hd_clamp(g->boss, 0, RE_BOSS_KINDS);
   g->boss_call = hd_clamp(g->boss_call, 0, 1);
   g->boss_limit = hd_clamp(g->boss_limit, 0, 1 << 20);
   g->boss_t = hd_clamp(g->boss_t, 0, 1 << 20);
   g->boss_noise &= (1 << REMOTE_HEARD) - 1;
   g->bell_t = hd_clamp(g->bell_t, 0, REMOTE_BELL_FRAMES);
   g->bell_owed = hd_clamp(g->bell_owed, 0, 1);
   g->drill_t = hd_clamp(g->drill_t, 0, REMOTE_DRILL_MINUTES * FPM);
   g->net_t = hd_clamp(g->net_t, 0, REMOTE_NET_MINUTES * FPM);
   g->washer_t = hd_clamp(g->washer_t, 0, REMOTE_WASHER_MINUTES * FPM);
   g->headphones = hd_clamp(g->headphones, 0, 1);
   g->cat_x = hd_clamp(g->cat_x, ROOM3D_AT(0), ROOM3D_AT(1600));
   g->cat_z = hd_clamp(g->cat_z, ROOM3D_AT(-210), ROOM3D_AT(1250));
   g->cat_y = hd_clamp(g->cat_y, 0, CM(120));
   g->cat_yaw &= ANGLE_FULL - 1;
   g->cat_walk = hd_clamp(g->cat_walk, 0, 16384);
   g->cat_phase &= ANGLE_FULL - 1;
   g->cat_mode = hd_clamp(g->cat_mode, CAT_REST, CAT_DOWN);
   g->cat_t = hd_clamp(g->cat_t, 0, 1 << 20);
   g->cat_spot = hd_clamp(g->cat_spot, 0, SPOT_COUNT - 1);
   g->cat_goal = hd_clamp(g->cat_goal, 0, SPOT_COUNT - 1);
   g->cat_keys = hd_clamp(g->cat_keys, 0, 2);
   g->cat_keys_t = hd_clamp(g->cat_keys_t, 0, 1 << 30);
   g->cat_logged = hd_clamp(g->cat_logged, 0, 1);
   g->mate = hd_clamp(g->mate, 0, 1);
   if (g->mate)
   {
      g->mate_x = hd_clamp(g->mate_x, ROOM3D_AT(0), ROOM3D_AT(1600));
      g->mate_z = hd_clamp(g->mate_z, ROOM3D_AT(-210), ROOM3D_AT(1250));
   }
   else
      g->mate_x = g->mate_z = 0;
   g->mate_yaw &= ANGLE_FULL - 1;
   g->mate_walk = hd_clamp(g->mate_walk, 0, 16384);
   g->mate_phase &= ANGLE_FULL - 1;
   g->mate_goal = hd_clamp(g->mate_goal, 0, MATE_SPOTS - 1);
   g->mate_t = hd_clamp(g->mate_t, 0, 1 << 20);
   g->mate_talk_t = hd_clamp(g->mate_talk_t, 0, 1 << 20);
   g->phone_n = hd_clamp(g->phone_n, 0, 1 << 30);
   for (i = 0; i < REMOTE_PHONE; i++)
   {
      g->phone_text[i] = hd_clamp(g->phone_text[i], 0, RT_COUNT - 1);
      g->phone_who[i] = hd_clamp(g->phone_who[i], 0, RW_COUNT - 1);
      g->phone_min[i] = hd_clamp(g->phone_min[i], 0, REMOTE_DAY_MINUTES);
   }
   g->phone_new_t = hd_clamp(g->phone_new_t, 0, 240);
   g->screen = hd_clamp(g->screen, RSC_TITLE, RSC_CREDITS);
   if (g->screen == RSC_END && !g->over)
      g->screen = RSC_DAY;
   g->screen_t = hd_clamp(g->screen_t, 0, 1 << 30);
   g->menu = hd_clamp(g->menu, 0, 3);
   g->help_page = hd_clamp(g->help_page, 0, REMOTE_HELP_PAGES);
   g->help_back = g->help_back == RSC_DAY ? RSC_DAY : RSC_TITLE;
   g->paused = hd_clamp(g->paused, 0, 1);
   g->attract = hd_clamp(g->attract, 0, 1);
   g->attract_goal = hd_clamp(g->attract_goal, 0, 1 << 20);
   g->attract_t = hd_clamp(g->attract_t, 0, 1 << 20);
   g->best = hd_clamp(g->best, 0, 10000000);
   g->best_new = hd_clamp(g->best_new, 0, 1);
   g->nav &= PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
   g->music_resume = hd_clamp(g->music_resume, 0, 1 << 30);
   g->weekend = hd_clamp(g->weekend, 0, 3);
   g->ask = hd_clamp(g->ask, 0, RE_BOSS_KINDS * 2 - 1);
   g->dlg = hd_clamp(g->dlg, 0, REMOTE_DLG_COUNT);
   g->dlg_sel = hd_clamp(g->dlg_sel, 0, REMOTE_DLG_ANSWERS - 1);
   g->dlg_t = hd_clamp(g->dlg_t, 0, 1 << 20);
   g->dlg_points = hd_clamp(g->dlg_points, -1000000, 1000000);
   g->postponed &= (1 << RE_BOSS_KINDS) - 1;
   g->hotfixes = hd_clamp(g->hotfixes, 0, REMOTE_EVENTS_MAX);
   g->chatter_at = hd_clamp(g->chatter_at, 0, REMOTE_DAY_MINUTES * 2);
}
