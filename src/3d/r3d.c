/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The 3D renderer (r3d.h): triangles lit per face, clipped to the view in
 * camera space, projected with 4 bits of subpixel precision and filled with
 * a depth buffer (1/z, which is linear on screen). Integer math only; the
 * filling runs on up to 4 cores by bands of rows (par.c), each band drawing
 * every triangle in the same order, so the picture never depends on the
 * number of cores.
 */
#include <string.h>
#include "r3d.h"

/* ---- the scene's triangles, built once ---- */

typedef struct
{
   r3d_vec v[3];
   int32_t nx, ny, nz; /* the face's normal, 16384 long (2.14) */
   uint32_t color;
   int32_t glow;
} r3d_tri;

static r3d_tri tris[R3D_MAX_TRIS];
static int32_t tri_count, mesh_first;
static int32_t place_x, place_y, place_z, place_c = TRIG_ONE, place_s;
static int32_t glow_on;

void r3d_clear(void)
{
   tri_count = 0;
   mesh_first = 0;
   r3d_place(0, 0, 0, 0);
   glow_on = 0;
}

void r3d_place(int32_t x, int32_t y, int32_t z, int32_t yaw)
{
   place_x = x;
   place_y = y;
   place_z = z;
   place_c = hd_cos(yaw);
   place_s = hd_sin(yaw);
}

void r3d_begin(void)
{
   mesh_first = tri_count;
}

r3d_mesh r3d_end(void)
{
   r3d_mesh m;
   m.first = mesh_first;
   m.count = tri_count - mesh_first;
   return m;
}

void r3d_glow(int32_t on)
{
   glow_on = on ? 1 : 0;
}

int32_t r3d_triangles(void)
{
   return tri_count;
}

static uint64_t isqrt64(uint64_t v)
{
   uint64_t r = 0, bit = (uint64_t)1 << 62;
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
   return r;
}

/* A direction made 16384 long; a zero vector stays zero. */
static void unit(int64_t x, int64_t y, int64_t z, int32_t *ox, int32_t *oy, int32_t *oz)
{
   int64_t m = (x < 0 ? -x : x) | (y < 0 ? -y : y) | (z < 0 ? -z : z), len;
   while (m >= ((int64_t)1 << 30))
   {
      x /= 2;
      y /= 2;
      z /= 2;
      m >>= 1;
   }
   len = (int64_t)isqrt64((uint64_t)(x * x + y * y + z * z));
   if (!len)
   {
      *ox = *oy = *oz = 0;
      return;
   }
   *ox = (int32_t)(x * TRIG_ONE / len);
   *oy = (int32_t)(y * TRIG_ONE / len);
   *oz = (int32_t)(z * TRIG_ONE / len);
}

static r3d_vec to_world(r3d_vec p)
{
   r3d_vec w;
   w.x = place_x + (int32_t)(((int64_t)p.x * place_c + (int64_t)p.z * place_s) >> 14);
   w.y = place_y + p.y;
   w.z = place_z + (int32_t)(((int64_t)p.z * place_c - (int64_t)p.x * place_s) >> 14);
   return w;
}

static r3d_vec vec(int32_t x, int32_t y, int32_t z)
{
   r3d_vec v;
   v.x = x;
   v.y = y;
   v.z = z;
   return v;
}

/* A triangle in the place's axes, turned to face `hint` (any length; zero keeps the order). */
static void add_tri(r3d_vec a, r3d_vec b, r3d_vec c, uint32_t color, int64_t hx, int64_t hy, int64_t hz)
{
   r3d_tri *t;
   int64_t ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
   int64_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
   int32_t ix, iy, iz, jx, jy, jz;
   if (tri_count >= R3D_MAX_TRIS)
      return;
   unit(nx, ny, nz, &ix, &iy, &iz);
   if (!ix && !iy && !iz)
      return; /* no area */
   unit(hx, hy, hz, &jx, &jy, &jz);
   if ((int64_t)ix * jx + (int64_t)iy * jy + (int64_t)iz * jz < 0)
   {
      r3d_vec k = b;
      b = c;
      c = k;
   }
   t = &tris[tri_count++];
   t->v[0] = to_world(a);
   t->v[1] = to_world(b);
   t->v[2] = to_world(c);
   ux = t->v[1].x - t->v[0].x;
   uy = t->v[1].y - t->v[0].y;
   uz = t->v[1].z - t->v[0].z;
   vx = t->v[2].x - t->v[0].x;
   vy = t->v[2].y - t->v[0].y;
   vz = t->v[2].z - t->v[0].z;
   unit(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx, &t->nx, &t->ny, &t->nz);
   t->color = color & 0xffffffu;
   t->glow = glow_on;
}

static void add_quad(r3d_vec a, r3d_vec b, r3d_vec c, r3d_vec d, uint32_t color, int64_t hx, int64_t hy, int64_t hz)
{
   add_tri(a, b, c, color, hx, hy, hz);
   add_tri(a, c, d, color, hx, hy, hz);
}

void r3d_quad(r3d_vec a, r3d_vec b, r3d_vec c, r3d_vec d, uint32_t color)
{
   add_quad(a, b, c, d, color, 0, 0, 0);
}

void r3d_box(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1, uint32_t color, int32_t flags)
{
   int64_t o = (flags & R3D_INSIDE) ? -1 : 1;
   r3d_vec p000 = vec(x0, y0, z0), p100 = vec(x1, y0, z0), p010 = vec(x0, y1, z0), p110 = vec(x1, y1, z0);
   r3d_vec p001 = vec(x0, y0, z1), p101 = vec(x1, y0, z1), p011 = vec(x0, y1, z1), p111 = vec(x1, y1, z1);
   if (!(flags & R3D_SKIP_BOTTOM))
      add_quad(p000, p100, p101, p001, color, 0, -o, 0);
   if (!(flags & R3D_SKIP_TOP))
      add_quad(p010, p110, p111, p011, color, 0, o, 0);
   if (!(flags & R3D_SKIP_Z))
   {
      add_quad(p000, p100, p110, p010, color, 0, 0, -o);
      add_quad(p001, p101, p111, p011, color, 0, 0, o);
   }
   if (!(flags & R3D_SKIP_X))
   {
      add_quad(p000, p001, p011, p010, color, -o, 0, 0);
      add_quad(p100, p101, p111, p110, color, o, 0, 0);
   }
}

void r3d_cylinder(int32_t x, int32_t y, int32_t z, int32_t r, int32_t r_top, int32_t h, int32_t sides, uint32_t color)
{
   int32_t i;
   sides = hd_clamp(sides, 3, 32);
   for (i = 0; i < sides; i++)
   {
      int32_t a0 = i * ANGLE_FULL / sides, a1 = (i + 1) * ANGLE_FULL / sides, am = (a0 + a1) / 2;
      int32_t c0 = hd_cos(a0), s0 = hd_sin(a0), c1 = hd_cos(a1), s1 = hd_sin(a1);
      r3d_vec b0 = vec(x + (int32_t)(((int64_t)r * c0) >> 14), y, z + (int32_t)(((int64_t)r * s0) >> 14));
      r3d_vec b1 = vec(x + (int32_t)(((int64_t)r * c1) >> 14), y, z + (int32_t)(((int64_t)r * s1) >> 14));
      r3d_vec t0 = vec(x + (int32_t)(((int64_t)r_top * c0) >> 14), y + h, z + (int32_t)(((int64_t)r_top * s0) >> 14));
      r3d_vec t1 = vec(x + (int32_t)(((int64_t)r_top * c1) >> 14), y + h, z + (int32_t)(((int64_t)r_top * s1) >> 14));
      add_tri(b0, b1, t1, color, hd_cos(am), 0, hd_sin(am));
      if (r_top > 0)
         add_tri(b0, t1, t0, color, hd_cos(am), 0, hd_sin(am));
      if (r_top > 0)
         add_tri(vec(x, y + h, z), t0, t1, color, 0, 1, 0);
      add_tri(vec(x, y, z), b0, b1, color, 0, -1, 0);
   }
}

void r3d_ball(int32_t x, int32_t y, int32_t z, int32_t rx, int32_t ry, int32_t rz, int32_t rings, uint32_t color)
{
   int32_t i, j, seg;
   rings = hd_clamp(rings, 2, 12);
   seg = rings * 2;
   for (i = 0; i < rings; i++)
   {
      /* from the bottom pole (-1024) to the top one (1024) */
      int32_t la0 = -1024 + i * 2048 / rings, la1 = -1024 + (i + 1) * 2048 / rings;
      int32_t cy0 = hd_sin(la0), cr0 = hd_cos(la0), cy1 = hd_sin(la1), cr1 = hd_cos(la1);
      for (j = 0; j < seg; j++)
      {
         int32_t a0 = j * ANGLE_FULL / seg, a1 = (j + 1) * ANGLE_FULL / seg;
         r3d_vec p[4];
         int32_t k, la[4], lo[4];
         la[0] = la0, lo[0] = a0;
         la[1] = la0, lo[1] = a1;
         la[2] = la1, lo[2] = a1;
         la[3] = la1, lo[3] = a0;
         for (k = 0; k < 4; k++)
         {
            int32_t ring = (la[k] == la0) ? cr0 : cr1, h = (la[k] == la0) ? cy0 : cy1;
            p[k] = vec(x + (int32_t)(((int64_t)rx * ring >> 14) * hd_cos(lo[k]) >> 14), y + (int32_t)(((int64_t)ry * h) >> 14),
                       z + (int32_t)(((int64_t)rz * ring >> 14) * hd_sin(lo[k]) >> 14));
         }
         {
            int64_t hx = (int64_t)p[0].x + p[1].x + p[2].x + p[3].x - 4LL * x;
            int64_t hy = (int64_t)p[0].y + p[1].y + p[2].y + p[3].y - 4LL * y;
            int64_t hz = (int64_t)p[0].z + p[1].z + p[2].z + p[3].z - 4LL * z;
            add_tri(p[0], p[1], p[2], color, hx, hy, hz);
            add_tri(p[0], p[2], p[3], color, hx, hy, hz);
         }
      }
   }
}

/* ---- drawing ---- */

/* A triangle on screen: 28.4 pixels, 1/z per corner, its color lit. */
typedef struct
{
   int32_t x[3], y[3], iz[3];
   int32_t y0, y1; /* the rows it covers */
   uint32_t color;
} r3d_px;

static r3d_px drawn[R3D_MAX_DRAWN];
static int32_t drawn_count;
static uint32_t zbuf[HD_OUT_MAX_W * HD_OUT_MAX_H];

int32_t r3d_drawn(void)
{
   return drawn_count;
}

/* The frame's constants: the camera's turn, the lens, the light. */
typedef struct
{
   const r3d_view *v;
   int32_t cy, sy, cp, sp; /* yaw and pitch */
   int32_t w, h, f;        /* the surface and the focal length, pixels */
   int32_t lx, ly, lz;     /* the light, 2.14 */
   int32_t fog_span;
} frame_k;

/* A point of the camera's space: x right, y up, z ahead; 16.16 meters. */
typedef struct
{
   int32_t x, y, z;
} cam_pt;

static cam_pt to_camera(const frame_k *k, r3d_vec p)
{
   cam_pt c;
   int64_t dx = (int64_t)p.x - k->v->eye.x, dy = (int64_t)p.y - k->v->eye.y, dz = (int64_t)p.z - k->v->eye.z;
   int64_t x = (dx * k->cy - dz * k->sy) >> 14;
   int64_t z = (dx * k->sy + dz * k->cy) >> 14;
   int64_t y2 = (dy * k->cp + z * k->sp) >> 14;
   int64_t z2 = (z * k->cp - dy * k->sp) >> 14;
   c.x = (int32_t)x;
   c.y = (int32_t)y2;
   c.z = (int32_t)z2;
   return c;
}

/* The five planes the view keeps: in front of near, and inside the screen's four edges (one pixel of margin). */
static int64_t plane(const frame_k *k, int32_t i, cam_pt p)
{
   int64_t hw = k->w / 2 + 1, hh = k->h / 2 + 1;
   switch (i)
   {
   case 0: return (int64_t)p.z - k->v->near_z;
   case 1: return (int64_t)p.x * k->f + (int64_t)p.z * hw;
   case 2: return (int64_t)p.z * hw - (int64_t)p.x * k->f;
   case 3: return (int64_t)p.z * hh - (int64_t)p.y * k->f;
   default: return (int64_t)p.y * k->f + (int64_t)p.z * hh;
   }
}

#define POLY_MAX 9

static int32_t clip(const frame_k *k, cam_pt *poly, int32_t n)
{
   cam_pt tmp[POLY_MAX];
   int32_t i, j, m;
   for (i = 0; i < 5 && n >= 3; i++)
   {
      m = 0;
      for (j = 0; j < n; j++)
      {
         cam_pt a = poly[j], b = poly[(j + 1) % n];
         int64_t da = plane(k, i, a), db = plane(k, i, b);
         if (da >= 0 && m < POLY_MAX)
            tmp[m++] = a;
         if (((da >= 0) != (db >= 0)) && m < POLY_MAX)
         {
            /*
             * where the edge crosses the plane, t in 0..65536, always from
             * the same end of the edge: two triangles that share it get
             * the same point, so no crack opens between them
             */
            cam_pt p = a, q = b, c;
            int64_t dp = da, dq = db, t;
            if (a.x > b.x || (a.x == b.x && (a.y > b.y || (a.y == b.y && a.z > b.z))))
               p = b, q = a, dp = db, dq = da;
            t = (dp << 16) / (dp - dq);
            c.x = p.x + (int32_t)(((int64_t)(q.x - p.x) * t) >> 16);
            c.y = p.y + (int32_t)(((int64_t)(q.y - p.y) * t) >> 16);
            c.z = p.z + (int32_t)(((int64_t)(q.z - p.z) * t) >> 16);
            tmp[m++] = c;
         }
      }
      memcpy(poly, tmp, sizeof(cam_pt) * (size_t)m);
      n = m;
   }
   return n;
}

static uint32_t shade(const frame_k *k, const r3d_tri *t, int32_t nx, int32_t ny, int32_t nz, int64_t depth)
{
   const r3d_view *v = k->v;
   uint32_t c = t->color;
   if (!t->glow)
   {
      int32_t d = (int32_t)(((int64_t)nx * k->lx + (int64_t)ny * k->ly + (int64_t)nz * k->lz) >> 14);
      /* a little more light from above than from below, so faces that look away from the sun still read */
      int32_t amb = v->ambient + ((ny * (v->ambient / 4)) >> 14);
      int32_t l = amb + (d > 0 ? (v->diffuse * d) >> 14 : 0);
      int32_t r = hd_min(255, (int32_t)((c >> 16) & 0xff) * l >> 8);
      int32_t g = hd_min(255, (int32_t)((c >> 8) & 0xff) * l >> 8);
      int32_t b = hd_min(255, (int32_t)(c & 0xff) * l >> 8);
      c = (uint32_t)(r << 16 | g << 8 | b);
   }
   if (v->fog_far > v->fog_near && depth > v->fog_near)
   {
      int32_t f = depth >= v->fog_far ? 256 : (int32_t)(((depth - v->fog_near) << 8) / k->fog_span);
      c = gfx_mix(c, v->fog, f);
   }
   return c;
}

static void emit(const frame_k *k, const r3d_tri *t, const r3d_vec w[3], int32_t nx, int32_t ny, int32_t nz)
{
   cam_pt poly[POLY_MAX];
   int32_t n, i, inside = 1;
   int64_t depth;
   uint32_t color;
   /* only faces turned toward the eye */
   if (((int64_t)k->v->eye.x - w[0].x) * nx + ((int64_t)k->v->eye.y - w[0].y) * ny + ((int64_t)k->v->eye.z - w[0].z) * nz <= 0)
      return;
   for (i = 0; i < 3; i++)
      poly[i] = to_camera(k, w[i]);
   for (i = 0; i < 5 && inside; i++)
      if (plane(k, i, poly[0]) < 0 && plane(k, i, poly[1]) < 0 && plane(k, i, poly[2]) < 0)
         inside = 0;
   if (!inside)
      return;
   depth = ((int64_t)poly[0].z + poly[1].z + poly[2].z) / 3;
   color = shade(k, t, nx, ny, nz, depth);
   n = clip(k, poly, 3);
   for (i = 1; i + 1 < n; i++)
   {
      r3d_px *d;
      int32_t j, idx[3];
      if (drawn_count >= R3D_MAX_DRAWN)
         return;
      d = &drawn[drawn_count];
      idx[0] = 0, idx[1] = i, idx[2] = i + 1;
      for (j = 0; j < 3; j++)
      {
         cam_pt p = poly[idx[j]];
         int64_t z = p.z < k->v->near_z ? k->v->near_z : p.z;
         d->x[j] = (int32_t)((int64_t)k->w * 8 + ((int64_t)p.x * k->f * 16) / z);
         d->y[j] = (int32_t)((int64_t)k->h * 8 - ((int64_t)p.y * k->f * 16) / z);
         d->iz[j] = (int32_t)(((int64_t)1 << 38) / z);
      }
      d->y0 = hd_max(0, (hd_min(d->y[0], hd_min(d->y[1], d->y[2])) - 8) >> 4);
      d->y1 = hd_min(k->h - 1, (hd_max(d->y[0], hd_max(d->y[1], d->y[2])) + 8) >> 4);
      d->color = color;
      if (d->y0 <= d->y1)
         drawn_count++;
   }
}

typedef struct
{
   hd_surface *s;
} fill_job;

static void fill_rows(void *ctx, int32_t row0, int32_t row1)
{
   fill_job *job = (fill_job *)ctx;
   hd_surface *s = job->s;
   int32_t i;
   for (i = 0; i < drawn_count; i++)
   {
      const r3d_px *t = &drawn[i];
      int64_t x0 = t->x[0], y0 = t->y[0], x1 = t->x[1], y1 = t->y[1], x2 = t->x[2], y2 = t->y[2];
      int64_t z0 = t->iz[0], z1 = t->iz[1], z2 = t->iz[2];
      int64_t area, dzdx, dzdy, k;
      int32_t ya = hd_max(row0, t->y0), yb = hd_min(row1 - 1, t->y1), xa, xb, y, x;
      if (ya > yb)
         continue;
      area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
      if (!area)
         continue;
      if (area < 0)
      {
         k = x1, x1 = x2, x2 = k;
         k = y1, y1 = y2, y2 = k;
         k = z1, z1 = z2, z2 = k;
         area = -area;
      }
      /* 1/z across the screen: a plane, 16 extra bits of fraction, per subpixel */
      dzdx = (((z1 - z0) * (y2 - y0) - (z2 - z0) * (y1 - y0)) << 16) / area;
      dzdy = (((z2 - z0) * (x1 - x0) - (z1 - z0) * (x2 - x0)) << 16) / area;
      xa = (int32_t)hd_max(0, (int32_t)((hd_min((int32_t)x0, hd_min((int32_t)x1, (int32_t)x2)) - 8) >> 4));
      xb = (int32_t)hd_min(s->w - 1, (int32_t)((hd_max((int32_t)x0, hd_max((int32_t)x1, (int32_t)x2)) + 8) >> 4));
      for (y = ya; y <= yb; y++)
      {
         int64_t py = (int64_t)y * 16 + 8, px = (int64_t)xa * 16 + 8;
         int64_t w0 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
         int64_t w1 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
         int64_t w2 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
         int64_t s0 = -(y2 - y1) * 16, s1 = -(y0 - y2) * 16, s2 = -(y1 - y0) * 16;
         int64_t zf = (z0 << 16) + dzdx * (px - x0) + dzdy * (py - y0), zs = dzdx * 16;
         uint32_t *row = s->px + (int64_t)y * s->w, *zrow = zbuf + (int64_t)y * s->w;
         for (x = xa; x <= xb; x++, w0 += s0, w1 += s1, w2 += s2, zf += zs)
         {
            uint32_t z;
            if ((w0 | w1 | w2) < 0)
               continue;
            z = (uint32_t)(zf >> 16);
            if (z > zrow[x])
            {
               zrow[x] = z;
               row[x] = t->color;
            }
         }
      }
   }
}

static void emit_all(const frame_k *k, const r3d_mesh *m, const r3d_instance *ins)
{
   int32_t i, j;
   int32_t c = ins ? hd_cos(ins->yaw) : TRIG_ONE, sn = ins ? hd_sin(ins->yaw) : 0;
   for (i = m->first; i < m->first + m->count && i < tri_count; i++)
   {
      const r3d_tri *t = &tris[i];
      r3d_vec w[3];
      int32_t nx = t->nx, ny = t->ny, nz = t->nz;
      if (!ins)
      {
         emit(k, t, t->v, nx, ny, nz);
         continue;
      }
      for (j = 0; j < 3; j++)
      {
         w[j].x = ins->at.x + (int32_t)(((int64_t)t->v[j].x * c + (int64_t)t->v[j].z * sn) >> 14);
         w[j].y = ins->at.y + t->v[j].y;
         w[j].z = ins->at.z + (int32_t)(((int64_t)t->v[j].z * c - (int64_t)t->v[j].x * sn) >> 14);
      }
      {
         int32_t rx = (int32_t)(((int64_t)nx * c + (int64_t)nz * sn) >> 14);
         int32_t rz = (int32_t)(((int64_t)nz * c - (int64_t)nx * sn) >> 14);
         if (ins->tint >> 24)
         {
            r3d_tri tt = *t;
            tt.color = gfx_mix(t->color, ins->tint & 0xffffffu, (int32_t)(ins->tint >> 24));
            emit(k, &tt, w, rx, ny, rz);
         }
         else
            emit(k, t, w, rx, ny, rz);
      }
   }
}

void r3d_draw(hd_surface *s, const r3d_view *v, const r3d_mesh *scene, const r3d_instance *inst, int32_t count)
{
   frame_k k;
   fill_job job;
   int32_t i, half = hd_max(1, v->fov / 2), t;
   k.v = v;
   k.cy = hd_cos(v->yaw);
   k.sy = hd_sin(v->yaw);
   k.cp = hd_cos(v->pitch);
   k.sp = hd_sin(v->pitch);
   k.w = s->w;
   k.h = s->h;
   /* focal = (h / 2) / tan(fov / 2) */
   t = hd_sin(half);
   k.f = t ? (int32_t)(((int64_t)(s->h / 2) * hd_cos(half)) / t) : s->h;
   unit(v->sun.x, v->sun.y, v->sun.z, &k.lx, &k.ly, &k.lz);
   k.fog_span = hd_max(1, v->fog_far - v->fog_near);
   for (i = 0; i < s->w * s->h; i++)
   {
      s->px[i] = v->sky;
      zbuf[i] = 0;
   }
   drawn_count = 0;
   if (scene)
      emit_all(&k, scene, NULL);
   for (i = 0; i < count; i++)
      emit_all(&k, &inst[i].mesh, &inst[i]);
   job.s = s;
   hd_rows(s->w, s->h, fill_rows, &job);
}
