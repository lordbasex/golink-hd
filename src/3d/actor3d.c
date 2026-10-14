/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * People for 3D worlds (actor3d.h): a body of 12 parts (hips, torso, head,
 * upper arms, forearms with hands, thighs, shins with shoes), each a mesh
 * built once with its joint at the origin, posed every frame by turning the
 * parts around their joints: standing, walking (a cycle with the arms
 * swinging against the legs and a small bob), sitting and typing. Everything
 * is integers, so a pose is the same on every computer.
 */
#include <string.h>
#include "actor3d.h"

#define CM(n) FX_FRAC(n, 100)

static void bx(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1, uint32_t c)
{
   r3d_box(CM(x0), CM(y0), CM(z0), CM(x1), CM(y1), CM(z1), c, 0);
}

static void cyl(int32_t x, int32_t y, int32_t z, int32_t r, int32_t rt, int32_t h, int32_t sides, uint32_t c)
{
   r3d_cylinder(CM(x), CM(y), CM(z), CM(r), CM(rt), CM(h), sides, c);
}

static void ball(int32_t x, int32_t y, int32_t z, int32_t rx, int32_t ry, int32_t rz, int32_t rings, uint32_t c)
{
   r3d_ball(CM(x), CM(y), CM(z), CM(rx), CM(ry), CM(rz), rings, c);
}

/* A limb hanging from its joint at the origin, down `len` centimeters: rounded at both ends. */
static void limb(int32_t r0, int32_t r1, int32_t len, uint32_t c)
{
   cyl(0, -len, 0, r1, r0, len, 8, c);
   ball(0, 0, 0, r0, r0, r0, 3, c);
}

void actor3d_build(actor3d_kit *k, const actor3d_look *look, int32_t fabric_tex)
{
   int32_t side;
   r3d_place(0, 0, 0, 0);
   r3d_detail(0);
   r3d_bevel(CM(2)); /* soft edges on the body's boxes */
   /* hips: the joint is the middle of the pelvis */
   r3d_begin();
   r3d_material(fabric_tex, 128);
   bx(-15, -8, -9, 15, 6, 9, look->bottom);
   r3d_material(-1, 0);
   k->part[ACT_HIPS] = r3d_end();
   /* torso: from the waist up to the shoulders, a jumper with a hood on the back */
   r3d_begin();
   r3d_material(fabric_tex, 128);
   bx(-17, 0, -10, 17, 46, 10, look->top);
   bx(-15, 38, 9, 15, 50, 15, look->top);       /* the hood */
   r3d_material(-1, 0);
   bx(-2, 20, -11, 2, 40, -10, look->top_detail); /* a zip */
   bx(-12, 22, -11, -6, 26, -10, look->top_detail);
   cyl(0, 46, 0, 6, 5, 8, 8, look->skin);      /* the neck */
   k->part[ACT_TORSO] = r3d_end();
   /* head: the joint is the neck's top; a face toward -z */
   r3d_begin();
   ball(0, 12, 0, 11, 13, 11, 5, look->skin);
   ball(0, 18, 2, 12, 9, 12, 4, look->hair);     /* hair */
   bx(-11, 8, -2, -9, 20, 8, look->hair);
   bx(9, 8, -2, 11, 20, 8, look->hair);
   ball(-4, 13, -10, 2, 2, 1, 2, 0x1d1d1d);      /* eyes */
   ball(4, 13, -10, 2, 2, 1, 2, 0x1d1d1d);
   bx(-8, 12, -12, 8, 13, -11, look->glasses);   /* glasses */
   bx(-8, 12, -12, -2, 17, -11, look->glasses);
   bx(2, 12, -12, 8, 17, -11, look->glasses);
   ball(0, 10, -11, 2, 2, 2, 2, look->skin);     /* nose */
   bx(-3, 5, -11, 3, 6, -10, 0x9c4a3a);          /* mouth */
   ball(-11, 12, 0, 2, 3, 2, 2, look->skin);     /* ears */
   ball(11, 12, 0, 2, 3, 2, 2, look->skin);
   k->part[ACT_HEAD] = r3d_end();
   for (side = 0; side < 2; side++)
   {
      /* upper arm and forearm with the hand, hanging from the shoulder and the elbow */
      r3d_begin();
      r3d_material(fabric_tex, 128);
      limb(6, 5, 28, look->top);
      r3d_material(-1, 0);
      k->part[ACT_UPPER_L + side] = r3d_end();
      r3d_begin();
      r3d_material(fabric_tex, 128);
      limb(5, 4, 24, look->top);
      r3d_material(-1, 0);
      ball(0, -28, 0, 5, 6, 4, 3, look->skin);
      k->part[ACT_FORE_L + side] = r3d_end();
      /* thigh and shin with the shoe, hanging from the hip and the knee */
      r3d_begin();
      r3d_material(fabric_tex, 128);
      limb(8, 7, 42, look->bottom);
      r3d_material(-1, 0);
      k->part[ACT_THIGH_L + side] = r3d_end();
      r3d_begin();
      r3d_material(fabric_tex, 128);
      limb(7, 6, 40, look->bottom);
      r3d_material(-1, 0);
      bx(-6, -48, -14, 6, -40, 6, look->shoes);
      bx(-6, -48, -14, 6, -47, 6, 0xf2f2f2);
      k->part[ACT_SHIN_L + side] = r3d_end();
   }
   r3d_bevel(0);
}

/* A part at joint j (actor space), turned by `turn` after the actor's own: into the instances. */
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

static r3d_vec v3(int32_t x, int32_t y, int32_t z)
{
   r3d_vec v;
   v.x = x;
   v.y = y;
   v.z = z;
   return v;
}

static r3d_vec add(r3d_vec a, r3d_vec b)
{
   return v3(a.x + b.x, a.y + b.y, a.z + b.z);
}

int32_t actor3d_place(const actor3d_kit *k, const actor3d_pose *p, r3d_instance *out)
{
   int32_t n = 0, side, yawm[9], id[9], sw = 0, bob = 0;
   int32_t thigh[2], knee[2], upper[2], elbow[2], spread[2], lean = 0, hip_y = CM(94), head_turn = p->look;
   r3d_vec hip, chest, shoulder[2], hipj[2], hand[2];
   r3d_rotation(p->yaw, 0, 0, yawm);
   r3d_rotation(0, 0, 0, id);
   /* the pose's angles, in the engine's units (4096 a turn); a positive pitch swings a limb's end forward (toward the face) */
   if (p->walk > 0)
      sw = (int32_t)(((int64_t)hd_sin(p->phase & 4095) * p->walk) >> 14); /* -walk..walk */
   for (side = 0; side < 2; side++)
   {
      int32_t s = side ? -sw : sw;
      thigh[side] = s * 340 / 256 / 64;                  /* +-33 degrees at a full walk */
      knee[side] = -(hd_max(0, -s) * 420 / 256 / 64 + 30); /* the leg behind bends, the shin backward */
      upper[side] = -s * 260 / 256 / 64;                 /* the arms swing against the legs */
      elbow[side] = 120 + hd_abs(s) * 80 / 256 / 64;
      spread[side] = side ? -60 : 60;
   }
   if (p->walk > 0)
      bob = (int32_t)(((int64_t)CM(2) * hd_abs(hd_cos(p->phase & 4095))) >> 14);
   if (p->sit)
   {
      hip_y = CM(54);
      for (side = 0; side < 2; side++)
      {
         thigh[side] = 1024;   /* forward, level */
         knee[side] = -1024;   /* the shins down again */
      }
      lean = -60; /* a little forward */
   }
   if (p->crouch)
   {
      hip_y = CM(52);
      for (side = 0; side < 2; side++)
      {
         thigh[side] = 1200; /* forward and up */
         knee[side] = -1900; /* the shins back down */
      }
      lean = 260;
   }
   if (p->hold)
   {
      /* both hands in front of the chest */
      for (side = 0; side < 2; side++)
      {
         upper[side] = 420;
         elbow[side] = 900;
         spread[side] = side ? -160 : 160;
      }
   }
   if (p->type)
   {
      /* the arms forward to a keyboard, the hands moving a little */
      for (side = 0; side < 2; side++)
      {
         upper[side] = 500;
         elbow[side] = 700 + (int32_t)(((int64_t)hd_sin((p->phase * (side ? 3 : 2)) & 4095) * 40) >> 14);
         spread[side] = side ? -120 : 120;
      }
   }
   if (p->wave)
   {
      upper[1] = 1900;
      elbow[1] = 300 + (int32_t)(((int64_t)hd_sin((p->phase * 4) & 4095) * 300) >> 14);
      spread[1] = -200;
   }
   hip = v3(0, hip_y + bob, 0);
   /* hips and legs */
   put(out, &n, &k->part[ACT_HIPS], p->at, yawm, hip, id);
   for (side = 0; side < 2; side++)
   {
      int32_t t[9], kturn[9];
      r3d_vec kneep;
      hipj[side] = add(hip, v3(side ? -CM(8) : CM(8), -CM(4), 0));
      r3d_rotation(0, thigh[side], 0, t);
      put(out, &n, &k->part[ACT_THIGH_L + side], p->at, yawm, hipj[side], t);
      kneep = add(hipj[side], r3d_rot_apply(t, v3(0, -CM(42), 0)));
      r3d_rotation(0, thigh[side] + knee[side], 0, kturn);
      put(out, &n, &k->part[ACT_SHIN_L + side], p->at, yawm, kneep, kturn);
   }
   /* torso, head and arms, leaning */
   {
      int32_t tl[9], ht[9];
      r3d_rotation(0, lean, 0, tl);
      chest = add(hip, v3(0, CM(6), 0));
      put(out, &n, &k->part[ACT_TORSO], p->at, yawm, chest, tl);
      r3d_rotation(head_turn, lean + p->nod, 0, ht);
      put(out, &n, &k->part[ACT_HEAD], p->at, yawm, add(chest, r3d_rot_apply(tl, v3(0, CM(54), 0))), ht);
      for (side = 0; side < 2; side++)
      {
         int32_t u[9], f[9], ut[9], ft[9];
         r3d_vec elb;
         shoulder[side] = add(chest, r3d_rot_apply(tl, v3(side ? -CM(22) : CM(22), CM(42), 0)));
         r3d_rotation(0, upper[side], spread[side] / 3, u);
         r3d_rot_mul(tl, u, ut);
         put(out, &n, &k->part[ACT_UPPER_L + side], p->at, yawm, shoulder[side], ut);
         elb = add(shoulder[side], r3d_rot_apply(ut, v3(0, -CM(28), 0)));
         r3d_rotation(0, upper[side] + elbow[side], spread[side] / 3, f);
         r3d_rot_mul(tl, f, ft);
         put(out, &n, &k->part[ACT_FORE_L + side], p->at, yawm, elb, ft);
         hand[side] = add(elb, r3d_rot_apply(ft, v3(0, -CM(28), 0)));
      }
      /* what the hands hold: between them, or in the right one; level with the body, tilted by its pitch */
      if (p->prop && p->prop->count)
      {
         int32_t pt[9];
         r3d_vec grip = p->hold ? v3((hand[0].x + hand[1].x) / 2, (hand[0].y + hand[1].y) / 2, (hand[0].z + hand[1].z) / 2) : hand[1];
         r3d_rotation(0, lean + p->prop_pitch, p->prop_roll, pt);
         put(out, &n, p->prop, p->at, yawm, grip, pt);
      }
   }
   if (p->lie)
   {
      /*
       * Lying on the back: the whole body turned a quarter around the hips
       * (the face up, the head toward the back), then lowered so the back
       * rests on at.y.
       */
      int32_t t[9], back[9], i, j;
      r3d_vec hipw = add(p->at, r3d_rot_apply(yawm, hip));
      r3d_rotation(p->yaw, 1024, 0, t);
      r3d_rotation(-p->yaw, 0, 0, back);
      r3d_rot_mul(t, back, t);
      for (i = 0; i < n; i++)
      {
         r3d_vec d = v3(out[i].at.x - hipw.x, out[i].at.y - hipw.y, out[i].at.z - hipw.z);
         int32_t m[9];
         d = r3d_rot_apply(t, d);
         out[i].at = v3(hipw.x + d.x, p->at.y + CM(12) + d.y, hipw.z + d.z);
         r3d_rot_mul(t, out[i].rot, m);
         for (j = 0; j < 9; j++)
            out[i].rot[j] = m[j];
      }
      return n; /* no shadow on the floor: it lies on something */
   }
   /* the soft shadow on the floor under the person */
   if (k->shadow.count)
   {
      r3d_instance *i = &out[n++];
      memset(i, 0, sizeof *i);
      i->mesh = k->shadow;
      i->at = p->at;
      i->at.y = p->floor_y;
   }
   return n;
}
