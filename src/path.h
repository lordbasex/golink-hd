/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/* Grid pathfinding (path.c). */
#ifndef HD_PATH_H
#define HD_PATH_H

#include "hd.h"

/* Whether a cell can be entered. */
typedef int (*hd_walk_fn)(void *ctx, int32_t x, int32_t y);

/*
 * The next cell from (sx, sy) toward (tx, ty), searching at most budget
 * cells; 1 and the cell in *nx, *ny, or 0 when there is no step to take.
 * With no way to the goal it heads for the reachable cell closest to it.
 */
int hd_path_next(int32_t sx, int32_t sy, int32_t tx, int32_t ty, hd_walk_fn walk, void *ctx, int32_t budget, int32_t *nx, int32_t *ny);

#endif
