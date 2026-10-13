/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * go-link HD's 3D: low-poly scenes drawn in software, with integers only,
 * like the rest of the engine, so every build draws the same pixels (see
 * docs/3d.md). The 2D engine never calls it; only 3D games do.
 *
 * Units: positions are 16.16 meters (FX_ONE is one meter), y points up;
 * angles are the engine's (4096 a turn, gfx.h). A scene is a list of
 * triangles built once (r3d_* builders) and only read while drawing; what
 * moves (a character, a door) is drawn each frame as an instance: a mesh
 * range placed with a position and a turn.
 */
#ifndef HD_R3D_H
#define HD_R3D_H

#include <stdint.h>
#include "gfx.h"

#define R3D_MAX_VERTS 98304
#define R3D_MAX_TRIS 65536
#define R3D_MAX_DRAWN 65536 /* triangles on screen in one frame, after clipping */

typedef struct
{
   int32_t x, y, z;
} r3d_vec;

/* A built piece of the scene: its first triangle and how many. */
typedef struct
{
   int32_t first, count;
} r3d_mesh;

/* ---- building (once, before playing) ---- */

/* Forgets every triangle. */
void r3d_clear(void);
/* Where the next pieces go: a place and a turn around y (0..4095), until the next r3d_place. */
void r3d_place(int32_t x, int32_t y, int32_t z, int32_t yaw);
/* Starts and ends a mesh: the triangles added in between. */
void r3d_begin(void);
r3d_mesh r3d_end(void);
/* A box from (x0, y0, z0) to (x1, y1, z1), in the place's own axes; skip: faces left out (R3D_SKIP_*). */
enum { R3D_SKIP_BOTTOM = 1, R3D_SKIP_TOP = 2, R3D_SKIP_X = 4, R3D_INSIDE = 8, R3D_SKIP_Z = 16, R3D_SKIP_SIDES = R3D_SKIP_X | R3D_SKIP_Z };
/* Only the faces toward -z and +z: a picture, a screen, a window pane. */
#define R3D_PANE (R3D_SKIP_BOTTOM | R3D_SKIP_TOP | R3D_SKIP_X)
void r3d_box(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1, uint32_t color, int32_t flags);
/* A cylinder standing on (x, y, z): radius r, height h, `sides` faces around (3..32); r_top for a cone or a cup. */
void r3d_cylinder(int32_t x, int32_t y, int32_t z, int32_t r, int32_t r_top, int32_t h, int32_t sides, uint32_t color);
/* A low-poly ball (a leaf, a lamp's bulb, a head): center, radii, `rings` rows (2..12). */
void r3d_ball(int32_t x, int32_t y, int32_t z, int32_t rx, int32_t ry, int32_t rz, int32_t rings, uint32_t color);
/* A flat quad (a picture, a rug, a screen), one side, corners in order. */
void r3d_quad(r3d_vec a, r3d_vec b, r3d_vec c, r3d_vec d, uint32_t color);
/* Its colors take light (0) or glow by themselves (1: screens, lamps). */
void r3d_glow(int32_t on);
int32_t r3d_triangles(void);

/* ---- drawing (every frame) ---- */

typedef struct
{
   r3d_vec eye;            /* 16.16 meters */
   int32_t yaw, pitch;     /* 0..4095: yaw 0 looks toward +z, pitch > 0 looks down */
   int32_t fov;            /* the vertical field of view, 0..4095 (e.g. 768 is 67.5 degrees) */
   int32_t near_z;         /* 16.16 meters */
   uint32_t sky;           /* 0xRRGGBB behind everything */
   r3d_vec sun;            /* the light's direction, toward the light, any length */
   int32_t ambient, diffuse; /* 0..256 */
   uint32_t fog;           /* 0xRRGGBB */
   int32_t fog_near, fog_far; /* 16.16 meters; fog_far 0 = no fog */
} r3d_view;

/* What moves this frame: a mesh at a place and a turn. */
typedef struct
{
   r3d_mesh mesh;
   r3d_vec at;
   int32_t yaw;
   uint32_t tint; /* 0 as built, else 0xAARRGGBB mixed toward RGB by A */
} r3d_instance;

/* The scene (every built triangle) and the instances, seen from the view, into the surface. */
void r3d_draw(hd_surface *s, const r3d_view *v, const r3d_mesh *scene, const r3d_instance *inst, int32_t count);
/* How many triangles the last r3d_draw put on screen. */
int32_t r3d_drawn(void);

#endif
