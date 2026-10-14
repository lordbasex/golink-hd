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
   [RA_SHOWER] = { 590, 1060, 590, 30, 1175, FACE_SOUTH, P_STAND, 15, 200, 150 },
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
   static int built;
   r3d_vec lo, hi;
   if (built)
      return;
   hd_r3d_build();
   lo.x = ROOM3D_AT(0), lo.y = 0, lo.z = ROOM3D_AT(-210);
   hi.x = ROOM3D_AT(1600), hi.y = 0, hi.z = ROOM3D_AT(1250);
   play3d_grid(lo, hi, 0);
   built = 1;
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
      kinds[n++] = RE_CALL + rng_range(&s->rng, RE_BOSS_KINDS - 1);
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
   if (kind < RE_BOSS_KINDS)
   {
      g->boss = kind + 1;
      g->boss_call = 0;
      g->boss_t = kind == RE_MEETING ? hd_max(10 * 60, (MEETING_AT - g->t / FPM) * FPM) : boss_events[kind].seconds * 60;
      g->boss_limit = g->boss_t;
      phone(g, RW_BOSS, RT_BOSS_ASK + kind * 2 + rng_range(&s->rng, 2));
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
   static const int32_t rise[5] = { REMOTE_NOISE, REMOTE_NOISE, REMOTE_NOISE, 0, REMOTE_NOISE };
   int32_t heard[5], i;
   heard[0] = g->washer_t > 0;
   heard[1] = g->bell_owed;
   heard[2] = g->mate && g->mate_x > ROOM3D_AT(750) && g->mate_x < ROOM3D_AT(1150) && g->mate_z > ROOM3D_AT(800);
   heard[3] = g->drill_t > 0 && !g->headphones;
   heard[4] = g->energy < REMOTE_WORK_ASLEEP;
   for (i = 0; i < 5; i++)
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

static void day(hd_state *s, hd_remote *g)
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
         if (at_desk(g) && g->energy >= REMOTE_WORK_ASLEEP)
         {
            suspect(g, -boss_events[kind].answered);
            phone(g, RW_YOU, RT_BOSS_YES + kind);
            if (boss_events[kind].minutes)
            {
               g->boss_call = 1;
               g->boss_t = boss_events[kind].minutes * FPM;
               g->boss_noise = 0;
            }
            else
               g->boss = 0;
         }
         else if (--g->boss_t <= 0)
         {
            suspect(g, boss_events[kind].missed);
            phone(g, RW_BOSS, RT_BOSS_MISS + kind);
            g->boss = 0;
         }
      }
      else
      {
         on_call(g);
         if (--g->boss_t <= 0)
            g->boss = 0, g->boss_call = 0;
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
         suspect(g, g->energy < REMOTE_WORK_ASLEEP ? REMOTE_ASLEEP_RISE : -REMOTE_WORK_CALM);
      else if (g->away_t > REMOTE_AWAY_GRACE * FPM)
         suspect(g, REMOTE_AWAY_RISE);
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
      return g->boss == RE_MEETING + 1 ? RS_MEETING : RS_CALL;
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
   int32_t best = g->best;
   memset(g, 0, sizeof *g);
   g->best = best;
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

void hd_remote_start(hd_state *s)
{
   hd_remote *g = HD_GAME3D(s, hd_remote);
   ready();
   memset(&s->show, 0, sizeof s->show);
   s->show.on = 3;
   memset(g, 0, sizeof *g);
   g->screen = RSC_TITLE;
}

static void to_title(hd_remote *g)
{
   int32_t best = g->best;
   memset(g, 0, sizeof *g);
   g->best = best;
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
   int pressed = (b & ~g->buttons & PAD_JUMP) != 0;
   int32_t dx, dz, tilt, want, top;
   g->buttons = b;
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
   day(s, g);
   play3d_follow(target(g), ahead(g), g->cam_yaw, g->cam_pitch, REMOTE_CAM_DIST, &g->cam_dist, &g->cam_lift);
   g->t++;
}

/* ---- the menus ---- */

/* A menu's line moved by the D-pad or the stick; returns 1 when the line was chosen (A or START). */
static int menu_step(hd_remote *g, const hd_input *in, int32_t lines)
{
   uint32_t b = in->buttons, nav = 0, fresh;
   int chosen;
   if (in->ly < -16000)
      nav |= PAD_UP;
   if (in->ly > 16000)
      nav |= PAD_DOWN;
   nav |= b & (PAD_UP | PAD_DOWN);
   fresh = nav & ~(uint32_t)g->nav;
   g->nav = (int32_t)nav;
   if (fresh & PAD_UP)
      g->menu = (g->menu + lines - 1) % lines;
   if (fresh & PAD_DOWN)
      g->menu = (g->menu + 1) % lines;
   chosen = (b & ~g->buttons & (PAD_JUMP | PAD_START)) != 0;
   g->buttons = b;
   return chosen;
}

static int any_input(const hd_input *in)
{
   return in->buttons || hd_abs(in->lx) > 16000 || hd_abs(in->ly) > 16000;
}

void hd_remote_step(hd_state *s, const hd_input in[MAX_PLAYERS])
{
   hd_remote *g = HD_GAME3D(s, hd_remote);
   uint32_t b = in[0].buttons;
   ready();
   g->screen_t = hd_min(g->screen_t + 1, 1 << 30);
   switch (g->screen)
   {
   case RSC_TITLE:
      if (menu_step(g, &in[0], 2))
      {
         if (g->menu == 0)
            start_morning(s, g, (uint32_t)g->screen_t);
         else
         {
            g->screen = RSC_HELP;
            g->help_page = 0;
            g->help_back = RSC_TITLE;
         }
      }
      else if (any_input(&in[0]))
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
         if (menu_step(g, &in[0], 4))
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
static const uint32_t status_color[5] = { 0x57cc99, 0xf6bd60, 0xe63946, 0xe63946, 0x8d99ae };

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
   if (g->boss && !g->boss_call)
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
   if (g->boss && !g->boss_call)
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
   int32_t start = g->over == REND_FIRED ? 150 : 30, k = g->over_t - start, rows = 0, labels[4], values[4], colors[4], i, total_at;
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
   labels[rows] = RT_END_POINTS, values[rows] = g->points, colors[rows++] = 0xffe66d;
   if (g->over == REND_MISSED)
      labels[rows] = RT_END_HALF, values[rows] = -(g->points - g->score), colors[rows++] = 0xe63946;
   if (g->over == REND_SURVIVED)
   {
      labels[rows] = RT_END_SURVIVED, values[rows] = 1000, colors[rows++] = 0x57cc99;
      labels[rows] = RT_END_DISCRETION, values[rows] = REMOTE_SUSPICION_MAX - g->suspicion, colors[rows++] = 0x57cc99;
      if (worked > REMOTE_OVERTIME)
         labels[rows] = RT_END_OVERTIME, values[rows] = -2 * (worked - REMOTE_OVERTIME), colors[rows++] = 0xe63946;
   }
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
   /* bottom: what the button does, or what is being done and how far it went */
   if (g->act)
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
}

/* The worker's pose from the state: walking, or the activity's pose. */
static void worker_pose(const hd_remote *g, actor3d_pose *pose)
{
   memset(pose, 0, sizeof *pose);
   pose->at.x = g->x;
   pose->at.z = g->z;
   pose->yaw = g->yaw;
   pose->walk = g->walk;
   pose->phase = g->phase;
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
      pose->phase = (g->t * 40) & 4095;
      if (g->act == RA_WORK + 1)
      {
         pose->nod = g->energy < REMOTE_WORK_ASLEEP ? -420 : -80; /* asleep: the head down on the desk */
         pose->type = g->energy >= REMOTE_WORK_ASLEEP;
         pose->look = ((g->t / 240) & 1) ? 180 : 0;
      }
   }
}

/* The house seen from `v` with the worker (in `worker`'s pose), the flatmate and the cat. */
static void scene(hd_surface *sf, const hd_remote *g, r3d_view *v, const actor3d_pose *worker)
{
   r3d_instance inst[ACT_INSTANCES * 2 + PET_INSTANCES];
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
   n = actor3d_place(room3d_person(ROOM3D_WORKER), worker, inst);
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
      n += actor3d_place(room3d_person(ROOM3D_FLATMATE), &pose, inst + n);
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
   r3d_draw(sf, v, room3d_scene(), inst, n);
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
         panel(sf, x - 14 * scale, yy - 5 * scale, w + 28 * scale, 22 * scale, 0xffe66d, 60);
         text_draw(sf, ">", x - 12 * scale, yy, 2 * scale, 0xffe66d, 1, -1);
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
   static const int32_t items[2] = { RT_MENU_PLAY, RT_MENU_HELP };
   char line[64];
   title_text(sf, 40 * scale, scale);
   panel(sf, (HD_OUT_W - 300 * scale) / 2, 166 * scale, 300 * scale, (g->best > 0 ? 98 : 74) * scale, 0x0b0d12, 175);
   menu_draw(sf, items, 2, g->menu, 180 * scale, scale);
   if (g->best > 0)
   {
      snprintf(line, sizeof line, "%s: %d", remote_text(RT_RECORD), (int)g->best);
      text_center(sf, line, 238 * scale, scale, 0xbfc7d5);
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

static void pause_hud(hd_surface *sf, const hd_remote *g, int32_t scale)
{
   static const int32_t items[4] = { RT_PAUSE_GO_ON, RT_MENU_HELP, RT_PAUSE_RESTART, RT_PAUSE_QUIT };
   panel(sf, 0, 0, HD_OUT_W, HD_OUT_H, 0x0b0d12, 170);
   text_center(sf, remote_text(RT_PAUSE), 60 * scale, 4 * scale, 0xffe66d);
   menu_draw(sf, items, 4, g->menu, 130 * scale, scale);
}

void hd_remote_draw(const hd_state *s, void *screen)
{
   hd_surface *sf = (hd_surface *)screen;
   const hd_remote *g = HD_GAME3D_C(s, hd_remote);
   int32_t scale = hd_res;
   r3d_view v;
   actor3d_pose pose;
   ready();
   if (g->screen == RSC_TITLE || (g->screen == RSC_HELP && g->help_back == RSC_TITLE))
   {
      title_scene(sf, g);
      if (g->screen == RSC_TITLE)
         title_hud(sf, g, scale);
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
   g->boss_noise &= 31;
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
   g->screen = hd_clamp(g->screen, RSC_TITLE, RSC_END);
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
   g->nav &= PAD_UP | PAD_DOWN;
}
