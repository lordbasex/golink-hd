/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The flat of room3d.c, shared by the 3D demo and the games played in it:
 * its scene, its light and the people built for it. hd_r3d_build() makes
 * them once (and bakes the light, so r3d_solid_at and r3d_reach work).
 */
#ifndef HD_ROOM3D_H
#define HD_ROOM3D_H

#include "r3d.h"
#include "actor3d.h"
#include "pet3d.h"

/* The flat's corner (x 0, z 0 of its plan) in the world, 16.16 meters. */
#define ROOM3D_BASE FX(40)
/* A point of the flat's plan in centimeters, in the world (16.16 meters). */
#define ROOM3D_AT(cm) (ROOM3D_BASE + FX_FRAC(cm, 100))

enum { ROOM3D_WORKER = 0, ROOM3D_FLATMATE, ROOM3D_PEOPLE };

const r3d_mesh *room3d_scene(void);
const r3d_light_cfg *room3d_light(void);
const actor3d_kit *room3d_person(int32_t who);
/* What people hold (actor3d_pose.prop). */
enum { ROOM3D_PROP_MUG = 0, ROOM3D_PROP_PAN, ROOM3D_PROP_CAN, ROOM3D_PROP_BOWL, ROOM3D_PROP_PAD, ROOM3D_PROP_GUITAR,
       ROOM3D_PROP_WATERING, ROOM3D_PROP_PARCEL, ROOM3D_PROP_HEADPHONES,
       ROOM3D_PROP_SHOWER, /* the shower's water: streaks 28 cm apart from its top down 140 cm; move it down a little each frame */
       ROOM3D_PROPS };
/* Where the shower's water falls in the plan (centimeters, under its head and inside the tub), and from how high. */
#define ROOM3D_SHOWER_X 640
#define ROOM3D_SHOWER_Z 1208
#define ROOM3D_SHOWER_Y 175
const r3d_mesh *room3d_prop(int32_t which);
/* The house's cat. */
const pet3d_kit *room3d_cat(void);
/* The texture of one of the office desk's two screens (0, 1), for tex3d_terminal. */
int32_t room3d_desk_screen(int32_t which);

#endif
