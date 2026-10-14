/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * TELETRABAJO: CÁMARA APAGADA (docs/game-remote-work.md): the game's state
 * (inside hd_state, see HD_GAME3D), its tuning, its activities and its texts.
 */
#ifndef HD_REMOTE_H
#define HD_REMOTE_H

#include "hd.h"

/* The activities, each at its place in the house (remote.c, `activities`). */
enum
{
   RA_WORK = 0, /* at the desk, for as long as the player wants */
   RA_NAP,
   RA_COFFEE,
   RA_COOK,
   RA_FRIDGE,
   RA_EAT,
   RA_SERIES,
   RA_CONSOLE,
   RA_GUITAR,
   RA_SUN,
   RA_PLANTS,
   RA_SHOWER,
   RA_WASHING,
   RA_COUNT
};

/* The game's state: 32-bit fields only (a save state is these words). */
typedef struct
{
   int32_t t;                  /* frames since the day began */
   int32_t x, z, yaw;          /* the worker's feet (16.16 meters) and where it faces (actor3d_pose) */
   int32_t speed;              /* 16.16 meters a frame */
   int32_t walk, phase;        /* the walk's strength and cycle (actor3d_pose) */
   int32_t cam_yaw, cam_pitch; /* the follow camera's turn and tilt */
   int32_t cam_dist;           /* its distance now, 16.16 meters (play3d_follow) */
   int32_t cam_lift;           /* how much it rose over walls, added to the tilt (play3d_follow) */
   int32_t energy;             /* 0..REMOTE_ENERGY_MAX */
   int32_t work;               /* the work done, in half frames at full speed (REMOTE_WORK_UNIT a game minute) */
   int32_t points;
   int32_t act;                /* the activity under way + 1, 0: walking */
   int32_t act_t;              /* frames into it */
   int32_t act_gain;           /* the energy it gave so far */
   int32_t prompt;             /* the activity in reach + 1, 0: none (for the HUD) */
   uint32_t buttons;           /* last frame's buttons, to tell a press */
   int32_t done[RA_COUNT];     /* how many times each activity was done */
   int32_t toast, toast_t;     /* the last message (a text id + 1) and its frames left */
   int32_t toast_points;       /* the points shown with it */
   int32_t screen_scroll;      /* how far the code on the desk's screens went up (pixels) */
} hd_remote;

/* Every number that balances the game, in one place. */
#define REMOTE_FRAMES_PER_MINUTE 66            /* a game minute: 540 of them make the 10-minute day */
#define REMOTE_DAY_MINUTES 540                 /* 09:00 to 18:00 */
#define REMOTE_WORK_GOAL 360                   /* six hours of work */
#define REMOTE_WORK_UNIT (REMOTE_FRAMES_PER_MINUTE * 2) /* `work` for one minute at full speed */
#define REMOTE_ENERGY_MAX 1000
#define REMOTE_ENERGY_START 700
#define REMOTE_WORK_TIRED 300                  /* under it, work goes at half speed */
#define REMOTE_WORK_ASLEEP 100                 /* under it, the worker falls asleep on the keyboard */
#define REMOTE_WORK_DRAIN 2                    /* energy a game minute of work costs */
#define REMOTE_REPEAT_FULL 2                   /* an activity pays in full this many times, then a fifth */
#define REMOTE_WALK FX_FRAC(25, 1000)          /* 1.5 m/s */
#define REMOTE_RUN FX_FRAC(58, 1000)           /* 3.5 m/s */
#define REMOTE_ACCEL FX_FRAC(4, 1000)          /* speed gained or lost a frame */
#define REMOTE_TURN 220                        /* how fast the worker turns, of 4096 a frame */
#define REMOTE_RADIUS FX_FRAC(22, 100)         /* the worker's size on the floor */
#define REMOTE_CAM_DIST FX_FRAC(380, 100)      /* the follow camera's distance when nothing is in the way */
#define REMOTE_CAM_PITCH_MIN 250
#define REMOTE_CAM_PITCH_MAX 1000
#define REMOTE_TOAST_FRAMES 180                /* a message stays three seconds */

/* The game's texts, each in English, Spanish and Portuguese (remote_text.c). */
enum
{
   RT_TITLE = 0,
   RT_HINT_WALK,
   RT_WORK,          /* the HUD's labels */
   RT_ENERGY,
   RT_POINTS,
   RT_STOP,          /* "A: stop" while doing something */
   RT_ASLEEP,        /* asleep on the keyboard */
   RT_PROMPT,        /* RA_COUNT prompts: "take a nap" */
   RT_DOING = RT_PROMPT + RA_COUNT,  /* RA_COUNT: "napping..." */
   RT_DONE = RT_DOING + RA_COUNT,    /* RA_COUNT: the line when it ends */
   RT_AGAIN = RT_DONE + RA_COUNT,    /* an activity done too often */
   RT_COUNT
};
const char *remote_text(int32_t id);

#endif
