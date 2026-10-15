/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Rows drawn on several cores: a pass over a big picture (720p and 1080p)
 * is cut into small bands of rows, which the threads take one after another
 * as each finishes its last (a part of the picture full of furniture costs
 * more than the ceiling, so equal shares would leave cores waiting). Every
 * band writes only its own rows and reads what does not change, so the
 * picture is the same pixel for pixel however it is cut; small pictures
 * (the 360p screen) stay on one thread.
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

#define PAR_BANDS 12 /* bands per thread: small enough to share the work out evenly */

/* One pass: its rows and the next band to take (taken by every thread at once). */
typedef struct
{
   hd_rows_fn fn;
   void *ctx;
   int32_t h, size;
   volatile long next;
} pass;

/* The first row of the next band to do (an atomic add: each band goes to one thread). */
static int32_t take(pass *p)
{
#if defined(_MSC_VER)
   return (int32_t)InterlockedExchangeAdd(&p->next, p->size);
#else
   return (int32_t)__sync_fetch_and_add(&p->next, (long)p->size);
#endif
}

static void work(pass *p)
{
   int32_t y;
   while ((y = take(p)) < p->h)
      p->fn(p->ctx, y, hd_min(p->h, y + p->size));
}

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
   work((pass *)arg);
   return 0;
}
#elif !defined(HD_NO_THREADS)
static void *run_band(void *arg)
{
   work((pass *)arg);
   return NULL;
}
#endif

void hd_rows(int32_t w, int32_t h, hd_rows_fn fn, void *ctx)
{
   pass p;
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
   p.fn = fn;
   p.ctx = ctx;
   p.h = h;
   p.size = hd_max(1, h / (n * PAR_BANDS));
   p.next = 0;
   /* the other threads start, this one works too; a thread that cannot start leaves its share to the others */
#if defined(_WIN32)
   for (k = 1; k < n; k++)
      th[k] = (HANDLE)_beginthreadex(NULL, 0, run_band, &p, 0, NULL);
   work(&p);
   for (k = 1; k < n; k++)
      if (th[k])
      {
         WaitForSingleObject(th[k], INFINITE);
         CloseHandle(th[k]);
      }
#elif !defined(HD_NO_THREADS)
   for (k = 1; k < n; k++)
      started[k] = pthread_create(&th[k], NULL, run_band, &p) == 0;
   work(&p);
   for (k = 1; k < n; k++)
      if (started[k])
         pthread_join(th[k], NULL);
#else
   work(&p);
#endif
}
