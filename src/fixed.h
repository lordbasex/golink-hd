/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Fixed point numbers and the random generator. Game logic never uses float:
 * compilers may fuse or reorder float operations differently on each CPU,
 * and the same inputs must give the same frames everywhere (the device, the
 * browser's WebAssembly build and every platform it is built for).
 *
 * Positions and speeds are 16.16: the upper 16 bits are pixels, the lower 16
 * bits the fraction. Right shifts of negative numbers are arithmetic on every
 * compiler it is built with (GCC, Clang, MSVC), so >> is a floor.
 */
#ifndef HD_FIXED_H
#define HD_FIXED_H

#include <stdint.h>

#define FX_ONE 65536
/* An integer number of pixels as 16.16. */
#define FX(n) ((int32_t)(n) * FX_ONE)
/* A fraction num/den as 16.16, e.g. FX_FRAC(35, 100) is 0.35. */
#define FX_FRAC(num, den) ((int32_t)(((int64_t)(num) * FX_ONE) / (den)))
/* The whole pixels of a 16.16 value, rounded down. */
#define FX_INT(v) ((int32_t)(v) >> 16)

static inline int32_t fx_mul(int32_t a, int32_t b)
{
   return (int32_t)(((int64_t)a * b) >> 16);
}

static inline int32_t hd_abs(int32_t v) { return v < 0 ? -v : v; }
static inline int32_t hd_min(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t hd_max(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t hd_clamp(int32_t v, int32_t lo, int32_t hi)
{
   return v < lo ? lo : (v > hi ? hi : v);
}

/* xorshift32: the state never becomes 0 when it starts non-zero. */
static inline uint32_t rng_next(uint32_t *s)
{
   uint32_t x = *s;
   x ^= x << 13;
   x ^= x >> 17;
   x ^= x << 5;
   *s = x;
   return x;
}

/* A number in [0, n). */
static inline int32_t rng_range(uint32_t *s, int32_t n)
{
   return (int32_t)(rng_next(s) % (uint32_t)n);
}

#endif
