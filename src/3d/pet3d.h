/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Pets for 3D worlds: a cat of parts built once (pet3d_build) and posed
 * every frame (pet3d_place) into r3d instances, like the people of
 * actor3d.h. See docs/3d.md.
 */
#ifndef HD_PET3D_H
#define HD_PET3D_H

#include "r3d.h"

enum
{
   PET_BODY = 0,
   PET_HEAD,
   PET_TAIL, PET_TAIL_TIP,
   PET_LEG_FL, PET_LEG_FR, PET_LEG_BL, PET_LEG_BR,
   PET_PARTS
};
/* The most instances a pose makes: every part and the shadow. */
#define PET_INSTANCES (PET_PARTS + 1)

/* How a cat looks (0xRRGGBB): its fur, the stripes over it, the chest and paws, the eyes. */
typedef struct
{
   uint32_t fur, stripes, white, eyes, nose;
} pet3d_look;

typedef struct
{
   r3d_mesh part[PET_PARTS];
   r3d_mesh shadow;
} pet3d_kit;

/* A pose: where and how the cat is this frame. */
typedef struct
{
   r3d_vec at;      /* the paws, 16.16 meters */
   int32_t floor_y; /* where the shadow lies */
   int32_t yaw;     /* as actor3d: the face looks along -z of its own parts, so yaw 2048 faces +z */
   int32_t walk;    /* 0 standing, 16384 a full trot */
   int32_t phase;   /* the walk's (and the tail's) cycle, 0..4095 */
   int32_t sit;     /* sitting up on its back legs */
   int32_t lie;     /* lying as a loaf, the paws under it */
   int32_t look;    /* the head turned */
} pet3d_pose;

/* Builds the parts; `shadow` stays empty (the game may set one). */
void pet3d_build(pet3d_kit *k, const pet3d_look *look);
/* The instances of a pose into out (up to PET_INSTANCES); returns how many. */
int32_t pet3d_place(const pet3d_kit *k, const pet3d_pose *p, r3d_instance *out);

#endif
