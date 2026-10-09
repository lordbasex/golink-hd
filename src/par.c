/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Rows drawn on several cores: a pass over a big picture (720p and 1080p)
 * is cut into bands of rows, each done by its own thread. Every band writes
 * only its own rows and reads what does not change, so the picture is the
 * same pixel for pixel whatever the number of threads; small pictures (the
 * 360p screen) stay on one.
 */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "gfx.h"

#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#elif !defined(HD_NO_THREADS)
#include <pthread.h>
#include <unistd.h>
#endif

/* Under this many pixels a pass is not worth the threads. */
#define PAR_MIN_PIXELS (1280 * 720)
#define PAR_MAX 8

typedef struct
{
   hd_rows_fn fn;
   void *ctx;
   int32_t y0, y1;
} band;

static int32_t cores(void)
{
   static int32_t n;
   if (!n)
   {
#if defined(_WIN32)
      SYSTEM_INFO si;
      GetSystemInfo(&si);
      n = (int32_t)si.dwNumberOfProcessors;
#elif !defined(HD_NO_THREADS)
      long c = sysconf(_SC_NPROCESSORS_ONLN);
      n = c > 0 ? (int32_t)c : 1;
#else
      n = 1;
#endif
      n = hd_clamp(n, 1, 4); /* the host also encodes the video and runs other rooms */
   }
   return n;
}

#if defined(_WIN32)
static unsigned __stdcall run_band(void *arg)
{
   band *b = (band *)arg;
   b->fn(b->ctx, b->y0, b->y1);
   return 0;
}
#elif !defined(HD_NO_THREADS)
static void *run_band(void *arg)
{
   band *b = (band *)arg;
   b->fn(b->ctx, b->y0, b->y1);
   return NULL;
}
#endif

void hd_rows(int32_t w, int32_t h, hd_rows_fn fn, void *ctx)
{
   band bands[PAR_MAX];
   int32_t n = (int64_t)w * h < PAR_MIN_PIXELS ? 1 : cores(), k;
#if defined(_WIN32)
   HANDLE th[PAR_MAX];
#elif !defined(HD_NO_THREADS)
   pthread_t th[PAR_MAX];
   int started[PAR_MAX];
#endif
   if (n <= 1 || h < n)
   {
      fn(ctx, 0, h);
      return;
   }
   for (k = 0; k < n; k++)
   {
      bands[k].fn = fn;
      bands[k].ctx = ctx;
      bands[k].y0 = (int32_t)((int64_t)h * k / n);
      bands[k].y1 = (int32_t)((int64_t)h * (k + 1) / n);
   }
   /* the other bands on new threads, the first on this one; a thread that cannot start runs here */
#if defined(_WIN32)
   for (k = 1; k < n; k++)
   {
      th[k] = (HANDLE)_beginthreadex(NULL, 0, run_band, &bands[k], 0, NULL);
      if (!th[k])
         run_band(&bands[k]);
   }
   run_band(&bands[0]);
   for (k = 1; k < n; k++)
      if (th[k])
      {
         WaitForSingleObject(th[k], INFINITE);
         CloseHandle(th[k]);
      }
#elif !defined(HD_NO_THREADS)
   for (k = 1; k < n; k++)
   {
      started[k] = pthread_create(&th[k], NULL, run_band, &bands[k]) == 0;
      if (!started[k])
         run_band(&bands[k]);
   }
   run_band(&bands[0]);
   for (k = 1; k < n; k++)
      if (started[k])
         pthread_join(th[k], NULL);
#else
   for (k = 0; k < n; k++)
      fn(ctx, bands[k].y0, bands[k].y1);
#endif
}
