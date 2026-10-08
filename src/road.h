/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The pseudo 3D road (mode7.c): a loop of segments, each with a curve and a
 * height, seen from a camera behind the player, drawn front to back with
 * the hills hiding what is behind them.
 */
#ifndef HD_ROAD_H
#define HD_ROAD_H

#include "gfx.h"

#define ROAD_SEG 200          /* a segment's length, world units */
#define ROAD_WIDTH 2000       /* half the road's width */
#define ROAD_CAM_HEIGHT 1000  /* the camera over the road */
#define ROAD_DEPTH 55050      /* 1 / tan(half the field of view), 16.16 (about 100 degrees) */
#define ROAD_MAX_DRAW 200     /* segments drawn ahead */

typedef struct
{
   int32_t count;
   const int8_t *curve; /* per segment: how much it bends (negative left) */
   const int16_t *hill; /* per segment: the road's height at its start */
} hd_road;

/* Draws what stands at segment seg (screen centre x, row y, the road's half width there), cut at row clip. */
typedef void (*hd_road_sprite_fn)(void *ctx, hd_surface *s, int32_t seg, int32_t x, int32_t y, int32_t half_width, int32_t clip);

/* pos is how far along the road (world units), player_x where across it (-256 left edge .. 256 right edge). */
void gfx_road(hd_surface *s, const hd_road *r, int32_t pos, int32_t player_x, int32_t draw_dist, hd_road_sprite_fn sprite, void *ctx);

#endif
