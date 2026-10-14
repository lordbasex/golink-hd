/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * TELETRABAJO: CÁMARA APAGADA (docs/game-remote-work.md): the game's state
 * (inside hd_state, see HD_GAME3D), its tuning and its texts.
 */
#ifndef HD_REMOTE_H
#define HD_REMOTE_H

#include "hd.h"

/* The game's state: 32-bit fields only (a save state is these words). */
typedef struct
{
   int32_t t;                 /* frames since the day began */
   int32_t x, z, yaw;         /* the worker's feet (16.16 meters) and where it faces (actor3d_pose) */
   int32_t speed;             /* 16.16 meters a frame */
   int32_t walk, phase;       /* the walk's strength and cycle (actor3d_pose) */
   int32_t cam_yaw, cam_pitch; /* the follow camera's turn and tilt */
   int32_t cam_dist;          /* its distance now, 16.16 meters (play3d_follow) */
   int32_t cam_lift;          /* how much it rose over walls, added to the tilt (play3d_follow) */
} hd_remote;

/* Every number that balances the game, in one place. */
#define REMOTE_FRAMES_PER_MINUTE 66            /* a game minute: 540 of them make the 10-minute day */
#define REMOTE_WALK FX_FRAC(25, 1000)          /* 1.5 m/s */
#define REMOTE_RUN FX_FRAC(58, 1000)           /* 3.5 m/s */
#define REMOTE_ACCEL FX_FRAC(4, 1000)          /* speed gained or lost a frame */
#define REMOTE_TURN 220                        /* how fast the worker turns, of 4096 a frame */
#define REMOTE_RADIUS FX_FRAC(22, 100)         /* the worker's size on the floor */
#define REMOTE_CAM_DIST FX_FRAC(380, 100)      /* the follow camera's distance when nothing is in the way */
#define REMOTE_CAM_PITCH_MIN 250
#define REMOTE_CAM_PITCH_MAX 1000

/* The game's texts, each in English, Spanish and Portuguese (remote_text.c). */
enum
{
   RT_TITLE = 0,
   RT_HINT_WALK,
   RT_COUNT
};
const char *remote_text(int32_t id);

#endif
