/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The showcase demo (core option "Demo: showcase"): one scene for each of
 * the engine's effects, changed with L and R (or Select), each a small
 * playable thing:
 *   0 Mode 7     a kart on a track seen in perspective, trees around it
 *   1 Road       a car on a pseudo 3D road with curves, hills and trees
 *   2 Cave       a boned hero in the dark: torches, its own light, bloom,
 *                bats that find their way to it (A*), an echo, a dialog
 *   3 Sea        waves, underwater colors, muffled sound, bubbles, fish
 *   4 Colors     the demo's level graded (night, sepia...), zoomed, with
 *                bloom and blur
 * Every picture is made in code; everything moves by the state alone.
 */
#include <string.h>
#include "hd.h"
#include "gfx.h"
#include "road.h"
#include "bones.h"
#include "path.h"
#include "text.h"

enum { SC_KART = 0, SC_ROAD, SC_CAVE, SC_SEA, SC_COLORS, SC_COUNT };
#define TRANS 40

/* ---- pictures, made once ---- */

static uint32_t floor_px[512 * 512], kart_px[32 * 24], tree_px[24 * 40], car_px[48 * 28], rtree_px[32 * 48];
static uint32_t bat_px[2][16 * 10], fish_px[20 * 10], weed_px[8 * 14], guide_px[32 * 32];
static uint32_t rock_px[2][16 * 16], torch_px[8 * 16];
static uint32_t torso_px[22 * 12], head_px[16 * 16], uarm_px[12 * 6], farm_px[13 * 5], thigh_px[14 * 7], shin_px[15 * 6];
static hd_image floor_im = { 512, 512, floor_px }, kart_im = { 32, 24, kart_px }, tree_im = { 24, 40, tree_px };
static hd_image car_im = { 48, 28, car_px }, rtree_im = { 32, 48, rtree_px };
static hd_image bat_im[2] = { { 16, 10, bat_px[0] }, { 16, 10, bat_px[1] } }, fish_im = { 20, 10, fish_px }, weed_im = { 8, 14, weed_px };
static hd_image guide_im = { 32, 32, guide_px }, rock_im[2] = { { 16, 16, rock_px[0] }, { 16, 16, rock_px[1] } }, torch_im = { 8, 16, torch_px };
static hd_image torso_im = { 22, 12, torso_px }, head_im = { 16, 16, head_px }, uarm_im = { 12, 6, uarm_px };
static hd_image farm_im = { 13, 5, farm_px }, thigh_im = { 14, 7, thigh_px }, shin_im = { 15, 6, shin_px };

#define INK 0xff1a1020u

static uint32_t hash2(int32_t x, int32_t y, uint32_t seed)
{
   uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
   h = (h ^ (h >> 13)) * 1274126177u;
   return h ^ (h >> 16);
}

/* An ellipse filled in a picture, with a dark outline one pixel wide. */
static void blob(uint32_t *px, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint32_t c, int outline)
{
   int32_t x, y;
   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
      {
         int64_t dx = x - cx, dy = y - cy, r = (int64_t)rx * rx * ry * ry;
         int64_t d = dx * dx * ry * ry + dy * dy * rx * rx;
         int64_t inner = (int64_t)(rx - 1) * (rx - 1) * (ry - 1) * (ry - 1);
         int64_t din = dx * dx * (ry - 1) * (ry - 1) + dy * dy * (rx - 1) * (rx - 1);
         if (d <= r)
            px[y * w + x] = (outline && rx > 1 && ry > 1 && din > inner) ? INK : c;
      }
}

static void box(uint32_t *px, int32_t w, int32_t x0, int32_t y0, int32_t bw, int32_t bh, uint32_t c)
{
   int32_t x, y;
   for (y = y0; y < y0 + bh; y++)
      for (x = x0; x < x0 + bw; x++)
         px[y * w + x] = (x == x0 || y == y0 || x == x0 + bw - 1 || y == y0 + bh - 1) ? INK : c;
}

/* A limb lying along +x: a rounded bar. */
static void limb(uint32_t *px, int32_t w, int32_t h, uint32_t c)
{
   int32_t x, y;
   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
      {
         int edge = y == 0 || y == h - 1 || x == 0 || x == w - 1;
         int corner = (x == 0 || x == w - 1) && (y == 0 || y == h - 1);
         if (!corner)
            px[y * w + x] = edge ? INK : c;
      }
}

/* The kart track: grass, a ring of road with red and white edges, a checkered finish. */
static void make_floor(void)
{
   int32_t x, y;
   for (y = 0; y < 512; y++)
      for (x = 0; x < 512; x++)
      {
         int64_t dx = x - 256, dy = y - 256;
         int64_t d = dx * dx * 4 / 5 + dy * dy; /* a slightly wide ring */
         uint32_t c = (((x >> 5) + (y >> 5)) & 1) ? 0xff4aa84au : 0xff52b452u;
         if ((hash2(x, y, 3) & 63) == 0)
            c = 0xff3e943eu;
         if (d > 120 * 120 && d < 200 * 200)
         {
            c = 0xff6a6a72u;
            if (d < 126 * 126 || d > 194 * 194)
               c = ((((x + y) >> 3) & 1) ? 0xffe83a3au : 0xfff0f0f0u);
            if (x > 380 && x < 396 && y > 236 && y < 276 && y > 230)
               c = (((x >> 2) + (y >> 2)) & 1) ? 0xff101010u : 0xfff8f8f8u;
         }
         floor_px[y * 512 + x] = c;
      }
}

static void make_pictures(void)
{
   int32_t i, x, y;
   make_floor();
   memset(kart_px, 0, sizeof kart_px);
   box(kart_px, 32, 2, 10, 28, 10, 0xffd83a3au);  /* body */
   box(kart_px, 32, 0, 14, 7, 9, 0xff2a2a30u);    /* wheels */
   box(kart_px, 32, 25, 14, 7, 9, 0xff2a2a30u);
   blob(kart_px, 32, 24, 16, 7, 6, 6, 0xfff8c838u, 1); /* helmet */
   box(kart_px, 32, 8, 18, 16, 3, 0xff8a1a1au);
   memset(tree_px, 0, sizeof tree_px);
   box(tree_px, 24, 10, 26, 5, 14, 0xff7a4a22u);
   blob(tree_px, 24, 40, 12, 14, 11, 13, 0xff2e8a3eu, 1);
   blob(tree_px, 24, 40, 9, 10, 4, 4, 0xff4ab85au, 0);
   memset(car_px, 0, sizeof car_px);
   box(car_px, 48, 4, 8, 40, 14, 0xff3a78e0u);
   box(car_px, 48, 12, 2, 24, 8, 0xff6aa0f0u);
   box(car_px, 48, 0, 16, 9, 11, 0xff2a2a30u);
   box(car_px, 48, 39, 16, 9, 11, 0xff2a2a30u);
   box(car_px, 48, 8, 12, 6, 3, 0xffe83a3au); /* lights */
   box(car_px, 48, 34, 12, 6, 3, 0xffe83a3au);
   memset(rtree_px, 0, sizeof rtree_px);
   box(rtree_px, 32, 13, 32, 6, 16, 0xff7a4a22u);
   blob(rtree_px, 32, 48, 16, 18, 15, 17, 0xff2a7a36u, 1);
   blob(rtree_px, 32, 48, 11, 12, 5, 5, 0xff48aa58u, 0);
   for (i = 0; i < 2; i++)
   {
      memset(bat_px[i], 0, sizeof bat_px[i]);
      blob(bat_px[i], 16, 10, 8, 5, 3, 3, 0xff6a3a8au, 1);
      for (x = 0; x < 16; x++)
      {
         int32_t wy = i ? 2 + hd_abs(x - 8) / 2 : 7 - hd_abs(x - 8) / 2; /* wings up or down */
         if (hd_abs(x - 8) > 2 && wy >= 0 && wy < 10)
            bat_px[i][wy * 16 + x] = 0xff4a2a6au;
      }
      bat_px[i][4 * 16 + 7] = bat_px[i][4 * 16 + 9] = 0xffff5050u; /* eyes */
   }
   memset(fish_px, 0, sizeof fish_px);
   blob(fish_px, 20, 10, 9, 5, 8, 4, 0xfff09030u, 1);
   for (y = 2; y < 8; y++)
      fish_px[y * 20 + 18] = fish_px[y * 20 + 19 - (y > 4)] = 0xffe07020u;
   fish_px[4 * 20 + 4] = INK;
   memset(weed_px, 0, sizeof weed_px);
   blob(weed_px, 8, 14, 4, 7, 3, 7, 0xff3a9a5au, 1);
   memset(guide_px, 0, sizeof guide_px);
   blob(guide_px, 32, 32, 16, 16, 15, 15, 0xff3a78e0u, 1); /* the frame */
   blob(guide_px, 32, 32, 16, 18, 10, 11, 0xfff2b48cu, 1); /* the face */
   blob(guide_px, 32, 32, 16, 9, 11, 5, 0xffd8d8e0u, 1);   /* white hair */
   guide_px[17 * 32 + 12] = guide_px[17 * 32 + 20] = INK;  /* eyes */
   for (x = 13; x < 20; x++)
      guide_px[23 * 32 + x] = 0xff8a3a2au; /* a smile */
   for (i = 0; i < 2; i++)
      for (y = 0; y < 16; y++)
         for (x = 0; x < 16; x++)
         {
            uint32_t n = hash2(x + i * 16, y, 9) & 15;
            rock_px[i][y * 16 + x] = n < 3 ? 0xff6a5c80u : (n < 6 ? 0xffa292bcu : 0xff8a7aa4u);
         }
   memset(torch_px, 0, sizeof torch_px);
   box(torch_px, 8, 3, 6, 3, 10, 0xff7a4a22u);
   blob(torch_px, 8, 16, 4, 3, 3, 3, 0xffffc040u, 0);
   torch_px[2 * 8 + 4] = 0xffffffa0u;
   /* the hero's parts, each lying along +x from its pin */
   memset(torso_px, 0, sizeof torso_px);
   limb(torso_px, 22, 12, 0xff3a78e0u);
   memset(head_px, 0, sizeof head_px);
   blob(head_px, 16, 16, 9, 8, 7, 7, 0xfff2b48cu, 1);
   blob(head_px, 16, 16, 12, 8, 4, 7, 0xff5a3420u, 1); /* hair at the top of the head (+x is up the spine) */
   head_px[10 * 16 + 6] = INK;                          /* an eye */
   memset(uarm_px, 0, sizeof uarm_px);
   limb(uarm_px, 12, 6, 0xff3a78e0u);
   memset(farm_px, 0, sizeof farm_px);
   limb(farm_px, 13, 5, 0xfff2b48cu);
   memset(thigh_px, 0, sizeof thigh_px);
   limb(thigh_px, 14, 7, 0xff3048a0u);
   memset(shin_px, 0, sizeof shin_px);
   limb(shin_px, 15, 6, 0xff3048a0u);
   for (y = 0; y < 6; y++)
      for (x = 10; x < 15; x++)
         shin_px[y * 15 + x] = (y == 0 || y == 5 || x == 14) ? INK : 0xff6b3a22u; /* the shoe */
}

/* ---- the boned hero ---- */

enum { B_ROOT = 0, B_SPINE, B_HEAD, B_UARM_B, B_FARM_B, B_THIGH_B, B_SHIN_B, B_THIGH_F, B_SHIN_F, B_UARM_F, B_FARM_F, B_COUNT };

static hd_skeleton hero;
static hd_anim idle_anim, walk_anim, jump_anim, swim_anim;

static void make_hero(void)
{
   static const struct { int32_t parent, length, angle; } bones[B_COUNT] = {
      { -1, 0, 0 },          /* root at the hips */
      { B_ROOT, 20, 3072 },  /* spine, up */
      { B_SPINE, 0, 0 },     /* head */
      { B_SPINE, 11, 2048 }, /* back arm, hanging */
      { B_UARM_B, 12, 0 },
      { B_ROOT, 13, 1024 },  /* back leg, down */
      { B_THIGH_B, 14, 0 },
      { B_ROOT, 13, 1024 },  /* front leg */
      { B_THIGH_F, 14, 0 },
      { B_SPINE, 11, 2048 }, /* front arm */
      { B_UARM_F, 12, 0 },
   };
   static const struct { int32_t bone; const hd_image *im; int32_t px, py, z; } parts[] = {
      { B_UARM_B, &uarm_im, 0, 3, 0 }, { B_FARM_B, &farm_im, 0, 2, 0 },
      { B_THIGH_B, &thigh_im, 0, 3, 1 }, { B_SHIN_B, &shin_im, 0, 3, 1 },
      { B_SPINE, &torso_im, 0, 6, 2 }, { B_HEAD, &head_im, 1, 8, 3 },
      { B_THIGH_F, &thigh_im, 0, 3, 4 }, { B_SHIN_F, &shin_im, 0, 3, 4 },
      { B_UARM_F, &uarm_im, 0, 3, 5 }, { B_FARM_F, &farm_im, 0, 2, 5 },
   };
   int32_t i;
   memset(&hero, 0, sizeof hero);
   hero.bones = B_COUNT;
   for (i = 0; i < B_COUNT; i++)
   {
      hero.bone[i].parent = bones[i].parent;
      hero.bone[i].length = bones[i].length;
      hero.bone[i].angle = bones[i].angle;
   }
   hero.parts = (int32_t)(sizeof parts / sizeof parts[0]);
   for (i = 0; i < hero.parts; i++)
   {
      hero.part[i].bone = parts[i].bone;
      hero.part[i].im = parts[i].im;
      hero.part[i].px = parts[i].px;
      hero.part[i].py = parts[i].py;
      hero.part[i].z = parts[i].z;
   }
   /* walk: legs swing, knees bend, arms swing the other way, a bob */
   memset(&walk_anim, 0, sizeof walk_anim);
   walk_anim.length = 40;
   walk_anim.loop = 1;
   walk_anim.count = 4;
   {
      static const int16_t k[4][6] = {
         /* thigh B, shin B, thigh F, shin F, arm B, arm F */
         { -300, 120, 300, 40, 260, -260 },
         { 0, 300, 0, 60, 0, 0 },
         { 300, 40, -300, 120, -260, 260 },
         { 0, 60, 0, 300, 0, 0 },
      };
      for (i = 0; i < 4; i++)
      {
         walk_anim.key[i].frame = i * 10;
         walk_anim.key[i].angle[B_THIGH_B] = k[i][0];
         walk_anim.key[i].angle[B_SHIN_B] = k[i][1];
         walk_anim.key[i].angle[B_THIGH_F] = k[i][2];
         walk_anim.key[i].angle[B_SHIN_F] = k[i][3];
         walk_anim.key[i].angle[B_UARM_B] = k[i][4];
         walk_anim.key[i].angle[B_UARM_F] = k[i][5];
         walk_anim.key[i].angle[B_FARM_B] = (int16_t)(-120 - k[i][4] / 4);
         walk_anim.key[i].angle[B_FARM_F] = (int16_t)(-120 - k[i][5] / 4);
         walk_anim.key[i].lift = (int16_t)((i & 1) ? 2 : 0);
      }
   }
   /* idle: breathing */
   memset(&idle_anim, 0, sizeof idle_anim);
   idle_anim.length = 90;
   idle_anim.loop = 1;
   idle_anim.count = 2;
   idle_anim.key[0].frame = 0;
   idle_anim.key[0].angle[B_UARM_B] = 60;
   idle_anim.key[0].angle[B_UARM_F] = -60;
   idle_anim.key[1].frame = 45;
   idle_anim.key[1].angle[B_SPINE] = -40;
   idle_anim.key[1].angle[B_HEAD] = 40;
   idle_anim.key[1].angle[B_UARM_B] = 100;
   idle_anim.key[1].angle[B_UARM_F] = -100;
   idle_anim.key[1].lift = 1;
   /* jump: knees up, arms up */
   memset(&jump_anim, 0, sizeof jump_anim);
   jump_anim.length = 1;
   jump_anim.count = 1;
   jump_anim.key[0].angle[B_THIGH_B] = -500;
   jump_anim.key[0].angle[B_SHIN_B] = 700;
   jump_anim.key[0].angle[B_THIGH_F] = -200;
   jump_anim.key[0].angle[B_SHIN_F] = 500;
   jump_anim.key[0].angle[B_UARM_B] = 1300;
   jump_anim.key[0].angle[B_UARM_F] = -1500;
   /* swim: arms round, legs kick */
   memset(&swim_anim, 0, sizeof swim_anim);
   swim_anim.length = 60;
   swim_anim.loop = 1;
   swim_anim.count = 3;
   for (i = 0; i < 3; i++)
   {
      swim_anim.key[i].frame = i * 20;
      swim_anim.key[i].angle[B_SPINE] = -700;
      swim_anim.key[i].angle[B_UARM_B] = (int16_t)(i == 0 ? -1600 : (i == 1 ? -400 : 600));
      swim_anim.key[i].angle[B_UARM_F] = (int16_t)(i == 0 ? 600 : (i == 1 ? -1600 : -400));
      swim_anim.key[i].angle[B_THIGH_B] = (int16_t)(i == 1 ? -300 : 200);
      swim_anim.key[i].angle[B_THIGH_F] = (int16_t)(i == 1 ? 200 : -300);
      swim_anim.key[i].angle[B_SHIN_B] = 200;
      swim_anim.key[i].angle[B_SHIN_F] = 200;
   }
}

/* ---- the cave ---- */

#define CAVE_W 40
#define CAVE_H 23
static const char *cave_rows[CAVE_H] = {
   "########################################",
   "########################################",
   "####..........######...............#####",
   "###...........#####.................####",
   "##.....T.......###.......T...........###",
   "##..............#.....................##",
   "##....................................##",
   "##..........####..........#####.......##",
   "##.........######........#######......##",
   "##..........####...........###........##",
   "##.........................T..........##",
   "##...T................................##",
   "##.............####...................##",
   "###...........######.........####....###",
   "####...........####.........######..####",
   "##.....................................#",
   "##.................T...................#",
   "##.....................................#",
   "#.....####.................####........#",
   "#....######...............######.......#",
   "########################################",
   "########################################",
   "########################################",
};

static int cave_rock(int32_t x, int32_t y)
{
   if (x < 0 || y < 0 || x >= CAVE_W || y >= CAVE_H)
      return 1;
   return cave_rows[y][x] == '#';
}

static int cave_walk(void *ctx, int32_t x, int32_t y)
{
   (void)ctx;
   return !cave_rock(x, y);
}

/* ---- the road ---- */

#define ROAD_N 600
static int8_t road_curve[ROAD_N];
static int16_t road_hill[ROAD_N];
static hd_road road = { ROAD_N, road_curve, road_hill };

static void make_road(void)
{
   /* sections: length, curve, hill rise (world units over the section) */
   static const int16_t sections[][3] = {
      { 50, 0, 0 }, { 60, 4, 0 }, { 40, 0, 800 }, { 70, -6, 0 }, { 40, 0, -800 }, { 50, 2, 1200 },
      { 60, 0, -1200 }, { 80, -3, 0 }, { 50, 7, 0 }, { 100, 0, 0 },
   };
   int32_t n = 0, k, i, h = 0;
   for (k = 0; k < (int32_t)(sizeof sections / sizeof sections[0]) && n < ROAD_N; k++)
   {
      for (i = 0; i < sections[k][0] && n < ROAD_N; i++, n++)
      {
         /* ease in and out of every curve and hill: a curve swells and fades, a hill rises along a smooth step */
         int32_t t = i * 256 / sections[k][0], ease = t < 128 ? t * 2 : (256 - t) * 2;
         int32_t smooth = (3 * t * t * 256 - 2 * t * t * t) >> 16;
         road_curve[n] = (int8_t)(sections[k][1] * ease / 256);
         road_hill[n] = (int16_t)(h + sections[k][2] * smooth / 256);
      }
      h += sections[k][2];
   }
   for (; n < ROAD_N; n++)
   {
      road_curve[n] = 0;
      road_hill[n] = (int16_t)h;
   }
   /* the road loops: bring it back to the start's height */
   for (n = 0; n < ROAD_N; n++)
      road_hill[n] = (int16_t)(road_hill[n] - (int32_t)road_hill[ROAD_N - 1] * n / (ROAD_N - 1));
}

static int32_t show_ready;

void hd_show_build(void)
{
   make_pictures();
   make_hero();
   make_road();
   show_ready = 1;
}

/* ---- the scenes ---- */

static void enter_scene(hd_state *s, int32_t scene)
{
   hd_show *w = &s->show;
   w->scene = scene;
   w->t = 0;
   w->dlg = 0;
   /* each scene's sound */
   if (scene == SC_CAVE)
      hd_audio_effects(s, 0, 180, 150, 110);
   else if (scene == SC_SEA)
      hd_audio_effects(s, 40, 0, 0, 0);
   else
      hd_audio_effects(s, 0, 0, 0, 0);
   if (scene == SC_KART)
   {
      w->kx = FX(256 + 160);
      w->ky = FX(256);
      w->ka = 3072; /* facing up the track */
      w->kv = 0;
   }
   else if (scene == SC_ROAD)
   {
      w->rpos = 0;
      w->rx = 0;
      w->rv = 0;
   }
   else if (scene == SC_CAVE || scene == SC_SEA)
   {
      int32_t i;
      w->hx = FX(5 * TILE);
      w->hy = FX(15 * TILE);
      w->hvx = w->hvy = 0;
      w->hface = 1;
      w->hground = 0;
      for (i = 0; i < SHOW_BATS; i++)
      {
         w->bat_x[i] = FX((8 + i * 8) * TILE);
         w->bat_y[i] = FX(3 * TILE + (i & 1) * 2 * TILE);
         w->bat_tx[i] = -1;
      }
      w->bat_hit = 0;
      if (scene == SC_CAVE)
      {
         w->dlg = 1;
         w->dlg_chars = 0;
      }
   }
   else if (scene == SC_COLORS)
   {
      w->grade = GRADE_NIGHT;
      w->zoom = 256;
      w->blur = 0;
      w->bloom = 0;
      fx_grade_build(w->grade, NULL);
   }
}

/* A loaded save state's showcase values, brought back into range (save.c). */
void hd_show_sanitize(hd_show *w)
{
   int32_t i;
   w->on = w->on ? 1 : 0;
   w->scene = hd_clamp(w->scene, 0, SC_COUNT - 1);
   w->next = hd_clamp(w->next, 0, SC_COUNT - 1);
   w->trans = hd_clamp(w->trans, 0, TRANS);
   w->t = hd_clamp(w->t, 0, 1 << 30);
   w->grade = hd_clamp(w->grade, 0, GRADE_GREY);
   w->zoom = w->zoom ? hd_clamp(w->zoom, 128, 512) : 0; /* 0 until the colors scene starts */
   w->blur = hd_clamp(w->blur, 0, 8);
   w->bloom = w->bloom ? 1 : 0;
   w->kx = hd_clamp(w->kx, 0, FX(511));
   w->ky = hd_clamp(w->ky, 0, FX(511));
   w->ka &= ANGLE_FULL - 1;
   w->kv = hd_clamp(w->kv, -FX(4), FX(4));
   w->rpos = hd_clamp(w->rpos, 0, ROAD_N * ROAD_SEG - 1);
   w->rx = hd_clamp(w->rx, -460, 460);
   w->rv = hd_clamp(w->rv, 0, 300);
   w->hx = hd_clamp(w->hx, 0, FX(CAVE_W * TILE));
   w->hy = hd_clamp(w->hy, 0, FX(CAVE_H * TILE));
   w->hvx = hd_clamp(w->hvx, -FX(8), FX(8));
   w->hvy = hd_clamp(w->hvy, -FX(8), FX(8));
   w->hface = hd_clamp(w->hface, -1, 1);
   w->hground = w->hground ? 1 : 0;
   w->hwalk = hd_clamp(w->hwalk, 0, 1 << 30);
   w->hjump = hd_clamp(w->hjump, 0, 1 << 30);
   for (i = 0; i < SHOW_BATS; i++)
   {
      w->bat_x[i] = hd_clamp(w->bat_x[i], 0, FX(CAVE_W * TILE));
      w->bat_y[i] = hd_clamp(w->bat_y[i], 0, FX(CAVE_H * TILE));
      w->bat_tx[i] = hd_clamp(w->bat_tx[i], -1, CAVE_W);
      w->bat_ty[i] = hd_clamp(w->bat_ty[i], -1, CAVE_H);
   }
   w->bat_hit = hd_clamp(w->bat_hit, 0, 60);
   w->dlg = w->dlg ? 1 : 0;
   w->dlg_chars = hd_clamp(w->dlg_chars, 0, 1000);
}

void hd_show_start(hd_state *s)
{
   memset(&s->show, 0, sizeof s->show);
   s->show.on = 1;
   enter_scene(s, SC_KART);
}

static const char *const dialog_text[LANGS] = {
   "Welcome to the cave! It is dark in here: your own light and the torches show the way. Watch out, the bats always find you.",
   ("\xc2\xa1" /* split: B would read as a hex digit */ "Bienvenido a la cueva! Aqu\xc3\xad est\xc3\xa1 oscuro: tu propia luz y las antorchas muestran el camino. Cuidado, los murci\xc3\xa9lagos siempre te encuentran."),
   "Bem-vindo \xc3\xa0 caverna! Aqui est\xc3\xa1 escuro: sua pr\xc3\xb3pria luz e as tochas mostram o caminho. Cuidado, os morcegos sempre encontram voc\xc3\xaa.",
};

static void kart_step(hd_show *w, uint32_t pad)
{
   int32_t turn = ((pad & PAD_RIGHT) ? 1 : 0) - ((pad & PAD_LEFT) ? 1 : 0), u, v, max = FX(3);
   uint32_t ground;
   if (pad & (PAD_B | PAD_A))
      w->kv += FX_FRAC(8, 100);
   else if (pad & (PAD_Y | PAD_X))
      w->kv -= FX_FRAC(12, 100);
   w->kv -= w->kv / 40; /* rolling friction */
   u = FX_INT(w->kx);
   v = FX_INT(w->ky);
   ground = (u >= 0 && v >= 0 && u < 512 && v < 512) ? floor_px[v * 512 + u] & 0xffffffu : 0;
   if (ground == 0x4aa84au || ground == 0x52b452u || ground == 0x3e943eu || !ground)
      max = FX_FRAC(3, 2); /* grass is slow */
   w->kv = hd_clamp(w->kv, -FX(1), max);
   if (w->kv > FX_FRAC(1, 4) || w->kv < -FX_FRAC(1, 4))
      w->ka = (w->ka + turn * 24 * (w->kv > 0 ? 1 : -1)) & (ANGLE_FULL - 1);
   w->kx = hd_clamp(w->kx + (int32_t)(((int64_t)w->kv * hd_cos(w->ka)) >> 14), 0, FX(511));
   w->ky = hd_clamp(w->ky + (int32_t)(((int64_t)w->kv * hd_sin(w->ka)) >> 14), 0, FX(511));
}

static void road_step(hd_show *w, uint32_t pad)
{
   int32_t seg = (w->rpos / ROAD_SEG) % ROAD_N, dir = ((pad & PAD_RIGHT) ? 1 : 0) - ((pad & PAD_LEFT) ? 1 : 0);
   int32_t max = hd_abs(w->rx) > 256 ? 80 : 300; /* off the road is slow */
   if (pad & (PAD_B | PAD_A))
      w->rv += 3;
   else if (pad & (PAD_Y | PAD_X))
      w->rv -= 8;
   else
      w->rv -= 1;
   w->rv = hd_clamp(w->rv, 0, max);
   if (w->rv > max)
      w->rv = max;
   w->rx += dir * w->rv / 24;
   w->rx -= road_curve[seg] * w->rv / 70; /* curves push outward */
   w->rx = hd_clamp(w->rx, -460, 460);
   w->rpos = (w->rpos + w->rv) % (ROAD_N * ROAD_SEG);
}

static void hero_step(hd_state *s, uint32_t pad, uint32_t pressed, int swim)
{
   hd_show *w = &s->show;
   int32_t dir = ((pad & PAD_RIGHT) ? 1 : 0) - ((pad & PAD_LEFT) ? 1 : 0), cx, cy, i;
   if (dir)
      w->hface = dir;
   if (swim)
   {
      int32_t up = ((pad & PAD_DOWN) ? 1 : 0) - ((pad & PAD_UP) ? 1 : 0);
      w->hvx += dir * FX_FRAC(1, 10);
      w->hvy += up * FX_FRAC(1, 10) + FX_FRAC(1, 100); /* sinks slowly */
      w->hvx = w->hvx * 15 / 16;
      w->hvy = w->hvy * 15 / 16;
   }
   else
   {
      w->hvx = dir * FX_FRAC(22, 10);
      if ((pressed & (PAD_B | PAD_A)) && w->hground)
      {
         w->hvy = -FX_FRAC(68, 10);
         hd_play(s, SFX_JUMP, FX_INT(w->hx));
      }
      w->hvy = hd_min(w->hvy + FX_FRAC(4, 10), FX(7));
   }
   /* the hero's box: 16 wide, 56 tall, its feet at (hx, hy) */
   w->hx += w->hvx;
   cx = FX_INT(w->hx);
   cy = FX_INT(w->hy);
   for (i = cy - 55; i <= cy - 1; i += 8)
   {
      if (cave_rock((cx + 8) >> 4, i >> 4))
      {
         w->hx = FX(((cx + 8) >> 4) * TILE - 9);
         w->hvx = 0;
      }
      if (cave_rock((cx - 8) >> 4, i >> 4))
      {
         w->hx = FX(((cx - 8) >> 4) * TILE + TILE + 8);
         w->hvx = 0;
      }
   }
   w->hy += w->hvy;
   cx = FX_INT(w->hx);
   cy = FX_INT(w->hy);
   w->hground = 0;
   if (w->hvy >= 0 && (cave_rock((cx - 7) >> 4, cy >> 4) || cave_rock((cx + 7) >> 4, cy >> 4)))
   {
      w->hy = FX((cy >> 4) * TILE);
      w->hvy = 0;
      w->hground = 1;
   }
   else if (w->hvy < 0 && (cave_rock((cx - 7) >> 4, (cy - 56) >> 4) || cave_rock((cx + 7) >> 4, (cy - 56) >> 4)))
   {
      w->hy = FX(((cy - 56) >> 4) * TILE + TILE + 56);
      w->hvy = 0;
   }
   w->hwalk = (dir && (w->hground || swim)) ? w->hwalk + 1 : 0;
}

static void bats_step(hd_state *s)
{
   hd_show *w = &s->show;
   int32_t i, hx = FX_INT(w->hx), hy = FX_INT(w->hy) - 30;
   if (w->bat_hit)
      w->bat_hit--;
   for (i = 0; i < SHOW_BATS; i++)
   {
      int32_t bx = FX_INT(w->bat_x[i]), by = FX_INT(w->bat_y[i]), nx, ny, tx, ty, dx, dy, d;
      /* a new step every 8 frames, spread over the bats */
      if ((s->frame + i * 2) % 8 == 0 || w->bat_tx[i] < 0)
      {
         if (hd_path_next(bx >> 4, by >> 4, hx >> 4, hy >> 4, cave_walk, NULL, 600, &nx, &ny))
         {
            w->bat_tx[i] = nx;
            w->bat_ty[i] = ny;
         }
         else
         {
            w->bat_tx[i] = hx >> 4;
            w->bat_ty[i] = hy >> 4;
         }
      }
      tx = w->bat_tx[i] * TILE + TILE / 2 + (hd_sin(s->frame * 64 + i * 1000) >> 12);
      ty = w->bat_ty[i] * TILE + TILE / 2 + (hd_sin(s->frame * 90 + i * 700) >> 12);
      dx = tx - bx;
      dy = ty - by;
      d = hd_max(1, hd_abs(dx) + hd_abs(dy));
      w->bat_x[i] += (int32_t)((int64_t)FX_FRAC(13, 10) * dx / d);
      w->bat_y[i] += (int32_t)((int64_t)FX_FRAC(13, 10) * dy / d);
      if (!w->bat_hit && hd_abs(bx - hx) < 14 && hd_abs(by - hy) < 26)
      {
         w->bat_hit = 60;
         w->hvx = (bx < hx ? 1 : -1) * FX(4);
         w->hvy = -FX(4);
         hd_play(s, SFX_HURT, hx);
      }
   }
}

void hd_show_step(hd_state *s, const hd_input in[MAX_PLAYERS])
{
   hd_show *w = &s->show;
   hd_player *p = &s->p[0];
   uint32_t pad, pressed;
   p->prev = p->pad;
   pad = in[0].buttons;
   if (in[0].lx > STICK_DEAD)
      pad |= PAD_RIGHT;
   if (in[0].lx < -STICK_DEAD)
      pad |= PAD_LEFT;
   if (in[0].ly > STICK_DEAD)
      pad |= PAD_DOWN;
   if (in[0].ly < -STICK_DEAD)
      pad |= PAD_UP;
   p->pad = pad;
   pressed = pad & ~p->prev;
   w->t++;
   if (w->trans > 0)
   {
      if (--w->trans == TRANS / 2)
         enter_scene(s, w->next);
      return;
   }
   if (pressed & (PAD_R | PAD_SELECT | PAD_L))
   {
      w->next = (w->scene + ((pressed & PAD_L) ? SC_COUNT - 1 : 1)) % SC_COUNT;
      w->trans = TRANS;
      hd_play(s, SFX_CHECK, HD_W / 2);
      return;
   }
   /* a dialog waits for a button */
   if (w->dlg)
   {
      int32_t total = text_glyphs(dialog_text[hd_lang]);
      if (w->dlg_chars < total)
         w->dlg_chars++;
      if (pressed & (PAD_B | PAD_A | PAD_START))
      {
         if (w->dlg_chars < total)
            w->dlg_chars = total;
         else
            w->dlg = 0;
      }
      return;
   }
   switch (w->scene)
   {
   case SC_KART:
      kart_step(w, pad);
      break;
   case SC_ROAD:
      road_step(w, pad);
      break;
   case SC_CAVE:
      hero_step(s, pad, pressed, 0);
      bats_step(s);
      break;
   case SC_SEA:
      hero_step(s, pad, pressed, 1);
      if ((s->frame & 7) == 0)
      {
         /* bubbles from the hero and the sea floor */
         hd_particle *q = &s->part[s->part_next];
         s->part_next = (s->part_next + 1) % MAX_PARTICLES;
         q->life = q->max = 120;
         q->x = (s->frame & 8) ? w->hx : FX(rng_range(&s->rng, HD_W));
         q->y = (s->frame & 8) ? w->hy - FX(50) : FX(HD_H - 8);
         q->vx = rng_range(&s->rng, FX(1)) - FX_FRAC(1, 2);
         q->vy = -FX_FRAC(6, 10) - rng_range(&s->rng, FX_FRAC(4, 10));
         q->color = 0xffc0e8ffu;
         q->flags = 2;
      }
      break;
   case SC_COLORS:
      if (pressed & PAD_RIGHT)
         w->grade = w->grade % (GRADE_GREY) + 1;
      if (pressed & PAD_LEFT)
         w->grade = (w->grade + GRADE_GREY - 2) % GRADE_GREY + 1;
      if (pressed & (PAD_RIGHT | PAD_LEFT))
         fx_grade_build(w->grade, NULL);
      if (pad & PAD_UP)
         w->zoom = hd_min(w->zoom + 4, 512);
      if (pad & PAD_DOWN)
         w->zoom = hd_max(w->zoom - 4, 128);
      if (pressed & (PAD_Y | PAD_X))
         w->bloom = !w->bloom;
      if (pressed & (PAD_B | PAD_A))
         w->blur = w->blur ? 0 : 2;
      s->cam_x = (s->cam_x + FX(1)) % FX(MAP_W * TILE - HD_W * 2);
      break;
   }
   /* particles move in every scene */
   {
      int32_t i;
      for (i = 0; i < MAX_PARTICLES; i++)
      {
         hd_particle *q = &s->part[i];
         if (!q->life)
            continue;
         q->life--;
         q->x += q->vx + (hd_sin(q->life * 80) >> 6);
         q->y += q->vy;
      }
   }
}

/* ---- drawing ---- */

static void sky(hd_surface *sf, int32_t to, uint32_t top, uint32_t bottom)
{
   int32_t y, x;
   for (y = 0; y < hd_min(to, sf->h); y++)
   {
      uint32_t c = gfx_mix(top, bottom, y * 256 / hd_max(1, to));
      for (x = 0; x < sf->w; x++)
         sf->px[y * sf->w + x] = c;
   }
}

/* Far hills on the horizon, scrolled sideways. */
static void hills(hd_surface *sf, int32_t horizon, int32_t scroll, uint32_t color)
{
   int32_t x, y;
   for (x = 0; x < sf->w; x++)
   {
      int32_t u = x + scroll, h = 18 + (hd_sin(u * 9) >> 10) + (hd_sin(u * 23 + 700) >> 11);
      for (y = hd_max(0, horizon - h); y < horizon && y < sf->h; y++)
         sf->px[y * sf->w + x] = color;
   }
}

typedef struct { int32_t x, y, scale; } spot;

static void draw_kart(const hd_state *s, hd_surface *sf)
{
   const hd_show *w = &s->show;
   hd_mode7 m;
   spot spots[16];
   int32_t n = 0, i, j;
   memset(&m, 0, sizeof m);
   m.angle = w->ka;
   m.x = w->kx - (int32_t)(((int64_t)FX(70) * hd_cos(w->ka)) >> 14);
   m.y = w->ky - (int32_t)(((int64_t)FX(70) * hd_sin(w->ka)) >> 14);
   m.height = 30;
   m.horizon = sf->h * 2 / 5;
   m.focal = 280;
   m.wrap = 0;
   m.outside = 0x3e943eu;
   m.fog = 150;
   m.fog_color = 0xb8d8f0u;
   sky(sf, m.horizon + 1, 0x3a6ad0u, 0xb8d8f0u);
   hills(sf, m.horizon + 1, -w->ka * 640 / 1024, 0x6a8ab8u);
   gfx_mode7(sf, &floor_im, &m);
   /* trees around the track, far ones first */
   for (i = 0; i < 16; i++)
   {
      int32_t a = i * 256, r = 230 + (i & 1) * 40, sx, sy, sc;
      int32_t wx = FX(256) + (int32_t)(((int64_t)FX(r) * hd_cos(a)) >> 14), wy = FX(256) + (int32_t)(((int64_t)FX(r) * hd_sin(a)) >> 14);
      if (gfx_mode7_project(&m, wx, wy, sf->w, &sx, &sy, &sc))
      {
         spots[n].x = sx;
         spots[n].y = sy;
         spots[n].scale = sc;
         n++;
      }
   }
   for (i = 1; i < n; i++)
      for (j = i; j > 0 && spots[j].scale < spots[j - 1].scale; j--)
      {
         spot t = spots[j];
         spots[j] = spots[j - 1];
         spots[j - 1] = t;
      }
   for (i = 0; i < n; i++)
      if (spots[i].scale > FX_FRAC(1, 8) && spots[i].scale < FX(2)) /* too close: it would cover the screen */
         gfx_blit_rot(sf, &tree_im, spots[i].x, spots[i].y, 12, 39, 0, spots[i].scale * 2, spots[i].scale * 2, NULL);
   /* the kart, leaning into its turn */
   gfx_shadow(sf, sf->w / 2, sf->h - 54, 30, 5, 140);
   gfx_blit_rot(sf, &kart_im, sf->w / 2, sf->h - 56, 16, 23, ((s->p[0].pad & PAD_RIGHT) ? 40 : 0) - ((s->p[0].pad & PAD_LEFT) ? 40 : 0), FX(3), FX(3), NULL);
}


static void road_sprite(void *ctx, hd_surface *sf, int32_t seg, int32_t x, int32_t y, int32_t half_width, int32_t clip)
{
   int32_t side, scale = half_width * FX_ONE / 220;
   (void)ctx;
   if (y > clip + 40) /* hidden behind a hill */
      return;
   if (seg % 8 || scale < FX_FRAC(1, 16) || scale > FX(6))
      return;
   for (side = -1; side <= 1; side += 2)
      gfx_blit_rot(sf, &rtree_im, x + side * (half_width + half_width / 2 + ((seg / 8) & 1) * half_width / 2), y, 16, 47, 0, scale, scale, NULL);
}

static void draw_road(const hd_state *s, hd_surface *sf)
{
   const hd_show *w = &s->show;
   int32_t seg = (w->rpos / ROAD_SEG) % ROAD_N, lean = ((s->p[0].pad & PAD_RIGHT) ? 60 : 0) - ((s->p[0].pad & PAD_LEFT) ? 60 : 0);
   sky(sf, sf->h / 2 + 10, 0x2a5ac0u, 0xf0c890u);
   hills(sf, sf->h / 2 + 10, seg * 3 + road_curve[seg] * 20, 0x5a7aa8u);
   gfx_road(sf, &road, w->rpos, w->rx, 160, road_sprite, NULL);
   gfx_shadow(sf, sf->w / 2, sf->h - 22, 70, 6, 150);
   gfx_blit_rot(sf, &car_im, sf->w / 2, sf->h - 24, 24, 27, lean, FX(3), FX(3), NULL);
}

static void draw_hero(const hd_state *s, hd_surface *sf, int32_t ox, int32_t oy, int swim)
{
   const hd_show *w = &s->show;
   hd_pose pose, a, b;
   hd_style st;
   memset(&st, 0, sizeof st);
   st.outline = 0xff1a1020u;
   if (w->bat_hit > 40 && ((w->bat_hit >> 2) & 1))
      st.flags = DRAW_WHITE;
   if (swim)
      bones_pose(&swim_anim, w->t, &pose);
   else if (!w->hground)
      bones_pose(&jump_anim, 0, &pose);
   else
   {
      bones_pose(&idle_anim, w->t, &a);
      bones_pose(&walk_anim, w->hwalk, &b);
      bones_blend(&a, &b, w->hwalk ? 256 : 0, &pose);
   }
   if (!swim)
      gfx_shadow(sf, FX_INT(w->hx) - ox, FX_INT(w->hy) - oy, 14, 3, 140);
   bones_draw(sf, &hero, &pose, FX_INT(w->hx) - ox, FX_INT(w->hy) - 27 - oy, FX(1), w->hface < 0, &st);
}

static void draw_cave(const hd_state *s, hd_surface *sf)
{
   const hd_show *w = &s->show;
   static hd_light lights[24];
   int32_t x, y, n = 0, i;
   hd_style st;
   for (y = 0; y < sf->h; y++)
      for (x = 0; x < sf->w; x++)
         sf->px[y * sf->w + x] = 0x5a4a6cu + ((hash2(x >> 3, y >> 3, 5) & 15) << 16);
   for (y = 0; y < CAVE_H; y++)
      for (x = 0; x < CAVE_W; x++)
      {
         char c = cave_rows[y][x];
         if (c == '#')
         {
            hd_style rs;
            memset(&rs, 0, sizeof rs);
            gfx_blit(sf, &rock_im[(x + y) & 1], x * TILE, y * TILE, &rs);
         }
         else if (c == 'T')
         {
            gfx_blit(sf, &torch_im, x * TILE + 4, y * TILE, NULL);
            lights[n].x = x * TILE + 8;
            lights[n].y = y * TILE + 3;
            lights[n].radius = 110 + (hd_sin(s->frame * 97 + x * 333) + hd_sin(s->frame * 41 + y * 911)) / 2000;
            lights[n].color = 0xffb060u;
            lights[n].strength = 256;
            n++;
         }
      }
   draw_hero(s, sf, 0, 0, 0);
   memset(&st, 0, sizeof st);
   st.outline = 0xc0101010u;
   for (i = 0; i < SHOW_BATS; i++)
   {
      gfx_blit(sf, &bat_im[(s->frame >> 3) & 1], FX_INT(w->bat_x[i]) - 8, FX_INT(w->bat_y[i]) - 5, &st);
      lights[n].x = FX_INT(w->bat_x[i]);
      lights[n].y = FX_INT(w->bat_y[i]);
      lights[n].radius = 22;
      lights[n].color = 0xc050ffu;
      lights[n].strength = 200;
      n++;
   }
   lights[n].x = FX_INT(w->hx);
   lights[n].y = FX_INT(w->hy) - 30;
   lights[n].radius = 135;
   lights[n].color = 0xffe0b0u;
   lights[n].strength = 256;
   n++;
   fx_lights(sf, 228, lights, n);
   fx_bloom(sf, 185, 220);
}

static void draw_sea(const hd_state *s, hd_surface *sf)
{
   int32_t x, y, i;
   hd_style st;
   for (y = 0; y < sf->h; y++)
   {
      uint32_t c = gfx_mix(0x2a8ac8u, 0x0a2a58u, y * 256 / sf->h);
      for (x = 0; x < sf->w; x++)
         sf->px[y * sf->w + x] = c;
   }
   /* light from the surface: soft diagonal beams, added */
   for (i = 0; i < 5; i++)
   {
      int32_t bx = (i * 150 + s->frame / 3) % (sf->w + 200) - 100;
      for (y = 0; y < sf->h * 2 / 3; y++)
         for (x = bx + y / 3; x < bx + y / 3 + 30; x++)
            if ((uint32_t)x < (uint32_t)sf->w)
            {
               uint32_t c = sf->px[y * sf->w + x];
               int32_t a = 26 * (sf->h * 2 / 3 - y) / (sf->h * 2 / 3);
               sf->px[y * sf->w + x] = gfx_mix(c, 0xd0f0ffu, a);
            }
   }
   /* sand and swaying weed */
   for (y = sf->h - 24; y < sf->h; y++)
      for (x = 0; x < sf->w; x++)
         sf->px[y * sf->w + x] = (hash2(x, y, 4) & 7) ? 0xc8b078u : 0xa89058u;
   for (i = 0; i < 14; i++)
   {
      int32_t bx = 20 + i * 46, k;
      for (k = 0; k < 4; k++)
         gfx_blit_rot(sf, &weed_im, bx + (hd_sin(s->frame * 30 + i * 500 + k * 300) * k >> 13), sf->h - 22 - k * 12, 4, 13,
                      hd_sin(s->frame * 30 + i * 500 + k * 300) >> 7, FX(1), FX(1), NULL);
   }
   /* fish, swimming in waves */
   for (i = 0; i < 6; i++)
   {
      int32_t dir = (i & 1) ? 1 : -1, fx = ((s->frame * (1 + i % 3) * dir + i * 211) % (sf->w + 80) + sf->w + 80) % (sf->w + 80) - 40;
      int32_t fy = 60 + i * 42 + (hd_sin(s->frame * 50 + i * 900) >> 11);
      memset(&st, 0, sizeof st);
      st.flags = dir > 0 ? DRAW_FLIP_X : 0;
      gfx_blit_rot(sf, &fish_im, fx, fy, 10, 5, hd_sin(s->frame * 50 + i * 900) >> 8, FX(2), FX(2), &st);
   }
   draw_hero(s, sf, 0, 0, 1);
   /* bubbles */
   for (i = 0; i < MAX_PARTICLES; i++)
   {
      const hd_particle *q = &s->part[i];
      if (q->life)
         gfx_fill(sf, FX_INT(q->x) - 1, FX_INT(q->y) - 1, 3, 3, 0xd8f0ffu);
   }
   fx_waves(sf, 0, sf->h - 1, 3, 70, s->frame * 50);
   fx_grade_build(GRADE_UNDERWATER, NULL);
   fx_grade(sf, 160);
}

/* The colors scene: the demo's level drawn by the game's own renderer. */

static uint32_t big_px[ZOOM_MAX_W * ZOOM_MAX_H];

static void draw_colors(const hd_state *s, hd_surface *sf)
{
   const hd_show *w = &s->show;
   int32_t zoom = w->zoom ? w->zoom : 256;
   hd_surface big;
   big.px = big_px;
   big.w = hd_min(sf->w * 256 / zoom, ZOOM_MAX_W);
   big.h = hd_min(sf->h * 256 / zoom, ZOOM_MAX_H);
   hd_draw_world(s, &big, FX_INT(s->cam_x), hd_max(0, MAP_H * TILE - big.h));
   gfx_scale(sf, &big);
   if (w->bloom)
      fx_bloom(sf, 120, 256);
   if (w->blur)
      fx_blur(sf, w->blur);
   fx_grade(sf, 256);
}

static const char *const scene_names[SC_COUNT] = { "MODE 7", "ROAD", "CAVE", "SEA", "COLORS" };
static const char *const hints[LANGS][SC_COUNT] = {
   { "B GO - Y BRAKE - LEFT RIGHT TURN", "B GO - Y BRAKE - LEFT RIGHT STEER", "LEFT RIGHT WALK - B JUMP", "D-PAD SWIM", "LEFT RIGHT COLORS - UP DOWN ZOOM - Y BLOOM - B BLUR" },
   { "B ACELERA - Y FRENA - IZQUIERDA DERECHA GIRA", "B ACELERA - Y FRENA - IZQUIERDA DERECHA", "IZQUIERDA DERECHA CAMINA - B SALTA", "CRUCETA NADA", "IZQ. DER. COLORES - ARRIBA ABAJO ZOOM - Y BLOOM - B BLUR" },
   { "B ACELERA - Y FREIA - ESQUERDA DIREITA VIRA", "B ACELERA - Y FREIA - ESQUERDA DIREITA", "ESQUERDA DIREITA ANDA - B PULA", "DIRECIONAL NADA", "ESQ. DIR. CORES - CIMA BAIXO ZOOM - Y BLOOM - B BLUR" },
};
static const char *const grade_names[] = { "", "NIGHT", "SEPIA", "UNDERWATER", "SUNSET", "GREY" };

void hd_show_draw(const hd_state *s, void *screen)
{
   hd_surface *sf = (hd_surface *)screen;
   const hd_show *w = &s->show;
   char line[64];
   if (!show_ready)
      hd_show_build();
   switch (w->scene)
   {
   case SC_KART: draw_kart(s, sf); break;
   case SC_ROAD: draw_road(s, sf); break;
   case SC_CAVE: draw_cave(s, sf); break;
   case SC_SEA: draw_sea(s, sf); break;
   default: draw_colors(s, sf); break;
   }
   /* the scene's name and its buttons */
   line[0] = (char)('1' + w->scene);
   line[1] = '/';
   line[2] = (char)('0' + SC_COUNT);
   line[3] = ' ';
   strcpy(line + 4, scene_names[w->scene]);
   if (w->scene == SC_COLORS && w->grade > 0 && w->grade <= 5)
   {
      strcat(line, " - ");
      strcat(line, grade_names[w->grade]);
   }
   text_draw(sf, line, 12, 12, 2, 0xf8c838u, 1, -1);
   text_draw(sf, "L R: SCENE", sf->w - 12 - text_width("L R: SCENE", 1), 14, 1, 0xffffffu, 1, -1);
   text_draw(sf, hints[hd_lang][w->scene], 12, sf->h - 18, 1, 0xffffffu, 1, -1);
   if (w->dlg)
      text_dialog(sf, &guide_im, hd_lang == 0 ? "THE GUIDE" : (hd_lang == 1 ? "EL GU\xc3\x8d" "A" : "O GUIA"), dialog_text[hd_lang], w->dlg_chars, (s->frame >> 4) & 1);
   /* between scenes: the picture breaks into blocks and fades, then comes back */
   if (w->trans > 0)
   {
      int32_t k = w->trans > TRANS / 2 ? TRANS - w->trans : w->trans; /* 0..TRANS/2 */
      fx_pixelate(sf, 1 + k * 24 / (TRANS / 2));
      fx_fade(sf, 0x000000u, k * 256 / (TRANS / 2));
   }
}
