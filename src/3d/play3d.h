/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * What a 3D game played on foot needs, on top of r3d: a walk grid made from
 * the baked solids (where a person fits), moving a person that slides along
 * walls, a stick turned into a walk relative to the camera, and a follow
 * camera pulled in front of walls. All integers, so a game's rules stay the
 * same on every computer. See docs/3d.md.
 */
#ifndef HD_PLAY3D_H
#define HD_PLAY3D_H

#include "r3d.h"

/* The walk grid's cell (10 cm) and the most cells it holds. */
#define PLAY3D_CELL FX_FRAC(10, 100)
#define PLAY3D_GRID_MAX (256 * 256)

/*
 * Builds the walk grid over lo..hi (x and z, 16.16 meters) after r3d_bake:
 * a cell is free when the floor is under it (a solid just below floor_y)
 * and nothing solid stands between knee and head height above it.
 * Returns the free cells.
 */
int32_t play3d_grid(r3d_vec lo, r3d_vec hi, int32_t floor_y);
/* Whether a person of `radius` fits standing at (x, z). */
int32_t play3d_fits(int32_t x, int32_t z, int32_t radius);
/* Moves a person by (dx, dz), sliding along what blocks it; returns 1 when it moved at all. */
int32_t play3d_move(int32_t *x, int32_t *z, int32_t dx, int32_t dz, int32_t radius);

/*
 * A way through the walk grid to a goal, for people and pets the game moves
 * itself: the direction (dx, dz), 16384 long, from (x, z) toward a point a
 * few cells ahead on the shortest way to (gx, gz), around furniture and
 * through doorways. Each of PLAY3D_ROUTES slots keeps the distances to its
 * last goal (worked out again only when the goal moves to another cell), so
 * a walker per slot costs little. The distances depend only on the grid and
 * the goal, never on earlier calls, so a save state needs nothing from here.
 * Returns 0 at the goal's cell or when the goal cannot be reached from there.
 */
#define PLAY3D_ROUTES 4
#define PLAY3D_ROUTE_RADIUS FX_FRAC(23, 100)
int32_t play3d_route(int32_t slot, int32_t x, int32_t z, int32_t gx, int32_t gz, int32_t *dx, int32_t *dz);

/* The angle (4096 a turn) of the direction (dx, dz): 0 toward +z, 1024 toward +x. */
int32_t play3d_angle(int32_t dx, int32_t dz);
/* The square root of v (rounded down). */
int32_t play3d_sqrt(int64_t v);
/* An angle moved toward `want` by at most `step`, the short way round. */
int32_t play3d_turn(int32_t from, int32_t want, int32_t step);

/*
 * A stick (or the D-pad) as a walk on the floor seen from a camera turned
 * `cam_yaw`: the direction in (dx, dz), 16384 long at full tilt (shorter
 * when the stick is tilted a little), 0 inside the dead zone.
 */
void play3d_stick(int32_t lx, int32_t ly, uint32_t buttons, int32_t cam_yaw, int32_t *dx, int32_t *dz);

/*
 * A follow camera: the eye `want` from `target`, turned yaw and tilted
 * pitch (as r3d_view). When something stands between them it first rises
 * (`lift`, added to the pitch, up to PLAY3D_LIFT_TOP: over the walls of a
 * world without a ceiling), and what still stands in the way pulls it in
 * (`dist`, the distance now). Both come back slowly, so walls never cut
 * the person and the view never jumps. `ahead` is where the target will be
 * soon (the target itself when it stands still): the camera starts rising
 * before a wall comes between. Keep dist and lift in the state.
 */
#define PLAY3D_LIFT_TOP 1000
void play3d_follow(r3d_vec target, r3d_vec ahead, int32_t yaw, int32_t pitch, int32_t want, int32_t *dist, int32_t *lift);
/* The eye of a follow camera at `dist`. */
r3d_vec play3d_eye(r3d_vec target, int32_t yaw, int32_t pitch, int32_t dist);

#endif
