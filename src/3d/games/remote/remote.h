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
   RA_DOOR,    /* only while the bell rings */
   RA_CAT,     /* wherever the cat rests */
   RA_PHONES,  /* the headphones on the desk, only while the drill goes */
   RA_COUNT
};

/* The day's events (section 5 of the design): the boss's, then the house's. */
enum
{
   RE_MEETING = 0, /* the daily meeting at 10:00 */
   RE_CALL,        /* a surprise video call */
   RE_MESSAGE,     /* an urgent message */
   RE_SHARE,       /* "share your screen" */
   RE_HOTFIX,      /* production is down: deploy a fix, at the desk until it is out */
   RE_WEEKEND,     /* "can you work this weekend?": a question with two answers */
   RE_CAMERA,      /* a video call with the camera on: the boss sees what goes on behind */
   RE_BOSS_KINDS,
   RE_DELIVERY = RE_BOSS_KINDS, /* the bell rings */
   RE_CAT,         /* the cat goes for the keyboard */
   RE_DRILL,       /* the neighbor's drill */
   RE_NET,         /* the internet drops */
   RE_MATE,        /* the flatmate comes back early */
   RE_KINDS
};
#define REMOTE_EVENTS_MAX 12

/* How the day ended (`over`). */
enum { REND_PLAYING = 0, REND_FIRED, REND_MISSED, REND_SURVIVED };

/* The chat status the boss sees. */
enum { RS_ONLINE = 0, RS_AWAY, RS_CALL, RS_MEETING, RS_OFFLINE, RS_DEPLOY, RS_COUNT };

/* Who wrote a message on the phone. */
enum { RW_BOSS = 0, RW_YOU, RW_CAT, RW_MATE, RW_HOUSE, RW_COUNT };
#define REMOTE_PHONE 8 /* the messages the phone keeps */

/* The screens (section 7 of the design). */
enum { RSC_TITLE = 0, RSC_HELP, RSC_MORNING, RSC_DAY, RSC_END, RSC_CREDITS };
#define REMOTE_HELP_PAGES 3

/* What the cat is doing (`cat_mode`). */
enum { CAT_REST = 0, CAT_WALK, CAT_UP, CAT_DOWN };

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
   /* phase 3: the day */
   int32_t suspicion;          /* 0..REMOTE_SUSPICION_MAX: the boss's */
   int32_t away_t;             /* frames since the worker left the desk */
   int32_t over, over_t;       /* how the day ended (REND_*) and the frames since */
   int32_t score;              /* the day's score, once it ended */
   int32_t ev_count, ev_next;  /* the day's events and the next one to come */
   int32_t ev_at[REMOTE_EVENTS_MAX];   /* the game minute each one comes */
   int32_t ev_kind[REMOTE_EVENTS_MAX]; /* RE_* */
   int32_t boss;               /* the boss event under way + 1, 0: none */
   int32_t boss_call;          /* 0: waiting for the worker at the desk, 1: on the call */
   int32_t boss_t;             /* frames left to answer, or of the call */
   int32_t boss_limit;         /* the frames there were to answer (for the countdown bar) */
   int32_t boss_noise;         /* what the boss already heard on this call (one bit each) */
   int32_t bell_t;             /* frames the delivery still waits at the door */
   int32_t bell_owed;          /* 1: the delivery comes back during the next call */
   int32_t drill_t, net_t;     /* frames left of the drill and of no internet */
   int32_t washer_t;           /* frames the washing machine still spins */
   int32_t headphones;         /* 1: on, the drill is far away */
   int32_t cat_x, cat_z, cat_y, cat_yaw; /* the cat (16.16 meters, y over the floor) */
   int32_t cat_walk, cat_phase;
   int32_t cat_mode, cat_t;    /* CAT_*, and frames left resting or hopping */
   int32_t cat_spot, cat_goal; /* where it is (or was) and where it goes, in the cat's spots */
   int32_t cat_keys;           /* 0: free, 1: going for the keyboard, 2: on it */
   int32_t cat_keys_t;         /* frames on the keyboard */
   int32_t cat_logged;         /* 1: it already logged the worker off this time */
   int32_t mate;               /* 1: the flatmate is home */
   int32_t mate_x, mate_z, mate_yaw, mate_walk, mate_phase;
   int32_t mate_goal, mate_t;  /* where it goes, and frames left where it stands */
   int32_t mate_talk_t;        /* frames until it talks again */
   int32_t phone_n;            /* how many messages came in all (the last REMOTE_PHONE are kept) */
   int32_t phone_text[REMOTE_PHONE], phone_who[REMOTE_PHONE], phone_min[REMOTE_PHONE];
   int32_t phone_new_t;        /* frames the phone still shows its newest message bright */
   /* phase 4: the screens */
   int32_t screen, screen_t;   /* RSC_*, and frames on it */
   int32_t menu;               /* the chosen line of the title's or the pause's menu */
   int32_t help_page, help_back; /* the page of how to play, and the screen it goes back to */
   int32_t paused;             /* 1: the day is paused (its menu over it) */
   int32_t attract;            /* 1: a bot plays a short day while the title waits (the demo) */
   int32_t attract_goal, attract_t; /* the demo bot's next place and frames into what it does */
   int32_t best;               /* the best score of this session (the host keeps it between sessions later) */
   int32_t hero;               /* who plays: 0 a man, 1 a woman (the title's menu; the model, the voice and the texts' gender) */
   int32_t best_new;           /* 1: this day's score beat it */
   int32_t nav;                /* last frame's menu directions (the stick as the D-pad) */
   /* phase 5: the sound */
   int32_t music_resume;       /* where the day's music was when a call took over (it goes on from there) */
   /* more of the boss */
   int32_t weekend;            /* the weekend question: 0 not asked or not answered, 1 "of course" (sold), 2 "I have plans" */
   int32_t hotfixes;           /* hotfixes deployed today */
   int32_t chatter_at;         /* the game minute the boss writes something that needs no answer */
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
#define REMOTE_SUSPICION_MAX 1000              /* the boss's patience: fired at once */
#define REMOTE_AWAY_GRACE 10                   /* game minutes away before the status says "away" */
#define REMOTE_AWAY_RISE 2                     /* suspicion a game minute away after that */
#define REMOTE_ASLEEP_RISE 3                   /* suspicion a game minute asleep on the keyboard */
#define REMOTE_WORK_CALM 1                     /* suspicion real work takes away, */
#define REMOTE_WORK_CALM_EVERY 3               /* every this many game minutes of it */
#define REMOTE_ANSWER_CALM 50                  /* how much of an event's "answered" calm counts, % */
#define REMOTE_NOISE 100                       /* suspicion when the boss hears something on a call */
#define REMOTE_CAT_LOGOFF (20 * 60)            /* frames on the keyboard until the cat logs the worker off */
#define REMOTE_OVERTIME 420                    /* work minutes after which each one costs points */
#define REMOTE_WASHER_MINUTES 45               /* the washing machine spins after it is loaded */
#define REMOTE_DRILL_MINUTES 30
#define REMOTE_NET_MINUTES 10
#define REMOTE_BELL_FRAMES (30 * 60)           /* the delivery waits half a minute */
#define REMOTE_ATTRACT_AFTER (10 * 60)         /* the title waits this long for START before the demo plays */
#define REMOTE_ATTRACT_FRAMES (50 * 60)        /* how long the demo plays */
#define REMOTE_MORNING_FRAMES 270              /* the alarm, getting up and the camera flying to the worker */

#define REMOTE_HEARD 8   /* what the boss can hear or see on a call (boss_noise has a bit each) */
#define REMOTE_CHATTER 10
#define REMOTE_WEEKEND_COST 250 /* points the weekend sold takes from the score */
#define REMOTE_WEEKEND_NO 60    /* suspicion for "I have plans" */
#define REMOTE_HOTFIX_POINTS 150

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
   RT_SUSPICION,
   RT_STATUS,        /* RS_COUNT: RS_* */
   RT_WHO = RT_STATUS + RS_COUNT,    /* RW_COUNT: who wrote a message */
   RT_ON_CALL = RT_WHO + RW_COUNT,   /* "on a call" instead of "A: stop" */
   RT_CAT_OFF,       /* "A: get the cat off" */
   RT_ANSWER,        /* "answer at the desk" with the countdown */
   RT_GREETING,      /* the boss's good morning */
   RT_BOSS_ASK,      /* RE_BOSS_KINDS x 2: the boss asks */
   RT_BOSS_YES = RT_BOSS_ASK + RE_BOSS_KINDS * 2, /* RE_BOSS_KINDS: the worker answers */
   RT_BOSS_MISS = RT_BOSS_YES + RE_BOSS_KINDS,    /* RE_BOSS_KINDS: the boss, unanswered */
   RT_HEARD = RT_BOSS_MISS + RE_BOSS_KINDS,       /* REMOTE_HEARD: what the boss hears on a call (washer, bell, flatmate, drill, snoring), then sees on camera (the cat, the flatmate, the worker asleep) */
   RT_BELL = RT_HEARD + REMOTE_HEARD, /* the house's events */
   RT_BELL_GONE,
   RT_CAT_KEYS,
   RT_CAT_TYPED,
   RT_CAT_LOGOFF,
   RT_CAT_MOVED,
   RT_DRILL,
   RT_DRILL_END,
   RT_NET,
   RT_NET_BACK,
   RT_MATE_HOME,
   RT_MATE_TALK,     /* 4 */
   RT_WASHER = RT_MATE_TALK + 4,
   RT_END_TIME = RT_WASHER + 1,      /* 18:00 */
   RT_END_FIRED_MSG,
   RT_END_TITLE,     /* 5: fired, sofa intern, model employee, remote ninja, legend */
   RT_END_POINTS = RT_END_TITLE + 5,
   RT_END_SURVIVED,
   RT_END_DISCRETION,
   RT_END_OVERTIME,
   RT_END_HALF,
   RT_END_TOTAL,
   RT_END_AGAIN,
   RT_END_MENU,      /* "A: menu" */
   RT_NEW_RECORD,
   RT_RECORD,        /* "Record" */
   RT_MENU_PLAY,     /* the title's menu */
   RT_MENU_HELP,
   RT_MENU_LANGUAGE, /* "Language" (the language's own name follows) */
   RT_MENU_CREDITS,
   RT_PRESS_START,
   RT_DEMO,
   RT_HELP_TITLE,    /* REMOTE_HELP_PAGES */
   RT_HELP_LINE = RT_HELP_TITLE + REMOTE_HELP_PAGES, /* REMOTE_HELP_PAGES x 4 */
   RT_HELP_NEXT = RT_HELP_LINE + REMOTE_HELP_PAGES * 4,
   RT_HELP_DONE,
   RT_MORNING_RING,
   RT_MORNING_LINE,
   RT_SKIP,
   RT_PAUSE,         /* the pause's title and menu */
   RT_PAUSE_GO_ON,
   RT_PAUSE_RESTART,
   RT_PAUSE_QUIT,
   RT_WEEKEND_CHOICE, /* "A: of course, boss   X: I have plans" */
   RT_WEEKEND_NO,     /* the worker's answer, and the boss's */
   RT_WEEKEND_NO_BOSS,
   RT_HOTFIX_DONE,
   RT_END_WEEKEND,    /* the end's row: the weekend sold */
   RT_CALL_BOSS,      /* the video call's window: the boss's name, the camera on, off, deploying */
   RT_CAM_ON,
   RT_CAM_OFF,
   RT_DEPLOYING,
   RT_CHATTER,        /* REMOTE_CHATTER: the boss writing what needs no answer */
   RT_MENU_HERO = RT_CHATTER + REMOTE_CHATTER, /* "Main character" (the choice follows) */
   RT_HERO,           /* 2: a man, a woman */
   RT_COUNT = RT_HERO + 2
};
const char *remote_text(int32_t id);
/* Whose texts: 0 a man's, 1 a woman's (Spanish and Portuguese say some words otherwise); the game sets it from hd_remote.hero. */
extern int32_t remote_text_hero;

/* A day straight away, without the title or the morning (tests and tools; the game itself starts at the title, hd_remote_start). */
void hd_remote_new_day(hd_state *s);

/*
 * The places the day uses, for bots and tests: where the worker starts
 * activity `a`, the floor beside the cat's spot `a`, the flatmate's spot `a`
 * (centimeters of the plan). Each returns 0 past the last one.
 */
enum { REMOTE_PLACE_ACTIVITY = 0, REMOTE_PLACE_CAT, REMOTE_PLACE_MATE };
int32_t remote_place(int32_t kind, int32_t a, int32_t *x, int32_t *z);

#endif
