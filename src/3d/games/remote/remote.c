/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * TELETRABAJO: CÁMARA APAGADA (golinkhd_load_demo(e, 3), hdrun --demo
 * remote; the design is docs/game-remote-work.md). A remote worker's day
 * in the flat of room3d.c, played in third person.
 *
 * Phase 1: walking. The worker walks and runs where the walk grid lets it
 * (play3d), turns toward where it goes, and a follow camera turned with the
 * right stick or L and R stays behind it, pulled in front of walls.
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

/* Where the worker can walk: the flat and its balcony, never out of the front door (the grid ends at the outer walls). */
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
   g->yaw = 3072;
   g->cam_yaw = 1024;
   g->cam_pitch = 620;
   g->cam_dist = REMOTE_CAM_DIST;
   play3d_follow(target(g), ahead(g), g->cam_yaw, g->cam_pitch, REMOTE_CAM_DIST, &g->cam_dist, &g->cam_lift);
}

void hd_remote_step(hd_state *s, const hd_input in[MAX_PLAYERS])
{
   hd_remote *g = HD_GAME3D(s, hd_remote);
   uint32_t b = in[0].buttons;
   int32_t dx, dz, tilt, want, top;
   ready();
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
   g->walk = (int32_t)((int64_t)g->speed * 16384 / REMOTE_WALK);
   g->walk = hd_min(g->walk, 22000);
   g->phase = (g->phase + (int32_t)((int64_t)g->speed * 60 / REMOTE_WALK)) & (ANGLE_FULL - 1);
   play3d_follow(target(g), ahead(g), g->cam_yaw, g->cam_pitch, REMOTE_CAM_DIST, &g->cam_dist, &g->cam_lift);
   g->t++;
}

void hd_remote_draw(const hd_state *s, void *screen)
{
   hd_surface *sf = (hd_surface *)screen;
   const hd_remote *g = HD_GAME3D_C(s, hd_remote);
   r3d_view v;
   r3d_instance inst[ACT_INSTANCES];
   actor3d_pose pose;
   int32_t n, minutes, scale = hd_res;
   char clock[16];
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
   memset(&pose, 0, sizeof pose);
   pose.at.x = g->x;
   pose.at.z = g->z;
   pose.yaw = g->yaw;
   pose.walk = g->walk;
   pose.phase = g->phase;
   n = actor3d_place(room3d_person(ROOM3D_WORKER), &pose, inst);
   r3d_draw(sf, &v, room3d_scene(), inst, n);
   /* phase 1's HUD: the name, the clock and the buttons */
   minutes = 9 * 60 + hd_min(g->t / REMOTE_FRAMES_PER_MINUTE, 540);
   snprintf(clock, sizeof clock, "%02d:%02d", (int)(minutes / 60), (int)(minutes % 60));
   text_draw(sf, remote_text(RT_TITLE), 10 * scale, 10 * scale, scale, 0xffffff, 1, -1);
   text_draw(sf, clock, HD_OUT_W - text_width(clock, 2 * scale) - 10 * scale, 10 * scale, 2 * scale, 0xffffff, 1, -1);
   text_draw(sf, remote_text(RT_HINT_WALK), 10 * scale, HD_OUT_H - 20 * scale, scale, 0xffffff, 1, -1);
}

void hd_remote_sanitize(hd_state *s)
{
   hd_remote *g = HD_GAME3D(s, hd_remote);
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
}
