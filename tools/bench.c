/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * bench: how long the engine takes per frame (step, draw and sound), for the
 * platformer and each showcase scene, against the frame budget of
 * go-link-hd.md (4 ms for the engine on the host). Built with -O2 and no
 * sanitizers: `make bench`. Exits 1 when a scene's average is over --limit ms.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hd.h"

static double now_ms(void)
{
   struct timespec t;
   timespec_get(&t, TIME_UTC);
   return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static uint32_t fb[HD_MAX_W * HD_MAX_H];
static int16_t audio[HD_SAMPLES_PER_FRAME * 2];
static hd_state s;

static double run(int scene, int frames, double *worst)
{
   hd_input in[MAX_PLAYERS];
   double total = 0;
   int f;
   *worst = 0;
   for (f = 0; f < frames; f++)
   {
      double t0;
      memset(in, 0, sizeof in);
      in[0].buttons = (f < 4 && scene < 0) ? PAD_START : (PAD_B | PAD_RIGHT | ((f % 40) < 2 ? PAD_JUMP : 0));
      t0 = now_ms();
      hd_step(&s, in);
      hd_draw(&s, fb);
      hd_mix(&s, audio, 1);
      t0 = now_ms() - t0;
      if (f >= 30) /* past the first frames (caches, the scene's own start) */
      {
         total += t0;
         if (t0 > *worst)
            *worst = t0;
      }
   }
   return total / (frames - 30);
}

int main(int argc, char **argv)
{
   static const char *names[] = { "mode 7", "road", "cave (lights, bloom, A*, bones)", "sea (waves, grade, low pass)", "colors (grade, zoom, bloom, blur)" };
   double limit = argc > 2 && !strcmp(argv[1], "--limit") ? atof(argv[2]) : 1e9, avg, worst;
   int sc, over = 0, frames = 300;
   hd_static_init();
   hd_reset(&s);
   avg = run(-1, frames, &worst);
   printf("%-38s %6.2f ms average, %6.2f ms worst\n", "platformer", avg, worst);
   over |= avg > limit;
   for (sc = 0; sc < 5; sc++)
   {
      hd_reset(&s);
      hd_show_start(&s);
      while (s.show.scene != sc)
      {
         hd_input in[MAX_PLAYERS];
         memset(in, 0, sizeof in);
         in[0].buttons = (s.frame & 1) ? 0 : PAD_R;
         hd_step(&s, in);
      }
      while (s.show.trans)
      {
         hd_input in[MAX_PLAYERS];
         memset(in, 0, sizeof in);
         hd_step(&s, in);
      }
      if (sc == 4)
      {
         s.show.bloom = 1;
         s.show.blur = 2;
         s.show.zoom = 160;
      }
      avg = run(sc, frames, &worst);
      printf("%-38s %6.2f ms average, %6.2f ms worst\n", names[sc], avg, worst);
      over |= avg > limit;
   }
   return over;
}
