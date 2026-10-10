/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The demo's level, built from a list of pieces (ground, gaps, steps,
 * platforms, coins, enemies) instead of a hand-typed map. Built once at
 * start; the game only reads it. A cell that changes while playing (a coin
 * taken, a checkpoint reached) is a bit in hd_state.taken, never a change here.
 */
#include <string.h>
#include "hd.h"

uint8_t hd_map[MAP_MAX_H][MAP_MAX_W];
int32_t hd_map_w = 224, hd_map_h = 24;
int32_t hd_enemy_start[MAX_ENEMIES][2];
int32_t hd_enemy_count;
int32_t hd_enemy_kind_of[MAX_ENEMIES]; /* the built-in level has walkers only */
int32_t hd_start_x, hd_start_y;

enum
{
   P_GROUND, /* a, b: from column a for b columns, top at row c */
   P_GAP,    /* nothing: from column a for b columns */
   P_STEPS,  /* b columns of stairs going up from row c, one row per column */
   P_SHELF,  /* one-way platform: columns a..a+b-1 at row c */
   P_BRICKS, /* solid bricks: columns a..a+b-1 at row c */
   P_COINS,  /* a row of b coins from column a at row c */
   P_ARC,    /* b coins in an arc from column a, its lowest row c */
   P_ENEMY,  /* an enemy at column a, standing on row c */
   P_CHECK,  /* a checkpoint at column a, standing on row c */
   P_FLAG,   /* the goal at column a, standing on row c */
   P_END
};

typedef struct { int8_t kind; int16_t a, b, c; } piece;

/* The ground's top row is 20 (y = 320 px); the level is 24 rows (384 px). */
static const piece pieces[] = {
   { P_GROUND, 0, 30, 20 },
   { P_COINS, 10, 5, 17 },
   { P_SHELF, 18, 5, 16 },
   { P_COINS, 18, 5, 15 },
   { P_ENEMY, 26, 0, 20 },
   { P_GAP, 30, 3, 0 },
   { P_ARC, 29, 5, 18 },
   { P_GROUND, 33, 22, 20 },
   { P_BRICKS, 38, 3, 17 },
   { P_COINS, 38, 3, 16 },
   { P_ENEMY, 44, 0, 20 },
   { P_STEPS, 48, 4, 19 },
   { P_GROUND, 52, 6, 16 },
   { P_ENEMY, 54, 0, 16 },
   { P_GROUND, 58, 8, 20 },
   { P_CHECK, 60, 0, 20 },
   { P_GAP, 66, 4, 0 },
   { P_SHELF, 67, 2, 17 },
   { P_COINS, 67, 2, 16 },
   { P_GROUND, 70, 20, 20 },
   { P_SHELF, 74, 4, 16 },
   { P_SHELF, 79, 4, 12 },
   { P_COINS, 79, 4, 11 },
   { P_SHELF, 84, 4, 16 },
   { P_ENEMY, 76, 0, 20 },
   { P_ENEMY, 82, 0, 20 },
   { P_ENEMY, 87, 0, 20 },
   { P_GAP, 90, 5, 0 },
   { P_BRICKS, 92, 1, 16 },
   { P_ARC, 89, 7, 15 },
   { P_GROUND, 95, 25, 20 },
   { P_BRICKS, 100, 6, 16 },
   { P_COINS, 100, 6, 15 },
   { P_BRICKS, 108, 4, 13 },
   { P_COINS, 108, 4, 12 },
   { P_ENEMY, 104, 0, 20 },
   { P_ENEMY, 112, 0, 20 },
   { P_ENEMY, 116, 0, 20 },
   { P_CHECK, 118, 0, 20 },
   { P_GAP, 120, 3, 0 },
   { P_GROUND, 123, 3, 18 },
   { P_GAP, 126, 3, 0 },
   { P_GROUND, 129, 3, 16 },
   { P_GAP, 132, 3, 0 },
   { P_GROUND, 135, 25, 20 },
   { P_ARC, 132, 4, 13 },
   { P_STEPS, 140, 5, 19 },
   { P_GROUND, 145, 4, 15 },
   { P_SHELF, 150, 5, 13 },
   { P_COINS, 150, 5, 12 },
   { P_ENEMY, 152, 0, 20 },
   { P_ENEMY, 156, 0, 20 },
   { P_SHELF, 157, 3, 16 },
   { P_CHECK, 166, 0, 20 },
   { P_GAP, 160, 4, 0 },
   { P_SHELF, 160, 4, 18 },
   { P_GROUND, 164, 60, 20 },
   { P_COINS, 168, 8, 17 },
   { P_ENEMY, 172, 0, 20 },
   { P_ENEMY, 178, 0, 20 },
   { P_ENEMY, 184, 0, 20 },
   { P_STEPS, 190, 6, 19 },
   { P_GROUND, 196, 4, 14 },
   { P_ARC, 197, 6, 9 },
   { P_FLAG, 208, 0, 20 },
   { P_END, 0, 0, 0 },
};

static void set(int32_t x, int32_t y, uint8_t t)
{
   if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H)
      hd_map[y][x] = t;
}

void hd_level_build(void)
{
   const piece *p;
   int32_t x, y;
   memset(hd_map, 0, sizeof hd_map);
   hd_map_w = 224;
   hd_map_h = 24;
   hd_enemy_count = 0;
   memset(hd_enemy_kind_of, 0, sizeof hd_enemy_kind_of);
   for (p = pieces; p->kind != P_END; p++)
   {
      switch (p->kind)
      {
      case P_GROUND:
         for (x = p->a; x < p->a + p->b; x++)
            for (y = p->c; y < MAP_H; y++)
               set(x, y, T_GROUND);
         break;
      case P_GAP:
         for (x = p->a; x < p->a + p->b; x++)
            for (y = 0; y < MAP_H; y++)
               set(x, y, T_EMPTY);
         break;
      case P_STEPS:
         for (x = 0; x < p->b; x++)
            for (y = p->c - x; y < MAP_H; y++)
               set(p->a + x, y, T_GROUND);
         break;
      case P_SHELF:
         for (x = p->a; x < p->a + p->b; x++)
            set(x, p->c, T_PLATFORM);
         break;
      case P_BRICKS:
         for (x = p->a; x < p->a + p->b; x++)
            set(x, p->c, T_BRICK);
         break;
      case P_COINS:
         for (x = p->a; x < p->a + p->b; x++)
            set(x, p->c, T_COIN);
         break;
      case P_ARC:
         for (x = 0; x < p->b; x++)
         {
            /* a hump: 0, 1, 2, 2, 1, 0 rows up */
            int32_t up = hd_min(x, p->b - 1 - x);
            set(p->a + x, p->c - hd_min(up, 2), T_COIN);
         }
         break;
      case P_ENEMY:
         if (hd_enemy_count < MAX_ENEMIES)
         {
            hd_enemy_start[hd_enemy_count][0] = p->a * TILE + (TILE - EW) / 2;
            hd_enemy_start[hd_enemy_count][1] = p->c * TILE - EH;
            hd_enemy_count++;
         }
         break;
      case P_CHECK:
         set(p->a, p->c - 1, T_CHECK);
         break;
      case P_FLAG:
         set(p->a, p->c - 1, T_FLAG);
         break;
      }
   }
   /* enemies in reading order (top to bottom, left to right), as a package's level lists them */
   for (x = 1; x < hd_enemy_count; x++)
      for (y = x; y > 0 && (hd_enemy_start[y][1] < hd_enemy_start[y - 1][1] ||
                            (hd_enemy_start[y][1] == hd_enemy_start[y - 1][1] && hd_enemy_start[y][0] < hd_enemy_start[y - 1][0]));
           y--)
      {
         int32_t t0 = hd_enemy_start[y][0], t1 = hd_enemy_start[y][1];
         hd_enemy_start[y][0] = hd_enemy_start[y - 1][0];
         hd_enemy_start[y][1] = hd_enemy_start[y - 1][1];
         hd_enemy_start[y - 1][0] = t0;
         hd_enemy_start[y - 1][1] = t1;
      }
   hd_start_x = 4 * TILE;
   hd_start_y = 20 * TILE - PH;
}
