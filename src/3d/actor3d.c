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

/*
 * Playing a guitar: the arms' angles (found by rendering it from the front
 * and the side until the hands met the guitar) and where the guitar sits,
 * in centimeters from the hips: a little to the strumming side, at the
 * belly, in front of it.
 */
#define STRUM_UPPER0 300
#define STRUM_ELBOW0 650
#define STRUM_SPREAD0 -700
#define STRUM_UPPER1 520
#define STRUM_ELBOW1 250
#define STRUM_SPREAD1 -1500
#define STRUM_GX 4
#define STRUM_GY 22
#define STRUM_GZ 20

/* A pose's angles (4096 a turn; a positive pitch swings a limb's end forward, toward the face), side 0 and 1. */
typedef struct
{
   int32_t thigh[2], knee[2], upper[2], elbow[2], spread[2], lean, hip_y, bob, head_turn;
} pose_angles;

static void angles(const actor3d_pose *p, pose_angles *a)
{
   int32_t side, sw = 0;
   memset(a, 0, sizeof *a);
   a->hip_y = CM(94);
   a->head_turn = p->look;
   if (p->walk > 0)
      sw = (int32_t)(((int64_t)hd_sin(p->phase & 4095) * p->walk) >> 14); /* -walk..walk */
   for (side = 0; side < 2; side++)
   {
      int32_t s = side ? -sw : sw;
      a->thigh[side] = s * 340 / 256 / 64;                  /* +-33 degrees at a full walk */
      a->knee[side] = -(hd_max(0, -s) * 420 / 256 / 64 + 30); /* the leg behind bends, the shin backward */
      a->upper[side] = -s * 260 / 256 / 64;                 /* the arms swing against the legs */
      a->elbow[side] = 120 + hd_abs(s) * 80 / 256 / 64;
      a->spread[side] = side ? -60 : 60;
   }
   if (p->walk > 0)
      a->bob = (int32_t)(((int64_t)CM(2) * hd_abs(hd_cos(p->phase & 4095))) >> 14);
   if (p->sit)
   {
      a->hip_y = CM(54);
      for (side = 0; side < 2; side++)
      {
         a->thigh[side] = 1024;   /* forward, level */
         a->knee[side] = -1024;   /* the shins down again */
      }
      a->lean = -60; /* a little forward */
   }
   if (p->crouch)
   {
      a->hip_y = CM(52);
      for (side = 0; side < 2; side++)
      {
         a->thigh[side] = 1200; /* forward and up */
         a->knee[side] = -1900; /* the shins back down */
      }
      a->lean = 260;
   }
   if (p->hold)
   {
      /* both hands in front of the chest */
      for (side = 0; side < 2; side++)
      {
         a->upper[side] = 420;
         a->elbow[side] = 900;
         a->spread[side] = side ? -160 : 160;
      }
   }
   if (p->strum)
   {
      /* side 0 strums over the guitar's body at the hip, side 1 reaches out along its neck */
      a->upper[0] = STRUM_UPPER0;
      a->elbow[0] = STRUM_ELBOW0 + (int32_t)(((int64_t)hd_sin((p->phase * 3) & 4095) * 90) >> 14);
      a->spread[0] = STRUM_SPREAD0;
      a->upper[1] = STRUM_UPPER1;
      a->elbow[1] = STRUM_ELBOW1;
      a->spread[1] = STRUM_SPREAD1;
   }
   if (p->type)
   {
      /* the arms forward to a keyboard, the hands moving a little */
      for (side = 0; side < 2; side++)
      {
         a->upper[side] = 500;
         a->elbow[side] = 700 + (int32_t)(((int64_t)hd_sin((p->phase * (side ? 3 : 2)) & 4095) * 40) >> 14);
         a->spread[side] = side ? -120 : 120;
      }
   }
   if (p->wave)
   {
      a->upper[1] = 1900;
      a->elbow[1] = 300 + (int32_t)(((int64_t)hd_sin((p->phase * 4) & 4095) * 300) >> 14);
      a->spread[1] = -200;
   }
}

int32_t actor3d_place(const actor3d_kit *k, const actor3d_pose *p, r3d_instance *out)
{
   int32_t n = 0, side, yawm[9], id[9];
   int32_t thigh[2], knee[2], upper[2], elbow[2], spread[2], lean, hip_y, bob, head_turn;
   r3d_vec hip, chest, shoulder[2], hipj[2], hand[2];
   int32_t lean_m[9];
   pose_angles a;
   r3d_rotation(p->yaw, 0, 0, yawm);
   r3d_rotation(0, 0, 0, id);
   angles(p, &a);
   memcpy(thigh, a.thigh, sizeof thigh);
   memcpy(knee, a.knee, sizeof knee);
   memcpy(upper, a.upper, sizeof upper);
   memcpy(elbow, a.elbow, sizeof elbow);
   memcpy(spread, a.spread, sizeof spread);
   lean = a.lean, hip_y = a.hip_y, bob = a.bob, head_turn = a.head_turn;
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
      memcpy(lean_m, tl, sizeof lean_m);
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
         r3d_vec grip = p->strum ? add(hip, r3d_rot_apply(lean_m, v3(CM(STRUM_GX), CM(STRUM_GY), -CM(STRUM_GZ)))) : p->hold ? v3((hand[0].x + hand[1].x) / 2, (hand[0].y + hand[1].y) / 2, (hand[0].z + hand[1].z) / 2) : hand[1];
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

/* ---- a person made by an artist (model3d.h) ---- */

/* The bones the pose turns, found by name once per model. */
enum { MB_HIPS = 0, MB_SPINE, MB_HEAD, MB_UPPER, MB_FORE = MB_UPPER + 2, MB_WRIST = MB_FORE + 2, MB_THIGH = MB_WRIST + 2, MB_SHIN = MB_THIGH + 2, MB_COUNT = MB_SHIN + 2 };
/* side 0 of a pose is the person's right: the model's ".R" */
static const char *const bone_names[MB_COUNT] = { "Hips", "Abdomen", "Head", "UpperArm.R", "UpperArm.L", "LowerArm.R", "LowerArm.L",
                                                  "Wrist.R", "Wrist.L", "UpperLeg.R", "UpperLeg.L", "LowerLeg.R", "LowerLeg.L" };

/*
 * An actor's turn (its face toward -z) as the model's (its front toward
 * +z): the same turn seen half a turn around y.
 */
static void to_model(const int32_t a[9], int32_t m[9])
{
   static const int32_t s[3] = { -1, 1, -1 };
   int32_t i, j;
   for (i = 0; i < 3; i++)
      for (j = 0; j < 3; j++)
         m[i * 3 + j] = s[i] * s[j] * a[i * 3 + j];
}

/*
 * The turn that brings direction d (any length) straight down, the
 * shortest way (Rodrigues' formula in 2.14): a limb at rest (an artist's
 * arms are open in an A) made to hang, as the pose's angles expect.
 */
static void hang(r3d_vec d, int32_t r[9])
{
   uint64_t len = (uint64_t)((int64_t)d.x * d.x + (int64_t)d.y * d.y + (int64_t)d.z * d.z), root = 0, bit = (uint64_t)1 << 62;
   int64_t vx, vz, c, k;
   int32_t i;
   for (i = 0; i < 9; i++)
      r[i] = (i % 4 == 0) ? 16384 : 0;
   while (bit > len)
      bit >>= 2;
   while (bit)
   {
      if (len >= root + bit)
         len -= root + bit, root = (root >> 1) + bit;
      else
         root >>= 1;
      bit >>= 2;
   }
   if (!root)
      return;
   /* d made unit (2.14); v = d x down = (d.z, 0, -d.x), c = d . down = -d.y */
   vx = (int64_t)d.z * 16384 / (int64_t)root;
   vz = -(int64_t)d.x * 16384 / (int64_t)root;
   c = -(int64_t)d.y * 16384 / (int64_t)root;
   if (c <= -16000)
      return; /* pointing up: left as it is */
   k = ((int64_t)16384 << 14) / (16384 + c); /* 1 / (1 + c) */
   /* R = I + [v]x + (v v' - |v|^2 I) / (1 + c) */
   r[0] = 16384 - (int32_t)(((vz * vz >> 14) * k) >> 14);
   r[1] = (int32_t)-vz;
   r[2] = (int32_t)(((vx * vz >> 14) * k) >> 14);
   r[3] = (int32_t)vz;
   r[4] = 16384 - (int32_t)((((vx * vx + vz * vz) >> 14) * k) >> 14);
   r[5] = (int32_t)-vx;
   r[6] = (int32_t)(((vx * vz >> 14) * k) >> 14);
   r[7] = (int32_t)vx;
   r[8] = 16384 - (int32_t)(((vx * vx >> 14) * k) >> 14);
}

/* A limb's global turn: hanging straight down from its rest, then turned by `turn` (in the model). */
static void set_turn(model3d_xf *g, const int32_t turn[9], const model3d_xf *rest, const int32_t down[9])
{
   model3d_xf t, r, h;
   memcpy(t.r, turn, sizeof t.r);
   t.t.x = t.t.y = t.t.z = 0;
   memcpy(h.r, down, sizeof h.r);
   h.t = t.t;
   r = *rest;
   r.t = t.t;
   model3d_mul(&h, &r, &r);
   model3d_mul(&t, &r, &r);
   memcpy(g->r, r.r, sizeof g->r);
}

int32_t actor3d_place_model(model3d_body *body, const actor3d_pose *p, const r3d_mesh *shadow, r3d_instance *out)
{
   const model3d *m = body->model;
   model3d_xf local[MODEL3D_BONES], global[MODEL3D_BONES];
   int32_t bone[MB_COUNT], turn_of[MODEL3D_BONES], turns[MB_COUNT][9], down[MB_COUNT][9];
   int32_t n = 0, i, side, clip, time, still, place[9], yawm[9], over_hips = 0;
   pose_angles a;
   r3d_vec at = p->at;
   if (!m || !model3d_bones(m))
      return 0;
   angles(p, &a);
   for (i = 0; i < MB_COUNT; i++)
      bone[i] = model3d_bone(m, bone_names[i]);
   /* each limb made to hang first: from its joint toward the next one at rest (the foot: the shin's first child) */
   for (i = 0; i < MB_COUNT; i++)
   {
      int32_t next = -1, k;
      r3d_vec d;
      hang(v3(0, -1, 0), down[i]);
      if (bone[i] < 0)
         continue;
      if (i >= MB_UPPER && i < MB_UPPER + 2)
         next = bone[MB_FORE + i - MB_UPPER];
      else if (i >= MB_FORE && i < MB_FORE + 2)
         next = bone[MB_WRIST + i - MB_FORE];
      else if (i >= MB_THIGH && i < MB_THIGH + 2)
         next = bone[MB_SHIN + i - MB_THIGH];
      else if (i >= MB_SHIN && i < MB_SHIN + 2)
         for (k = 0; k < model3d_bones(m) && next < 0; k++)
            if (model3d_parent(m, k) == bone[i])
               next = k;
      if (next < 0)
         continue;
      d.x = model3d_rest(m, next)->t.x - model3d_rest(m, bone[i])->t.x;
      d.y = model3d_rest(m, next)->t.y - model3d_rest(m, bone[i])->t.y;
      d.z = model3d_rest(m, next)->t.z - model3d_rest(m, bone[i])->t.z;
      hang(d, down[i]);
   }
   /* the clip: walking or running with the legs, waving, or standing and breathing */
   still = p->walk <= 0 || p->sit || p->crouch || p->lie;
   if (!still)
   {
      clip = model3d_clip(m, p->walk > 18000 ? "Run" : "Walk");
      time = (int32_t)((int64_t)model3d_clip_frames(m, clip) * 65536 * (p->phase & 4095) / 4096);
   }
   else if (p->wave && model3d_clip(m, "Wave") >= 0)
   {
      clip = model3d_clip(m, "Wave");
      time = p->clock << 16;
   }
   else
   {
      clip = model3d_clip(m, "Idle");
      time = p->clock << 16;
   }
   model3d_sample(m, clip, time, local);
   /*
    * Where no clip fits, the pose's angles turn the bones: each given a
    * global turn (the angle in the body, after its rest turn), what hangs
    * from it following.
    */
   for (i = 0; i < MODEL3D_BONES; i++)
      turn_of[i] = -1;
   if (still && (p->sit || p->crouch || p->hold || p->type || p->strum || p->lie || (p->wave && clip != model3d_clip(m, "Wave"))))
   {
      int32_t lean_a[9], lean_m[9];
      r3d_rotation(0, a.lean, 0, lean_a);
      to_model(lean_a, lean_m);
      if (bone[MB_SPINE] >= 0)
         turn_of[bone[MB_SPINE]] = MB_SPINE, memcpy(turns[MB_SPINE], lean_m, sizeof lean_m);
      for (side = 0; side < 2; side++)
      {
         int32_t r[9], u[9];
         r3d_rotation(0, a.thigh[side], 0, r);
         to_model(r, turns[MB_THIGH + side]);
         r3d_rotation(0, a.thigh[side] + a.knee[side], 0, r);
         to_model(r, turns[MB_SHIN + side]);
         r3d_rotation(0, a.upper[side], a.spread[side] / 3, r);
         r3d_rot_mul(lean_a, r, u);
         to_model(u, turns[MB_UPPER + side]);
         r3d_rotation(0, a.upper[side] + a.elbow[side], a.spread[side] / 3, r);
         r3d_rot_mul(lean_a, r, u);
         to_model(u, turns[MB_FORE + side]);
         if (bone[MB_THIGH + side] >= 0)
            turn_of[bone[MB_THIGH + side]] = MB_THIGH + side;
         if (bone[MB_SHIN + side] >= 0)
            turn_of[bone[MB_SHIN + side]] = MB_SHIN + side;
         if (bone[MB_UPPER + side] >= 0)
            turn_of[bone[MB_UPPER + side]] = MB_UPPER + side;
         if (bone[MB_FORE + side] >= 0)
            turn_of[bone[MB_FORE + side]] = MB_FORE + side;
      }
      over_hips = 1;
   }
   if (bone[MB_HEAD] >= 0 && (p->look || p->nod || turn_of[bone[MB_SPINE] >= 0 ? bone[MB_SPINE] : 0] >= 0))
   {
      int32_t r[9];
      r3d_rotation(a.head_turn, a.lean + p->nod, 0, r);
      to_model(r, turns[MB_HEAD]);
      turn_of[bone[MB_HEAD]] = MB_HEAD;
   }
   for (i = 0; i < model3d_bones(m); i++)
   {
      int32_t par = model3d_parent(m, i);
      if (par < 0)
         global[i] = local[i];
      else
         model3d_mul(&global[par], &local[i], &global[i]);
      if (i == bone[MB_HIPS] && over_hips)
      {
         /* the hips lowered as the pose's (sitting, crouching), the model's own height kept */
         const model3d_xf *rest = model3d_rest(m, i);
         global[i].t.y = rest->t.y + (int32_t)((int64_t)(a.hip_y - CM(94)) * rest->t.y / CM(94));
         global[i].t.x = rest->t.x, global[i].t.z = rest->t.z;
      }
      if (turn_of[i] >= 0)
         set_turn(&global[i], turns[turn_of[i]], model3d_rest(m, i), down[turn_of[i]]);
   }
   /* the model turned to the person's facing (its front +z is the actor's back), and lying down when it lies */
   r3d_rotation(p->yaw + 2048, 0, 0, place);
   r3d_rotation(p->yaw, 0, 0, yawm);
   {
      int32_t lie[9], back[9];
      r3d_vec hip_w = at;
      if (p->lie && bone[MB_HIPS] >= 0)
      {
         r3d_vec h = r3d_rot_apply(place, global[bone[MB_HIPS]].t), d;
         hip_w.x += h.x, hip_w.y += h.y, hip_w.z += h.z;
         r3d_rotation(p->yaw, 1024, 0, lie);
         r3d_rotation(-p->yaw, 0, 0, back);
         r3d_rot_mul(lie, back, lie);
         d = r3d_rot_apply(lie, v3(at.x - hip_w.x, at.y - hip_w.y, at.z - hip_w.z));
         at = v3(hip_w.x + d.x, p->at.y + CM(12) + d.y, hip_w.z + d.z);
         r3d_rot_mul(lie, place, place);
         r3d_rot_mul(lie, yawm, yawm);
      }
   }
   model3d_body_pose(body, global, place, at);
   memset(&out[n], 0, sizeof out[n]);
   out[n++].mesh = body->mesh;
   /* what the hands hold: between them, at the belly for a guitar, or in the right hand */
   if (p->prop && p->prop->count && bone[MB_WRIST] >= 0 && bone[MB_WRIST + 1] >= 0)
   {
      r3d_vec hand[2], grip;
      int32_t pt[9], lean_a[9];
      r3d_instance *ins = &out[n++];
      for (side = 0; side < 2; side++)
      {
         r3d_vec w = global[bone[MB_WRIST + side]].t, e = global[bone[MB_FORE + side]].t;
         /* the hand a little past the wrist, along the forearm */
         hand[side] = v3(w.x + (w.x - e.x) / 3, w.y + (w.y - e.y) / 3, w.z + (w.z - e.z) / 3);
      }
      if (p->strum && bone[MB_HIPS] >= 0)
      {
         int32_t lm[9];
         r3d_vec h = global[bone[MB_HIPS]].t;
         r3d_rotation(0, a.lean, 0, lean_a);
         to_model(lean_a, lm);
         grip = model3d_apply(lm, v3(-CM(STRUM_GX), CM(STRUM_GY) - CM(10), CM(STRUM_GZ)));
         grip = v3(h.x + grip.x, h.y + grip.y, h.z + grip.z);
      }
      else if (p->hold)
         grip = v3((hand[0].x + hand[1].x) / 2, (hand[0].y + hand[1].y) / 2, (hand[0].z + hand[1].z) / 2);
      else
         grip = hand[0];
      grip = r3d_rot_apply(place, grip);
      memset(ins, 0, sizeof *ins);
      ins->mesh = *p->prop;
      ins->at = v3(at.x + grip.x, at.y + grip.y, at.z + grip.z);
      r3d_rotation(0, a.lean + p->prop_pitch, p->prop_roll, pt);
      r3d_rot_mul(yawm, pt, ins->rot);
   }
   if (shadow && shadow->count && !p->lie)
   {
      r3d_instance *i2 = &out[n++];
      memset(i2, 0, sizeof *i2);
      i2->mesh = *shadow;
      i2->at = p->at;
      i2->at.y = p->floor_y;
   }
   return n;
}
