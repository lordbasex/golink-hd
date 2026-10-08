/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Skeletal animation: bones in a tree, picture parts pinned to them and
 * animations as keys of each bone's angle, interpolated in between. A big,
 * smooth character from a few drawings, the same on every computer.
 */
#ifndef HD_BONES_H
#define HD_BONES_H

#include "gfx.h"

#define BONES_MAX 16
#define KEYS_MAX 12

typedef struct
{
   int32_t parent; /* -1 for the root */
   int32_t length; /* pixels to where its children start */
   int32_t angle;  /* at rest, from its parent's direction (0..4095; 1024 points down) */
} hd_bone;

typedef struct
{
   int32_t bone;          /* where it is pinned */
   const hd_image *im;
   int32_t px, py;        /* the pin, in the picture */
   int32_t turn;          /* the picture's own turn from the bone's direction */
   int32_t z;             /* drawing order: lower first */
} hd_part;

typedef struct
{
   int32_t frame;                /* when (frames from the start) */
   int16_t angle[BONES_MAX];     /* each bone's turn from rest */
   int16_t lift;                 /* the root moved up (pixels): a bob in the walk */
} hd_key;

typedef struct
{
   int32_t length; /* frames; the last key holds or loops to the first */
   int32_t loop;
   int32_t count;
   hd_key key[KEYS_MAX];
} hd_anim;

typedef struct
{
   int32_t bones, parts;
   hd_bone bone[BONES_MAX];
   hd_part part[BONES_MAX];
} hd_skeleton;

/* A pose: each bone's turn and the root's lift at time t of an animation. */
typedef struct
{
   int32_t angle[BONES_MAX];
   int32_t lift;
} hd_pose;

void bones_pose(const hd_anim *a, int32_t t, hd_pose *out);
/* Blends two poses: t 0..256 from a to b (turning the short way). */
void bones_blend(const hd_pose *a, const hd_pose *b, int32_t t, hd_pose *out);
/*
 * Draws a skeleton with its root at (x, y), scaled (16.16), mirrored when
 * flip; style applies to every part (outline, flash...).
 */
void bones_draw(hd_surface *s, const hd_skeleton *sk, const hd_pose *pose, int32_t x, int32_t y, int32_t scale, int32_t flip, const hd_style *style);

#endif
