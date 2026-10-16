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

/*
 * The threads stay: started on the first big pass and asleep between
 * passes (a frame makes about ten), instead of a new thread for every
 * pass. A pass wakes them all, each takes bands until none is left, and
 * the caller, which works too, waits for the last. hd_rows_stop ends them
 * (golinkhd_destroy, before the library can be unloaded).
 */
#if defined(_WIN32)
typedef HANDLE pool_thread;
static CRITICAL_SECTION pool_lock;
static CONDITION_VARIABLE pool_go, pool_done;
static volatile LONG pool_ready; /* 0 not made, 1 being made, 2 ready */
#define POOL_LOCK() EnterCriticalSection(&pool_lock)
#define POOL_UNLOCK() LeaveCriticalSection(&pool_lock)
#define POOL_WAIT(c) SleepConditionVariableCS(&(c), &pool_lock, INFINITE)
#define POOL_WAKE_ALL(c) WakeAllConditionVariable(&(c))
#define POOL_WAKE(c) WakeConditionVariable(&(c))
#elif !defined(HD_NO_THREADS)
typedef pthread_t pool_thread;
static pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pool_go = PTHREAD_COND_INITIALIZER, pool_done = PTHREAD_COND_INITIALIZER;
#define POOL_LOCK() pthread_mutex_lock(&pool_lock)
#define POOL_UNLOCK() pthread_mutex_unlock(&pool_lock)
#define POOL_WAIT(c) pthread_cond_wait(&(c), &pool_lock)
#define POOL_WAKE_ALL(c) pthread_cond_broadcast(&(c))
#define POOL_WAKE(c) pthread_cond_signal(&(c))
#endif

#if defined(_WIN32) || !defined(HD_NO_THREADS)
static struct
{
   pool_thread th[PAR_MAX];
   int32_t n;      /* threads running (the caller is one more) */
   int32_t quit;   /* 1: the threads end */
   uint32_t gen;   /* bumped by every pass */
   int32_t busy;   /* threads still on the pass */
   pass *job;
} pool;
static volatile long pool_in_use; /* a pass under way (another thread's pass runs on its own) */

static void pool_thread_loop(void)
{
   uint32_t seen = 0;
   POOL_LOCK();
   for (;;)
   {
      pass *p;
      while (pool.gen == seen && !pool.quit)
         POOL_WAIT(pool_go);
      if (pool.quit)
         break;
      seen = pool.gen;
      p = pool.job;
      POOL_UNLOCK();
      work(p);
      POOL_LOCK();
      if (--pool.busy == 0)
         POOL_WAKE(pool_done);
   }
   POOL_UNLOCK();
}

#if defined(_WIN32)
static unsigned __stdcall pool_main(void *arg)
{
   (void)arg;
   pool_thread_loop();
   return 0;
}

static void pool_init(void)
{
   /* made once, by whichever thread comes first */
   if (InterlockedCompareExchange(&pool_ready, 1, 0) == 0)
   {
      InitializeCriticalSection(&pool_lock);
      InitializeConditionVariable(&pool_go);
      InitializeConditionVariable(&pool_done);
      InterlockedExchange(&pool_ready, 2);
   }
   while (pool_ready != 2)
      Sleep(0);
}
#else
static void *pool_main(void *arg)
{
   (void)arg;
   pool_thread_loop();
   return NULL;
}

static void pool_init(void)
{
}
#endif

/* The threads for `want` of them besides the caller (started once; the ones that cannot start are left out). */
static void pool_start(int32_t want)
{
   POOL_LOCK();
   while (pool.n < want && pool.n < PAR_MAX)
   {
#if defined(_WIN32)
      HANDLE t = (HANDLE)_beginthreadex(NULL, 0, pool_main, NULL, 0, NULL);
      if (!t)
         break;
      pool.th[pool.n++] = t;
#else
      if (pthread_create(&pool.th[pool.n], NULL, pool_main, NULL) != 0)
         break;
      pool.n++;
#endif
   }
   POOL_UNLOCK();
}

static int pool_enter(void)
{
#if defined(_MSC_VER)
   return InterlockedCompareExchange(&pool_in_use, 1, 0) == 0;
#else
   return __sync_bool_compare_and_swap(&pool_in_use, 0, 1);
#endif
}
#endif

void hd_rows_stop(void)
{
#if defined(_WIN32) || !defined(HD_NO_THREADS)
   int32_t k, n;
#if defined(_WIN32)
   if (pool_ready != 2)
      return;
#endif
   POOL_LOCK();
   pool.quit = 1;
   POOL_WAKE_ALL(pool_go);
   n = pool.n;
   POOL_UNLOCK();
   for (k = 0; k < n; k++)
   {
#if defined(_WIN32)
      WaitForSingleObject(pool.th[k], INFINITE);
      CloseHandle(pool.th[k]);
#else
      pthread_join(pool.th[k], NULL);
#endif
   }
   POOL_LOCK();
   pool.n = 0;
   pool.quit = 0;
   pool.gen = 0;
   POOL_UNLOCK();
#endif
}

void hd_rows(int32_t w, int32_t h, hd_rows_fn fn, void *ctx)
{
   pass p;
   int32_t n = (int64_t)w * h < PAR_MIN_PIXELS ? 1 : cores();
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
#if defined(_WIN32) || !defined(HD_NO_THREADS)
   if (!pool_enter())
   {
      work(&p); /* another thread's pass has the pool: this one on its own */
      return;
   }
   pool_init();
   pool_start(n - 1);
   POOL_LOCK();
   pool.job = &p;
   pool.busy = pool.n;
   pool.gen++;
   POOL_WAKE_ALL(pool_go);
   POOL_UNLOCK();
   work(&p);
   POOL_LOCK();
   while (pool.busy > 0)
      POOL_WAIT(pool_done);
   POOL_UNLOCK();
#if defined(_MSC_VER)
   InterlockedExchange(&pool_in_use, 0);
#else
   __sync_lock_release(&pool_in_use);
#endif
#else
   work(&p);
#endif
}
