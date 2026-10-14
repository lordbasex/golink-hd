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

/* The flat's corner (x 0, z 0 of its plan) in the world, 16.16 meters. */
#define ROOM3D_BASE FX(40)
/* A point of the flat's plan in centimeters, in the world (16.16 meters). */
#define ROOM3D_AT(cm) (ROOM3D_BASE + FX_FRAC(cm, 100))

enum { ROOM3D_WORKER = 0, ROOM3D_FLATMATE, ROOM3D_PEOPLE };

const r3d_mesh *room3d_scene(void);
const r3d_light_cfg *room3d_light(void);
const actor3d_kit *room3d_person(int32_t who);
/* The texture of one of the office desk's two screens (0, 1), for tex3d_terminal. */
int32_t room3d_desk_screen(int32_t which);

#endif
