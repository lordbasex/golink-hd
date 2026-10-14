/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * TELETRABAJO: CÁMARA APAGADA's balance, measured: bots that play whole days
 * the way people do (they walk the house along play3d_route, so the way to
 * the desk takes its time, react to the boss after a while, rest when tired,
 * answer the door, take the cat off the keyboard, put the headphones on),
 * each in its own style, over many days. It prints how often each style
 * survives, misses the hours or is fired, its score and how high the boss's
 * suspicion got. See docs/game-remote-work.md, section 13 (phase 6).
 *
 *   make remotebots              four styles, 50 days each
 *   tools/remotebots [DAYS] [STYLE] [-v]
 *
 * Every number comes from the game's own rules (remote.h), so after changing
 * one, run it again: a careful player should survive most days, a lazy one
 * should not, and the boss's suspicion should move.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hd.h"
#include "room3d.h"
#include "play3d.h"
#include "games/remote/remote.h"

typedef struct
{
   const char *name;
   int32_t rest_below, rest_until; /* rests when energy is under the first, until over the second */
   int32_t react;                  /* frames before it notices the boss (up to twice as many) */
   int32_t bell, cat, phones;      /* answers the door, takes the cat off the keys, puts the headphones on */
   int32_t slack;                  /* wanders off to the fun things now and then */
   int32_t weekend;                /* the weekend question: 1 "of course", 2 "I have plans" */
   int32_t rests[4];               /* what it does to rest */
} style;

static const style styles[] = {
   { "careful", 350, 750, 30, 1, 1, 1, 0, 2, { RA_NAP, RA_COOK, RA_EAT, RA_COFFEE } },
   { "casual", 300, 600, 120, 1, 1, 0, 0, 2, { RA_COFFEE, RA_SERIES, RA_EAT, RA_FRIDGE } },
   { "slacker", 500, 900, 240, 0, 1, 0, 1, 2, { RA_SERIES, RA_CONSOLE, RA_GUITAR, RA_SUN } },
   { "workaholic", 120, 300, 60, 0, 1, 1, 0, 1, { RA_COFFEE, RA_COFFEE, RA_FRIDGE, RA_COFFEE } },
};
#define STYLES ((int)(sizeof styles / sizeof styles[0]))

static hd_state st;
static uint32_t bot_rng; /* the bots' own dice, apart from the game's */

static int32_t dice(int32_t n)
{
   bot_rng ^= bot_rng << 13;
   bot_rng ^= bot_rng >> 17;
   bot_rng ^= bot_rng << 5;
   return (int32_t)(bot_rng % (uint32_t)n);
}

static int by_value(const void *a, const void *b)
{
   return *(const int32_t *)a - *(const int32_t *)b;
}

/* The stick toward (tx, tz) along the house's ways, as the camera sees it, running. */
static void walk_to(const hd_remote *g, hd_input *in, int32_t tx, int32_t tz)
{
   int32_t dx, dz;
   if (!play3d_route(2, g->x, g->z, tx, tz, &dx, &dz))
   {
      int32_t l = play3d_sqrt((int64_t)(tx - g->x) * (tx - g->x) + (int64_t)(tz - g->z) * (tz - g->z));
      if (!l)
         return;
      dx = (int32_t)((int64_t)(tx - g->x) * 16384 / l);
      dz = (int32_t)((int64_t)(tz - g->z) * 16384 / l);
   }
   in->lx = hd_clamp((int32_t)(((int64_t)dx * hd_cos(g->cam_yaw) - (int64_t)dz * hd_sin(g->cam_yaw)) >> 14) * 2, -32767, 32767);
   in->ly = hd_clamp(-(int32_t)(((int64_t)dx * hd_sin(g->cam_yaw) + (int64_t)dz * hd_cos(g->cam_yaw)) >> 14) * 2, -32767, 32767);
   in->buttons |= PAD_RUN;
}

/* One day of a style; returns the highest suspicion it saw. */
static int32_t play_day(const style *sty, uint32_t seed)
{
   hd_remote *g = HD_GAME3D(&st, hd_remote);
   int32_t f, want = RA_WORK, wait = 0, last_boss = 0, resting = 0, rest_pick = 0, slack_t = 0, peak = 0;
   uint32_t prev = 0;
   hd_reset(&st);
   st.rng = seed * 2654435761u;
   hd_remote_new_day(&st);
   bot_rng = seed * 7919u + 1;
   for (f = 0; f < 60 * 700 && !g->over; f++)
   {
      hd_input in[MAX_PLAYERS];
      int32_t a = g->act - 1, press = 0, urgent;
      memset(in, 0, sizeof in);
      if (g->boss && !last_boss)
         wait = sty->react + dice(sty->react + 1);
      last_boss = g->boss;
      if (wait > 0)
         wait--;
      urgent = g->boss && !g->boss_call && wait == 0;
      if (g->act)
      {
         /* doing something: stop it when the boss calls, when rested, or when something at home needs the worker */
         if (a == RA_WORK)
         {
            int free = !g->boss_call && !g->boss;
            if (g->boss == RE_WEEKEND + 1 && !g->boss_call && wait == 0)
            {
               /* the weekend question: A for "of course", X for "I have plans" */
               if (!(prev & (PAD_JUMP | PAD_RUN)))
                  in[0].buttons |= sty->weekend == 1 ? PAD_JUMP : PAD_RUN;
            }
            else if (g->cat_keys == 2 && sty->cat)
               press = 1;
            else if (free && g->energy < sty->rest_below)
               press = 1, resting = 1, rest_pick = sty->rests[dice(4)];
            else if (free && sty->slack && dice(3600) < 2)
               press = 1, slack_t = 600 + dice(1800);
            else if (free && g->bell_t > 600 && sty->bell)
               press = 1;
            else if (free && g->drill_t > 0 && !g->headphones && sty->phones)
               press = 1;
         }
         else if (urgent || (resting && g->energy >= sty->rest_until))
            press = 1;
      }
      else
      {
         int32_t px, pz;
         if (urgent || (g->boss && g->boss_call))
            want = RA_WORK;
         else if (g->bell_t > 300 && sty->bell)
            want = RA_DOOR;
         else if (g->drill_t > 0 && !g->headphones && sty->phones)
            want = RA_PHONES;
         else if (resting && g->energy < sty->rest_until)
            want = rest_pick;
         else if (slack_t > 0)
            slack_t--, want = sty->rests[(f / 1200) % 4];
         else
            resting = 0, want = RA_WORK;
         if (g->prompt == want + 1)
            press = 1;
         else if (remote_place(REMOTE_PLACE_ACTIVITY, want, &px, &pz))
            walk_to(g, &in[0], ROOM3D_AT(px), ROOM3D_AT(pz));
      }
      /* a press is a new press: the button let go between two */
      if (press && !(prev & PAD_JUMP))
         in[0].buttons |= PAD_JUMP;
      prev = in[0].buttons;
      hd_step(&st, in);
      peak = hd_max(peak, g->suspicion);
   }
   return peak;
}

int main(int argc, char **argv)
{
   int32_t days = 50, only = -1, verbose = 0, i, s;
   for (i = 1; i < argc; i++)
   {
      if (!strcmp(argv[i], "-v"))
         verbose = 1;
      else if (i == 1)
         days = hd_clamp(atoi(argv[i]), 1, 1000);
      else
         only = atoi(argv[i]);
   }
   hd_static_init();
   printf("%-10s %9s %7s %6s %9s %15s %8s %16s\n", "style", "survived", "missed", "fired", "work", "score p10/50/90", "", "peak suspicion");
   for (s = 0; s < STYLES; s++)
   {
      static int32_t scores[1000], peaks[1000];
      int32_t survived = 0, missed = 0, fired = 0;
      int64_t work = 0;
      const hd_remote *g = HD_GAME3D_C(&st, hd_remote);
      if (only >= 0 && s != only)
         continue;
      for (i = 0; i < days; i++)
      {
         peaks[i] = play_day(&styles[s], (uint32_t)i + 1);
         scores[i] = g->score;
         work += g->work / REMOTE_WORK_UNIT;
         survived += g->over == REND_SURVIVED;
         missed += g->over == REND_MISSED;
         fired += g->over == REND_FIRED;
         if (verbose)
            printf("  day %d: end %d, score %d, worked %d min, suspicion %d (peak %d)\n", (int)i + 1, (int)g->over, (int)g->score,
                   (int)(g->work / REMOTE_WORK_UNIT), (int)g->suspicion, (int)peaks[i]);
      }
      qsort(scores, (size_t)days, sizeof scores[0], by_value);
      qsort(peaks, (size_t)days, sizeof peaks[0], by_value);
      printf("%-10s %8d%% %6d%% %5d%% %5d min %5d/%d/%d %8s %7d (90%%: %d)\n", styles[s].name, (int)(survived * 100 / days), (int)(missed * 100 / days),
             (int)(fired * 100 / days), (int)(work / days), (int)scores[days / 10], (int)scores[days / 2], (int)scores[days * 9 / 10], "",
             (int)peaks[days / 2], (int)peaks[days * 9 / 10]);
   }
   return 0;
}
