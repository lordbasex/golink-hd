/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The 3D proof of concept (golinkhd_load_demo(e, 2)): a lived-in room built
 * in code from boxes, cylinders and balls, four of them around a hall so the
 * renderer has a real load, and a camera the first player walks with the
 * stick or the pad (left and right turn, up and down walk, L and R look).
 * Its state is the showcase's (hd_show, `on` 2): kx and ky are where the eye
 * stands, ka where it looks, kv how far up or down.
 */
#include <stdio.h>
#include <string.h>
#include "hd.h"
#include "gfx.h"
#include "text.h"
#include "r3d.h"

#define M(n) FX(n)
#define CM(n) FX_FRAC(n, 100)
/* the rooms sit away from 0 so the eye's position stays positive (hd_show clamps it to 0..511) */
#define BASE CM(1000)

static r3d_mesh scene;
static int32_t ready;

/* ---- the furniture, each at the place set by r3d_place ---- */

static void desk(void)
{
   int32_t i;
   r3d_box(0, CM(72), 0, CM(140), CM(76), CM(70), 0xb98a5a, 0);                 /* top */
   r3d_box(CM(2), 0, CM(2), CM(6), CM(72), CM(6), 0x3a3a40, 0);                 /* legs */
   r3d_box(CM(134), 0, CM(2), CM(138), CM(72), CM(6), 0x3a3a40, 0);
   r3d_box(CM(2), 0, CM(64), CM(6), CM(72), CM(68), 0x3a3a40, 0);
   r3d_box(CM(134), 0, CM(64), CM(138), CM(72), CM(68), 0x3a3a40, 0);
   r3d_box(CM(96), CM(40), CM(4), CM(136), CM(72), CM(66), 0xa77a4c, 0);        /* drawers */
   for (i = 0; i < 3; i++)
      r3d_box(CM(112), CM(46 + i * 9), CM(1), CM(120), CM(48 + i * 9), CM(4), 0xd8d0c0, 0);
   /* two screens on stands, glowing with code */
   for (i = 0; i < 2; i++)
   {
      int32_t x = CM(28 + i * 50);
      r3d_box(x + CM(16), CM(76), CM(50), x + CM(24), CM(78), CM(58), 0x222228, 0);
      r3d_box(x + CM(19), CM(78), CM(54), x + CM(21), CM(92), CM(56), 0x222228, 0);
      r3d_box(x, CM(88), CM(52), x + CM(40), CM(114), CM(55), 0x1a1a1e, 0);
      r3d_glow(1);
      r3d_box(x + CM(1), CM(89), CM(51), x + CM(39), CM(113), CM(52), i ? 0x2b4f7a : 0x1d3a2a, R3D_PANE);
      r3d_box(x + CM(3), CM(108), CM(50), x + CM(20), CM(110), CM(51), 0x7fd18b, R3D_PANE);
      r3d_box(x + CM(5), CM(104), CM(50), x + CM(30), CM(106), CM(51), 0xe0c070, R3D_PANE);
      r3d_box(x + CM(5), CM(100), CM(50), x + CM(24), CM(102), CM(51), 0x9ab8ff, R3D_PANE);
      r3d_glow(0);
   }
   r3d_box(CM(30), CM(76), CM(20), CM(76), CM(78), CM(34), 0x2c2c30, 0);           /* keyboard */
   r3d_box(CM(84), CM(76), CM(24), CM(90), CM(78), CM(32), 0x2c2c30, 0);           /* mouse */
   r3d_cylinder(CM(12), CM(76), CM(20), CM(4), CM(4), CM(10), 8, 0xe8e2d0);     /* mug */
   r3d_cylinder(CM(12), CM(85), CM(20), CM(3), CM(3), CM(1), 8, 0x4a2c1a);      /* coffee */
   r3d_box(CM(4), CM(76), CM(36), CM(22), CM(80), CM(60), 0xc0392b, 0);          /* books on the desk */
   r3d_box(CM(5), CM(80), CM(37), CM(21), CM(83), CM(59), 0x2e86c1, 0);
   r3d_box(CM(100), CM(76), CM(8), CM(114), CM(77), CM(26), 0xf4e8a0, 0);        /* sticky notes */
   r3d_cylinder(CM(126), CM(76), CM(56), CM(6), CM(5), CM(10), 6, 0x7a5236);    /* the plant's pot */
   r3d_ball(CM(126), CM(92), CM(56), CM(9), CM(8), CM(9), 3, 0x4f9a4a);
   r3d_cylinder(CM(124), CM(76), CM(14), CM(5), CM(1), CM(30), 6, 0x303034);    /* lamp */
   r3d_glow(1);
   r3d_ball(CM(124), CM(106), CM(14), CM(6), CM(4), CM(6), 3, 0xfff1c0);
   r3d_glow(0);
}

static void chair(void)
{
   int32_t i;
   for (i = 0; i < 5; i++)
   {
      int32_t a = i * ANGLE_FULL / 5;
      int32_t x = (int32_t)(((int64_t)CM(26) * hd_cos(a)) >> 14), z = (int32_t)(((int64_t)CM(26) * hd_sin(a)) >> 14);
      r3d_box(x / 2 - CM(2), CM(4), z / 2 - CM(2), x + CM(2), CM(7), z + CM(2), 0x26262a, 0);
      r3d_ball(x, CM(3), z, CM(3), CM(3), CM(3), 2, 0x111114);
   }
   r3d_cylinder(0, CM(6), 0, CM(3), CM(3), CM(38), 6, 0x8a8a90);
   r3d_box(-CM(24), CM(44), -CM(24), CM(24), CM(52), CM(24), 0x2b2f55, 0);       /* seat */
   r3d_box(-CM(22), CM(54), CM(18), CM(22), CM(110), CM(26), 0x2b2f55, 0);       /* back */
   r3d_box(-CM(28), CM(52), -CM(12), -CM(24), CM(68), CM(12), 0x26262a, 0);       /* arms */
   r3d_box(CM(24), CM(52), -CM(12), CM(28), CM(68), CM(12), 0x26262a, 0);
}

static void shelf(int32_t seed)
{
   int32_t row, x;
   uint32_t rng = 0x9e3779b9u ^ (uint32_t)seed;
   static const uint32_t book[] = { 0xc0392b, 0x2e86c1, 0x27ae60, 0xf39c12, 0x8e44ad, 0x34495e, 0xd35400, 0xecf0f1, 0x16a085 };
   r3d_box(0, 0, 0, CM(4), CM(190), CM(32), 0xe6dccb, 0);
   r3d_box(CM(96), 0, 0, CM(100), CM(190), CM(32), 0xe6dccb, 0);
   r3d_box(0, 0, CM(30), CM(100), CM(190), CM(32), 0xd9cfbd, 0);
   for (row = 0; row < 5; row++)
   {
      int32_t y = CM(2 + row * 38);
      r3d_box(CM(4), y, 0, CM(96), y + CM(2), CM(30), 0xe6dccb, 0);
      if (row == 4)
         break;
      for (x = CM(6); x < CM(90);)
      {
         int32_t w = CM(2 + (int32_t)(rng_next(&rng) % 4)), h = CM(20 + (int32_t)(rng_next(&rng) % 12));
         uint32_t c = book[rng_next(&rng) % 9];
         if (rng_next(&rng) % 9 == 0)
         {
            /* a small thing among the books: a box, a toy, a cup */
            r3d_ball(x + CM(5), y + CM(7), CM(14), CM(5), CM(5), CM(5), 3, c);
            x += CM(12);
            continue;
         }
         r3d_box(x, y + CM(2), CM(4), x + w, y + CM(2) + h, CM(26), c, 0);
         x += w + CM(1);
      }
   }
}

static void sofa(void)
{
   int32_t i;
   r3d_box(0, CM(10), 0, CM(200), CM(42), CM(90), 0x5d6d7e, 0);
   r3d_box(0, CM(42), CM(70), CM(200), CM(86), CM(90), 0x566573, 0);
   r3d_box(0, CM(42), 0, CM(18), CM(62), CM(70), 0x566573, 0);
   r3d_box(CM(182), CM(42), 0, CM(200), CM(62), CM(70), 0x566573, 0);
   for (i = 0; i < 3; i++)
      r3d_box(CM(22 + i * 53), CM(42), CM(6), CM(72 + i * 53), CM(50), CM(68), 0x6c7a89, 0);
   r3d_box(CM(30), CM(50), CM(56), CM(64), CM(80), CM(68), 0xe67e22, 0); /* cushions */
   r3d_box(CM(140), CM(50), CM(58), CM(172), CM(78), CM(68), 0xf1c40f, 0);
   r3d_box(CM(80), CM(50), CM(10), CM(130), CM(53), CM(50), 0xbdc3c7, 0);  /* a blanket left on it */
   for (i = 0; i < 4; i++)
      r3d_cylinder(CM(8 + (i & 1) * 184), 0, CM(8 + (i >> 1) * 74), CM(3), CM(3), CM(10), 6, 0x2c2c2c);
}

static void coffee_table(void)
{
   r3d_box(0, CM(38), 0, CM(100), CM(42), CM(55), 0x8b5a2b, 0);
   r3d_box(CM(4), 0, CM(4), CM(8), CM(38), CM(8), 0x6e4522, 0);
   r3d_box(CM(92), 0, CM(4), CM(96), CM(38), CM(8), 0x6e4522, 0);
   r3d_box(CM(4), 0, CM(47), CM(8), CM(38), CM(51), 0x6e4522, 0);
   r3d_box(CM(92), 0, CM(47), CM(96), CM(38), CM(51), 0x6e4522, 0);
   r3d_box(CM(10), CM(42), CM(10), CM(40), CM(44), CM(30), 0x1f1f24, 0);        /* a laptop, closed */
   r3d_cylinder(CM(60), CM(42), CM(30), CM(4), CM(4), CM(9), 8, 0x2980b9);      /* a can */
   r3d_cylinder(CM(72), CM(42), CM(22), CM(10), CM(10), CM(2), 10, 0xecf0f1);   /* a plate with crumbs */
   r3d_box(CM(50), CM(42), CM(38), CM(90), CM(43), CM(50), 0xe74c3c, 0);        /* a pizza box */
   r3d_box(CM(14), CM(44), CM(36), CM(30), CM(46), CM(46), 0x111111, 0);        /* the remote */
}

static void plant(int32_t tall)
{
   int32_t i;
   r3d_cylinder(0, 0, 0, CM(18), CM(22), CM(40), 10, 0xb5651d);
   r3d_cylinder(0, CM(40), 0, CM(20), CM(20), CM(2), 10, 0x3b2a1a);
   r3d_cylinder(0, CM(40), 0, CM(2), CM(2), tall, 5, 0x6b4f2a);
   for (i = 0; i < 7; i++)
   {
      int32_t a = i * 585, r = CM(14 + (i % 3) * 5);
      r3d_ball((int32_t)(((int64_t)r * hd_cos(a)) >> 14), tall - CM(10) + CM((i % 4) * 9), (int32_t)(((int64_t)r * hd_sin(a)) >> 14),
               CM(16), CM(10), CM(16), 3, i & 1 ? 0x2e7d32 : 0x43a047);
   }
}

static void rug(void)
{
   int32_t i;
   r3d_box(0, 0, 0, CM(240), CM(1), CM(170), 0x9b3b3b, R3D_SKIP_BOTTOM | R3D_SKIP_SIDES);
   for (i = 0; i < 6; i++)
      r3d_box(CM(10 + i * 38), CM(1), CM(10), CM(30 + i * 38), CM(2), CM(160), i & 1 ? 0xc0705a : 0x7a2e2e, R3D_SKIP_BOTTOM | R3D_SKIP_SIDES);
}

static void frame_on_wall(uint32_t c)
{
   r3d_box(0, 0, 0, CM(60), CM(44), CM(3), 0x2c2c2c, 0);
   r3d_box(CM(4), CM(4), -CM(1), CM(56), CM(40), 0, c, R3D_PANE);
   r3d_ball(CM(30), CM(22), -CM(1), CM(10), CM(10), CM(1), 2, 0xf5f5f5);
}

/* A room 6 x 5 m with its door toward -z, everything inside it. */
static void room(int32_t ox, int32_t oz, int32_t yaw, int32_t seed)
{
   int32_t c = hd_cos(yaw), s = hd_sin(yaw);
   int32_t i;
#define AT_Y(x, y, z, a) r3d_place(ox + (int32_t)(((int64_t)(x) * c + (int64_t)(z) * s) >> 14), (y), \
                                   oz + (int32_t)(((int64_t)(z) * c - (int64_t)(x) * s) >> 14), (yaw + (a)) & (ANGLE_FULL - 1))
#define AT(x, y, z, a) AT_Y(x, y, z, a)
   AT(0, 0, 0, 0);
   r3d_box(0, -CM(2), 0, M(6), 0, M(5), 0xc8a27a, R3D_SKIP_BOTTOM | R3D_SKIP_SIDES); /* floor boards */
   for (i = 0; i < 12; i++)
      r3d_box(CM(i * 50), 0, 0, CM(i * 50 + 1), 200, M(5), 0xa98460, R3D_SKIP_BOTTOM | R3D_SKIP_SIDES);
   /* the walls, seen from inside; a window on the back one, the door's gap on the front */
   r3d_box(0, 0, M(5), M(6), M(3), M(5) + CM(10), 0xefe6d8, 0);
   r3d_box(-CM(10), 0, 0, 0, M(3), M(5), 0xe8dcc6, 0);
   r3d_box(M(6), 0, 0, M(6) + CM(10), M(3), M(5), 0xe8dcc6, 0);
   r3d_box(0, 0, -CM(10), M(2), M(3), 0, 0xe8dcc6, 0);
   r3d_box(CM(300), 0, -CM(10), M(6), M(3), 0, 0xe8dcc6, 0);
   r3d_box(M(2), CM(210), -CM(10), CM(300), M(3), 0, 0xe8dcc6, 0);
   r3d_box(0, 0, M(5) - CM(1), M(6), CM(10), M(5), 0xffffff, 0);          /* skirting */
   r3d_glow(1);
   r3d_box(CM(150), CM(110), M(5) - CM(1), CM(330), CM(220), M(5), 0x9fd3f5, R3D_PANE);
   r3d_glow(0);
   r3d_box(CM(150), CM(164), M(5) - CM(3), CM(330), CM(166), M(5), 0xffffff, 0);
   r3d_box(CM(239), CM(110), M(5) - CM(3), CM(241), CM(220), M(5), 0xffffff, 0);
   r3d_box(CM(140), CM(104), M(5) - CM(18), CM(340), CM(110), M(5), 0xffffff, 0); /* the sill */
   r3d_box(CM(130), CM(100), M(5) - CM(14), CM(146), CM(236), M(5) - CM(6), 0x6a9fb5, 0); /* curtains */
   r3d_box(CM(334), CM(100), M(5) - CM(14), CM(350), CM(236), M(5) - CM(6), 0x6a9fb5, 0);
   AT(CM(150), 0, CM(420), 0);
   desk();
   AT(CM(220), 0, CM(330), 2048);
   chair();
   AT(CM(495), 0, CM(30), 3072);
   shelf(seed);
   AT(CM(40), 0, CM(60), 1024);
   sofa();
   AT(CM(170), 0, CM(110), 1024);
   coffee_table();
   AT(CM(110), 0, CM(70), 0);
   rug();
   AT(CM(560), 0, CM(450), 0);
   plant(CM(110));
   AT(CM(40), 0, CM(460), 0);
   plant(CM(70));
   /* two pictures at 1.5 m, facing into the room */
   AT_Y(CM(40), CM(150), M(5) - CM(3), 0);
   frame_on_wall(0x5dade2);
   AT_Y(CM(380), CM(150), M(5) - CM(3), 0);
   frame_on_wall(0xf5b041);
   /* a ceiling light */
   AT(CM(300), 0, CM(250), 0);
   r3d_cylinder(0, CM(270), 0, CM(1), CM(1), CM(30), 4, 0x333333);
   r3d_cylinder(0, CM(250), 0, CM(30), CM(8), CM(20), 12, 0xf0e6c8);
#undef AT
#undef AT_Y
}

void hd_r3d_build(void)
{
   if (ready)
      return;
   r3d_clear();
   r3d_begin();
   room(BASE, BASE, 0, 1);
   room(BASE + M(14), BASE, 1024, 2);
   room(BASE + M(14), BASE + M(14), 2048, 3);
   room(BASE, BASE + M(14), 3072, 4);
   scene = r3d_end();
   ready = 1;
}

void hd_r3d_start(hd_state *s)
{
   memset(&s->show, 0, sizeof s->show);
   s->show.on = 2;
   s->show.kx = BASE + CM(250);
   s->show.ky = BASE + CM(40);
   s->show.ka = 0;
   s->show.kv = 300;
}

void hd_r3d_step(hd_state *s, const hd_input in[MAX_PLAYERS])
{
   hd_show *w = &s->show;
   uint32_t pad = in[0].buttons;
   int32_t turn = 0, walk = 0;
   if ((pad & PAD_LEFT) || in[0].lx < -STICK_DEAD)
      turn = -24;
   if ((pad & PAD_RIGHT) || in[0].lx > STICK_DEAD)
      turn = 24;
   if ((pad & PAD_UP) || in[0].ly < -STICK_DEAD)
      walk = CM(4);
   if ((pad & PAD_DOWN) || in[0].ly > STICK_DEAD)
      walk = -CM(4);
   if (pad & PAD_L)
      w->kv = hd_clamp(w->kv - 8, -300, 700);
   if (pad & PAD_R)
      w->kv = hd_clamp(w->kv + 8, -300, 700);
   w->ka = (w->ka + turn) & (ANGLE_FULL - 1);
   w->kx = hd_clamp(w->kx + (int32_t)(((int64_t)walk * hd_sin(w->ka)) >> 14), 0, FX(511));
   w->ky = hd_clamp(w->ky + (int32_t)(((int64_t)walk * hd_cos(w->ka)) >> 14), 0, FX(511));
   w->t++;
}

void hd_r3d_draw(const hd_state *s, void *screen)
{
   hd_surface *sf = (hd_surface *)screen;
   const hd_show *w = &s->show;
   r3d_view v;
   char line[96];
   int32_t scale = hd_res;
   hd_r3d_build();
   memset(&v, 0, sizeof v);
   v.eye.x = w->kx;
   v.eye.y = CM(165);
   v.eye.z = w->ky;
   v.yaw = w->ka;
   v.pitch = w->kv;
   v.fov = 780;
   v.near_z = CM(5);
   v.sky = 0x1b2333;
   v.sun.x = -FX(2);
   v.sun.y = FX(5);
   v.sun.z = -FX(3);
   v.ambient = 150;
   v.diffuse = 130;
   v.fog = 0x1b2333;
   v.fog_near = M(9);
   v.fog_far = M(26);
   r3d_draw(sf, &v, &scene, NULL, 0);
   snprintf(line, sizeof line, hd_lang == 1 ? "3D  %d TRI\xc3\x81NGULOS, %d EN PANTALLA" : hd_lang == 2 ? "3D  %d TRI\xc3\x82NGULOS, %d NA TELA" : "3D  %d TRIANGLES, %d ON SCREEN",
            (int)r3d_triangles(), (int)r3d_drawn());
   text_draw(sf, line, 8 * scale, 8 * scale, scale, 0xffffff, 1, -1);
}

void hd_r3d_sanitize(hd_show *w)
{
   w->kx = hd_clamp(w->kx, 0, FX(511));
   w->ky = hd_clamp(w->ky, 0, FX(511));
   w->ka &= ANGLE_FULL - 1;
   w->kv = hd_clamp(w->kv, -300, 700);
   w->t = hd_clamp(w->t, 0, 1 << 30);
}
