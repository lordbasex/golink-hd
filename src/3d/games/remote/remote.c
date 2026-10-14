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
 */
#include <stdio.h>
#include <string.h>
#include "remote.h"
#include "gfx.h"
#include "text.h"
#include "r3d.h"
#include "actor3d.h"
#include "play3d.h"
#include "room3d.h"

#define CM(n) FX_FRAC(n, 100)

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
   int32_t noisy;            /* the boss hears it during a call (phase 3) */
} activity;

static const activity activities[RA_COUNT] = {
   [RA_WORK] = { 950, 1035, 950, 0, 1110, FACE_NORTH + 200, P_SIT | P_TYPE, 0, 0, 0, 0 },
   [RA_NAP] = { 255, 1010, 115, 58, 1010, FACE_EAST, P_LIE, 30, 400, 300, 0 },
   [RA_COFFEE] = { 1495, 105, 1495, 0, 105, FACE_EAST, P_HOLD, 3, 120, 30, 1 },
   [RA_COOK] = { 1450, 108, 1450, 0, 108, FACE_SOUTH, P_HOLD, 25, 300, 250, 1 },
   [RA_FRIDGE] = { 1485, 520, 1485, 0, 520, FACE_EAST, P_HOLD, 5, 80, 60, 0 },
   [RA_EAT] = { 971, 300, 971, 0, 236, FACE_SOUTH, P_SIT | P_HOLD, 20, 250, 200, 0 },
   [RA_SERIES] = { 605, 130, 515, 0, 130, FACE_EAST, P_SIT, 25, 100, 250, 1 },
   [RA_CONSOLE] = { 1375, 950, 1375, 0, 1030, FACE_NORTH, P_SIT | P_HOLD, 20, -50, 350, 1 },
   [RA_GUITAR] = { 1470, 1170, 1470, 0, 1170, FACE_SOUTH, P_HOLD, 15, 50, 220, 1 },
   [RA_SUN] = { 650, -60, 640, 38, -138, FACE_WEST, P_LIE, 15, 150, 200, 0 },
   [RA_PLANTS] = { 440, -135, 440, 0, -135, FACE_SOUTH, P_HOLD, 5, 0, 80, 0 },
   [RA_SHOWER] = { 590, 1060, 590, 30, 1175, FACE_SOUTH, P_STAND, 15, 200, 150, 1 },
   [RA_WASHING] = { 120, 700, 120, 0, 700, FACE_WEST, P_CROUCH, 5, 0, 100, 1 },
};
#define REACH CM(75) /* how near the place the prompt shows */

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

/* The activity whose place is nearest within reach, or -1. */
static int32_t in_reach(const hd_remote *g)
{
   int32_t i, best = -1;
   int64_t best_d = (int64_t)REACH * REACH;
   for (i = 0; i < RA_COUNT; i++)
   {
      int64_t dx = (int64_t)g->x - ROOM3D_AT(activities[i].ux), dz = (int64_t)g->z - ROOM3D_AT(activities[i].uz);
      int64_t d = dx * dx + dz * dz;
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

static void start(hd_remote *g, int32_t a)
{
   const activity *v = &activities[a];
   g->act = a + 1;
   g->act_t = 0;
   g->act_gain = 0;
   g->x = ROOM3D_AT(v->ax);
   g->z = ROOM3D_AT(v->az);
   g->yaw = v->ayaw & (ANGLE_FULL - 1);
   g->speed = 0;
   g->walk = 0;
}

/* The activity ends: whole (it ran its time) or stopped early (a share of its points). */
static void finish(hd_remote *g, int whole)
{
   int32_t a = g->act - 1;
   const activity *v = &activities[a];
   if (a == RA_WORK)
   {
      if (g->act_t >= 30 * REMOTE_FRAMES_PER_MINUTE)
         toast(g, RT_DONE + a, 0);
   }
   else
   {
      int32_t total = v->minutes * REMOTE_FRAMES_PER_MINUTE;
      int32_t pts = whole ? v->points : (int32_t)((int64_t)v->points * g->act_t / hd_max(1, total));
      int32_t again = g->done[a] >= REMOTE_REPEAT_FULL;
      if (again)
         pts /= 5;
      g->points += pts;
      if (whole || g->act_t * 2 >= total)
         g->done[a]++;
      toast(g, again ? RT_AGAIN : RT_DONE + a, pts);
   }
   g->act = 0;
   g->x = ROOM3D_AT(v->ux);
   g->z = ROOM3D_AT(v->uz);
}

/* One frame of an activity under way. */
static void doing(hd_remote *g, int pressed)
{
   int32_t a = g->act - 1;
   const activity *v = &activities[a];
   int minute = (g->t % REMOTE_FRAMES_PER_MINUTE) == REMOTE_FRAMES_PER_MINUTE - 1;
   g->act_t++;
   if (a == RA_WORK)
   {
      /* full speed with energy, half when tired, nothing asleep on the keyboard */
      int32_t rate = g->energy > REMOTE_WORK_TIRED ? 2 : g->energy >= REMOTE_WORK_ASLEEP ? 1 : 0;
      g->work += rate;
      g->screen_scroll += rate;
      if (minute)
         g->energy = hd_max(0, g->energy - REMOTE_WORK_DRAIN);
   }
   else
   {
      /* the energy comes as it goes, so stopping early keeps what was had */
      int32_t total = v->minutes * REMOTE_FRAMES_PER_MINUTE;
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

void hd_remote_start(hd_state *s)
{
   hd_remote *g = HD_GAME3D(s, hd_remote);
   ready();
   memset(&s->show, 0, sizeof s->show);
   s->show.on = 3;
   memset(g, 0, sizeof *g);
   /* in the hall, facing east toward the front door, the camera behind */
   g->x = ROOM3D_AT(450);
   g->z = ROOM3D_AT(725);
   g->yaw = FACE_EAST;
   g->cam_yaw = 1024;
   g->cam_pitch = 620;
   g->cam_dist = REMOTE_CAM_DIST;
   g->energy = REMOTE_ENERGY_START;
   play3d_follow(target(g), ahead(g), g->cam_yaw, g->cam_pitch, REMOTE_CAM_DIST, &g->cam_dist, &g->cam_lift);
}

void hd_remote_step(hd_state *s, const hd_input in[MAX_PLAYERS])
{
   hd_remote *g = HD_GAME3D(s, hd_remote);
   uint32_t b = in[0].buttons;
   int pressed = (b & ~g->buttons & PAD_JUMP) != 0;
   int32_t dx, dz, tilt, want, top;
   ready();
   g->buttons = b;
   /* the camera: L and R or the right stick turn it, the right stick's up and down tilt it */
   if (b & PAD_L)
      g->cam_yaw += 28;
   if (b & PAD_R)
      g->cam_yaw -= 28;
   if (hd_abs(in[0].rx) > 6000)
      g->cam_yaw -= in[0].rx * 36 / 32767;
   if (hd_abs(in[0].ry) > 6000)
      g->cam_pitch = hd_clamp(g->cam_pitch - in[0].ry * 14 / 32767, REMOTE_CAM_PITCH_MIN, REMOTE_CAM_PITCH_MAX);
   g->cam_yaw &= ANGLE_FULL - 1;
   if (g->toast_t > 0)
      g->toast_t--;
   if (g->act)
   {
      g->prompt = 0;
      doing(g, pressed);
   }
   else
   {
      /* the walk: toward where the stick points as the camera sees it, turning on the way */
      play3d_stick(in[0].lx, in[0].ly, b, g->cam_yaw, &dx, &dz);
      tilt = play3d_sqrt((int64_t)dx * dx + (int64_t)dz * dz);
      top = (b & PAD_RUN) ? REMOTE_RUN : REMOTE_WALK;
      want = (int32_t)((int64_t)top * hd_min(tilt, 16384) / 16384);
      if (tilt)
         g->yaw = play3d_turn(g->yaw, (play3d_angle(dx, dz) + ANGLE_FULL / 2) & (ANGLE_FULL - 1), REMOTE_TURN);
      g->speed += hd_clamp(want - g->speed, -REMOTE_ACCEL * 2, REMOTE_ACCEL);
      if (g->speed > 0)
      {
         /* along where the worker faces (its face looks along -z of its own parts) */
         int32_t mx = (int32_t)(((int64_t)-g->speed * hd_sin(g->yaw)) >> 14);
         int32_t mz = (int32_t)(((int64_t)-g->speed * hd_cos(g->yaw)) >> 14);
         if (!play3d_move(&g->x, &g->z, mx, mz, REMOTE_RADIUS))
            g->speed /= 2; /* against a wall: no walking on the spot */
      }
      /* the legs follow the speed: a full walk at walking speed, longer strides running */
      g->walk = hd_min((int32_t)((int64_t)g->speed * 16384 / REMOTE_WALK), 22000);
      g->phase = (g->phase + (int32_t)((int64_t)g->speed * 60 / REMOTE_WALK)) & (ANGLE_FULL - 1);
      g->prompt = in_reach(g) + 1;
      if (pressed && g->prompt)
         start(g, g->prompt - 1);
   }
   play3d_follow(target(g), ahead(g), g->cam_yaw, g->cam_pitch, REMOTE_CAM_DIST, &g->cam_dist, &g->cam_lift);
   g->t++;
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

static void hud(hd_surface *sf, const hd_remote *g)
{
   int32_t scale = hd_res, minutes = 9 * 60 + hd_min(g->t / REMOTE_FRAMES_PER_MINUTE, REMOTE_DAY_MINUTES), worked = g->work / REMOTE_WORK_UNIT;
   int32_t e = g->energy * 256 / REMOTE_ENERGY_MAX;
   char line[96];
   /* top left: the clock, the work done and the energy */
   panel(sf, 6 * scale, 6 * scale, 150 * scale, 76 * scale, 0x101418, 150);
   snprintf(line, sizeof line, "%02d:%02d", (int)(minutes / 60), (int)(minutes % 60));
   text_draw(sf, line, 12 * scale, 11 * scale, 2 * scale, 0xffffff, 1, -1);
   snprintf(line, sizeof line, "%s %d h %02d / 6 h", remote_text(RT_WORK), (int)(worked / 60), (int)(worked % 60));
   bar(sf, 12 * scale, 32 * scale, 138 * scale, worked * 256 / REMOTE_WORK_GOAL, worked >= REMOTE_WORK_GOAL ? 0x57cc99 : 0x4ea8de, line, scale);
   bar(sf, 12 * scale, 56 * scale, 138 * scale, e, e > 77 ? 0xf6bd60 : 0xe63946, remote_text(RT_ENERGY), scale);
   /* top right: the points */
   snprintf(line, sizeof line, "%s %d", remote_text(RT_POINTS), (int)g->points);
   panel(sf, HD_OUT_W - text_width(line, scale) - 18 * scale, 6 * scale, text_width(line, scale) + 12 * scale, 16 * scale, 0x101418, 150);
   text_draw(sf, line, HD_OUT_W - text_width(line, scale) - 12 * scale, 10 * scale, scale, 0xffe66d, 1, -1);
   /* bottom: what the button does, or what is being done and how far it went */
   if (g->act)
   {
      int32_t a = g->act - 1, total = activities[a].minutes * REMOTE_FRAMES_PER_MINUTE;
      const char *what = a == RA_WORK && g->energy < REMOTE_WORK_ASLEEP ? remote_text(RT_ASLEEP) : remote_text(RT_DOING + a);
      int32_t w = hd_max(text_width(what, scale), text_width(remote_text(RT_STOP), scale)) + 24 * scale, x = (HD_OUT_W - w) / 2, y = HD_OUT_H - 46 * scale;
      panel(sf, x, y, w, 38 * scale, 0x101418, 170);
      text_center(sf, what, y + 6 * scale, scale, 0xffffff);
      if (total > 0)
      {
         panel(sf, x + 8 * scale, y + 18 * scale, w - 16 * scale, 4 * scale, 0x000000, 160);
         gfx_fill(sf, x + 8 * scale, y + 18 * scale, (int32_t)((int64_t)(w - 16 * scale) * hd_min(g->act_t, total) / total), 4 * scale, 0x57cc99);
      }
      text_center(sf, remote_text(RT_STOP), y + 26 * scale, scale, 0xbfc7d5);
   }
   else if (g->prompt)
   {
      const char *what = remote_text(RT_PROMPT + g->prompt - 1);
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
      int32_t w = text_width(msg, scale) + 24 * scale, y = 90 * scale;
      panel(sf, (HD_OUT_W - w) / 2, y, w, (g->toast_points ? 30 : 18) * scale, 0x101418, 170);
      text_center(sf, msg, y + 5 * scale, scale, 0xffffff);
      if (g->toast_points)
      {
         snprintf(line, sizeof line, "+%d", (int)g->toast_points);
         text_center(sf, line, y + 17 * scale, scale, 0xffe66d);
      }
   }
}

void hd_remote_draw(const hd_state *s, void *screen)
{
   hd_surface *sf = (hd_surface *)screen;
   const hd_remote *g = HD_GAME3D_C(s, hd_remote);
   r3d_view v;
   r3d_instance inst[ACT_INSTANCES];
   actor3d_pose pose;
   int32_t n, working = g->act == RA_WORK + 1;
   ready();
   memset(&v, 0, sizeof v);
   v.eye = play3d_eye(target(g), g->cam_yaw, g->cam_pitch + g->cam_lift, g->cam_dist);
   v.yaw = g->cam_yaw;
   v.pitch = g->cam_pitch + g->cam_lift;
   v.fov = 560;
   v.near_z = CM(10);
   v.sky_top = 0x6f9fd0;
   v.sky_bottom = 0xd6e6f2;
   v.exposure = 256;
   v.smooth = 1;
   v.bloom = 200;
   v.vignette = 110;
   v.light = room3d_light();
   /* the desk's screens: code going up while working, asleep otherwise */
   tex3d_terminal(room3d_desk_screen(0), 11, g->screen_scroll, working && g->energy >= REMOTE_WORK_ASLEEP);
   tex3d_terminal(room3d_desk_screen(1), 12, g->screen_scroll / 2 + 40, working && g->energy >= REMOTE_WORK_ASLEEP);
   memset(&pose, 0, sizeof pose);
   pose.at.x = g->x;
   pose.at.z = g->z;
   pose.yaw = g->yaw;
   pose.walk = g->walk;
   pose.phase = g->phase;
   if (g->act)
   {
      const activity *a = &activities[g->act - 1];
      pose.at.y = CM(a->ay);
      pose.floor_y = CM(a->ay);
      pose.sit = (a->pose & P_SIT) != 0;
      pose.type = (a->pose & P_TYPE) != 0;
      pose.hold = (a->pose & P_HOLD) != 0;
      pose.crouch = (a->pose & P_CROUCH) != 0;
      pose.lie = (a->pose & P_LIE) != 0;
      pose.phase = (g->t * 40) & 4095;
      if (working)
      {
         pose.nod = g->energy < REMOTE_WORK_ASLEEP ? -420 : -80; /* asleep: the head down on the desk */
         pose.type = g->energy >= REMOTE_WORK_ASLEEP;
         pose.look = ((g->t / 240) & 1) ? 180 : 0;
      }
   }
   n = actor3d_place(room3d_person(ROOM3D_WORKER), &pose, inst);
   r3d_draw(sf, &v, room3d_scene(), inst, n);
   hud(sf, g);
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
}
