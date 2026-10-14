/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Models made by artists, for 3D games: a package's .hdm files (made from
 * glTF by examples/remote/models/hdm.py), each a mesh of colored triangles,
 * and when it has bones, its skeleton and animations. Everything is in
 * integers, so a model moves the same on every computer: positions in 16.16
 * meters (y up, the model's front toward +z), turns as matrices of 2.14.
 *
 * A skinned model is drawn as a body: its own copy of the triangles in r3d,
 * moved every frame to its bones' pose (model3d_body_pose) and drawn as an
 * instance at the world's origin. See docs/3d.md.
 */
#ifndef HD_MODEL3D_H
#define HD_MODEL3D_H

#include <stddef.h>
#include "r3d.h"

#define MODEL3D_MAX 16        /* models a package holds */
#define MODEL3D_BONES 64
#define MODEL3D_CLIPS 16

/* A bone's turn and place: relative to its parent (local) or in the model (global). */
typedef struct
{
   int32_t r[9]; /* rows of 2.14 */
   r3d_vec t;    /* 16.16 meters */
} model3d_xf;

typedef struct model3d model3d;

/* Loads a model by name (the package's "models"); NULL, or what is wrong with the file. */
const char *model3d_load(const char *name, const uint8_t *data, size_t size);
/* Every model gone (another package, or the built-in game). */
void model3d_free_all(void);
/* The model of a name, NULL when the package has none. */
const model3d *model3d_find(const char *name);
/* Bumped whenever the models change: what was built from them is built again. */
extern int32_t model3d_gen;

/* What a model fills at rest: its box (16.16 m; it stands on y 0, centered in x and z). */
void model3d_bounds(const model3d *m, r3d_vec *lo, r3d_vec *hi);
/*
 * A model's triangles into the mesh being built (between r3d_begin and
 * r3d_end), as built at the place r3d_place set: scaled by `scale` (16.16
 * along the model's x, y and z), turned by `yaw` and moved to `center` in the
 * place's axes; a negative scale mirrors it. For furniture.
 */
void model3d_add(const model3d *m, r3d_vec scale, int32_t yaw, r3d_vec center);

int32_t model3d_bones(const model3d *m);
/* A bone by name (-1: none), its parent (-1: the root). */
int32_t model3d_bone(const model3d *m, const char *name);
int32_t model3d_parent(const model3d *m, int32_t bone);
/* A clip by name (-1: none) and its length in game frames (60 a second). */
int32_t model3d_clip(const model3d *m, const char *name);
int32_t model3d_clip_frames(const model3d *m, int32_t clip);
/* The bones' local turns and places at `time` (16.16 game frames) of a clip, going round; clip -1: at rest. */
void model3d_sample(const model3d *m, int32_t clip, int32_t time, model3d_xf *local);
/* A bone's place at rest, in the model, and its turn. */
const model3d_xf *model3d_rest(const model3d *m, int32_t bone);
/* Global from local (parents come before their children). */
void model3d_globals(const model3d *m, const model3d_xf *local, model3d_xf *global);

/* Matrices: out = a b; a turn applied to a point. */
void model3d_mul(const model3d_xf *a, const model3d_xf *b, model3d_xf *out);
r3d_vec model3d_apply(const int32_t r[9], r3d_vec v);

/* A model's triangles in r3d, posed every frame. */
typedef struct
{
   const model3d *model;
   r3d_mesh mesh;
   int32_t gen; /* model3d_gen when it was built */
} model3d_body;

/*
 * Builds the body's triangles (after r3d_bake, like whatever moves). Without
 * bones they stay as built at the place r3d_place set (furniture).
 */
void model3d_body_build(model3d_body *b, const model3d *m);
/*
 * Poses a skinned body: its bones' global pose, then the whole model turned
 * by `place` (rows of 2.14) and moved to `at`. Draw it as an instance with
 * its mesh at the origin (at 0, yaw 0).
 */
void model3d_body_pose(model3d_body *b, const model3d_xf *global, const int32_t place[9], r3d_vec at);

#endif
