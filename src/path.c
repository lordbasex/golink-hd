/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Grid pathfinding (A*): the next cell toward a goal, around walls, in 8
 * directions without cutting corners. It searches a 64 x 64 window around
 * the start with a node budget, so it costs the same small time every
 * frame, and breaks ties the same way everywhere (lower f, then lower h,
 * then the cell found first), so every build picks the same path.
 */
#include <string.h>
#include "path.h"

#define WIN 64
#define CELLS (WIN * WIN)

static int32_t g[CELLS], from[CELLS], hcost[CELLS], seq_of[CELLS];
static uint8_t state[CELLS]; /* 0 unseen, 1 open, 2 closed */
static int32_t heap[CELLS], heap_n;

static int better(int32_t a, int32_t b)
{
   int32_t fa = g[a] + hcost[a], fb = g[b] + hcost[b];
   if (fa != fb)
      return fa < fb;
   if (hcost[a] != hcost[b])
      return hcost[a] < hcost[b];
   return seq_of[a] < seq_of[b];
}

static void push(int32_t c)
{
   int32_t i = heap_n++;
   heap[i] = c;
   while (i > 0 && better(heap[i], heap[(i - 1) / 2]))
   {
      int32_t p = (i - 1) / 2, t = heap[p];
      heap[p] = heap[i];
      heap[i] = t;
      i = p;
   }
}

static int32_t pop(void)
{
   int32_t top = heap[0], i = 0;
   heap[0] = heap[--heap_n];
   for (;;)
   {
      int32_t l = 2 * i + 1, r = l + 1, m = i, t;
      if (l < heap_n && better(heap[l], heap[m]))
         m = l;
      if (r < heap_n && better(heap[r], heap[m]))
         m = r;
      if (m == i)
         break;
      t = heap[m];
      heap[m] = heap[i];
      heap[i] = t;
      i = m;
   }
   return top;
}

/* Octile distance: 10 a straight step, 14 a diagonal one. */
static int32_t octile(int32_t dx, int32_t dy)
{
   dx = hd_abs(dx);
   dy = hd_abs(dy);
   return 10 * (dx + dy) - 6 * hd_min(dx, dy);
}

int hd_path_next(int32_t sx, int32_t sy, int32_t tx, int32_t ty, hd_walk_fn walk, void *ctx, int32_t budget, int32_t *nx, int32_t *ny)
{
   static const int32_t dirs[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
   int32_t ox = sx - WIN / 2, oy = sy - WIN / 2, start, best, seq = 0, k;
   if (sx == tx && sy == ty)
      return 0;
   /* a goal outside the window: aim at the window's cell nearest to it */
   tx = hd_clamp(tx, ox, ox + WIN - 1);
   ty = hd_clamp(ty, oy, oy + WIN - 1);
   memset(state, 0, sizeof state);
   heap_n = 0;
   start = (sy - oy) * WIN + (sx - ox);
   g[start] = 0;
   hcost[start] = octile(tx - sx, ty - sy);
   seq_of[start] = seq++;
   from[start] = -1;
   state[start] = 1;
   push(start);
   best = start;
   while (heap_n > 0 && budget-- > 0)
   {
      int32_t c = pop(), cx = c % WIN, cy = c / WIN;
      if (state[c] == 2)
         continue;
      state[c] = 2;
      if (hcost[c] < hcost[best] || (hcost[c] == hcost[best] && g[c] < g[best]))
         best = c;
      if (cx + ox == tx && cy + oy == ty)
         break;
      for (k = 0; k < 8; k++)
      {
         int32_t x = cx + dirs[k][0], y = cy + dirs[k][1], n, cost;
         if (x < 0 || y < 0 || x >= WIN || y >= WIN || !walk(ctx, x + ox, y + oy))
            continue;
         /* a diagonal step needs both straight neighbours free: no cutting corners */
         if (k >= 4 && (!walk(ctx, cx + dirs[k][0] + ox, cy + oy) || !walk(ctx, cx + ox, cy + dirs[k][1] + oy)))
            continue;
         n = y * WIN + x;
         cost = g[c] + (k >= 4 ? 14 : 10);
         if (state[n] == 2 || (state[n] == 1 && cost >= g[n]))
            continue;
         g[n] = cost;
         hcost[n] = octile(tx - (x + ox), ty - (y + oy));
         from[n] = c;
         if (state[n] != 1)
            seq_of[n] = seq++;
         state[n] = 1;
         push(n);
      }
   }
   if (best == start)
      return 0;
   /* walk back to the first step */
   while (from[best] != start && from[best] >= 0)
      best = from[best];
   *nx = best % WIN + ox;
   *ny = best / WIN + oy;
   return 1;
}
