/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * People for 3D worlds: a body of parts built once (actor3d_build) and posed
 * every frame (actor3d_place) into r3d instances. See docs/3d.md.
 */
#ifndef HD_ACTOR3D_H
#define HD_ACTOR3D_H

#include "r3d.h"

enum
{
   ACT_HIPS = 0,
   ACT_TORSO,
   ACT_HEAD,
   ACT_UPPER_L, ACT_UPPER_R,
   ACT_FORE_L, ACT_FORE_R,
   ACT_THIGH_L, ACT_THIGH_R,
   ACT_SHIN_L, ACT_SHIN_R,
   ACT_PARTS
};
/* The most instances a pose makes: every part, what it holds and the shadow. */
#define ACT_INSTANCES (ACT_PARTS + 2)

/* How a person looks (0xRRGGBB). */
typedef struct
{
   uint32_t skin, hair, top, top_detail, bottom, shoes, glasses;
} actor3d_look;

/* A person's parts; `shadow` is a soft round decal (built by the game, may be empty). */
typedef struct
{
   r3d_mesh part[ACT_PARTS];
   r3d_mesh shadow;
} actor3d_kit;

/* A pose: where and how the person stands this frame. */
typedef struct
{
   r3d_vec at;       /* the feet, 16.16 meters */
   int32_t floor_y;  /* where the shadow lies */
   int32_t yaw;      /* where the person faces: 0 is +z... the face looks toward -z of its own parts, so yaw 2048 faces +z */
   int32_t walk;     /* 0 standing, 16384 a full walk */
   int32_t phase;    /* the walk's (and the hands') cycle, 0..4095 */
   int32_t sit, type, wave; /* sitting, typing at a keyboard, waving the right arm */
   int32_t look, nod; /* the head turned and tilted */
   int32_t hold;      /* the arms forward at the chest: a mug, a watering can, a guitar, a controller */
   int32_t crouch;    /* the knees bent low: in front of the washing machine */
   int32_t lie;       /* lying on the back, the head toward where it faces' back; at.y is what it lies on (a bed, a lounger) */
   /*
    * What the hands hold (NULL: nothing): between both hands with `hold`, else
    * in the right one. Built around where it is gripped, its front toward -z
    * like the person's face; prop_pitch tilts it (a positive pitch raises its
    * front) and prop_roll turns it around its front (a guitar's neck up).
    */
   const r3d_mesh *prop;
   int32_t prop_pitch, prop_roll;
} actor3d_pose;

/* Builds the parts (with the fabric texture's id for the clothes, -1 none). */
void actor3d_build(actor3d_kit *k, const actor3d_look *look, int32_t fabric_tex);
/* The instances of a pose into out (up to ACT_INSTANCES); returns how many. */
int32_t actor3d_place(const actor3d_kit *k, const actor3d_pose *p, r3d_instance *out);

#endif
