/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Floors and roads seen in perspective, the arcade way: Mode 7 (a picture
 * turned and scaled per row, for karts and flying stages) and the pseudo 3D
 * road (segments with curves and hills, for racing seen from behind).
 */
#include "gfx.h"
#include "road.h"

void gfx_mode7(hd_surface *s, const hd_image *floor, const hd_mode7 *m)
{
   int32_t c = hd_cos(m->angle), sn = hd_sin(m->angle), y, x;
   int32_t depth = s->h - m->horizon;
   for (y = hd_max(m->horizon + 1, 0); y < s->h; y++)
   {
      int32_t dy = y - m->horizon;
      /* how far this row is, and how much floor one pixel covers (16.16) */
      int64_t z = ((int64_t)m->height << 16) * m->focal / dy;
      int64_t step = z / m->focal;
      int64_t wx = (int64_t)m->x + ((z * c) >> 14) - (((int64_t)(s->w / 2) * step * -sn) >> 14);
      int64_t wy = (int64_t)m->y + ((z * sn) >> 14) - (((int64_t)(s->w / 2) * step * c) >> 14);
      int64_t dx = (step * -sn) >> 14, dyw = (step * c) >> 14;
      int32_t fog = m->fog > 0 ? m->fog * hd_max(0, depth / 2 - dy) / hd_max(1, depth / 2) : 0;
      uint32_t *row = s->px + y * s->w;
      for (x = 0; x < s->w; x++, wx += dx, wy += dyw)
      {
         int32_t u = (int32_t)(wx >> 16), v = (int32_t)(wy >> 16);
         uint32_t col;
         if (m->wrap)
         {
            u = ((u % floor->w) + floor->w) % floor->w;
            v = ((v % floor->h) + floor->h) % floor->h;
            col = floor->px[v * floor->w + u] & 0xffffffu;
         }
         else if ((uint32_t)u < (uint32_t)floor->w && (uint32_t)v < (uint32_t)floor->h)
            col = floor->px[v * floor->w + u] & 0xffffffu;
         else
            col = m->outside;
         row[x] = fog ? gfx_mix(col, m->fog_color, fog) : col;
      }
   }
}

int gfx_mode7_project(const hd_mode7 *m, int32_t wx, int32_t wy, int32_t screen_w, int32_t *sx, int32_t *sy, int32_t *scale)
{
   int64_t rx = (int64_t)wx - m->x, ry = (int64_t)wy - m->y;
   int32_t c = hd_cos(m->angle), sn = hd_sin(m->angle);
   int64_t f = (rx * c + ry * sn) >> 14, side = (ry * c - rx * sn) >> 14; /* 16.16 */
   if (f < FX(4))
      return 0;
   *sy = m->horizon + (int32_t)((((int64_t)m->height << 16) * m->focal) / f);
   *sx = screen_w / 2 + (int32_t)(side * m->focal / f);
   *scale = (int32_t)(((int64_t)m->focal << 32) / f); /* FX_ONE at the focal distance */
   if (*scale > FX(16))
      *scale = FX(16);
   return 1;
}

/* The road (road.h). */

/* Where a point of the road lands on screen. */
typedef struct
{
   int32_t x, y, w; /* screen centre, row, half width of the road */
} proj;

static void project(proj *p, int64_t wx, int64_t wy, int64_t z, int32_t sw, int32_t sh)
{
   if (z < 1)
      z = 1;
   p->x = (int32_t)(sw / 2 + (((int64_t)ROAD_DEPTH * wx * (sw / 2) / z) >> 16));
   p->y = (int32_t)(sh / 2 - (((int64_t)ROAD_DEPTH * wy * (sh / 2) / z) >> 16));
   p->w = (int32_t)(((int64_t)ROAD_DEPTH * ROAD_WIDTH * (sw / 2) / z) >> 16);
}

static void band(hd_surface *s, int32_t y0, int32_t y1, int32_t x0, int32_t w0, int32_t x1, int32_t w1, int32_t dark, int32_t horizon_clip)
{
   int32_t y;
   uint32_t grass = dark ? 0x3a8a3au : 0x46a046u, rumble = dark ? 0xd83a3au : 0xf0f0f0u, road = dark ? 0x5a5a62u : 0x62626au;
   for (y = hd_max(y1, 0); y <= hd_min(y0, horizon_clip); y++)
   {
      int32_t t = y0 == y1 ? 0 : (y0 - y) * 256 / (y0 - y1); /* 0 near, 256 far */
      int32_t cx = x0 + (x1 - x0) * t / 256, w = w0 + (w1 - w0) * t / 256, rw = w / 8 + 1, x;
      uint32_t *row = s->px + y * s->w;
      for (x = 0; x < s->w; x++)
      {
         int32_t d = x - cx;
         uint32_t c;
         if (d < -w - rw || d > w + rw)
            c = grass;
         else if (d < -w || d > w)
            c = rumble;
         else if (!dark && (d > -w / 3 - w / 40 && d < -w / 3 + w / 40)) /* lane lines */
            c = 0xf0f0f0u;
         else if (!dark && (d > w / 3 - w / 40 && d < w / 3 + w / 40))
            c = 0xf0f0f0u;
         else
            c = road;
         row[x] = c;
      }
   }
}

void gfx_road(hd_surface *s, const hd_road *r, int32_t pos, int32_t player_x, int32_t draw_dist, hd_road_sprite_fn sprite, void *ctx)
{
   int32_t base = (pos / ROAD_SEG) % r->count, n, maxy = s->h - 1;
   int64_t frac = pos % ROAD_SEG, x = 0, dx = -((int64_t)r->curve[base] * frac / ROAD_SEG);
   /* the camera rides a little behind the player, so the nearest segment is in front of it */
   int64_t camz = (int64_t)pos - 300, camx = (int64_t)player_x * ROAD_WIDTH / 256;
   int64_t hill0 = r->hill[base] + ((int64_t)r->hill[(base + 1) % r->count] - r->hill[base]) * frac / ROAD_SEG;
   int64_t camy = ROAD_CAM_HEIGHT + hill0;
   proj near, far;
   int32_t clip[ROAD_MAX_DRAW];
   proj where[ROAD_MAX_DRAW];
   draw_dist = hd_min(draw_dist, ROAD_MAX_DRAW);
   for (n = 0; n < draw_dist; n++)
   {
      int32_t i = (base + n) % r->count, j = (i + 1) % r->count;
      int64_t z0 = (int64_t)(pos / ROAD_SEG + n) * ROAD_SEG, z1 = z0 + ROAD_SEG;
      project(&near, x - camx, r->hill[i] - camy, z0 - camz, s->w, s->h);
      x += dx;
      dx += r->curve[i];
      project(&far, x - camx, r->hill[j] - camy, z1 - camz, s->w, s->h);
      where[n] = near;
      clip[n] = maxy;
      if (far.y >= near.y || far.y >= maxy)
         continue;
      band(s, near.y, far.y, near.x, near.w, far.x, far.w, ((pos / ROAD_SEG + n) / 3) & 1, maxy);
      maxy = far.y;
   }
   /* sprites far to near, cut at the hill in front of them */
   if (sprite)
      for (n = draw_dist - 1; n > 0; n--)
         sprite(ctx, s, (base + n) % r->count, where[n].x, where[n].y, where[n].w, clip[n]);
}
