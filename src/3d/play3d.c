/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Walking in a 3D world (play3d.h): the walk grid, moving with slides,
 * the stick relative to the camera and the follow camera.
 */
#include <string.h>
#include "hd.h"
#include "play3d.h"

#define CM(n) FX_FRAC(n, 100)

static uint8_t grid[PLAY3D_GRID_MAX];
static int32_t grid_x0, grid_z0, grid_nx, grid_nz;

int32_t play3d_grid(r3d_vec lo, r3d_vec hi, int32_t floor_y)
{
   int32_t i, k, n = 0;
   memset(grid, 0, sizeof grid);
   grid_x0 = lo.x;
   grid_z0 = lo.z;
   grid_nx = hd_clamp((hi.x - lo.x) / PLAY3D_CELL, 0, 256);
   grid_nz = hd_clamp((hi.z - lo.z) / PLAY3D_CELL, 0, 256);
   for (k = 0; k < grid_nz; k++)
      for (i = 0; i < grid_nx; i++)
      {
         int32_t q, y, free_cell = 1;
         /* four points a cell, a quarter in from its corners: the voxels it covers */
         for (q = 0; q < 4 && free_cell; q++)
         {
            r3d_vec p;
            p.x = grid_x0 + i * PLAY3D_CELL + ((q & 1) ? CM(7) : CM(3));
            p.z = grid_z0 + k * PLAY3D_CELL + ((q & 2) ? CM(7) : CM(3));
            p.y = floor_y - CM(2);
            if (!r3d_solid_at(p))
               free_cell = 0; /* no floor: the air off a balcony */
            for (y = 15; y <= 170 && free_cell; y += 5)
            {
               p.y = floor_y + CM(y);
               if (r3d_solid_at(p))
                  free_cell = 0;
            }
         }
         grid[k * grid_nx + i] = (uint8_t)free_cell;
         n += free_cell;
      }
   return n;
}

static int32_t cell_free(int32_t x, int32_t z)
{
   int32_t i, k;
   if (x < grid_x0 || z < grid_z0)
      return 0;
   i = (x - grid_x0) / PLAY3D_CELL;
   k = (z - grid_z0) / PLAY3D_CELL;
   return i < grid_nx && k < grid_nz && grid[k * grid_nx + i];
}

int32_t play3d_fits(int32_t x, int32_t z, int32_t radius)
{
   /* the middle and eight points around it (cos 45 degrees is 11585 / 16384) */
   int32_t d = (int32_t)(((int64_t)radius * 11585) >> 14);
   return cell_free(x, z) && cell_free(x + radius, z) && cell_free(x - radius, z) && cell_free(x, z + radius) && cell_free(x, z - radius) &&
          cell_free(x + d, z + d) && cell_free(x - d, z + d) && cell_free(x + d, z - d) && cell_free(x - d, z - d);
}

int32_t play3d_move(int32_t *x, int32_t *z, int32_t dx, int32_t dz, int32_t radius)
{
   if (!dx && !dz)
      return 0;
   if (play3d_fits(*x + dx, *z + dz, radius))
   {
      *x += dx, *z += dz;
      return 1;
   }
   /* slide along the wall: the part of the step that still fits */
   if (dx && play3d_fits(*x + dx, *z, radius))
   {
      *x += dx;
      return 1;
   }
   if (dz && play3d_fits(*x, *z + dz, radius))
   {
      *z += dz;
      return 1;
   }
   /* around a corner or into a doorway: the step turned 30 and then 60 degrees each way */
   {
      static const int32_t turns[4] = { 341, -341, 683, -683 };
      int32_t i;
      for (i = 0; i < 4; i++)
      {
         int32_t c = hd_cos(turns[i]), sn = hd_sin(turns[i]);
         int32_t tx = (int32_t)(((int64_t)dx * c + (int64_t)dz * sn) >> 14);
         int32_t tz = (int32_t)(((int64_t)dz * c - (int64_t)dx * sn) >> 14);
         if (play3d_fits(*x + tx, *z + tz, radius))
         {
            *x += tx, *z += tz;
            return 1;
         }
      }
   }
   /* a doorway beside: the nearest sideways place (up to 30 cm) from where the walk goes on, and a step toward it */
   {
      int32_t len = play3d_sqrt((int64_t)dx * dx + (int64_t)dz * dz), k, side;
      int32_t px = (int32_t)((int64_t)-dz * CM(5) / len), pz = (int32_t)((int64_t)dx * CM(5) / len);
      int32_t fx = (int32_t)((int64_t)dx * CM(10) / len), fz = (int32_t)((int64_t)dz * CM(10) / len);
      for (k = 1; k <= 6; k++)
         for (side = 1; side >= -1; side -= 2)
         {
            int32_t ox = side * px * k, oz = side * pz * k, sx, sz;
            if (!play3d_fits(*x + ox, *z + oz, radius) || !play3d_fits(*x + ox + fx, *z + oz + fz, radius))
               continue;
            /* a step as long as the walk's, no further than the place itself */
            sx = (int32_t)((int64_t)side * px * len / CM(5));
            sz = (int32_t)((int64_t)side * pz * len / CM(5));
            if (hd_abs(sx) > hd_abs(ox) || hd_abs(sz) > hd_abs(oz))
               sx = ox, sz = oz;
            if (play3d_fits(*x + sx, *z + sz, radius))
            {
               *x += sx, *z += sz;
               return 1;
            }
         }
   }
   return 0;
}

static int64_t facing(int32_t a, int32_t dx, int32_t dz)
{
   return (int64_t)dx * hd_sin(a) + (int64_t)dz * hd_cos(a);
}

int32_t play3d_angle(int32_t dx, int32_t dz)
{
   int32_t best = 0, step, a;
   int64_t most;
   if (!dx && !dz)
      return 0;
   /* the best of 16 directions, then closer and closer around it */
   most = facing(0, dx, dz);
   for (a = 256; a < ANGLE_FULL; a += 256)
      if (facing(a, dx, dz) > most)
         most = facing(a, dx, dz), best = a;
   for (step = 128; step; step >>= 1)
   {
      int32_t up = (best + step) & (ANGLE_FULL - 1), down = (best - step) & (ANGLE_FULL - 1);
      int64_t fu = facing(up, dx, dz), fd = facing(down, dx, dz);
      if (fu > most && fu >= fd)
         best = up, most = fu;
      else if (fd > most)
         best = down, most = fd;
   }
   return best;
}

int32_t play3d_sqrt(int64_t v)
{
   int64_t r = 0, bit = (int64_t)1 << 62;
   if (v <= 0)
      return 0;
   while (bit > v)
      bit >>= 2;
   while (bit)
   {
      if (v >= r + bit)
      {
         v -= r + bit;
         r = (r >> 1) + bit;
      }
      else
         r >>= 1;
      bit >>= 2;
   }
   return (int32_t)r;
}

int32_t play3d_turn(int32_t from, int32_t want, int32_t step)
{
   int32_t d = ((want - from + ANGLE_FULL / 2) & (ANGLE_FULL - 1)) - ANGLE_FULL / 2;
   return (from + hd_clamp(d, -step, step)) & (ANGLE_FULL - 1);
}

void play3d_stick(int32_t lx, int32_t ly, uint32_t buttons, int32_t cam_yaw, int32_t *dx, int32_t *dz)
{
   const int32_t dead = 6000;
   int32_t sx = lx, sy = ly, len, tilt, fx, fz;
   *dx = *dz = 0;
   if (buttons & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN))
   {
      sx = (buttons & PAD_RIGHT) ? 32767 : (buttons & PAD_LEFT) ? -32767 : 0;
      sy = (buttons & PAD_DOWN) ? 32767 : (buttons & PAD_UP) ? -32767 : 0;
   }
   len = play3d_sqrt((int64_t)sx * sx + (int64_t)sy * sy);
   if (len <= dead)
      return;
   /* the tilt past the dead zone, 0..16384 */
   tilt = (int32_t)((int64_t)hd_min(len - dead, 32767 - dead) * 16384 / (32767 - dead));
   /* the stick's direction at that length: up is the camera's forward, right its right */
   fx = (int32_t)((int64_t)sx * tilt / len);
   fz = (int32_t)((int64_t)-sy * tilt / len);
   *dx = (int32_t)(((int64_t)fz * hd_sin(cam_yaw) + (int64_t)fx * hd_cos(cam_yaw)) >> 14);
   *dz = (int32_t)(((int64_t)fz * hd_cos(cam_yaw) - (int64_t)fx * hd_sin(cam_yaw)) >> 14);
}

r3d_vec play3d_eye(r3d_vec target, int32_t yaw, int32_t pitch, int32_t dist)
{
   r3d_vec e;
   int32_t flat = (int32_t)(((int64_t)dist * hd_cos(pitch)) >> 14);
   e.x = target.x - (int32_t)(((int64_t)flat * hd_sin(yaw)) >> 14);
   e.z = target.z - (int32_t)(((int64_t)flat * hd_cos(yaw)) >> 14);
   e.y = target.y + (int32_t)(((int64_t)dist * hd_sin(pitch)) >> 14);
   return e;
}

/* How far one line of sight from `eye` reaches `target`, 0..65536, not counting the first 30 cm (the person's own size, a door frame it walks through). */
static int32_t line_clear(r3d_vec target, r3d_vec eye, int32_t want)
{
   int32_t skip = (int32_t)((int64_t)CM(30) * 65536 / hd_max(want, CM(31)));
   r3d_vec from;
   from.x = target.x + (int32_t)(((int64_t)(eye.x - target.x) * skip) >> 16);
   from.y = target.y + (int32_t)(((int64_t)(eye.y - target.y) * skip) >> 16);
   from.z = target.z + (int32_t)(((int64_t)(eye.z - target.z) * skip) >> 16);
   return skip + (int32_t)(((int64_t)(65536 - skip) * r3d_reach(from, eye)) >> 16);
}

/*
 * How far the view reaches the person: the best of three lines of sight,
 * to the target and 15 cm to each side of it, so a person walking along a
 * wall (the line through the wall's own edge) is still seen.
 */
static int32_t view_clear(r3d_vec target, r3d_vec eye, int32_t yaw, int32_t want)
{
   int32_t best = line_clear(target, eye, want), side;
   for (side = -1; side <= 1 && best < 65536; side += 2)
   {
      r3d_vec t = target, e = eye;
      int32_t ox = side * (int32_t)(((int64_t)CM(15) * hd_cos(yaw)) >> 14), oz = -side * (int32_t)(((int64_t)CM(15) * hd_sin(yaw)) >> 14);
      t.x += ox, t.z += oz, e.x += ox, e.z += oz;
      best = hd_max(best, line_clear(t, e, want));
   }
   return best;
}

static int32_t lift_needed(r3d_vec target, int32_t yaw, int32_t pitch, int32_t want)
{
   int32_t need = 0;
   while (pitch + need < PLAY3D_LIFT_TOP && view_clear(target, play3d_eye(target, yaw, pitch + need, want), yaw, want) < 65536)
      need += 32;
   return hd_min(need, hd_max(0, PLAY3D_LIFT_TOP - pitch));
}

void play3d_follow(r3d_vec target, r3d_vec ahead, int32_t yaw, int32_t pitch, int32_t want, int32_t *dist, int32_t *lift)
{
   int32_t room;
   /* the least the camera must rise (over walls: worlds without a ceiling) to see the target, now and where it goes */
   int32_t need = hd_max(lift_needed(target, yaw, pitch, want), lift_needed(ahead, yaw, pitch, want));
   *lift += hd_clamp(need - *lift, -6, 40); /* up quickly, down slowly */
   /* what still stands in the way at the height it has now: in at once, out slowly */
   room = (int32_t)(((int64_t)want * view_clear(target, play3d_eye(target, yaw, pitch + *lift, want), yaw, want)) >> 16) - CM(15);
   room = hd_clamp(room, CM(40), want);
   if (room < *dist)
      *dist = room;
   else
      *dist += (room - *dist + 7) / 8;
}
