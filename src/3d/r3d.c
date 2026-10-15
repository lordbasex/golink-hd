/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The 3D renderer (r3d.h). Integer math only, so every build draws the same
 * pixels:
 *   - building: pieces become triangles in world space, big faces cut into
 *     cells (r3d_detail) so the baked light can show shadows on them, each
 *     corner with its texture coordinates;
 *   - baking: the solid pieces fill a grid of 5 cm cubes; from every corner
 *     of every triangle rays walk that grid toward the sun (a few, for soft
 *     edges), around it for the sky (the corners and the floor under a table
 *     get less) and toward each lamp; the light found is kept in the corner;
 *   - drawing: triangles turned toward the eye are clipped to the view in
 *     camera space, projected with 4 bits of subpixel precision and filled
 *     with a depth buffer (1/z), their texture, light and color interpolated
 *     across them (affine: the cells are small), then a filmic curve turns
 *     light into the screen's colors. The filling runs on up to 4 cores by
 *     bands of rows (par.c), each band drawing every triangle in the same
 *     order, so the picture never depends on the number of cores.
 */
#include <stdlib.h>
#include <string.h>
#include "r3d.h"

/* ---- textures ---- */

typedef struct
{
   int32_t wlog, hlog;
   uint32_t *px;
} r3d_tex;

static r3d_tex texs[R3D_MAX_TEX];
static int32_t tex_count;

int32_t r3d_tex_add(int32_t wlog, int32_t hlog, uint32_t *px)
{
   if (tex_count >= R3D_MAX_TEX || wlog < 0 || wlog > 11 || hlog < 0 || hlog > 11 || !px)
      return -1;
   texs[tex_count].wlog = wlog;
   texs[tex_count].hlog = hlog;
   texs[tex_count].px = px;
   return tex_count++;
}

uint32_t *r3d_tex_pixels(int32_t id)
{
   return id >= 0 && id < tex_count ? texs[id].px : NULL;
}

/* ---- the world's triangles, built once ---- */

enum { TF_GLOW = 1, TF_DECAL = 2, TF_FIT = 4, TF_GLASS = 8 };

typedef struct
{
   r3d_vec v[3];
   int32_t u[3], t[3];                /* texels, 8 bits of fraction */
   int32_t lr[3], lg[3], lb[3];       /* the baked light of each corner, 256 = 1.0 */
   int32_t nx, ny, nz;                /* the face's normal, 16384 long (2.14) */
   int32_t vn[3][3];                  /* each corner's normal for its light (the face's, or a round surface's own) */
   uint32_t color;
   int32_t cr, cg, cb;                /* the color as light, 256 = white */
   int32_t tex, flags, glow, glass;
   /*
    * A face with a light map (lm >= 0): its corners' place in it (texels, 8
    * bits of fraction), and the sun's light on them without shadows (the map
    * says how much of it arrives, and how much of the sky).
    */
   int32_t lm, ls[3], lt[3];
   int32_t sr[3], sg[3], sb[3];
} r3d_tri;

/*
 * Light maps: big faces get a small picture of their light, a texel each
 * 5 cm, with two values per texel: how much sun arrives (bits 0-7) and how
 * much sky (bits 16-23). Shadows then are as sharp as the voxel grid, not
 * as the triangles.
 */
typedef struct
{
   r3d_vec a, b, c, d; /* the face's corners in the world, around it */
   int32_t w, h, base; /* texels across (a -> b) and up (a -> d), its first texel in lm_px */
   int32_t tri;        /* one of its triangles, for its normal */
} r3d_lmface;

static r3d_lmface lm_faces[R3D_MAX_LM_FACES];
static uint32_t lm_px[R3D_LM_TEXELS];
static int32_t lm_face_count, lm_used;
#define LM_TEXEL (FX_ONE / 20)

static r3d_tri tris[R3D_MAX_TRIS];
static int32_t tri_count, mesh_first, baked_count;
static void lin3(uint32_t c, int32_t *r, int32_t *g, int32_t *b);
static uint32_t sky_top, sky_bottom;
/* a width that makes hd_rows split any number of rows over the cores */
#define PAR_WIDE (1 << 16)
static int32_t place_x, place_y, place_z, place_c = TRIG_ONE, place_s;
static int32_t cur_tex = -1, cur_tpm, cur_glow, cur_occlude = 1, cur_decal, cur_cell, cur_lm = -1, cur_glass, cur_bevel;

/* What stops light: boxes (world-aligned bounds), upright cylinders and balls. */
enum { SOLID_BOX = 0, SOLID_CYL, SOLID_BALL };
typedef struct
{
   int32_t kind;
   int32_t x0, y0, z0, x1, y1, z1; /* bounds */
   int32_t cx, cz, r0, r1;         /* a cylinder: center, radius at the bottom and the top; a ball: radii in r0 (xz), r1 (y) */
} r3d_solid;

static r3d_solid solids[R3D_MAX_SOLIDS];
static int32_t solid_count;

typedef struct
{
   r3d_vec at;
   int32_t radius, strength;
   uint32_t color;
} r3d_lamp;

static r3d_lamp lamps[R3D_MAX_LIGHTS];
static int32_t lamp_count;

void r3d_clear(void)
{
   tri_count = 0;
   mesh_first = 0;
   baked_count = 0;
   solid_count = 0;
   lamp_count = 0;
   lm_face_count = 0;
   lm_used = 0;
   cur_lm = -1;
   cur_glass = 0;
   cur_bevel = 0;
   r3d_place(0, 0, 0, 0);
   cur_tex = -1;
   cur_tpm = 0;
   cur_glow = 0;
   cur_occlude = 1;
   cur_decal = 0;
   cur_cell = 0;
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

void r3d_material(int32_t tex, int32_t texels_per_m)
{
   cur_tex = tex >= 0 && tex < tex_count ? tex : -1;
   cur_tpm = texels_per_m;
}

void r3d_glow(int32_t strength)
{
   cur_glow = hd_max(0, strength);
}

void r3d_occlude(int32_t on)
{
   cur_occlude = on ? 1 : 0;
}

void r3d_glass(int32_t cover)
{
   cur_glass = hd_clamp(cover, 0, 255);
}

void r3d_rotation(int32_t yaw, int32_t pitch, int32_t roll, int32_t out[9])
{
   int32_t cy = hd_cos(yaw), sy = hd_sin(yaw), cp = hd_cos(pitch), sp = hd_sin(pitch), cr = hd_cos(roll), sr = hd_sin(roll);
   int32_t ry[9], rx[9], rz[9], t[9];
   ry[0] = cy, ry[1] = 0, ry[2] = sy, ry[3] = 0, ry[4] = TRIG_ONE, ry[5] = 0, ry[6] = -sy, ry[7] = 0, ry[8] = cy;
   rx[0] = TRIG_ONE, rx[1] = 0, rx[2] = 0, rx[3] = 0, rx[4] = cp, rx[5] = -sp, rx[6] = 0, rx[7] = sp, rx[8] = cp;
   rz[0] = cr, rz[1] = -sr, rz[2] = 0, rz[3] = sr, rz[4] = cr, rz[5] = 0, rz[6] = 0, rz[7] = 0, rz[8] = TRIG_ONE;
   r3d_rot_mul(ry, rx, t);
   r3d_rot_mul(t, rz, out);
}

void r3d_rot_mul(const int32_t a[9], const int32_t b[9], int32_t out[9])
{
   int32_t i, j, r[9];
   for (i = 0; i < 3; i++)
      for (j = 0; j < 3; j++)
         r[i * 3 + j] = (int32_t)(((int64_t)a[i * 3] * b[j] + (int64_t)a[i * 3 + 1] * b[3 + j] + (int64_t)a[i * 3 + 2] * b[6 + j]) >> 14);
   memcpy(out, r, sizeof r);
}

r3d_vec r3d_rot_apply(const int32_t m[9], r3d_vec v)
{
   r3d_vec o;
   o.x = (int32_t)(((int64_t)m[0] * v.x + (int64_t)m[1] * v.y + (int64_t)m[2] * v.z) >> 14);
   o.y = (int32_t)(((int64_t)m[3] * v.x + (int64_t)m[4] * v.y + (int64_t)m[5] * v.z) >> 14);
   o.z = (int32_t)(((int64_t)m[6] * v.x + (int64_t)m[7] * v.y + (int64_t)m[8] * v.z) >> 14);
   return o;
}

void r3d_decal(int32_t on)
{
   cur_decal = on ? 1 : 0;
}

void r3d_detail(int32_t cell)
{
   cur_cell = hd_max(0, cell);
}

void r3d_truncate(int32_t count)
{
   if (count >= 0 && count < tri_count)
      tri_count = count;
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

static int32_t length(r3d_vec a, r3d_vec b)
{
   int64_t x = (int64_t)b.x - a.x, y = (int64_t)b.y - a.y, z = (int64_t)b.z - a.z;
   return (int32_t)isqrt64((uint64_t)(x * x + y * y + z * z));
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

static r3d_vec lerp(r3d_vec a, r3d_vec b, int32_t i, int32_t n)
{
   return vec(a.x + (int32_t)(((int64_t)b.x - a.x) * i / n), a.y + (int32_t)(((int64_t)b.y - a.y) * i / n),
              a.z + (int32_t)(((int64_t)b.z - a.z) * i / n));
}

/* Texture coordinates of a point along the piece's own axes, by the face's main direction (8 bits of fraction). */
static void world_uv(r3d_vec p, int32_t nx, int32_t ny, int32_t nz, int32_t *u, int32_t *t)
{
   int32_t ax = hd_abs(nx), ay = hd_abs(ny), az = hd_abs(nz);
   int64_t a, b;
   if (ay >= ax && ay >= az)
      a = p.x, b = p.z;
   else if (ax >= az)
      a = p.z, b = -(int64_t)p.y;
   else
      a = p.x, b = -(int64_t)p.y;
   *u = (int32_t)((a * cur_tpm) >> 8);
   *t = (int32_t)((b * cur_tpm) >> 8);
}

/*
 * A triangle in the place's axes, turned to face `hint` (any length; zero
 * keeps the order); uv: its corners' texture coordinates, or NULL to lay
 * the texture along the piece's axes.
 */
/* The corners' own normals (in the piece's space) for the next triangle: round surfaces lit smoothly. NULL: the face's. */
static const int32_t (*next_vn)[3];
static int keep_flat; /* a model's triangle is kept even without area (its place in the mesh matters: r3d_tri_pose) */

static void add_tri_uv(r3d_vec a, r3d_vec b, r3d_vec c, uint32_t color, int64_t hx, int64_t hy, int64_t hz, const int32_t *uv, const int32_t *lmuv)
{
   r3d_tri *t;
   r3d_vec p[3];
   int64_t ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
   int64_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
   int32_t ix, iy, iz, jx, jy, jz, k, flip = 0, ord[3] = { 0, 1, 2 };
   if (tri_count >= R3D_MAX_TRIS)
      return;
   unit(nx, ny, nz, &ix, &iy, &iz);
   if (!ix && !iy && !iz)
   {
      if (!keep_flat)
         return; /* no area */
      iy = TRIG_ONE;
   }
   unit(hx, hy, hz, &jx, &jy, &jz);
   if ((int64_t)ix * jx + (int64_t)iy * jy + (int64_t)iz * jz < 0)
   {
      flip = 1;
      ord[1] = 2;
      ord[2] = 1;
      ix = -ix, iy = -iy, iz = -iz;
   }
   p[0] = a;
   p[1] = flip ? c : b;
   p[2] = flip ? b : c;
   t = &tris[tri_count++];
   memset(t, 0, sizeof *t);
   for (k = 0; k < 3; k++)
   {
      t->v[k] = to_world(p[k]);
      if (uv)
      {
         t->u[k] = uv[ord[k] * 2];
         t->t[k] = uv[ord[k] * 2 + 1];
      }
      else
         world_uv(p[k], ix, iy, iz, &t->u[k], &t->t[k]);
   }
   ux = t->v[1].x - t->v[0].x;
   uy = t->v[1].y - t->v[0].y;
   uz = t->v[1].z - t->v[0].z;
   vx = t->v[2].x - t->v[0].x;
   vy = t->v[2].y - t->v[0].y;
   vz = t->v[2].z - t->v[0].z;
   unit(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx, &t->nx, &t->ny, &t->nz);
   for (k = 0; k < 3; k++)
   {
      if (next_vn)
      {
         /* turned with the piece, like its corners */
         const int32_t *n = next_vn[ord[k]];
         t->vn[k][0] = (int32_t)(((int64_t)n[0] * place_c + (int64_t)n[2] * place_s) >> 14);
         t->vn[k][1] = n[1];
         t->vn[k][2] = (int32_t)(((int64_t)n[2] * place_c - (int64_t)n[0] * place_s) >> 14);
      }
      else
         t->vn[k][0] = t->nx, t->vn[k][1] = t->ny, t->vn[k][2] = t->nz;
   }
   t->color = color & 0xffffffu;
   lin3(t->color, &t->cr, &t->cg, &t->cb);
   t->tex = cur_tex;
   t->glow = cur_glow;
   t->flags = (cur_glow ? TF_GLOW : 0) | (cur_decal ? TF_DECAL : 0) | (cur_glass ? TF_GLASS : 0);
   t->glass = cur_glass;
   t->lm = (lmuv && !cur_glow && !cur_decal) ? cur_lm : -1;
   for (k = 0; k < 3; k++)
   {
      t->lr[k] = t->lg[k] = t->lb[k] = 256;
      if (t->lm >= 0)
      {
         t->ls[k] = lmuv[ord[k] * 2];
         t->lt[k] = lmuv[ord[k] * 2 + 1];
      }
   }
}

static void add_tri(r3d_vec a, r3d_vec b, r3d_vec c, uint32_t color, int64_t hx, int64_t hy, int64_t hz)
{
   add_tri_uv(a, b, c, color, hx, hy, hz, NULL, NULL);
}

/* A triangle of a round surface: each corner lit by its own normal (na, nb, nc, 16384 long). */
static void add_tri_n(r3d_vec a, r3d_vec b, r3d_vec c, uint32_t color, int64_t hx, int64_t hy, int64_t hz, const int32_t na[3], const int32_t nb[3], const int32_t nc[3])
{
   int32_t n[3][3];
   memcpy(n[0], na, sizeof n[0]);
   memcpy(n[1], nb, sizeof n[1]);
   memcpy(n[2], nc, sizeof n[2]);
   next_vn = (const int32_t(*)[3])n;
   add_tri_uv(a, b, c, color, hx, hy, hz, NULL, NULL);
   next_vn = NULL;
}

void r3d_triangle(r3d_vec a, r3d_vec b, r3d_vec c, const int32_t na[3], const int32_t nb[3], const int32_t nc[3], uint32_t color)
{
   int64_t ux = (int64_t)b.x - a.x, uy = (int64_t)b.y - a.y, uz = (int64_t)b.z - a.z, vx = (int64_t)c.x - a.x, vy = (int64_t)c.y - a.y, vz = (int64_t)c.z - a.z;
   int64_t hx = uy * vz - uz * vy, hy = uz * vx - ux * vz, hz = ux * vy - uy * vx;
   while ((hx < 0 ? -hx : hx) > ((int64_t)1 << 40) || (hy < 0 ? -hy : hy) > ((int64_t)1 << 40) || (hz < 0 ? -hz : hz) > ((int64_t)1 << 40))
      hx /= 2, hy /= 2, hz /= 2;
   keep_flat = 1;
   if (na && nb && nc)
      add_tri_n(a, b, c, color, hx, hy, hz, na, nb, nc);
   else
      add_tri(a, b, c, color, hx, hy, hz);
   keep_flat = 0;
}

void r3d_tri_pose(int32_t index, r3d_vec a, r3d_vec b, r3d_vec c, const int32_t na[3], const int32_t nb[3], const int32_t nc[3])
{
   r3d_tri *t;
   int64_t ux, uy, uz, vx, vy, vz;
   int32_t nx, ny, nz, k;
   if (index < 0 || index >= tri_count)
      return;
   t = &tris[index];
   t->v[0] = a, t->v[1] = b, t->v[2] = c;
   ux = (int64_t)b.x - a.x, uy = (int64_t)b.y - a.y, uz = (int64_t)b.z - a.z;
   vx = (int64_t)c.x - a.x, vy = (int64_t)c.y - a.y, vz = (int64_t)c.z - a.z;
   unit(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx, &nx, &ny, &nz);
   if (nx | ny | nz)
      t->nx = nx, t->ny = ny, t->nz = nz; /* a triangle squeezed flat keeps its last face */
   for (k = 0; k < 3; k++)
   {
      t->vn[0][k] = na[k];
      t->vn[1][k] = nb[k];
      t->vn[2][k] = nc[k];
   }
}

/*
 * A quad (a b c d around it), cut into cells of cur_cell; with a stretched
 * texture (texels_per_m 0) the whole quad shows the texture once.
 */
static void add_quad(r3d_vec a, r3d_vec b, r3d_vec c, r3d_vec d, uint32_t color, int64_t hx, int64_t hy, int64_t hz)
{
   int32_t n = 1, m = 1, i, j, fit = cur_tex >= 0 && cur_tpm == 0, tw = 0, th = 0;
   /* faces that look down are never lit by much nor seen from above: no cells for them */
   if (cur_cell > 0 && hy >= 0)
   {
      n = hd_clamp((hd_max(length(a, b), length(d, c)) + cur_cell - 1) / cur_cell, 1, 48);
      m = hd_clamp((hd_max(length(a, d), length(b, c)) + cur_cell - 1) / cur_cell, 1, 48);
   }
   int32_t lw = 0, lh = 0;
   if (fit)
   {
      tw = 256 << texs[cur_tex].wlog;
      th = 256 << texs[cur_tex].hlog;
   }
   /* a light map for a big face that can be lit (not looking down, not glowing, not a decal) */
   cur_lm = -1;
   if (cur_cell > 0 && hy >= 0 && !cur_glow && !cur_decal && !cur_glass && lm_face_count < R3D_MAX_LM_FACES)
   {
      int32_t la = hd_max(length(a, b), length(d, c)), lb = hd_max(length(a, d), length(b, c));
      if (la >= LM_TEXEL * 3 && lb >= LM_TEXEL * 3)
      {
         lw = hd_clamp((la + LM_TEXEL - 1) / LM_TEXEL, 2, 400);
         lh = hd_clamp((lb + LM_TEXEL - 1) / LM_TEXEL, 2, 400);
         if (lm_used + lw * lh <= R3D_LM_TEXELS)
         {
            r3d_lmface *f = &lm_faces[lm_face_count];
            f->a = to_world(a);
            f->b = to_world(b);
            f->c = to_world(c);
            f->d = to_world(d);
            f->w = lw;
            f->h = lh;
            f->base = lm_used;
            f->tri = tri_count;
            lm_used += lw * lh;
            cur_lm = lm_face_count++;
         }
      }
   }
   for (j = 0; j < m; j++)
      for (i = 0; i < n; i++)
      {
         r3d_vec p00 = lerp(lerp(a, b, i, n), lerp(d, c, i, n), j, m);
         r3d_vec p10 = lerp(lerp(a, b, i + 1, n), lerp(d, c, i + 1, n), j, m);
         r3d_vec p11 = lerp(lerp(a, b, i + 1, n), lerp(d, c, i + 1, n), j + 1, m);
         r3d_vec p01 = lerp(lerp(a, b, i, n), lerp(d, c, i, n), j + 1, m);
         /* where the cell's corners are in the light map */
         int32_t s0 = lw * 256 * i / n, s1 = lw * 256 * (i + 1) / n, q0 = lh * 256 * j / m, q1 = lh * 256 * (j + 1) / m;
         int32_t lm1[6] = { s0, q0, s1, q0, s1, q1 }, lm2[6] = { s0, q0, s1, q1, s0, q1 };
         const int32_t *l1 = cur_lm >= 0 ? lm1 : NULL, *l2 = cur_lm >= 0 ? lm2 : NULL;
         if (fit)
         {
            /* the texture's top row along d -> c, so a picture on a wall stands up */
            int32_t u0 = tw * i / n, u1 = tw * (i + 1) / n, v0 = th - th * j / m - 1, v1 = th - th * (j + 1) / m;
            int32_t uv1[6] = { u0, v0, u1, v0, u1, v1 }, uv2[6] = { u0, v0, u1, v1, u0, v1 };
            add_tri_uv(p00, p10, p11, color, hx, hy, hz, uv1, l1);
            add_tri_uv(p00, p11, p01, color, hx, hy, hz, uv2, l2);
         }
         else
         {
            add_tri_uv(p00, p10, p11, color, hx, hy, hz, NULL, l1);
            add_tri_uv(p00, p11, p01, color, hx, hy, hz, NULL, l2);
         }
      }
   if (cur_lm >= 0 && lm_faces[cur_lm].tri >= tri_count)
   {
      /* no triangle came out of it (a degenerate face): give its texels back */
      lm_used -= lw * lh;
      lm_face_count--;
   }
   cur_lm = -1;
}

void r3d_quad(r3d_vec a, r3d_vec b, r3d_vec c, r3d_vec d, uint32_t color)
{
   add_quad(a, b, c, d, color, 0, 0, 0);
}

static void add_solid(int32_t kind, r3d_vec lo, r3d_vec hi, int32_t r0, int32_t r1)
{
   r3d_solid *s;
   r3d_vec c[8], w;
   int32_t i;
   if (!cur_occlude || cur_decal || cur_glass || solid_count >= R3D_MAX_SOLIDS)
      return;
   s = &solids[solid_count++];
   s->kind = kind;
   for (i = 0; i < 8; i++)
      c[i] = to_world(vec(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z));
   s->x0 = s->x1 = c[0].x;
   s->y0 = s->y1 = c[0].y;
   s->z0 = s->z1 = c[0].z;
   for (i = 1; i < 8; i++)
   {
      s->x0 = hd_min(s->x0, c[i].x), s->x1 = hd_max(s->x1, c[i].x);
      s->y0 = hd_min(s->y0, c[i].y), s->y1 = hd_max(s->y1, c[i].y);
      s->z0 = hd_min(s->z0, c[i].z), s->z1 = hd_max(s->z1, c[i].z);
   }
   w = to_world(vec((lo.x + hi.x) / 2, lo.y, (lo.z + hi.z) / 2));
   s->cx = w.x;
   s->cz = w.z;
   s->r0 = r0;
   s->r1 = r1;
}

void r3d_solid_box(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1)
{
   add_solid(SOLID_BOX, vec(hd_min(x0, x1), hd_min(y0, y1), hd_min(z0, z1)), vec(hd_max(x0, x1), hd_max(y0, y1), hd_max(z0, z1)), 0, 0);
}

void r3d_bevel(int32_t size)
{
   cur_bevel = hd_max(0, size);
}

/* One edge of a bevelled box: the strip between face A's edge (a0, a1) and face B's (b0, b1), lit from one face's normal to the other's. */
static void bevel_edge(r3d_vec a0, r3d_vec a1, const int32_t na[3], r3d_vec b0, r3d_vec b1, const int32_t nb[3], uint32_t color)
{
   int64_t hx = na[0] + nb[0], hy = na[1] + nb[1], hz = na[2] + nb[2];
   add_tri_n(a0, a1, b1, color, hx, hy, hz, na, na, nb);
   add_tri_n(a0, b1, b0, color, hx, hy, hz, na, nb, nb);
}

/*
 * A box with its edges cut at 45 degrees (r3d_bevel): the six faces a
 * little smaller, a strip along each edge and a triangle at each corner,
 * lit as if rounded. Edges catch the light like real furniture's.
 */
static void bevel_box(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1, uint32_t color, int32_t b)
{
   int32_t X0 = x0 + b, X1 = x1 - b, Y0 = y0 + b, Y1 = y1 - b, Z0 = z0 + b, Z1 = z1 - b, i, j, k;
   int32_t xs[2] = { x0, x1 }, ys[2] = { y0, y1 }, zs[2] = { z0, z1 }, XS[2] = { X0, X1 }, YS[2] = { Y0, Y1 }, ZS[2] = { Z0, Z1 };
   int32_t nx[2][3] = { { -TRIG_ONE, 0, 0 }, { TRIG_ONE, 0, 0 } }, ny[2][3] = { { 0, -TRIG_ONE, 0 }, { 0, TRIG_ONE, 0 } }, nz[2][3] = { { 0, 0, -TRIG_ONE }, { 0, 0, TRIG_ONE } };
   add_quad(vec(X0, y0, Z1), vec(X1, y0, Z1), vec(X1, y0, Z0), vec(X0, y0, Z0), color, 0, -1, 0);
   add_quad(vec(X0, y1, Z0), vec(X1, y1, Z0), vec(X1, y1, Z1), vec(X0, y1, Z1), color, 0, 1, 0);
   add_quad(vec(X1, Y0, z0), vec(X0, Y0, z0), vec(X0, Y1, z0), vec(X1, Y1, z0), color, 0, 0, -1);
   add_quad(vec(X0, Y0, z1), vec(X1, Y0, z1), vec(X1, Y1, z1), vec(X0, Y1, z1), color, 0, 0, 1);
   add_quad(vec(x0, Y0, Z0), vec(x0, Y0, Z1), vec(x0, Y1, Z1), vec(x0, Y1, Z0), color, -1, 0, 0);
   add_quad(vec(x1, Y0, Z1), vec(x1, Y0, Z0), vec(x1, Y1, Z0), vec(x1, Y1, Z1), color, 1, 0, 0);
   for (i = 0; i < 2; i++)
      for (j = 0; j < 2; j++)
      {
         /* along x (between a y face and a z face), along y (x and z), along z (x and y) */
         bevel_edge(vec(X0, ys[i], ZS[j]), vec(X1, ys[i], ZS[j]), ny[i], vec(X0, YS[i], zs[j]), vec(X1, YS[i], zs[j]), nz[j], color);
         bevel_edge(vec(xs[i], Y0, ZS[j]), vec(xs[i], Y1, ZS[j]), nx[i], vec(XS[i], Y0, zs[j]), vec(XS[i], Y1, zs[j]), nz[j], color);
         bevel_edge(vec(xs[i], YS[j], Z0), vec(xs[i], YS[j], Z1), nx[i], vec(XS[i], ys[j], Z0), vec(XS[i], ys[j], Z1), ny[j], color);
      }
   for (i = 0; i < 2; i++)
      for (j = 0; j < 2; j++)
         for (k = 0; k < 2; k++)
            add_tri_n(vec(xs[i], YS[j], ZS[k]), vec(XS[i], ys[j], ZS[k]), vec(XS[i], YS[j], zs[k]), color, i ? 1 : -1, j ? 1 : -1, k ? 1 : -1, nx[i], ny[j], nz[k]);
   add_solid(SOLID_BOX, vec(x0, y0, z0), vec(x1, y1, z1), 0, 0);
}

void r3d_box(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1, uint32_t color, int32_t flags)
{
   if (cur_bevel && !flags && cur_occlude && !cur_glass && !cur_decal)
   {
      /* only where the box is about four bevels thick: thin panels and legs keep sharp edges */
      int32_t b = hd_min(cur_bevel, hd_min(hd_min(x1 - x0, y1 - y0), z1 - z0) / 4);
      if (b * 4 >= cur_bevel * 3)
      {
         bevel_box(x0, y0, z0, x1, y1, z1, color, b);
         return;
      }
   }
   int64_t o = (flags & R3D_INSIDE) ? -1 : 1;
   r3d_vec p000 = vec(x0, y0, z0), p100 = vec(x1, y0, z0), p010 = vec(x0, y1, z0), p110 = vec(x1, y1, z0);
   r3d_vec p001 = vec(x0, y0, z1), p101 = vec(x1, y0, z1), p011 = vec(x0, y1, z1), p111 = vec(x1, y1, z1);
   if (!(flags & R3D_SKIP_BOTTOM))
      add_quad(p001, p101, p100, p000, color, 0, -o, 0);
   if (!(flags & R3D_SKIP_TOP))
      add_quad(p010, p110, p111, p011, color, 0, o, 0);
   if (!(flags & R3D_SKIP_Z))
   {
      add_quad(p100, p000, p010, p110, color, 0, 0, -o);
      add_quad(p001, p101, p111, p011, color, 0, 0, o);
   }
   if (!(flags & R3D_SKIP_X))
   {
      add_quad(p000, p001, p011, p010, color, -o, 0, 0);
      add_quad(p101, p100, p110, p111, color, o, 0, 0);
   }
   if (!(flags & R3D_INSIDE))
      add_solid(SOLID_BOX, p000, p111, 0, 0);
}

void r3d_cylinder(int32_t x, int32_t y, int32_t z, int32_t r, int32_t r_top, int32_t h, int32_t sides, uint32_t color)
{
   int32_t i, cell = cur_cell;
   int32_t rm = hd_max(r, r_top);
   sides = hd_clamp(sides, 3, 32);
   for (i = 0; i < sides; i++)
   {
      int32_t a0 = i * ANGLE_FULL / sides, a1 = (i + 1) * ANGLE_FULL / sides, am = (a0 + a1) / 2;
      int32_t c0 = hd_cos(a0), s0 = hd_sin(a0), c1 = hd_cos(a1), s1 = hd_sin(a1);
      r3d_vec b0 = vec(x + (int32_t)(((int64_t)r * c0) >> 14), y, z + (int32_t)(((int64_t)r * s0) >> 14));
      r3d_vec b1 = vec(x + (int32_t)(((int64_t)r * c1) >> 14), y, z + (int32_t)(((int64_t)r * s1) >> 14));
      r3d_vec t0 = vec(x + (int32_t)(((int64_t)r_top * c0) >> 14), y + h, z + (int32_t)(((int64_t)r_top * s0) >> 14));
      r3d_vec t1 = vec(x + (int32_t)(((int64_t)r_top * c1) >> 14), y + h, z + (int32_t)(((int64_t)r_top * s1) >> 14));
      cur_cell = 0;
      if (sides >= 6 && h > 0)
      {
         /* round: each edge lit by its own normal (a cone's leans by its slope) */
         int32_t n0[3], n1[3];
         int64_t slope = (int64_t)(r - r_top) * 16384 / h;
         unit(c0, slope, s0, &n0[0], &n0[1], &n0[2]);
         unit(c1, slope, s1, &n1[0], &n1[1], &n1[2]);
         if (r_top > 0)
         {
            add_tri_n(b1, b0, t0, color, hd_cos(am), 0, hd_sin(am), n1, n0, n0);
            add_tri_n(b1, t0, t1, color, hd_cos(am), 0, hd_sin(am), n1, n0, n1);
         }
         else
            add_tri_n(b0, b1, t1, color, hd_cos(am), 0, hd_sin(am), n0, n1, n1);
      }
      else if (r_top > 0)
         add_quad(b1, b0, t0, t1, color, hd_cos(am), 0, hd_sin(am));
      else
         add_tri(b0, b1, t1, color, hd_cos(am), 0, hd_sin(am));
      if (r_top > 0)
         add_tri(vec(x, y + h, z), t0, t1, color, 0, 1, 0);
      add_tri(vec(x, y, z), b0, b1, color, 0, -1, 0);
      cur_cell = cell;
   }
   add_solid(SOLID_CYL, vec(x - rm, y, z - rm), vec(x + rm, y + h, z + rm), r, r_top);
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
         int32_t k, lo[4], ring[4], hh[4];
         lo[0] = a0, ring[0] = cr0, hh[0] = cy0;
         lo[1] = a1, ring[1] = cr0, hh[1] = cy0;
         lo[2] = a1, ring[2] = cr1, hh[2] = cy1;
         lo[3] = a0, ring[3] = cr1, hh[3] = cy1;
         for (k = 0; k < 4; k++)
            p[k] = vec(x + (int32_t)((((int64_t)rx * ring[k]) >> 14) * hd_cos(lo[k]) >> 14), y + (int32_t)(((int64_t)ry * hh[k]) >> 14),
                       z + (int32_t)((((int64_t)rz * ring[k]) >> 14) * hd_sin(lo[k]) >> 14));
         {
            int64_t hx = (int64_t)p[0].x + p[1].x + p[2].x + p[3].x - 4LL * x;
            int64_t hy = (int64_t)p[0].y + p[1].y + p[2].y + p[3].y - 4LL * y;
            int64_t hz = (int64_t)p[0].z + p[1].z + p[2].z + p[3].z - 4LL * z;
            int32_t n[4][3];
            /* each corner lit by the ball's own normal there: (x/rx, y/ry, z/rz) of the unit sphere */
            int64_t ax = (int64_t)(ry >> 6) * (rz >> 6), ay = (int64_t)(rx >> 6) * (rz >> 6), az = (int64_t)(rx >> 6) * (ry >> 6);
            for (k = 0; k < 4; k++)
               unit(((((int64_t)ring[k] * hd_cos(lo[k])) >> 14) * ax) >> 14, ((int64_t)hh[k] * ay) >> 14, ((((int64_t)ring[k] * hd_sin(lo[k])) >> 14) * az) >> 14, &n[k][0], &n[k][1], &n[k][2]);
            add_tri_n(p[0], p[1], p[2], color, hx, hy, hz, n[0], n[1], n[2]);
            add_tri_n(p[0], p[2], p[3], color, hx, hy, hz, n[0], n[2], n[3]);
         }
      }
   }
   add_solid(SOLID_BALL, vec(x - rx, y - ry, z - rz), vec(x + rx, y + ry, z + rz), hd_max(rx, rz), ry);
}

void r3d_light(int32_t x, int32_t y, int32_t z, int32_t radius, uint32_t color, int32_t strength)
{
   if (lamp_count >= R3D_MAX_LIGHTS)
      return;
   lamps[lamp_count].at = to_world(vec(x, y, z));
   lamps[lamp_count].radius = hd_max(radius, FX_ONE / 4);
   lamps[lamp_count].color = color & 0xffffffu;
   lamps[lamp_count].strength = strength;
   lamp_count++;
}

/* ---- baking: the voxel grid and the rays ---- */

#define VOX_MAX (480 * 96 * 480)
static uint8_t vox[VOX_MAX / 8];
static int32_t vox_x0, vox_y0, vox_z0, vox_nx, vox_ny, vox_nz, vox_size;

static int vox_get(int32_t i, int32_t j, int32_t k)
{
   int32_t n;
   if (i < 0 || j < 0 || k < 0 || i >= vox_nx || j >= vox_ny || k >= vox_nz)
      return 0;
   n = (k * vox_ny + j) * vox_nx + i;
   return (vox[n >> 3] >> (n & 7)) & 1;
}

static void vox_fill(const r3d_solid *s)
{
   int32_t i0 = (s->x0 - vox_x0) / vox_size, i1 = (s->x1 - vox_x0 - 1) / vox_size;
   int32_t j0 = (s->y0 - vox_y0) / vox_size, j1 = (s->y1 - vox_y0 - 1) / vox_size;
   int32_t k0 = (s->z0 - vox_z0) / vox_size, k1 = (s->z1 - vox_z0 - 1) / vox_size;
   int32_t i, j, k, h = hd_max(1, s->y1 - s->y0);
   i0 = hd_max(i0, 0), j0 = hd_max(j0, 0), k0 = hd_max(k0, 0);
   i1 = hd_min(i1, vox_nx - 1), j1 = hd_min(j1, vox_ny - 1), k1 = hd_min(k1, vox_nz - 1);
   for (k = k0; k <= k1; k++)
      for (j = j0; j <= j1; j++)
         for (i = i0; i <= i1; i++)
         {
            int32_t n;
            if (s->kind != SOLID_BOX)
            {
               /* the cube's center inside the round shape */
               int64_t px = vox_x0 + (int64_t)i * vox_size + vox_size / 2 - s->cx;
               int64_t py = vox_y0 + (int64_t)j * vox_size + vox_size / 2;
               int64_t pz = vox_z0 + (int64_t)k * vox_size + vox_size / 2 - s->cz;
               if (s->kind == SOLID_CYL)
               {
                  int64_t r = s->r0 + ((int64_t)(s->r1 - s->r0) * (py - s->y0)) / h;
                  if (px * px + pz * pz > r * r)
                     continue;
               }
               else
               {
                  int64_t cy = (s->y0 + s->y1) / 2, ry = hd_max(1, s->r1), rx = hd_max(1, s->r0);
                  int64_t dy = py - cy;
                  /* (px/rx)^2 + (dy/ry)^2 + (pz/rx)^2 <= 1, scaled by 2^14 */
                  int64_t q = ((px * 128) / rx) * ((px * 128) / rx) + ((dy * 128) / ry) * ((dy * 128) / ry) + ((pz * 128) / rx) * ((pz * 128) / rx);
                  if (q > ((int64_t)1 << 28))
                     continue;
               }
            }
            n = (k * vox_ny + j) * vox_nx + i;
            vox[n >> 3] |= (uint8_t)(1 << (n & 7));
         }
}

static void vox_build(const r3d_light_cfg *c)
{
   int32_t i, x1, y1, z1;
   int64_t cells;
   memset(vox, 0, sizeof vox);
   if (!tri_count)
   {
      vox_nx = vox_ny = vox_nz = 0;
      return;
   }
   vox_x0 = x1 = tris[0].v[0].x;
   vox_y0 = y1 = tris[0].v[0].y;
   vox_z0 = z1 = tris[0].v[0].z;
   for (i = 0; i < tri_count; i++)
   {
      int32_t k;
      for (k = 0; k < 3; k++)
      {
         vox_x0 = hd_min(vox_x0, tris[i].v[k].x), x1 = hd_max(x1, tris[i].v[k].x);
         vox_y0 = hd_min(vox_y0, tris[i].v[k].y), y1 = hd_max(y1, tris[i].v[k].y);
         vox_z0 = hd_min(vox_z0, tris[i].v[k].z), z1 = hd_max(z1, tris[i].v[k].z);
      }
   }
   if (c->hi.x > c->lo.x)
   {
      vox_x0 = c->lo.x, x1 = c->hi.x;
      vox_y0 = c->lo.y, y1 = c->hi.y;
      vox_z0 = c->lo.z, z1 = c->hi.z;
   }
   /* 5 cm cubes, bigger when the world does not fit */
   vox_size = FX_ONE / 20;
   for (;;)
   {
      vox_nx = (x1 - vox_x0) / vox_size + 2;
      vox_ny = (y1 - vox_y0) / vox_size + 2;
      vox_nz = (z1 - vox_z0) / vox_size + 2;
      cells = (int64_t)vox_nx * vox_ny * vox_nz;
      if (cells <= VOX_MAX)
         break;
      vox_size = vox_size * 5 / 4;
   }
   for (i = 0; i < solid_count; i++)
      vox_fill(&solids[i]);
}

/* Whether a ray from p (16.16 meters) along d (2.14) is clear for `reach` (16.16 meters). */
static int ray_clear(r3d_vec p, int32_t dx, int32_t dy, int32_t dz, int32_t reach)
{
   int32_t step = vox_size / 2, walked;
   int64_t x = (int64_t)p.x - vox_x0, y = (int64_t)p.y - vox_y0, z = (int64_t)p.z - vox_z0;
   int64_t sx = ((int64_t)dx * step) >> 14, sy = ((int64_t)dy * step) >> 14, sz = ((int64_t)dz * step) >> 14;
   for (walked = 0; walked < reach; walked += step)
   {
      x += sx, y += sy, z += sz;
      if (x < 0 || y < 0 || z < 0 || y >= (int64_t)vox_ny * vox_size)
      {
         if (y >= (int64_t)vox_ny * vox_size || y < 0)
            return 1; /* out of the world above or below: the sky */
      }
      if (vox_get((int32_t)(x / vox_size), (int32_t)(y / vox_size), (int32_t)(z / vox_size)))
         return 0;
   }
   return 1;
}

int32_t r3d_solid_at(r3d_vec p)
{
   if (!vox_size || p.x < vox_x0 || p.y < vox_y0 || p.z < vox_z0)
      return 0;
   return vox_get((p.x - vox_x0) / vox_size, (p.y - vox_y0) / vox_size, (p.z - vox_z0) / vox_size);
}

int32_t r3d_reach(r3d_vec from, r3d_vec to)
{
   int64_t dx = (int64_t)to.x - from.x, dy = (int64_t)to.y - from.y, dz = (int64_t)to.z - from.z;
   int64_t most = hd_max(hd_max((int32_t)(dx < 0 ? -dx : dx), (int32_t)(dy < 0 ? -dy : dy)), (int32_t)(dz < 0 ? -dz : dz));
   int32_t n, i;
   if (!vox_size)
      return 65536;
   n = (int32_t)(most / (vox_size / 2)) + 1;
   for (i = 1; i <= n; i++)
   {
      r3d_vec q;
      q.x = from.x + (int32_t)(dx * i / n);
      q.y = from.y + (int32_t)(dy * i / n);
      q.z = from.z + (int32_t)(dz * i / n);
      if (r3d_solid_at(q))
         return (int32_t)((int64_t)(i - 1) * 65536 / n);
   }
   return 65536;
}

/* 26 directions around a point (a cube's neighbours), 16384 long. */
static int32_t dirs[26][3];
static int dirs_ready;

static void dirs_build(void)
{
   int32_t n = 0, x, y, z;
   for (x = -1; x <= 1; x++)
      for (y = -1; y <= 1; y++)
         for (z = -1; z <= 1; z++)
            if (x || y || z)
            {
               unit(x, y, z, &dirs[n][0], &dirs[n][1], &dirs[n][2]);
               n++;
            }
   dirs_ready = 1;
}

/* 0xRRGGBB to light, 256 = white (squared: close to the screen's gamma). */
static void lin3(uint32_t c, int32_t *r, int32_t *g, int32_t *b)
{
   int32_t cr = (c >> 16) & 0xff, cg = (c >> 8) & 0xff, cb = c & 0xff;
   *r = cr * cr * 256 / 65025;
   *g = cg * cg * 256 / 65025;
   *b = cb * cb * 256 / 65025;
}

enum { LP_SHADOWS = 0, LP_NONE, LP_MAPPED };

/*
 * The light at a point facing n: LP_SHADOWS all of it with the sun's and the
 * sky's shadows, LP_NONE without shadows (what moves), LP_MAPPED for a face
 * with a light map: the sun's part apart in sun_out (its shadow is in the
 * map) and the sky without its corners (in the map too); lamps are always
 * in, with their shadows except for LP_NONE.
 */
static void light_point(const r3d_light_cfg *c, r3d_vec p, int32_t nx, int32_t ny, int32_t nz, int32_t *lr, int32_t *lg, int32_t *lb, int mode, int32_t *sun_out)
{
   int shadows = mode == LP_SHADOWS;
   int32_t sr, sg, sb, kr, kg, kb, gr, gg, gb, l, k, sun = 0, sky = 0, sky_w = 0;
   int32_t sx, sy, sz, ndl;
   int64_t r = 0, g = 0, b = 0;
   r3d_vec q;
   q.x = p.x + (int32_t)(((int64_t)nx * (vox_size + vox_size / 4)) >> 14);
   q.y = p.y + (int32_t)(((int64_t)ny * (vox_size + vox_size / 4)) >> 14);
   q.z = p.z + (int32_t)(((int64_t)nz * (vox_size + vox_size / 4)) >> 14);
   lin3(c->sun_color, &sr, &sg, &sb);
   lin3(c->sky_color, &kr, &kg, &kb);
   lin3(c->ground_color, &gr, &gg, &gb);
   unit(c->sun.x, c->sun.y, c->sun.z, &sx, &sy, &sz);
   ndl = (int32_t)(((int64_t)nx * sx + (int64_t)ny * sy + (int64_t)nz * sz) >> 14);
   if (ndl > 0)
   {
      /* five rays around the sun's direction: a soft edge */
      static const int32_t jit[5][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
      int32_t soft = c->sun_soft * 12;
      for (k = 0; k < 5; k++)
      {
         int32_t dx, dy, dz;
         if (!shadows)
         {
            sun += 256;
            continue;
         }
         /* lean the ray across the sun's direction (on two axes perpendicular to it) */
         unit((int64_t)sx * 4 + (int64_t)jit[k][0] * soft, (int64_t)sy * 4, (int64_t)sz * 4 + (int64_t)jit[k][1] * soft, &dx, &dy, &dz);
         sun += ray_clear(q, dx, dy, dz, FX(12)) ? 256 : 0;
      }
      sun = sun / 5;
      l = (int32_t)(((int64_t)ndl * sun) >> 14) * c->sun_strength >> 8; /* 0..256 */
      if (mode == LP_MAPPED)
      {
         sun_out[0] = (int32_t)hd_min(4095, (int32_t)(((int64_t)sr * l) >> 8));
         sun_out[1] = (int32_t)hd_min(4095, (int32_t)(((int64_t)sg * l) >> 8));
         sun_out[2] = (int32_t)hd_min(4095, (int32_t)(((int64_t)sb * l) >> 8));
      }
      else
      {
         r += (int64_t)sr * l;
         g += (int64_t)sg * l;
         b += (int64_t)sb * l;
      }
   }
   else if (mode == LP_MAPPED)
      sun_out[0] = sun_out[1] = sun_out[2] = 0;
   /* the sky (and the ground's bounce) from every direction the face sees, less where things are near */
   for (k = 0; k < 26; k++)
   {
      int32_t w = (int32_t)(((int64_t)nx * dirs[k][0] + (int64_t)ny * dirs[k][1] + (int64_t)nz * dirs[k][2]) >> 14);
      if (w <= 0)
         continue;
      sky_w += w;
      if (!shadows || ray_clear(q, dirs[k][0], dirs[k][1], dirs[k][2], c->ao_reach))
         sky += w;
   }
   {
      int32_t ao = sky_w ? (int32_t)((int64_t)sky * 256 / sky_w) : 256;
      int32_t up = (ny + TRIG_ONE) >> 7, down = 256 - up; /* 0..256 */
      int32_t ks = (int32_t)(((int64_t)c->sky_strength * ao) >> 8), gs = (int32_t)(((int64_t)c->ground_strength * ao) >> 8);
      r += ((int64_t)kr * ks * (128 + up / 2) >> 8) + ((int64_t)gr * gs * down >> 8);
      g += ((int64_t)kg * ks * (128 + up / 2) >> 8) + ((int64_t)gg * gs * down >> 8);
      b += ((int64_t)kb * ks * (128 + up / 2) >> 8) + ((int64_t)gb * gs * down >> 8);
   }
   /* the lamps */
   for (k = 0; k < lamp_count && mode != LP_NONE; k++)
   {
      const r3d_lamp *m = &lamps[k];
      int32_t d = length(q, m->at), dx, dy, dz, n;
      int64_t att;
      int32_t mr, mg, mb;
      if (d >= m->radius || d <= 0)
         continue;
      unit((int64_t)m->at.x - q.x, (int64_t)m->at.y - q.y, (int64_t)m->at.z - q.z, &dx, &dy, &dz);
      n = (int32_t)(((int64_t)nx * dx + (int64_t)ny * dy + (int64_t)nz * dz) >> 14);
      if (n <= 0)
         continue;
      /* clear up to the lamp's own body */
      if (!ray_clear(q, dx, dy, dz, d - FX_ONE / 8))
         continue;
      att = 256 - ((int64_t)d * 256 / m->radius);
      att = att * att >> 8;
      lin3(m->color, &mr, &mg, &mb);
      att = (att * m->strength >> 8) * n >> 14;
      r += mr * att;
      g += mg * att;
      b += mb * att;
   }
   *lr = (int32_t)hd_min(4095, (int32_t)(r >> 8));
   *lg = (int32_t)hd_min(4095, (int32_t)(g >> 8));
   *lb = (int32_t)hd_min(4095, (int32_t)(b >> 8));
}

/* How much sun (0..255) and sky (0..255) reach a point of a light-mapped face. */
/*
 * What moves is lit without shadows or lamps (LP_NONE): its light depends on
 * its normal only, so it is worked out once for 128 x 128 normals (an
 * octahedron unfolded on a square) and looked up.
 */
#define NT 128
static int32_t nt_light[NT * NT][3];
static r3d_light_cfg nt_cfg;
static int nt_ready;

static int32_t nt_cell(int32_t nx, int32_t ny, int32_t nz)
{
   int64_t l1 = (int64_t)hd_abs(nx) + hd_abs(ny) + hd_abs(nz);
   int64_t u, v;
   if (!l1)
      return 0;
   u = (int64_t)nx * 16384 / l1, v = (int64_t)nz * 16384 / l1;
   if (ny < 0)
   {
      int64_t uu = (16384 - (v < 0 ? -v : v)) * (u < 0 ? -1 : 1), vv = (16384 - (u < 0 ? -u : u)) * (v < 0 ? -1 : 1);
      u = uu, v = vv;
   }
   return hd_clamp((int32_t)((u + 16384) * NT / 32769), 0, NT - 1) + hd_clamp((int32_t)((v + 16384) * NT / 32769), 0, NT - 1) * NT;
}

static void light_none(const r3d_light_cfg *c, int32_t nx, int32_t ny, int32_t nz, int32_t *lr, int32_t *lg, int32_t *lb)
{
   const int32_t *e;
   if (!nt_ready || memcmp(&nt_cfg, c, sizeof nt_cfg) != 0)
   {
      int32_t i, j;
      r3d_vec o = { 0, 0, 0 };
      for (j = 0; j < NT; j++)
         for (i = 0; i < NT; i++)
         {
            /* the cell's middle back to a normal */
            int64_t u = ((int64_t)i * 2 + 1) * 16384 / NT - 16384, v = ((int64_t)j * 2 + 1) * 16384 / NT - 16384;
            int64_t y = 16384 - (u < 0 ? -u : u) - (v < 0 ? -v : v);
            int32_t ux, uy, uz;
            if (y < 0)
            {
               int64_t uu = (16384 - (v < 0 ? -v : v)) * (u < 0 ? -1 : 1), vv = (16384 - (u < 0 ? -u : u)) * (v < 0 ? -1 : 1);
               u = uu, v = vv;
            }
            unit(u, y, v, &ux, &uy, &uz);
            light_point(c, o, ux, uy, uz, &nt_light[j * NT + i][0], &nt_light[j * NT + i][1], &nt_light[j * NT + i][2], LP_NONE, NULL);
         }
      nt_cfg = *c;
      nt_ready = 1;
   }
   e = nt_light[nt_cell(nx, ny, nz)];
   *lr = e[0], *lg = e[1], *lb = e[2];
}

static uint32_t lm_texel(const r3d_light_cfg *c, r3d_vec p, int32_t nx, int32_t ny, int32_t nz)
{
   static const int32_t jit[5][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
   int32_t sx, sy, sz, k, sun = 0, sky = 0, sky_w = 0, soft = c->sun_soft * 12;
   r3d_vec q;
   q.x = p.x + (int32_t)(((int64_t)nx * (vox_size + vox_size / 4)) >> 14);
   q.y = p.y + (int32_t)(((int64_t)ny * (vox_size + vox_size / 4)) >> 14);
   q.z = p.z + (int32_t)(((int64_t)nz * (vox_size + vox_size / 4)) >> 14);
   unit(c->sun.x, c->sun.y, c->sun.z, &sx, &sy, &sz);
   for (k = 0; k < 5; k++)
   {
      int32_t dx, dy, dz;
      unit((int64_t)sx * 4 + (int64_t)jit[k][0] * soft, (int64_t)sy * 4, (int64_t)sz * 4 + (int64_t)jit[k][1] * soft, &dx, &dy, &dz);
      sun += ray_clear(q, dx, dy, dz, FX(12)) ? 51 : 0;
   }
   for (k = 0; k < 26; k++)
   {
      int32_t w = (int32_t)(((int64_t)nx * dirs[k][0] + (int64_t)ny * dirs[k][1] + (int64_t)nz * dirs[k][2]) >> 14);
      if (w <= 0)
         continue;
      sky_w += w;
      if (ray_clear(q, dirs[k][0], dirs[k][1], dirs[k][2], c->ao_reach))
         sky += w;
   }
   sky = sky_w ? (int32_t)((int64_t)sky * 255 / sky_w) : 255;
   return (uint32_t)hd_clamp(sun, 0, 255) | (uint32_t)hd_clamp(sky, 0, 255) << 16;
}

typedef struct
{
   const r3d_light_cfg *c;
} bake_job;

/* The light maps of faces first..last-1 (each on its own, so on several cores). */
static void bake_faces(void *ctx, int32_t first, int32_t last)
{
   const r3d_light_cfg *c = ((bake_job *)ctx)->c;
   int32_t f, i, j;
   for (f = first; f < last; f++)
   {
      const r3d_lmface *lf = &lm_faces[f];
      const r3d_tri *t = &tris[lf->tri];
      for (j = 0; j < lf->h; j++)
         for (i = 0; i < lf->w; i++)
         {
            /* the texel's middle on the face */
            int32_t fi = (2 * i + 1) * 4096 / (2 * lf->w), fj = (2 * j + 1) * 4096 / (2 * lf->h);
            r3d_vec p = lerp(lerp(lf->a, lf->b, fi, 4096), lerp(lf->d, lf->c, fi, 4096), fj, 4096);
            lm_px[lf->base + j * lf->w + i] = lm_texel(c, p, t->nx, t->ny, t->nz);
         }
   }
}

/*
 * Each light map softened once (3 x 3 neighbours, the texel itself twice):
 * a few rays per texel leave a grain that this takes away, and shadow edges
 * stay where they are.
 */
static void blur_faces(void)
{
   static uint32_t tmp[400 * 400];
   int32_t f, i, j, di, dj;
   for (f = 0; f < lm_face_count; f++)
   {
      const r3d_lmface *lf = &lm_faces[f];
      uint32_t *px = lm_px + lf->base;
      for (j = 0; j < lf->h; j++)
         for (i = 0; i < lf->w; i++)
         {
            uint32_t sum = px[j * lf->w + i], n = 1;
            for (dj = -1; dj <= 1; dj++)
               for (di = -1; di <= 1; di++)
               {
                  int32_t x = i + di, y = j + dj;
                  if (x < 0 || y < 0 || x >= lf->w || y >= lf->h)
                     continue;
                  sum += px[y * lf->w + x]; /* two lanes of 8 bits in 16: 10 of them never carry over */
                  n++;
               }
            tmp[j * lf->w + i] = ((sum & 0xffffu) / n) | (((sum >> 16) / n) << 16);
         }
      memcpy(px, tmp, sizeof(uint32_t) * (size_t)(lf->w * lf->h));
   }
}

/* The corners' light of triangles first..last-1. */
static void bake_tris(void *ctx, int32_t first, int32_t last)
{
   const r3d_light_cfg *c = ((bake_job *)ctx)->c;
   int32_t i, k;
   for (i = first; i < last; i++)
   {
      r3d_tri *t = &tris[i];
      if (t->flags & (TF_GLOW | TF_DECAL))
         continue;
      for (k = 0; k < 3; k++)
      {
         int32_t sun[3] = { 0, 0, 0 };
         light_point(c, t->v[k], t->vn[k][0], t->vn[k][1], t->vn[k][2], &t->lr[k], &t->lg[k], &t->lb[k], t->lm >= 0 ? LP_MAPPED : LP_SHADOWS, sun);
         t->sr[k] = sun[0], t->sg[k] = sun[1], t->sb[k] = sun[2];
      }
   }
}

void r3d_bake(const r3d_light_cfg *c)
{
   bake_job j;
   dirs_build();
   vox_build(c);
   j.c = c;
   /* every face and triangle on its own: the bake runs on every core */
   hd_rows(PAR_WIDE, lm_face_count, bake_faces, &j);
   blur_faces();
   hd_rows(PAR_WIDE, tri_count, bake_tris, &j);
   baked_count = tri_count;
}

int32_t r3d_surface_below(r3d_vec p)
{
   int32_t i, best = R3D_NOTHING;
   for (i = 0; i < hd_min(baked_count, tri_count); i++)
   {
      const r3d_tri *t = &tris[i];
      const r3d_vec *a = &t->v[0], *b = &t->v[1], *c = &t->v[2];
      int64_t d0, d1, d2, y;
      if (t->ny < 4096)
         continue; /* not turned up (a wall, a side, an underside) */
      /* inside the triangle seen from above: the same side of its three edges */
      d0 = ((int64_t)b->x - a->x) * ((int64_t)p.z - a->z) - ((int64_t)b->z - a->z) * ((int64_t)p.x - a->x);
      d1 = ((int64_t)c->x - b->x) * ((int64_t)p.z - b->z) - ((int64_t)c->z - b->z) * ((int64_t)p.x - b->x);
      d2 = ((int64_t)a->x - c->x) * ((int64_t)p.z - c->z) - ((int64_t)a->z - c->z) * ((int64_t)p.x - c->x);
      if ((d0 < 0 || d1 < 0 || d2 < 0) && (d0 > 0 || d1 > 0 || d2 > 0))
         continue;
      /* its plane's height there: n . (q - a) = 0 */
      y = a->y - ((int64_t)t->nx * ((int64_t)p.x - a->x) + (int64_t)t->nz * ((int64_t)p.z - a->z)) / t->ny;
      if (y <= p.y && y > best)
         best = (int32_t)y;
   }
   return best;
}

/* ---- the screen's colors: linear light to 0..255 through a filmic curve ---- */

#define TONE_IN 16384 /* 4096 is 1.0 */
static uint8_t tone[TONE_IN];
static uint16_t srgb_lin[256]; /* 0..255 to 0..4096 (squared) */
static int tone_ready;

static void tone_build(void)
{
   int32_t i;
   for (i = 0; i < 256; i++)
      srgb_lin[i] = (uint16_t)(i * i * 4096 / 65025);
   for (i = 0; i < TONE_IN; i++)
   {
      /* x (2.51 x + 0.03) / (x (2.43 x + 0.59) + 0.14), x = i / 4096, with a little more exposure */
      int64_t x = (int64_t)i * 3 / 2;
      int64_t num = x * ((251 * x) / 100 + 4096 * 3 / 100);
      int64_t den = x * ((243 * x) / 100 + 4096 * 59 / 100) + (int64_t)4096 * 4096 * 14 / 100;
      int64_t y = den ? num * 4096 / den : 0; /* 0..4096 */
      if (y > 4096)
         y = 4096;
      tone[i] = (uint8_t)hd_min(255, (int32_t)isqrt64((uint64_t)(y * 65025 / 4096)));
   }
   tone_ready = 1;
}

/* ---- drawing ---- */

/* One value across a triangle as a plane on screen: its value at corner 0 and its steps per subpixel, 16 bits of fraction. */
typedef struct
{
   int64_t at0, dx, dy;
} attr;

enum { A_Z = 0, A_R, A_G, A_B, A_U, A_T, A_LS, A_LT, A_SR, A_SG, A_SB, A_COUNT };

/* A triangle on screen: 28.4 pixels, its corners' values, then (after setup) its planes. */
typedef struct
{
   int32_t x[3], y[3];
   int32_t v[A_COUNT][3]; /* 1/z, light r g b, texture u t, light map s t, the sun's light r g b */
   int32_t y0, y1, xa, xb; /* the pixels it covers */
   int32_t tex, flags;
   int32_t lm, lmw, lmh, lmbase; /* its light map: -1 none, else its size and first texel */
   int32_t sun_r, sun_g, sun_b;  /* the sun's light on it: the same on the whole face (a flat face, a far light) */
   int32_t glass;                /* see-through: how much it covers, 1..255; 0 solid */
   int32_t smooth_tex;           /* its texture is seen bigger than its texels: read it between four of them */
   attr a[A_COUNT];
} r3d_px;

/*
 * The frame's triangles, in stretches: the world is emitted on EMIT_PARTS
 * cores at once, each part into its own stretch, and the instances after
 * them in one more; every pass reads the stretches in order, so the result
 * is the same as on one core.
 */
#define EMIT_PARTS 4
#define SEGS (EMIT_PARTS + 1)
static r3d_px drawn[R3D_MAX_DRAWN];
static int32_t seg_first[SEGS], seg_n[SEGS], drawn_count;
static uint32_t zbuf[HD_OUT_MAX_W * HD_OUT_MAX_H];
static uint32_t aa_src[HD_OUT_MAX_W * HD_OUT_MAX_H];
/*
 * Bloom, of what gives light (lamps, screens): each glowing pixel's light
 * above white (10 bits a channel, of 4096 = 1.0 over the threshold, >> 3)
 * with its depth, then a quarter-size copy blurred twice. A row is cleared
 * only when something glows on it, and a pixel counts only if nothing
 * nearer covered it later (its depth is still the nearest).
 */
static uint32_t glow_src[HD_OUT_MAX_W * HD_OUT_MAX_H], glow_z[HD_OUT_MAX_W * HD_OUT_MAX_H];
static int32_t glow_row[HD_OUT_MAX_H], glow_frame = 1;
#define BLOOM_DIV 4
#define BLOOM_W (HD_OUT_MAX_W / BLOOM_DIV + 2)
#define BLOOM_H (HD_OUT_MAX_H / BLOOM_DIV + 2)
#define BLOOM_FROM 4300 /* above white: lamps, screens and the brightest sun glow */
static int32_t bloom_a[BLOOM_W * BLOOM_H * 3], bloom_b[BLOOM_W * BLOOM_H * 3];
static int32_t bloom_w, bloom_h, bloom_on;

typedef struct
{
   r3d_px *buf;
   int32_t n, cap;
} emit_out;

/* The world's triangles in runs of CHUNK, each run's bounds: a run out of view is skipped whole, and runs are drawn nearest first. */
#define CHUNK 64
#define CHUNKS_MAX (R3D_MAX_TRIS / CHUNK + 1)
static r3d_vec chunk_lo[CHUNKS_MAX], chunk_hi[CHUNKS_MAX];
static int32_t chunks_for = -1;
typedef struct
{
   int64_t key; /* distance from the eye, squared */
   int32_t chunk;
} chunk_order;
static chunk_order order[CHUNKS_MAX];

int32_t r3d_drawn(void)
{
   return drawn_count;
}

static void chunks_build(void)
{
   int32_t c, i, k;
   for (c = 0; c * CHUNK < tri_count; c++)
   {
      r3d_vec lo = tris[c * CHUNK].v[0], hi = lo;
      for (i = c * CHUNK; i < hd_min(tri_count, (c + 1) * CHUNK); i++)
         for (k = 0; k < 3; k++)
         {
            const r3d_vec *p = &tris[i].v[k];
            lo.x = hd_min(lo.x, p->x), hi.x = hd_max(hi.x, p->x);
            lo.y = hd_min(lo.y, p->y), hi.y = hd_max(hi.y, p->y);
            lo.z = hd_min(lo.z, p->z), hi.z = hd_max(hi.z, p->z);
         }
      chunk_lo[c] = lo;
      chunk_hi[c] = hi;
   }
   chunks_for = tri_count;
}

/* The frame's constants: the camera's turn, the lens. */
typedef struct
{
   const r3d_view *v;
   int32_t cy, sy, cp, sp; /* yaw and pitch */
   int32_t w, h, f;        /* the surface and the focal length, pixels */
   int32_t e;              /* the exposure */
} frame_k;

/* A point of the camera's space (x right, y up, z ahead; 16.16 meters) with what it carries. */
typedef struct
{
   int32_t x, y, z;
   int32_t u, t, lr, lg, lb;
   int32_t ls, lt, sr, sg, sb;
} cam_pt;

static void to_camera(const frame_k *k, r3d_vec p, cam_pt *c)
{
   int64_t dx = (int64_t)p.x - k->v->eye.x, dy = (int64_t)p.y - k->v->eye.y, dz = (int64_t)p.z - k->v->eye.z;
   int64_t x = (dx * k->cy - dz * k->sy) >> 14;
   int64_t z = (dx * k->sy + dz * k->cy) >> 14;
   c->x = (int32_t)x;
   c->y = (int32_t)((dy * k->cp + z * k->sp) >> 14);
   c->z = (int32_t)((z * k->cp - dy * k->sp) >> 14);
}

static void frame_setup(frame_k *k, const hd_surface *s, const r3d_view *v)
{
   int32_t half = hd_max(1, v->fov / 2), t;
   k->v = v;
   k->cy = hd_cos(v->yaw);
   k->sy = hd_sin(v->yaw);
   k->cp = hd_cos(v->pitch);
   k->sp = hd_sin(v->pitch);
   k->w = s->w;
   k->h = s->h;
   k->e = v->exposure > 0 ? v->exposure : 256;
   /* focal = (h / 2) / tan(fov / 2) */
   t = hd_sin(half);
   k->f = t ? (int32_t)(((int64_t)(s->h / 2) * hd_cos(half)) / t) : s->h;
}

int r3d_project(const hd_surface *s, const r3d_view *v, r3d_vec p, int32_t *x, int32_t *y)
{
   frame_k k;
   cam_pt c;
   frame_setup(&k, s, v);
   to_camera(&k, p, &c);
   if (c.z < v->near_z)
      return 0;
   *x = (int32_t)(k.w / 2 + ((int64_t)c.x * k.f) / c.z);
   *y = (int32_t)(k.h / 2 - ((int64_t)c.y * k.f) / c.z);
   return 1;
}

/* The five planes the view keeps: in front of near, and inside the screen's four edges (one pixel of margin). */
static int64_t plane(const frame_k *k, int32_t i, const cam_pt *p)
{
   int64_t hw = k->w / 2 + 1, hh = k->h / 2 + 1;
   switch (i)
   {
   case 0: return (int64_t)p->z - k->v->near_z;
   case 1: return (int64_t)p->x * k->f + (int64_t)p->z * hw;
   case 2: return (int64_t)p->z * hw - (int64_t)p->x * k->f;
   case 3: return (int64_t)p->z * hh - (int64_t)p->y * k->f;
   default: return (int64_t)p->y * k->f + (int64_t)p->z * hh;
   }
}

/* Whether a box of the world is wholly outside one of the view's planes. */
static int box_hidden(const frame_k *k, r3d_vec lo, r3d_vec hi)
{
   cam_pt c[8];
   int32_t i, j;
   for (i = 0; i < 8; i++)
   {
      r3d_vec p;
      p.x = i & 1 ? hi.x : lo.x;
      p.y = i & 2 ? hi.y : lo.y;
      p.z = i & 4 ? hi.z : lo.z;
      to_camera(k, p, &c[i]);
   }
   for (j = 0; j < 5; j++)
   {
      for (i = 0; i < 8; i++)
         if (plane(k, j, &c[i]) >= 0)
            break;
      if (i == 8)
         return 1;
   }
   return 0;
}

#define POLY_MAX 9

static int32_t mix_at(int32_t a, int32_t b, int64_t t)
{
   return a + (int32_t)(((int64_t)(b - a) * t) >> 16);
}

static int32_t clip(const frame_k *k, cam_pt *poly, int32_t n)
{
   cam_pt tmp[POLY_MAX];
   int32_t i, j, m;
   for (i = 0; i < 5 && n >= 3; i++)
   {
      int32_t out = 0;
      for (j = 0; j < n; j++)
         out += plane(k, i, &poly[j]) < 0;
      if (!out)
         continue; /* nothing crosses this plane */
      m = 0;
      for (j = 0; j < n; j++)
      {
         const cam_pt *a = &poly[j], *b = &poly[(j + 1) % n];
         int64_t da = plane(k, i, a), db = plane(k, i, b);
         if (da >= 0 && m < POLY_MAX)
            tmp[m++] = *a;
         if (((da >= 0) != (db >= 0)) && m < POLY_MAX)
         {
            /*
             * where the edge crosses the plane, t in 0..65536, always from
             * the same end of the edge: two triangles that share it get
             * the same point, so no crack opens between them
             */
            const cam_pt *p = a, *q = b;
            int64_t dp = da, dq = db, t;
            cam_pt c;
            if (a->x > b->x || (a->x == b->x && (a->y > b->y || (a->y == b->y && a->z > b->z))))
               p = b, q = a, dp = db, dq = da;
            t = (dp * 65536) / (dp - dq);
            c.x = mix_at(p->x, q->x, t);
            c.y = mix_at(p->y, q->y, t);
            c.z = mix_at(p->z, q->z, t);
            c.u = mix_at(p->u, q->u, t);
            c.t = mix_at(p->t, q->t, t);
            c.lr = mix_at(p->lr, q->lr, t);
            c.lg = mix_at(p->lg, q->lg, t);
            c.lb = mix_at(p->lb, q->lb, t);
            c.ls = mix_at(p->ls, q->ls, t);
            c.lt = mix_at(p->lt, q->lt, t);
            c.sr = mix_at(p->sr, q->sr, t);
            c.sg = mix_at(p->sg, q->sg, t);
            c.sb = mix_at(p->sb, q->sb, t);
            tmp[m++] = c;
         }
      }
      memcpy(poly, tmp, sizeof(cam_pt) * (size_t)m);
      n = m;
   }
   return n;
}

/*
 * One triangle of the world, at w (world space) with the normal n, toward
 * the screen: cr cg cb its color as light (256 = white), light its corners'
 * light, three values each (NULL: the baked one).
 */
static void emit(emit_out *o, const frame_k *k, const r3d_tri *t, const r3d_vec w[3], int32_t nx, int32_t ny, int32_t nz, int32_t cr, int32_t cg, int32_t cb,
                 const int32_t *light)
{
   cam_pt poly[POLY_MAX];
   int32_t n, i, inside = 1;
   /* only faces turned toward the eye */
   if (((int64_t)k->v->eye.x - w[0].x) * nx + ((int64_t)k->v->eye.y - w[0].y) * ny + ((int64_t)k->v->eye.z - w[0].z) * nz <= 0)
      return;
   for (i = 0; i < 3; i++)
      to_camera(k, w[i], &poly[i]);
   for (i = 0; i < 5 && inside; i++)
      if (plane(k, i, &poly[0]) < 0 && plane(k, i, &poly[1]) < 0 && plane(k, i, &poly[2]) < 0)
         inside = 0;
   if (!inside)
      return;
   for (i = 0; i < 3; i++)
   {
      int32_t lr = light ? light[i * 3] : t->lr[i], lg = light ? light[i * 3 + 1] : t->lg[i], lb = light ? light[i * 3 + 2] : t->lb[i];
      poly[i].u = t->u[i];
      poly[i].t = t->t[i];
      poly[i].ls = poly[i].lt = poly[i].sr = poly[i].sg = poly[i].sb = 0;
      if (t->lm >= 0 && !light)
      {
         poly[i].ls = t->ls[i];
         poly[i].lt = t->lt[i];
         poly[i].sr = hd_min(1023, (int32_t)((((int64_t)t->sr[i] * cr) >> 8) * k->e >> 8));
         poly[i].sg = hd_min(1023, (int32_t)((((int64_t)t->sg[i] * cg) >> 8) * k->e >> 8));
         poly[i].sb = hd_min(1023, (int32_t)((((int64_t)t->sb[i] * cb) >> 8) * k->e >> 8));
      }
      if (t->flags & TF_DECAL)
      {
         /* a decal keeps its color as it is: the pixel multiplies what is under it */
         poly[i].lr = cr, poly[i].lg = cg, poly[i].lb = cb;
         continue;
      }
      if (t->flags & TF_GLOW)
         lr = lg = lb = t->glow;
      /* the light times the face's color and the exposure: what the pixel multiplies its texture by */
      poly[i].lr = hd_min(1023, (int32_t)((((int64_t)lr * cr) >> 8) * k->e >> 8));
      poly[i].lg = hd_min(1023, (int32_t)((((int64_t)lg * cg) >> 8) * k->e >> 8));
      poly[i].lb = hd_min(1023, (int32_t)((((int64_t)lb * cb) >> 8) * k->e >> 8));
   }
   n = clip(k, poly, 3);
   for (i = 1; i + 1 < n; i++)
   {
      r3d_px *d;
      int32_t j, idx[3];
      if (o->n >= o->cap)
         return;
      d = &o->buf[o->n++];
      idx[0] = 0, idx[1] = i, idx[2] = i + 1;
      for (j = 0; j < 3; j++)
      {
         const cam_pt *p = &poly[idx[j]];
         int64_t z = p->z < k->v->near_z ? k->v->near_z : p->z;
         d->x[j] = (int32_t)((int64_t)k->w * 8 + ((int64_t)p->x * k->f * 16) / z);
         d->y[j] = (int32_t)((int64_t)k->h * 8 - ((int64_t)p->y * k->f * 16) / z);
         d->v[A_Z][j] = (int32_t)(((int64_t)1 << 38) / z);
         d->v[A_R][j] = p->lr;
         d->v[A_G][j] = p->lg;
         d->v[A_B][j] = p->lb;
         d->v[A_U][j] = p->u;
         d->v[A_T][j] = p->t;
         d->v[A_LS][j] = p->ls;
         d->v[A_LT][j] = p->lt;
         d->v[A_SR][j] = p->sr;
         d->v[A_SG][j] = p->sg;
         d->v[A_SB][j] = p->sb;
      }
      d->tex = t->tex;
      d->flags = t->flags;
      d->glass = t->glass;
      d->lm = light ? -1 : t->lm;
      d->sun_r = poly[0].sr, d->sun_g = poly[0].sg, d->sun_b = poly[0].sb;
      if (d->lm >= 0)
      {
         d->lmw = lm_faces[d->lm].w;
         d->lmh = lm_faces[d->lm].h;
         d->lmbase = lm_faces[d->lm].base;
      }
   }
}

typedef struct
{
   hd_surface *s;
} fill_job;

/* The i-th triangle of the frame, across the stretches. */
static r3d_px *drawn_at(int32_t i)
{
   int32_t p;
   for (p = 0; p < SEGS; p++)
   {
      if (i < seg_n[p])
         return &drawn[seg_first[p] + i];
      i -= seg_n[p];
   }
   return NULL;
}

/* The planes of triangles first..last-1 (each on its own, so on several cores). */
static void setup_rows(void *ctx, int32_t first, int32_t last)
{
   const hd_surface *s = ((fill_job *)ctx)->s;
   int32_t i, a;
   for (i = first; i < last; i++)
   {
      r3d_px *t = drawn_at(i);
      int64_t x0 = t->x[0], y0 = t->y[0], x1 = t->x[1], y1 = t->y[1], x2 = t->x[2], y2 = t->y[2], area, k;
      area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
      t->y0 = 1, t->y1 = 0; /* nothing to draw, until it is set up */
      if (!area)
         continue;
      if (area < 0)
      {
         /* turn it so its area is positive: corners 1 and 2 swap */
         k = t->x[1], t->x[1] = t->x[2], t->x[2] = (int32_t)k;
         k = t->y[1], t->y[1] = t->y[2], t->y[2] = (int32_t)k;
         for (a = 0; a < A_COUNT; a++)
            k = t->v[a][1], t->v[a][1] = t->v[a][2], t->v[a][2] = (int32_t)k;
         x1 = t->x[1], y1 = t->y[1], x2 = t->x[2], y2 = t->y[2];
         area = -area;
      }
      if (t->tex >= 0)
      {
         /* the texture repeats: move its coordinates near zero so the pixels' sums stay small */
         int32_t tw = 256 << texs[t->tex].wlog, th = 256 << texs[t->tex].hlog;
         int32_t mu = hd_min(t->v[A_U][0], hd_min(t->v[A_U][1], t->v[A_U][2])), mt = hd_min(t->v[A_T][0], hd_min(t->v[A_T][1], t->v[A_T][2]));
         int32_t su = (int32_t)(((int64_t)mu - (mu < 0 ? tw - 1 : 0)) / tw * tw), st = (int32_t)(((int64_t)mt - (mt < 0 ? th - 1 : 0)) / th * th);
         for (a = 0; a < 3; a++)
            t->v[A_U][a] -= su, t->v[A_T][a] -= st;
      }
      for (a = 0; a < A_COUNT; a++)
      {
         int64_t v0 = t->v[a][0], v1 = t->v[a][1], v2 = t->v[a][2];
         if (((a == A_U || a == A_T) && t->tex < 0) || (a >= A_LS && t->lm < 0) || a >= A_SR)
            continue;
         t->a[a].at0 = v0 * 65536;
         t->a[a].dx = (((v1 - v0) * (y2 - y0) - (v2 - v0) * (y1 - y0)) * 65536) / area;
         t->a[a].dy = (((v2 - v0) * (x1 - x0) - (v1 - v0) * (x2 - x0)) * 65536) / area;
      }
      t->smooth_tex = 0;
      if (t->tex >= 0)
      {
         /* how far the texture moves per pixel (texels times 256): under 3/4 of a texel, it is magnified */
         int64_t m = 192LL * 65536 / 16;
         int64_t ux = t->a[A_U].dx < 0 ? -t->a[A_U].dx : t->a[A_U].dx, uy = t->a[A_U].dy < 0 ? -t->a[A_U].dy : t->a[A_U].dy;
         int64_t vx = t->a[A_T].dx < 0 ? -t->a[A_T].dx : t->a[A_T].dx, vy = t->a[A_T].dy < 0 ? -t->a[A_T].dy : t->a[A_T].dy;
         t->smooth_tex = ux < m && uy < m && vx < m && vy < m;
      }
      t->y0 = hd_max(0, (hd_min(t->y[0], hd_min(t->y[1], t->y[2])) - 8) >> 4);
      t->y1 = hd_min(s->h - 1, (hd_max(t->y[0], hd_max(t->y[1], t->y[2])) + 8) >> 4);
      t->xa = hd_max(0, (hd_min(t->x[0], hd_min(t->x[1], t->x[2])) - 8) >> 4);
      t->xb = hd_min(s->w - 1, (hd_max(t->x[0], hd_max(t->x[1], t->x[2])) + 8) >> 4);
   }
}

/* A value of a plane at a subpixel offset, then shifted down to `frac` bits of fraction for the row's 32-bit sums. */
static int32_t plane_at(const attr *a, int64_t ox, int64_t oy, int32_t drop)
{
   return (int32_t)((a->at0 + a->dx * ox + a->dy * oy) >> drop);
}

/* a toward b by f (0..256), on all four channels of 0xAARRGGBB */
static inline uint32_t mix4(uint32_t a, uint32_t b, uint32_t f)
{
   uint32_t g = 256 - f;
   uint32_t lo = (((a & 0x00ff00ffu) * g + (b & 0x00ff00ffu) * f) >> 8) & 0x00ff00ffu;
   uint32_t hi = (((a >> 8) & 0x00ff00ffu) * g + ((b >> 8) & 0x00ff00ffu) * f) & 0xff00ff00u;
   return lo | hi;
}

static void fill_one(hd_surface *s, const r3d_px *t, int32_t row0, int32_t row1)
{
   int64_t x0 = t->x[0], y0 = t->y[0], x1 = t->x[1], y1 = t->y[1], x2 = t->x[2], y2 = t->y[2];
   int32_t ya = hd_max(row0, t->y0), yb = hd_min(row1 - 1, t->y1), xa = t->xa, xb = t->xb, y, x;
   const r3d_tex *tx = t->tex >= 0 ? &texs[t->tex] : NULL;
   uint32_t umask = tx ? ((1u << tx->wlog) - 1) : 0, vmask = tx ? ((1u << tx->hlog) - 1) : 0, wlog = tx ? (uint32_t)tx->wlog : 0;
   const uint32_t *tpx = tx ? tx->px : NULL;
   int decal = t->flags & TF_DECAL, glowing = bloom_on && (t->flags & TF_GLOW);
   int32_t glass = t->glass, smooth_tex = t->smooth_tex;
   /* each edge: its value at (xa, row) and its step per pixel */
   int64_t s0 = -(y2 - y1) * 16, s1 = -(y0 - y2) * 16, s2 = -(y1 - y0) * 16;
   /* the sums' steps per pixel: 1/z with 4 bits of fraction, light 16, texture 8 */
   int32_t zs = (int32_t)((t->a[A_Z].dx * 16) >> 12), rs = (int32_t)(t->a[A_R].dx * 16), gs = (int32_t)(t->a[A_G].dx * 16), bs = (int32_t)(t->a[A_B].dx * 16);
   int32_t us = tx ? (int32_t)((t->a[A_U].dx * 16) >> 8) : 0, ts = tx ? (int32_t)((t->a[A_T].dx * 16) >> 8) : 0;
   int lm = t->lm >= 0;
   const uint32_t *lmp = lm ? lm_px + t->lmbase : NULL;
   int32_t lmw = t->lmw, smax = (t->lmw - 1) * 256, tmax = (t->lmh - 1) * 256;
   int32_t lss = lm ? (int32_t)((t->a[A_LS].dx * 16) >> 8) : 0, lts = lm ? (int32_t)((t->a[A_LT].dx * 16) >> 8) : 0;
   int32_t sun_r = t->sun_r, sun_g = t->sun_g, sun_b = t->sun_b;
   for (y = ya; y <= yb; y++)
   {
      int64_t py = (int64_t)y * 16 + 8, px = (int64_t)xa * 16 + 8, oy = py - y0, ox;
      int64_t we[3], se[3];
      int32_t xs = xa, xe = xb, e, zf, rf, gf, bf, uf = 0, tf = 0, lsf = 0, ltf = 0;
      uint32_t *row = s->px + (int64_t)y * s->w, *zrow = zbuf + (int64_t)y * s->w;
      we[0] = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
      we[1] = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
      we[2] = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
      se[0] = s0, se[1] = s1, se[2] = s2;
      /* where the row is inside every edge: from xs to xe */
      for (e = 0; e < 3; e++)
      {
         if (se[e] == 0)
         {
            if (we[e] < 0)
               xe = xs - 1;
         }
         else if (se[e] > 0)
         {
            if (we[e] < 0)
               xs = hd_max(xs, xa + (int32_t)hd_min((int64_t)(xb - xa + 1), (-we[e] + se[e] - 1) / se[e]));
         }
         else if (we[e] < 0)
            xe = xs - 1;
         else
            xe = hd_min(xe, xa + (int32_t)hd_min((int64_t)(xb - xa), we[e] / -se[e]));
      }
      if (xs > xe)
         continue;
      ox = (int64_t)xs * 16 + 8 - x0;
      zf = plane_at(&t->a[A_Z], ox, oy, 12);
      rf = plane_at(&t->a[A_R], ox, oy, 0);
      gf = plane_at(&t->a[A_G], ox, oy, 0);
      bf = plane_at(&t->a[A_B], ox, oy, 0);
      if (tx)
      {
         uf = plane_at(&t->a[A_U], ox, oy, 8);
         tf = plane_at(&t->a[A_T], ox, oy, 8);
      }
      if (lm)
      {
         lsf = plane_at(&t->a[A_LS], ox, oy, 8);
         ltf = plane_at(&t->a[A_LT], ox, oy, 8);
      }
      for (x = xs; x <= xe; x++, zf += zs, rf += rs, gf += gs, bf += bs, uf += us, tf += ts, lsf += lss, ltf += lts)
      {
         uint32_t z = (uint32_t)(zf >> 4), texel = 0xffffffffu;
         int32_t lr, lg, lb;
         if (decal ? z + 64 < zrow[x] : z <= zrow[x])
            continue;
         if (tx)
         {
            if (smooth_tex)
            {
               /* between the four nearest texels (every channel, alpha too) */
               uint32_t su = (uint32_t)uf - 32768u, sv = (uint32_t)tf - 32768u, fx = (su >> 8) & 255, fy = (sv >> 8) & 255;
               uint32_t x0 = (su >> 16) & umask, x1 = (x0 + 1) & umask, y0 = ((sv >> 16) & vmask) << wlog, y1 = ((((sv >> 16) + 1) & vmask)) << wlog;
               texel = mix4(mix4(tpx[y0 | x0], tpx[y0 | x1], fx), mix4(tpx[y1 | x0], tpx[y1 | x1], fx), fy);
            }
            else
               texel = tpx[(((uint32_t)tf >> 16) & vmask) << wlog | (((uint32_t)uf >> 16) & umask)];
         }
         lr = rf >> 16, lg = gf >> 16, lb = bf >> 16;
         if (lm)
         {
            /* the light map, between its four nearest texels: how much sun (low lane) and sky (high lane) arrive */
            int32_t ls = hd_clamp((lsf >> 8) - 128, 0, smax), lt = hd_clamp((ltf >> 8) - 128, 0, tmax);
            int32_t ix = ls >> 8, iy = lt >> 8, fx = ls & 255, fy = lt & 255, ix1 = ix + (ix * 256 < smax), iy1 = iy + (iy * 256 < tmax);
            uint32_t a0 = lmp[iy * lmw + ix], a1 = lmp[iy * lmw + ix1], b0 = lmp[iy1 * lmw + ix], b1 = lmp[iy1 * lmw + ix1];
            uint32_t h0 = ((a0 * (uint32_t)(256 - fx) + a1 * (uint32_t)fx) >> 8) & 0x00ff00ffu;
            uint32_t h1 = ((b0 * (uint32_t)(256 - fx) + b1 * (uint32_t)fx) >> 8) & 0x00ff00ffu;
            uint32_t mv = ((h0 * (uint32_t)(256 - fy) + h1 * (uint32_t)fy) >> 8) & 0x00ff00ffu;
            int32_t sun = (int32_t)(mv & 0xff), sky = (int32_t)(mv >> 16);
            lr = (lr * sky + sun_r * sun) >> 8;
            lg = (lg * sky + sun_g * sun) >> 8;
            lb = (lb * sky + sun_b * sun) >> 8;
         }
         if (decal)
         {
            /* darken toward the decal's color by its texture's alpha */
            uint32_t d = row[x], a = texel >> 24, inv = 255 - a;
            uint32_t r = ((d >> 16) & 0xff) * (inv + a * (uint32_t)hd_clamp(lr, 0, 256) / 256) / 255;
            uint32_t g = ((d >> 8) & 0xff) * (inv + a * (uint32_t)hd_clamp(lg, 0, 256) / 256) / 255;
            uint32_t b = (d & 0xff) * (inv + a * (uint32_t)hd_clamp(lb, 0, 256) / 256) / 255;
            row[x] = r << 16 | g << 8 | b;
            continue;
         }
         if ((texel >> 24) < 128)
            continue; /* the texture's holes */
         {
            int32_t r = (srgb_lin[(texel >> 16) & 0xff] * hd_max(0, lr)) >> 8;
            int32_t g = (srgb_lin[(texel >> 8) & 0xff] * hd_max(0, lg)) >> 8;
            int32_t b = (srgb_lin[texel & 0xff] * hd_max(0, lb)) >> 8;
            uint32_t c = (uint32_t)tone[hd_min(r, TONE_IN - 1)] << 16 | (uint32_t)tone[hd_min(g, TONE_IN - 1)] << 8 | tone[hd_min(b, TONE_IN - 1)];
            if (glass)
            {
               /* glass shows what is behind it, tinted; it keeps no depth, so what is behind was drawn first */
               row[x] = gfx_mix(row[x], c, glass);
               continue;
            }
            zrow[x] = z;
            row[x] = c;
            if (glowing && (r > BLOOM_FROM || g > BLOOM_FROM || b > BLOOM_FROM))
            {
               int64_t at = (int64_t)y * s->w + x;
               if (glow_row[y] != glow_frame)
               {
                  /* the row's first glow this frame (a row is filled by one core): forget the old ones */
                  memset(glow_src + (int64_t)y * s->w, 0, sizeof(uint32_t) * (size_t)s->w);
                  glow_row[y] = glow_frame;
               }
               {
                  /* the light above white, in the light's own color (each channel in proportion, so a warm lamp glows warm, not red) */
                  int32_t m = hd_max(r, hd_max(g, b)), over = m - BLOOM_FROM;
                  glow_src[at] = (uint32_t)hd_clamp((int32_t)((int64_t)r * over / m) >> 3, 0, 1023) << 20 | (uint32_t)hd_clamp((int32_t)((int64_t)g * over / m) >> 3, 0, 1023) << 10 |
                                 (uint32_t)hd_clamp((int32_t)((int64_t)b * over / m) >> 3, 0, 1023);
               }
               glow_z[at] = z;
            }
         }
      }
   }
}

static void fill_rows(void *ctx, int32_t row0, int32_t row1)
{
   hd_surface *s = ((fill_job *)ctx)->s;
   int32_t p, i, pass;
   /* everything solid first, then the glass over it */
   for (pass = 0; pass < 2; pass++)
      for (p = 0; p < SEGS; p++)
         for (i = 0; i < seg_n[p]; i++)
         {
            const r3d_px *t = &drawn[seg_first[p] + i];
            if ((t->glass != 0) == pass && t->y0 <= t->y1 && t->y1 >= row0 && t->y0 < row1)
               fill_one(s, t, row0, row1);
         }
}

/* A mesh's triangles toward the screen (an instance's, or a stretch of the world's). */
static void emit_range(emit_out *o, const frame_k *k, int32_t first, int32_t end, const r3d_instance *ins)
{
   int32_t i, j;
   int32_t c = ins ? hd_cos(ins->yaw) : TRIG_ONE, sn = ins ? hd_sin(ins->yaw) : 0;
   const r3d_light_cfg *lc = k->v->light;
   end = hd_min(end, tri_count);
   for (i = first; i < end; i++)
   {
      const r3d_tri *t = &tris[i];
      r3d_vec w[3];
      int32_t rx, ny, rz, light[9], vn[3][3], cr = t->cr, cg = t->cg, cb = t->cb;
      if (!ins)
      {
         emit(o, k, t, t->v, t->nx, t->ny, t->nz, cr, cg, cb, NULL);
         continue;
      }
      if (ins->rot[0] | ins->rot[4] | ins->rot[8])
      {
         /* a whole turn: an arm, a leg */
         r3d_vec n;
         for (j = 0; j < 3; j++)
         {
            r3d_vec p = r3d_rot_apply(ins->rot, t->v[j]);
            w[j].x = ins->at.x + p.x, w[j].y = ins->at.y + p.y, w[j].z = ins->at.z + p.z;
         }
         n.x = t->nx, n.y = t->ny, n.z = t->nz;
         n = r3d_rot_apply(ins->rot, n);
         rx = n.x, ny = n.y, rz = n.z;
         for (j = 0; j < 3; j++)
         {
            n.x = t->vn[j][0], n.y = t->vn[j][1], n.z = t->vn[j][2];
            n = r3d_rot_apply(ins->rot, n);
            vn[j][0] = n.x, vn[j][1] = n.y, vn[j][2] = n.z;
         }
      }
      else
      {
         for (j = 0; j < 3; j++)
         {
            w[j].x = ins->at.x + (int32_t)(((int64_t)t->v[j].x * c + (int64_t)t->v[j].z * sn) >> 14);
            w[j].y = ins->at.y + t->v[j].y;
            w[j].z = ins->at.z + (int32_t)(((int64_t)t->v[j].z * c - (int64_t)t->v[j].x * sn) >> 14);
         }
         rx = (int32_t)(((int64_t)t->nx * c + (int64_t)t->nz * sn) >> 14);
         rz = (int32_t)(((int64_t)t->nz * c - (int64_t)t->nx * sn) >> 14);
         ny = t->ny;
         for (j = 0; j < 3; j++)
         {
            vn[j][0] = (int32_t)(((int64_t)t->vn[j][0] * c + (int64_t)t->vn[j][2] * sn) >> 14);
            vn[j][1] = t->vn[j][1];
            vn[j][2] = (int32_t)(((int64_t)t->vn[j][2] * c - (int64_t)t->vn[j][0] * sn) >> 14);
         }
      }
      if (ins->tint >> 24)
         lin3(gfx_mix(t->color, ins->tint & 0xffffffu, (int32_t)(ins->tint >> 24)), &cr, &cg, &cb);
      /* what moves is lit by the sun and the sky as it goes, without shadows */
      if (lc && !(t->flags & (TF_GLOW | TF_DECAL)))
      {
         /* each corner by its own normal: round parts (a head, a hand) look round */
         int same = vn[0][0] == vn[1][0] && vn[0][1] == vn[1][1] && vn[0][2] == vn[1][2] && vn[0][0] == vn[2][0] && vn[0][1] == vn[2][1] && vn[0][2] == vn[2][2];
         for (j = 0; j < 3; j++)
            if (j == 0 || !same)
               light_none(lc, vn[j][0], vn[j][1], vn[j][2], &light[j * 3], &light[j * 3 + 1], &light[j * 3 + 2]);
            else
               light[j * 3] = light[0], light[j * 3 + 1] = light[1], light[j * 3 + 2] = light[2];
      }
      else
         for (j = 0; j < 9; j++)
            light[j] = 256;
      emit(o, k, t, w, rx, ny, rz, cr, cg, cb, light);
   }
}

typedef struct
{
   const frame_k *k;
   int32_t first, end;          /* the scene's triangles */
   int32_t from[EMIT_PARTS + 1]; /* each part's runs in `order` */
   emit_out out[EMIT_PARTS];
} emit_job;

static void emit_parts(void *ctx, int32_t first, int32_t last)
{
   emit_job *j = (emit_job *)ctx;
   int32_t p, c;
   for (p = first; p < last; p++)
      for (c = j->from[p]; c < j->from[p + 1]; c++)
      {
         int32_t ch = order[c].chunk;
         emit_range(&j->out[p], j->k, hd_max(j->first, ch * CHUNK), hd_min(j->end, (ch + 1) * CHUNK), NULL);
      }
}

static int order_cmp(const void *a, const void *b)
{
   const chunk_order *x = (const chunk_order *)a, *y = (const chunk_order *)b;
   if (x->key != y->key)
      return x->key < y->key ? -1 : 1;
   return x->chunk - y->chunk;
}

/*
 * The world's triangles toward the screen: the runs in view, nearest first
 * (what is hidden behind them then fails the depth test before it costs
 * anything), split between the cores.
 */
static void emit_world(const frame_k *k, const r3d_mesh *scene)
{
   emit_job j;
   int32_t c, p, n = 0, end = hd_min(scene->first + scene->count, tri_count), cap = R3D_MAX_DRAWN / SEGS;
   for (c = scene->first / CHUNK; c * CHUNK < end; c++)
   {
      int64_t dx, dy, dz;
      if (box_hidden(k, chunk_lo[c], chunk_hi[c]))
         continue;
      dx = ((int64_t)chunk_lo[c].x + chunk_hi[c].x) / 2 - k->v->eye.x;
      dy = ((int64_t)chunk_lo[c].y + chunk_hi[c].y) / 2 - k->v->eye.y;
      dz = ((int64_t)chunk_lo[c].z + chunk_hi[c].z) / 2 - k->v->eye.z;
      order[n].key = (dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8) + (dz >> 8) * (dz >> 8);
      order[n].chunk = c;
      n++;
   }
   qsort(order, (size_t)n, sizeof order[0], order_cmp);
   j.k = k;
   j.first = scene->first;
   j.end = end;
   for (p = 0; p <= EMIT_PARTS; p++)
      j.from[p] = n * p / EMIT_PARTS;
   for (p = 0; p < EMIT_PARTS; p++)
   {
      j.out[p].buf = drawn + p * cap;
      j.out[p].n = 0;
      j.out[p].cap = cap;
   }
   hd_rows(PAR_WIDE, EMIT_PARTS, emit_parts, &j);
   for (p = 0; p < EMIT_PARTS; p++)
   {
      seg_first[p] = p * cap;
      seg_n[p] = j.out[p].n;
   }
}

/* Softens the edges: a pixel that differs a lot from its neighbours takes some of them. */
static void finish_rows(void *ctx, int32_t y0, int32_t y1);
/* The finish (bloom, vignette) to apply right after smoothing each band of rows, or NULL. */
static void *smooth_then;

static void smooth_rows(void *ctx, int32_t y0, int32_t y1)
{
   hd_surface *s = ((fill_job *)ctx)->s;
   int32_t x, y, w = s->w, h = s->h;
   for (y = hd_max(1, y0); y < hd_min(h - 1, y1); y++)
   {
      const uint32_t *r = aa_src + (int64_t)y * w;
      uint32_t *o = s->px + (int64_t)y * w;
      for (x = 1; x < w - 1; x++)
      {
         uint32_t c = r[x], n = r[x - w], sd = r[x + w], e = r[x + 1], wv = r[x - 1];
         int32_t lc = (int32_t)((c >> 16 & 0xff) * 2 + (c >> 8 & 0xff) * 5 + (c & 0xff)) >> 3;
         int32_t ln = (int32_t)((n >> 16 & 0xff) * 2 + (n >> 8 & 0xff) * 5 + (n & 0xff)) >> 3;
         int32_t ls = (int32_t)((sd >> 16 & 0xff) * 2 + (sd >> 8 & 0xff) * 5 + (sd & 0xff)) >> 3;
         int32_t le = (int32_t)((e >> 16 & 0xff) * 2 + (e >> 8 & 0xff) * 5 + (e & 0xff)) >> 3;
         int32_t lw = (int32_t)((wv >> 16 & 0xff) * 2 + (wv >> 8 & 0xff) * 5 + (wv & 0xff)) >> 3;
         int32_t hi = hd_max(lc, hd_max(hd_max(ln, ls), hd_max(le, lw))), lo = hd_min(lc, hd_min(hd_min(ln, ls), hd_min(le, lw)));
         if (hi - lo > 24)
         {
            uint32_t rb = ((c & 0xff00ffu) * 4 + (n & 0xff00ffu) + (sd & 0xff00ffu) + (e & 0xff00ffu) + (wv & 0xff00ffu)) >> 3;
            uint32_t g = ((c & 0xff00u) * 4 + (n & 0xff00u) + (sd & 0xff00u) + (e & 0xff00u) + (wv & 0xff00u)) >> 3;
            o[x] = (rb & 0xff00ffu) | (g & 0xff00u);
         }
      }
   }
   if (smooth_then)
      finish_rows(smooth_then, y0, y1); /* the band is final: finish it while it is in the cache */
}

/* Bloom, 1: each quarter-size cell the sum of its 4 x 4 pixels' light above white. */
static void bloom_down(void *ctx, int32_t y0, int32_t y1)
{
   hd_surface *s = ((fill_job *)ctx)->s;
   int32_t x, y, i, j;
   for (y = y0; y < y1; y++)
      for (x = 0; x < bloom_w; x++)
      {
         int32_t r = 0, g = 0, b = 0;
         for (j = 0; j < BLOOM_DIV; j++)
         {
            int32_t py = hd_min(y * BLOOM_DIV + j, s->h - 1);
            const uint32_t *row = glow_src + (int64_t)py * s->w, *gz = glow_z + (int64_t)py * s->w, *zr = zbuf + (int64_t)py * s->w;
            if (glow_row[py] != glow_frame)
               continue; /* nothing glows on this row */
            for (i = 0; i < BLOOM_DIV; i++)
            {
               int32_t px = hd_min(x * BLOOM_DIV + i, s->w - 1);
               uint32_t v = row[px];
               if (v && gz[px] == zr[px]) /* still the nearest thing there */
                  r += (int32_t)(v >> 20), g += (int32_t)(v >> 10 & 1023), b += (int32_t)(v & 1023);
            }
         }
         bloom_a[(y * bloom_w + x) * 3] = r, bloom_a[(y * bloom_w + x) * 3 + 1] = g, bloom_a[(y * bloom_w + x) * 3 + 2] = b;
      }
}

/* Bloom, 2 and 3: a wide soft blur (1 3 6 8 6 3 1, of 28) across, then down. */
static const int32_t bloom_k[7] = { 1, 3, 6, 8, 6, 3, 1 };

static void bloom_across(void *ctx, int32_t y0, int32_t y1)
{
   int32_t x, y, c, k;
   (void)ctx;
   for (y = y0; y < y1; y++)
      for (x = 0; x < bloom_w; x++)
         for (c = 0; c < 3; c++)
         {
            int32_t sum = 0;
            for (k = 0; k < 7; k++)
               sum += bloom_k[k] * bloom_a[(y * bloom_w + hd_clamp(x + k - 3, 0, bloom_w - 1)) * 3 + c];
            bloom_b[(y * bloom_w + x) * 3 + c] = sum / 28;
         }
}

static void bloom_down_blur(void *ctx, int32_t y0, int32_t y1)
{
   int32_t x, y, c, k;
   (void)ctx;
   for (y = y0; y < y1; y++)
      for (x = 0; x < bloom_w; x++)
         for (c = 0; c < 3; c++)
         {
            int32_t sum = 0;
            for (k = 0; k < 7; k++)
               sum += bloom_k[k] * bloom_b[(hd_clamp(y + k - 3, 0, bloom_h - 1) * bloom_w + x) * 3 + c];
            bloom_a[(y * bloom_w + x) * 3 + c] = sum / 28;
         }
}

typedef struct
{
   hd_surface *s;
   int32_t bloom, vignette;
} finish_job;

/* The vignette, kept as a factor across (per column) times a factor down (per row): 256 = untouched. */
static int32_t vig_col[HD_OUT_MAX_W], vig_row[HD_OUT_MAX_H];

static void vignette_build(int32_t w, int32_t h, int32_t amount)
{
   int32_t i;
   /* (1 - a dx^2)(1 - a dy^2), d from -1 to 1 across: about 1 - a d^2, a = amount * 3 / 16 / 256 */
   for (i = 0; i < w; i++)
   {
      int64_t d = (int64_t)(2 * i - w) * 1024 / w; /* -1024..1024 */
      vig_col[i] = 256 - (int32_t)(d * d * amount * 3 / 16 / 1024 / 1024);
   }
   for (i = 0; i < h; i++)
   {
      int64_t d = (int64_t)(2 * i - h) * 1024 / h;
      vig_row[i] = 256 - (int32_t)(d * d * amount * 3 / 16 / 1024 / 1024);
   }
}

/* Bloom, 4, and the vignette: the blurred light added back (between the four nearest cells), the corners darkened. */
static void finish_rows(void *ctx, int32_t y0, int32_t y1)
{
   const finish_job *f = (const finish_job *)ctx;
   hd_surface *s = f->s;
   int32_t x, y, w = s->w, c, cx;
   int32_t span[(BLOOM_W + 1) * 3];
   for (y = y0; y < y1; y++)
   {
      uint32_t *row = s->px + (int64_t)y * w;
      int32_t keep_y = f->vignette ? vig_row[y] : 256, any = 0;
      if (f->bloom)
      {
         /* this row's bloom at every cell's column, between the two nearest rows of cells */
         int32_t sy = hd_max(0, y * 256 / BLOOM_DIV - 128), cy = sy >> 8, fy = sy & 255, cy1 = hd_min(cy + 1, bloom_h - 1);
         for (cx = 0; cx < bloom_w; cx++)
            for (c = 0; c < 3; c++)
            {
               int32_t a = bloom_a[(cy * bloom_w + cx) * 3 + c], d = bloom_a[(cy1 * bloom_w + cx) * 3 + c];
               span[cx * 3 + c] = a + (((d - a) * fy) >> 8);
               any |= span[cx * 3 + c];
            }
         span[bloom_w * 3] = span[bloom_w * 3 - 3], span[bloom_w * 3 + 1] = span[bloom_w * 3 - 2], span[bloom_w * 3 + 2] = span[bloom_w * 3 - 1];
      }
      if (!any && keep_y == 256 && !f->vignette)
         continue;
      for (x = 0; x < w; x++)
      {
         uint32_t p = row[x];
         int32_t ch[3] = { (int32_t)(p >> 16 & 255), (int32_t)(p >> 8 & 255), (int32_t)(p & 255) };
         int32_t keep = f->vignette ? (vig_col[x] * keep_y) >> 8 : 256;
         if (any)
         {
            int32_t sx = hd_max(0, x * 256 / BLOOM_DIV - 128), k = sx >> 8, fx = sx & 255;
            const int32_t *a = &span[k * 3], *b = &span[hd_min(k + 1, bloom_w - 1) * 3];
            if (a[0] | a[1] | a[2] | b[0] | b[1] | b[2])
               for (c = 0; c < 3; c++)
                  ch[c] += ((a[c] + (((b[c] - a[c]) * fx) >> 8)) * f->bloom) >> 13; /* 16 pixels' light summed: per pixel v / 16, of 512 = 1.0 */
         }
         if (keep != 256)
            for (c = 0; c < 3; c++)
               ch[c] = (ch[c] * keep) >> 8;
         row[x] = (uint32_t)hd_min(255, ch[0]) << 16 | (uint32_t)hd_min(255, ch[1]) << 8 | (uint32_t)hd_min(255, ch[2]);
      }
   }
}

/* The sky, top to bottom, and an empty depth buffer. */
static void clear_rows(void *ctx, int32_t y0, int32_t y1)
{
   fill_job *job = (fill_job *)ctx;
   hd_surface *s = job->s;
   int32_t x, y;
   for (y = y0; y < y1; y++)
   {
      uint32_t c = gfx_mix(sky_top, sky_bottom, y * 256 / hd_max(1, s->h - 1));
      uint32_t *row = s->px + (int64_t)y * s->w;
      for (x = 0; x < s->w; x++)
         row[x] = c;
      memset(zbuf + (int64_t)y * s->w, 0, sizeof(uint32_t) * (size_t)s->w);
   }
}

#ifdef R3D_PROF
#include <stdio.h>
#include <time.h>
static double prof_t[8], prof_sum[8];
static int prof_n;
static void prof_mark(int i)
{
   struct timespec t;
   timespec_get(&t, TIME_UTC);
   prof_t[i] = t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
   if (i > 0)
      prof_sum[i] += prof_t[i] - prof_t[i - 1];
   if (i == 6 && ++prof_n % 200 == 0)
   {
      fprintf(stderr, "clear %.2f emit %.2f inst %.2f setup %.2f fill %.2f smooth %.2f (ms, %d tris)\n", prof_sum[1] / 200, prof_sum[2] / 200,
              prof_sum[3] / 200, prof_sum[4] / 200, prof_sum[5] / 200, prof_sum[6] / 200, (int)drawn_count);
      memset(prof_sum, 0, sizeof prof_sum);
   }
}
#define PROF(i) prof_mark(i)
#else
#define PROF(i) ((void)0)
#endif

void r3d_draw(hd_surface *s, const r3d_view *v, const r3d_mesh *scene, const r3d_instance *inst, int32_t count)
{
   frame_k k;
   fill_job job;
   int32_t i, cap = R3D_MAX_DRAWN / SEGS;
   if (!tone_ready)
      tone_build();
   if (!dirs_ready)
      dirs_build();
   if (chunks_for != tri_count)
      chunks_build();
   frame_setup(&k, s, v);
   job.s = s;
   bloom_on = v->bloom > 0;
   if (bloom_on)
      glow_frame = glow_frame == 0x7fffffff ? 1 : glow_frame + 1;
   sky_top = v->sky_top;
   sky_bottom = v->sky_bottom;
   PROF(0);
   /* every pass on every core, even on a small screen: a 3D frame has more work per pixel than a 2D one */
   hd_rows(PAR_WIDE, s->h, clear_rows, &job);
   PROF(1);
   memset(seg_n, 0, sizeof seg_n);
   if (scene)
      emit_world(&k, scene);
   PROF(2);
   {
      emit_out o;
      o.buf = drawn + EMIT_PARTS * cap;
      o.n = 0;
      o.cap = cap;
      for (i = 0; i < count; i++)
         emit_range(&o, &k, inst[i].mesh.first, inst[i].mesh.first + inst[i].mesh.count, &inst[i]);
      seg_first[EMIT_PARTS] = EMIT_PARTS * cap;
      seg_n[EMIT_PARTS] = o.n;
   }
   for (drawn_count = 0, i = 0; i < SEGS; i++)
      drawn_count += seg_n[i];
   PROF(3);
   hd_rows(1 << 12, drawn_count, setup_rows, &job);
   PROF(4);
   hd_rows(PAR_WIDE, s->h, fill_rows, &job);
   PROF(5);
   {
      finish_job f;
      f.s = s;
      f.bloom = hd_clamp(v->bloom, 0, 256);
      f.vignette = hd_clamp(v->vignette, 0, 256);
      smooth_then = NULL;
      if (f.vignette)
         vignette_build(s->w, s->h, f.vignette);
      if (f.bloom)
      {
         /* the bloom is made from the pixels' light before smoothing (glow_src), so it is ready first */
         bloom_w = (s->w + BLOOM_DIV - 1) / BLOOM_DIV;
         bloom_h = (s->h + BLOOM_DIV - 1) / BLOOM_DIV;
         hd_rows(PAR_WIDE, bloom_h, bloom_down, &job);
         hd_rows(PAR_WIDE, bloom_h, bloom_across, &job);
         hd_rows(PAR_WIDE, bloom_h, bloom_down_blur, &job);
      }
      if (v->smooth)
      {
         memcpy(aa_src, s->px, sizeof(uint32_t) * (size_t)s->w * (size_t)s->h);
         if (f.bloom || f.vignette)
            smooth_then = &f;
         hd_rows(PAR_WIDE, s->h, smooth_rows, &job);
         smooth_then = NULL;
      }
      else if (f.bloom || f.vignette)
         hd_rows(PAR_WIDE, s->h, finish_rows, &f);
   }
   PROF(6);
}
