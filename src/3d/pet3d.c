/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Pets for 3D worlds (pet3d.h): a cat of eight parts (body, head, a tail of
 * two pieces, four legs), each a mesh built once with its joint at the
 * origin, turned around its joint every frame: standing, trotting (the legs
 * in diagonal pairs), sitting and lying as a loaf, the tail always moving a
 * little. Integers only, so a pose is the same on every computer.
 */
#include <string.h>
#include "pet3d.h"

#define CM(n) FX_FRAC(n, 100)

static void cyl(int32_t x, int32_t y, int32_t z, int32_t r, int32_t rt, int32_t h, int32_t sides, uint32_t c)
{
   r3d_cylinder(CM(x), CM(y), CM(z), CM(r), CM(rt), CM(h), sides, c);
}

/* Half centimeters, for the small things on a face. */
static void ball2(int32_t x, int32_t y, int32_t z, int32_t rx, int32_t ry, int32_t rz, int32_t rings, uint32_t c)
{
   r3d_ball(FX_FRAC(x, 200), FX_FRAC(y, 200), FX_FRAC(z, 200), FX_FRAC(rx, 200), FX_FRAC(ry, 200), FX_FRAC(rz, 200), rings, c);
}

static void ball(int32_t x, int32_t y, int32_t z, int32_t rx, int32_t ry, int32_t rz, int32_t rings, uint32_t c)
{
   ball2(x * 2, y * 2, z * 2, rx * 2, ry * 2, rz * 2, rings, c);
}

/* A leg hanging from its joint, 12 cm, with a white paw. */
static void leg(const pet3d_look *look)
{
   r3d_begin();
   ball(0, 0, 0, 3, 4, 3, 3, look->fur);
   cyl(0, -11, 0, 2, 3, 11, 6, look->fur);
   ball2(0, -22, -1, 5, 3, 6, 2, look->white);
}

void pet3d_build(pet3d_kit *k, const pet3d_look *look)
{
   int32_t i;
   r3d_place(0, 0, 0, 0);
   r3d_detail(0);
   /* the body: the joint is its middle; the chest white, stripes across the back */
   r3d_begin();
   ball(0, 0, 0, 9, 9, 19, 5, look->fur);
   ball(0, -2, -12, 7, 7, 6, 3, look->white);
   for (i = 0; i < 4; i++)
      ball(0, 6, -8 + i * 6, 8, 3, 2, 3, look->stripes);
   k->part[PET_BODY] = r3d_end();
   /* the head: the joint is the neck; a face toward -z with ears, eyes that shine a little, a nose and whiskers */
   r3d_begin();
   ball(0, 0, 2, 6, 6, 6, 3, look->fur);             /* the neck, into the body */
   ball(0, 4, -4, 8, 7, 7, 4, look->fur);
   ball2(0, 2, -21, 9, 7, 6, 3, look->white);        /* the muzzle */
   ball2(0, 18, -12, 9, 2, 5, 2, look->stripes);      /* a stripe on the forehead */
   cyl(-5, 9, -4, 3, 0, 6, 4, look->fur);              /* the ears */
   cyl(5, 9, -4, 3, 0, 6, 4, look->fur);
   ball2(-6, 13, -24, 3, 2, 3, 2, 0x1a1a1a);
   ball2(6, 13, -24, 3, 2, 3, 2, 0x1a1a1a);
   r3d_glow(120);
   ball2(-6, 13, -25, 2, 2, 2, 2, look->eyes);
   ball2(6, 13, -25, 2, 2, 2, 2, look->eyes);
   r3d_glow(0);
   ball2(0, 6, -27, 2, 2, 2, 2, look->nose);
   for (i = 0; i < 2; i++)
   {
      int32_t s = i ? 1 : -1;
      r3d_box(CM(s * 2), FX_FRAC(4, 200), -CM(13), CM(s * 2) + CM(s * 7), FX_FRAC(4, 200) + 300, -CM(13) + 300, 0xeeeeee, 0);
      r3d_box(CM(s * 2), FX_FRAC(2, 200), -CM(13), CM(s * 2) + CM(s * 7), FX_FRAC(2, 200) + 300, -CM(13) + 300, 0xeeeeee, 0);
   }
   k->part[PET_HEAD] = r3d_end();
   /* the tail: two pieces up from the joint, the tip darker */
   r3d_begin();
   cyl(0, 0, 0, 2, 2, 14, 6, look->fur);
   ball(0, 14, 0, 2, 2, 2, 2, look->fur);
   k->part[PET_TAIL] = r3d_end();
   r3d_begin();
   cyl(0, 0, 0, 2, 1, 12, 6, look->stripes);
   ball2(0, 24, 0, 3, 3, 3, 2, look->stripes);
   k->part[PET_TAIL_TIP] = r3d_end();
   for (i = 0; i < 4; i++)
   {
      leg(look);
      k->part[PET_LEG_FL + i] = r3d_end();
   }
   memset(&k->shadow, 0, sizeof k->shadow);
}

static r3d_vec v3(int32_t x, int32_t y, int32_t z)
{
   r3d_vec v;
   v.x = x;
   v.y = y;
   v.z = z;
   return v;
}

static void put(r3d_instance *out, int32_t *n, const r3d_mesh *m, r3d_vec base, const int32_t yawm[9], r3d_vec j, const int32_t turn[9])
{
   r3d_instance *i = &out[(*n)++];
   r3d_vec w = r3d_rot_apply(yawm, j);
   memset(i, 0, sizeof *i);
   i->mesh = *m;
   i->at.x = base.x + w.x;
   i->at.y = base.y + w.y;
   i->at.z = base.z + w.z;
   r3d_rot_mul(yawm, turn, i->rot);
}

int32_t pet3d_place(const pet3d_kit *k, const pet3d_pose *p, r3d_instance *out)
{
   int32_t n = 0, i, yawm[9], body[9], t[9], m[9], sw = 0;
   /* a positive pitch raises a part's front (its -z) and swings a hanging part's end forward */
   int32_t body_y = CM(17), body_pitch = 0, tail_up = 800, tail_curl = -350, sway;
   int32_t legs[4];
   static const int32_t leg_x[4] = { 5, -5, 5, -5 }, leg_z[4] = { -13, -13, 13, 13 };
   r3d_vec bodyp;
   r3d_rotation(p->yaw, 0, 0, yawm);
   if (p->walk > 0)
      sw = (int32_t)(((int64_t)hd_sin(p->phase & 4095) * p->walk) >> 14);
   /* the legs in diagonal pairs: front left with back right */
   for (i = 0; i < 4; i++)
      legs[i] = ((i == 0 || i == 3) ? sw : -sw) * 300 / 16384;
   sway = (int32_t)(((int64_t)hd_sin((p->phase / 3) & 4095) * 260) >> 14);
   if (p->sit)
   {
      /* the haunches down with the back legs forward under them, the front legs straight */
      body_pitch = 400;
      body_y = CM(10);
      legs[0] = legs[1] = 0;
      legs[2] = legs[3] = 1500;
      tail_up = 1450;
      tail_curl = 250;
   }
   if (p->lie)
   {
      /* a loaf: low, the legs folded under, the tail around */
      body_y = CM(9);
      for (i = 0; i < 4; i++)
         legs[i] = i < 2 ? -1300 : 1300;
      tail_up = 1550;
      tail_curl = 300;
      sway = 900 + sway / 3; /* the tail around its side */
   }
   r3d_rotation(0, body_pitch, 0, body);
   bodyp = v3(0, body_y, 0);
   put(out, &n, &k->part[PET_BODY], p->at, yawm, bodyp, body);
   /* the head at the front of the body, up a little */
   r3d_rotation(p->look, 80 - body_pitch, 0, t);
   r3d_rot_mul(body, t, m);
   put(out, &n, &k->part[PET_HEAD], p->at, yawm, v3(0, body_y + r3d_rot_apply(body, v3(0, CM(7), -CM(15))).y, r3d_rot_apply(body, v3(0, CM(7), -CM(15))).z), m);
   /* the tail from the back of the body: up and back, swaying, the tip curled */
   {
      r3d_vec base = v3(0, body_y + r3d_rot_apply(body, v3(0, CM(4), CM(18))).y, r3d_rot_apply(body, v3(0, CM(4), CM(18))).z);
      int32_t tt[9], tip[9];
      r3d_rotation(sway, tail_up, 0, tt);
      put(out, &n, &k->part[PET_TAIL], p->at, yawm, base, tt);
      r3d_rotation(sway, tail_up + tail_curl, 0, tip);
      put(out, &n, &k->part[PET_TAIL_TIP], p->at, yawm, v3(base.x + r3d_rot_apply(tt, v3(0, CM(14), 0)).x, base.y + r3d_rot_apply(tt, v3(0, CM(14), 0)).y, base.z + r3d_rot_apply(tt, v3(0, CM(14), 0)).z), tip);
   }
   /* the legs from under the body */
   for (i = 0; i < 4; i++)
   {
      r3d_vec j = r3d_rot_apply(body, v3(CM(leg_x[i]), -CM(3), CM(leg_z[i])));
      r3d_rotation(0, legs[i], 0, t);
      put(out, &n, &k->part[PET_LEG_FL + i], p->at, yawm, v3(j.x, body_y + j.y, j.z), t);
   }
   if (k->shadow.count)
   {
      r3d_instance *s = &out[n++];
      memset(s, 0, sizeof *s);
      s->mesh = k->shadow;
      s->at = p->at;
      s->at.y = p->floor_y;
   }
   return n;
}
