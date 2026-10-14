/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Models from .hdm files (model3d.h): read byte by byte (the same on every
 * computer), animated with integer quaternions, skinned on the processor.
 *
 * The file (little endian):
 *   "HDM1", version 1, vertices, triangles, bones, clips (int32 each)
 *   each vertex:   x, y, z (int32 16.16 m), normal (3 int16, 2.14), 4 bones (uint8), 4 weights (uint8, sum 255)
 *   each triangle: 3 vertices (uint16), 0 (uint16), color (uint32 0xRRGGBB)
 *   each bone:     parent (int32, -1 the root), name (24 bytes), rest turn (4 int16 quaternion x y z w, 2.14),
 *                  rest place (3 int32 16.16 m), both relative to the parent
 *   each clip:     name (24 bytes), frames, frames a second (int32), then for every frame every bone's
 *                  turn (4 int16) and place (3 int32) relative to its parent
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hd.h"
#include "model3d.h"

typedef struct
{
   r3d_vec p;
   int32_t n[3];
   uint8_t bone[4], w[4];
} m3_vert;

typedef struct
{
   int32_t v[3];
   uint32_t color;
} m3_tri;

typedef struct
{
   char name[24];
   int32_t frames, fps;
   const uint8_t *data; /* frames x bones x 20 bytes */
} m3_clip;

struct model3d
{
   char name[32];
   int32_t nv, nt, nb, nc;
   r3d_vec lo, hi;
   m3_vert *v;
   m3_tri *t;
   int32_t parent[MODEL3D_BONES];
   char bone_name[MODEL3D_BONES][24];
   model3d_xf rest_local[MODEL3D_BONES], rest_global[MODEL3D_BONES], rest_inverse[MODEL3D_BONES];
   m3_clip clip[MODEL3D_CLIPS];
   uint8_t *file; /* the clips' frames point into it */
   r3d_vec *skinned; /* a vertex buffer for posing */
   int32_t (*normals)[3];
};

static model3d models[MODEL3D_MAX];
static int32_t model_count;
int32_t model3d_gen;

static int32_t rd32(const uint8_t *p)
{
   return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static int32_t rd16(const uint8_t *p)
{
   return (int16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

/* A unit quaternion (x, y, z, w in 2.14) as a turn's matrix. */
static void quat_rot(const int32_t q[4], int32_t r[9])
{
   int64_t x = q[0], y = q[1], z = q[2], w = q[3];
   r[0] = 16384 - (int32_t)((2 * (y * y + z * z)) >> 14);
   r[1] = (int32_t)((2 * (x * y - z * w)) >> 14);
   r[2] = (int32_t)((2 * (x * z + y * w)) >> 14);
   r[3] = (int32_t)((2 * (x * y + z * w)) >> 14);
   r[4] = 16384 - (int32_t)((2 * (x * x + z * z)) >> 14);
   r[5] = (int32_t)((2 * (y * z - x * w)) >> 14);
   r[6] = (int32_t)((2 * (x * z - y * w)) >> 14);
   r[7] = (int32_t)((2 * (y * z + x * w)) >> 14);
   r[8] = 16384 - (int32_t)((2 * (x * x + y * y)) >> 14);
}

static uint32_t isqrt(uint64_t v)
{
   uint64_t r = 0, b = (uint64_t)1 << 62;
   while (b > v)
      b >>= 2;
   while (b)
   {
      if (v >= r + b)
      {
         v -= r + b;
         r = (r >> 1) + b;
      }
      else
         r >>= 1;
      b >>= 2;
   }
   return (uint32_t)r;
}

/* Between two turns: a straight blend, made unit again (close frames, so it is near a real slerp). */
static void quat_blend(const int32_t a[4], const int32_t b[4], int32_t f, int32_t out[4])
{
   int64_t d = (int64_t)a[0] * b[0] + (int64_t)a[1] * b[1] + (int64_t)a[2] * b[2] + (int64_t)a[3] * b[3], len;
   int32_t s = d < 0 ? -1 : 1, k;
   int64_t q[4];
   for (k = 0; k < 4; k++)
      q[k] = a[k] + (((int64_t)s * b[k] - a[k]) * f >> 16);
   len = isqrt((uint64_t)(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]));
   for (k = 0; k < 4; k++)
      out[k] = len ? (int32_t)(q[k] * 16384 / len) : (k == 3 ? 16384 : 0);
}

r3d_vec model3d_apply(const int32_t r[9], r3d_vec v)
{
   r3d_vec o;
   o.x = (int32_t)(((int64_t)r[0] * v.x + (int64_t)r[1] * v.y + (int64_t)r[2] * v.z) >> 14);
   o.y = (int32_t)(((int64_t)r[3] * v.x + (int64_t)r[4] * v.y + (int64_t)r[5] * v.z) >> 14);
   o.z = (int32_t)(((int64_t)r[6] * v.x + (int64_t)r[7] * v.y + (int64_t)r[8] * v.z) >> 14);
   return o;
}

void model3d_mul(const model3d_xf *a, const model3d_xf *b, model3d_xf *out)
{
   model3d_xf m;
   int32_t i, j;
   for (i = 0; i < 3; i++)
      for (j = 0; j < 3; j++)
         m.r[i * 3 + j] = (int32_t)(((int64_t)a->r[i * 3] * b->r[j] + (int64_t)a->r[i * 3 + 1] * b->r[3 + j] + (int64_t)a->r[i * 3 + 2] * b->r[6 + j]) >> 14);
   m.t = model3d_apply(a->r, b->t);
   m.t.x += a->t.x, m.t.y += a->t.y, m.t.z += a->t.z;
   *out = m;
}

/* The inverse of a turn and place (the turn's transpose). */
static void inverse(const model3d_xf *a, model3d_xf *out)
{
   int32_t i, j;
   r3d_vec t;
   for (i = 0; i < 3; i++)
      for (j = 0; j < 3; j++)
         out->r[i * 3 + j] = a->r[j * 3 + i];
   t = model3d_apply(out->r, a->t);
   out->t.x = -t.x, out->t.y = -t.y, out->t.z = -t.z;
}

/* A bone's turn and place as the file keeps them. */
static void read_xf(const uint8_t *p, model3d_xf *x)
{
   int32_t q[4], k;
   for (k = 0; k < 4; k++)
      q[k] = rd16(p + 2 * k);
   quat_rot(q, x->r);
   x->t.x = rd32(p + 8), x->t.y = rd32(p + 12), x->t.z = rd32(p + 16);
}

static void model_free(model3d *m)
{
   free(m->v);
   free(m->t);
   free(m->file);
   free(m->skinned);
   free(m->normals);
   memset(m, 0, sizeof *m);
}

void model3d_free_all(void)
{
   int32_t i;
   for (i = 0; i < model_count; i++)
      model_free(&models[i]);
   model_count = 0;
   model3d_gen++;
}

const char *model3d_load(const char *name, const uint8_t *data, size_t size)
{
   model3d *m;
   const uint8_t *p = data, *end = data + size;
   int32_t i, k;
   if (model_count >= MODEL3D_MAX)
      return "too many models (16 at most)";
   if (size < 24 || memcmp(data, "HDM1", 4) || rd32(data + 4) != 1)
      return "not a go-link HD model (.hdm, version 1)";
   m = &models[model_count];
   memset(m, 0, sizeof *m);
   m->nv = rd32(data + 8), m->nt = rd32(data + 12), m->nb = rd32(data + 16), m->nc = rd32(data + 20);
   p += 24;
   if (m->nv <= 0 || m->nv > 65535 || m->nt <= 0 || m->nt > 65536 || m->nb < 0 || m->nb > MODEL3D_BONES || m->nc < 0 || m->nc > MODEL3D_CLIPS ||
       (size_t)(end - p) < (size_t)m->nv * 26 + (size_t)m->nt * 12 + (size_t)m->nb * 48)
      return "a model's sizes are wrong (65535 vertices, 64 bones and 16 clips at most)";
   m->v = (m3_vert *)calloc((size_t)m->nv, sizeof *m->v);
   m->t = (m3_tri *)calloc((size_t)m->nt, sizeof *m->t);
   m->skinned = (r3d_vec *)calloc((size_t)m->nv, sizeof *m->skinned);
   m->normals = (int32_t(*)[3])calloc((size_t)m->nv, sizeof *m->normals);
   m->file = (uint8_t *)malloc(size);
   if (!m->v || !m->t || !m->skinned || !m->normals || !m->file)
   {
      model_free(m);
      return "not enough memory for a model";
   }
   memcpy(m->file, data, size);
   p = m->file + 24;
   end = m->file + size;
   for (i = 0; i < m->nv; i++, p += 26)
   {
      m3_vert *v = &m->v[i];
      int32_t sum = 0;
      v->p.x = rd32(p), v->p.y = rd32(p + 4), v->p.z = rd32(p + 8);
      v->n[0] = rd16(p + 12), v->n[1] = rd16(p + 14), v->n[2] = rd16(p + 16);
      for (k = 0; k < 4; k++)
      {
         v->bone[k] = p[18 + k];
         v->w[k] = p[22 + k];
         sum += v->w[k];
         if (m->nb && v->bone[k] >= m->nb)
            v->w[k] = 0, v->bone[k] = 0;
      }
      if (!sum)
         v->w[0] = 255;
   }
   m->lo = m->hi = m->v[0].p;
   for (i = 1; i < m->nv; i++)
   {
      m->lo.x = hd_min(m->lo.x, m->v[i].p.x), m->hi.x = hd_max(m->hi.x, m->v[i].p.x);
      m->lo.y = hd_min(m->lo.y, m->v[i].p.y), m->hi.y = hd_max(m->hi.y, m->v[i].p.y);
      m->lo.z = hd_min(m->lo.z, m->v[i].p.z), m->hi.z = hd_max(m->hi.z, m->v[i].p.z);
   }
   for (i = 0; i < m->nt; i++, p += 12)
   {
      for (k = 0; k < 3; k++)
      {
         m->t[i].v[k] = (uint16_t)rd16(p + 2 * k);
         if (m->t[i].v[k] >= m->nv)
         {
            model_free(m);
            return "a model's triangle points past its vertices";
         }
      }
      m->t[i].color = (uint32_t)rd32(p + 8) & 0xffffffu;
   }
   for (i = 0; i < m->nb; i++, p += 48)
   {
      m->parent[i] = rd32(p);
      if (m->parent[i] >= i)
         m->parent[i] = -1; /* parents come first; anything else is the root */
      memcpy(m->bone_name[i], p + 4, 23);
      read_xf(p + 28, &m->rest_local[i]);
   }
   model3d_globals(m, m->rest_local, m->rest_global);
   for (i = 0; i < m->nb; i++)
      inverse(&m->rest_global[i], &m->rest_inverse[i]);
   for (i = 0; i < m->nc; i++)
   {
      m3_clip *c = &m->clip[i];
      if (end - p < 32)
      {
         model_free(m);
         return "a model's clip is cut short";
      }
      memcpy(c->name, p, 23);
      c->frames = rd32(p + 24);
      c->fps = rd32(p + 28);
      p += 32;
      if (c->frames <= 0 || c->frames > 100000 || c->fps <= 0 || c->fps > 240 || (end - p) / 20 / m->nb < c->frames)
      {
         model_free(m);
         return "a model's clip is cut short";
      }
      c->data = p;
      p += (size_t)c->frames * m->nb * 20;
   }
   snprintf(m->name, sizeof m->name, "%s", name);
   model_count++;
   model3d_gen++;
   return NULL;
}

const model3d *model3d_find(const char *name)
{
   int32_t i;
   for (i = 0; name && i < model_count; i++)
      if (!strcmp(models[i].name, name))
         return &models[i];
   return NULL;
}

void model3d_bounds(const model3d *m, r3d_vec *lo, r3d_vec *hi)
{
   *lo = m->lo;
   *hi = m->hi;
}

void model3d_add(const model3d *m, r3d_vec scale, int32_t yaw, r3d_vec center)
{
   int32_t r[9], i, k;
   r3d_rotation(yaw, 0, 0, r);
   for (i = 0; i < m->nt; i++)
   {
      r3d_vec p[3];
      int32_t n[3][3];
      for (k = 0; k < 3; k++)
      {
         const m3_vert *v = &m->v[m->t[i].v[k]];
         r3d_vec q, vn;
         q.x = (int32_t)((int64_t)v->p.x * scale.x >> 16);
         q.y = (int32_t)((int64_t)v->p.y * scale.y >> 16);
         q.z = (int32_t)((int64_t)v->p.z * scale.z >> 16);
         q = model3d_apply(r, q);
         p[k].x = q.x + center.x, p[k].y = q.y + center.y, p[k].z = q.z + center.z;
         vn.x = scale.x < 0 ? -v->n[0] : v->n[0], vn.y = scale.y < 0 ? -v->n[1] : v->n[1], vn.z = scale.z < 0 ? -v->n[2] : v->n[2];
         vn = model3d_apply(r, vn);
         n[k][0] = vn.x, n[k][1] = vn.y, n[k][2] = vn.z;
      }
      if (((int64_t)scale.x < 0) ^ ((int64_t)scale.y < 0) ^ ((int64_t)scale.z < 0))
         r3d_triangle(p[0], p[2], p[1], n[0], n[2], n[1], m->t[i].color); /* mirrored: its corners the other way round, its front still out */
      else
         r3d_triangle(p[0], p[1], p[2], n[0], n[1], n[2], m->t[i].color);
   }
}

int32_t model3d_bones(const model3d *m)
{
   return m ? m->nb : 0;
}

int32_t model3d_bone(const model3d *m, const char *name)
{
   int32_t i;
   for (i = 0; m && i < m->nb; i++)
      if (!strcmp(m->bone_name[i], name))
         return i;
   return -1;
}

int32_t model3d_parent(const model3d *m, int32_t bone)
{
   return m && bone >= 0 && bone < m->nb ? m->parent[bone] : -1;
}

int32_t model3d_clip(const model3d *m, const char *name)
{
   int32_t i;
   for (i = 0; m && i < m->nc; i++)
      if (!strcmp(m->clip[i].name, name))
         return i;
   return -1;
}

int32_t model3d_clip_frames(const model3d *m, int32_t clip)
{
   if (!m || clip < 0 || clip >= m->nc)
      return 1;
   return hd_max(1, m->clip[clip].frames * 60 / m->clip[clip].fps);
}

const model3d_xf *model3d_rest(const model3d *m, int32_t bone)
{
   return &m->rest_global[hd_clamp(bone, 0, m->nb - 1)];
}

void model3d_sample(const model3d *m, int32_t clip, int32_t time, model3d_xf *local)
{
   const m3_clip *c;
   int64_t at;
   int32_t f0, f1, frac, i, k;
   if (clip < 0 || clip >= m->nc)
   {
      memcpy(local, m->rest_local, sizeof(model3d_xf) * (size_t)m->nb);
      return;
   }
   c = &m->clip[clip];
   /* game frames (60 a second, 16.16) into the clip's frames, going round */
   at = (int64_t)time * c->fps / 60;
   at %= (int64_t)c->frames << 16;
   if (at < 0)
      at += (int64_t)c->frames << 16;
   f0 = (int32_t)(at >> 16);
   f1 = (f0 + 1) % c->frames;
   frac = (int32_t)(at & 0xffff);
   for (i = 0; i < m->nb; i++)
   {
      const uint8_t *a = c->data + ((size_t)f0 * m->nb + i) * 20, *b = c->data + ((size_t)f1 * m->nb + i) * 20;
      int32_t qa[4], qb[4], q[4];
      for (k = 0; k < 4; k++)
         qa[k] = rd16(a + 2 * k), qb[k] = rd16(b + 2 * k);
      quat_blend(qa, qb, frac, q);
      quat_rot(q, local[i].r);
      local[i].t.x = rd32(a + 8) + (int32_t)(((int64_t)rd32(b + 8) - rd32(a + 8)) * frac >> 16);
      local[i].t.y = rd32(a + 12) + (int32_t)(((int64_t)rd32(b + 12) - rd32(a + 12)) * frac >> 16);
      local[i].t.z = rd32(a + 16) + (int32_t)(((int64_t)rd32(b + 16) - rd32(a + 16)) * frac >> 16);
   }
}

void model3d_globals(const model3d *m, const model3d_xf *local, model3d_xf *global)
{
   int32_t i;
   for (i = 0; i < m->nb; i++)
      if (m->parent[i] < 0)
         global[i] = local[i];
      else
         model3d_mul(&global[m->parent[i]], &local[i], &global[i]);
}

void model3d_body_build(model3d_body *b, const model3d *m)
{
   int32_t i;
   memset(b, 0, sizeof *b);
   b->model = m;
   b->gen = model3d_gen;
   if (!m)
      return;
   r3d_begin();
   for (i = 0; i < m->nt; i++)
   {
      const m3_vert *a = &m->v[m->t[i].v[0]], *c = &m->v[m->t[i].v[1]], *d = &m->v[m->t[i].v[2]];
      r3d_triangle(a->p, c->p, d->p, a->n, c->n, d->n, m->t[i].color);
   }
   b->mesh = r3d_end();
}

void model3d_body_pose(model3d_body *b, const model3d_xf *global, const int32_t place[9], r3d_vec at)
{
   model3d *m = (model3d *)b->model;
   model3d_xf skin[MODEL3D_BONES];
   int32_t i, k;
   if (!m || !m->nb || b->mesh.count != m->nt)
      return;
   /* each bone's move from rest: global x rest's inverse, then the whole model's place */
   for (i = 0; i < m->nb; i++)
   {
      model3d_xf s, p;
      model3d_mul(&global[i], &m->rest_inverse[i], &s);
      memcpy(p.r, place, sizeof p.r);
      p.t = at;
      model3d_mul(&p, &s, &skin[i]);
   }
   for (i = 0; i < m->nv; i++)
   {
      const m3_vert *v = &m->v[i];
      int64_t x = 0, y = 0, z = 0, nx = 0, ny = 0, nz = 0;
      for (k = 0; k < 4; k++)
         if (v->w[k])
         {
            const model3d_xf *s = &skin[v->bone[k]];
            r3d_vec q = model3d_apply(s->r, v->p), n;
            r3d_vec vn;
            vn.x = v->n[0], vn.y = v->n[1], vn.z = v->n[2];
            n = model3d_apply(s->r, vn);
            x += (int64_t)(q.x + s->t.x) * v->w[k];
            y += (int64_t)(q.y + s->t.y) * v->w[k];
            z += (int64_t)(q.z + s->t.z) * v->w[k];
            nx += (int64_t)n.x * v->w[k], ny += (int64_t)n.y * v->w[k], nz += (int64_t)n.z * v->w[k];
         }
      m->skinned[i].x = (int32_t)(x / 255), m->skinned[i].y = (int32_t)(y / 255), m->skinned[i].z = (int32_t)(z / 255);
      m->normals[i][0] = (int32_t)(nx / 255), m->normals[i][1] = (int32_t)(ny / 255), m->normals[i][2] = (int32_t)(nz / 255);
   }
   for (i = 0; i < m->nt; i++)
   {
      const int32_t *t = m->t[i].v;
      r3d_tri_pose(b->mesh.first + i, m->skinned[t[0]], m->skinned[t[1]], m->skinned[t[2]], m->normals[t[0]], m->normals[t[1]], m->normals[t[2]]);
   }
}
