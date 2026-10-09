/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The software renderer's building blocks: surfaces, sprites with blend
 * modes, rotation and scale, outlines and shadows, whole-screen effects
 * (fades, color grading, bloom, blur, waves, pixelate, lights), Mode 7
 * floors and pseudo 3D roads. Integer math only, so every build draws the
 * same pixels. Colors are 0x00RRGGBB on surfaces, 0xAARRGGBB in pictures.
 */
#ifndef HD_GFX_H
#define HD_GFX_H

#include <stdint.h>
#include "hd.h"

/* A picture to draw into. */
typedef struct
{
   uint32_t *px;
   int32_t w, h;
} hd_surface;

/* Angles are 0..4095 for a full turn; sin and cos are 16384 = 1.0. */
#define ANGLE_FULL 4096
#define TRIG_ONE 16384
void hd_trig_build(void);
int32_t hd_sin(int32_t angle);
int32_t hd_cos(int32_t angle);

enum
{
   BLEND_NORMAL = 0,
   BLEND_ADD,      /* light: fire, glows */
   BLEND_MULTIPLY, /* shade: shadows, stains */
   BLEND_SCREEN    /* soft light */
};

enum
{
   DRAW_FLIP_X = 1,
   DRAW_FLIP_Y = 2,
   DRAW_WHITE = 4 /* the silhouette in white: a hit flash */
};

/* How a sprite is drawn. A zeroed struct is "as it is". */
typedef struct
{
   int32_t flags;    /* DRAW_* */
   int32_t blend;    /* BLEND_* */
   int32_t opacity;  /* 0..256; 0 means 256 (as drawn) */
   uint32_t tint;    /* 0xAARRGGBB: mixed toward RGB by A; 0 = none */
   uint32_t outline; /* 0xAARRGGBB: a one pixel outline around it; 0 = none */
} hd_style;

/*
 * a mixed toward b by t (0..256). Red and blue go together, green apart:
 * each product fits its own 16 bits, so it is exact. Inline: it runs once a
 * pixel in many loops.
 */
static inline uint32_t gfx_mix(uint32_t a, uint32_t b, int32_t t)
{
   uint32_t u = (uint32_t)(256 - t), v = (uint32_t)t;
   uint32_t rb = (((a & 0xff00ffu) * u + (b & 0xff00ffu) * v) >> 8) & 0xff00ffu;
   uint32_t g = (((a & 0xff00u) * u + (b & 0xff00u) * v) >> 8) & 0xff00u;
   return rb | g;
}
void gfx_fill(hd_surface *s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c);
/* A sprite with its top-left at (x, y). */
void gfx_blit(hd_surface *s, const hd_image *im, int32_t x, int32_t y, const hd_style *d);
/*
 * A sprite rotated by angle and scaled (16.16, FX_ONE = as is), with its
 * pivot (px, py, in the picture) at (x, y). Nearest neighbour, no blur.
 */
void gfx_blit_rot(hd_surface *s, const hd_image *im, int32_t x, int32_t y, int32_t px, int32_t py,
                  int32_t angle, int32_t sx, int32_t sy, const hd_style *d);
/* A soft dark ellipse on the ground: a shadow under a character. */
void gfx_shadow(hd_surface *s, int32_t cx, int32_t cy, int32_t rx, int32_t ry, int32_t strength);

/* Scales src into dst with nearest neighbour (the camera's zoom). */
void gfx_scale(hd_surface *dst, const hd_surface *src);

/* The game's world (draw.c) into any surface, its top-left at (cx, cy). */
void hd_draw_world(const hd_state *s, hd_surface *target, int32_t cx, int32_t cy);

/* Whole-screen effects (fx.c). */
void fx_fade(hd_surface *s, uint32_t color, int32_t amount); /* amount 0..256 toward color */
enum { GRADE_NONE = 0, GRADE_NIGHT, GRADE_SEPIA, GRADE_UNDERWATER, GRADE_SUNSET, GRADE_GREY, GRADE_LUT };
/* Builds the color grading table for a preset, or from a 16x16x16 LUT strip (256 x 16, the common format). */
void fx_grade_build(int32_t preset, const uint32_t *lut_strip);
void fx_grade(hd_surface *s, int32_t amount);  /* 0..256 of the graded picture */
void fx_bloom(hd_surface *s, int32_t threshold, int32_t strength);
void fx_blur(hd_surface *s, int32_t radius);
/* Rows y0..y1 shifted sideways by a sine wave: heat haze, underwater. */
void fx_waves(hd_surface *s, int32_t y0, int32_t y1, int32_t amplitude, int32_t wavelength, int32_t phase);
void fx_pixelate(hd_surface *s, int32_t block);

/* Lights: darkness from 0 (none) to 256 (black), then each light adds back. */
typedef struct
{
   int32_t x, y, radius; /* on screen, pixels */
   uint32_t color;       /* 0xRRGGBB */
   int32_t strength;     /* 0..256 */
} hd_light;
void fx_lights(hd_surface *s, int32_t darkness, const hd_light *lights, int32_t count);

/* Mode 7 (mode7.c): a picture as a floor seen in perspective. */
typedef struct
{
   int32_t x, y;      /* the camera over the floor, 16.16 picture pixels */
   int32_t angle;     /* 0..4095, where it looks */
   int32_t height;    /* how high it is, pixels */
   int32_t horizon;   /* the screen row of the horizon */
   int32_t focal;     /* the lens: bigger is narrower */
   int32_t wrap;      /* 1 repeats the picture, 0 shows outside instead */
   uint32_t outside;  /* the color past the picture's edge */
   int32_t fog;       /* 0..256 of the sky color mixed in at the horizon */
   uint32_t fog_color;
} hd_mode7;
void gfx_mode7(hd_surface *s, const hd_image *floor, const hd_mode7 *m);
/*
 * Where a point of the floor lands on screen, for sprites standing on it;
 * returns 0 behind the camera. scale is 16.16 (FX_ONE at the focal distance).
 */
int gfx_mode7_project(const hd_mode7 *m, int32_t wx, int32_t wy, int32_t screen_w, int32_t *sx, int32_t *sy, int32_t *scale);

/*
 * par.c: a pass over rows 0..h of a w x h picture, cut into bands done on
 * several cores when the picture is big (fn(ctx, y0, y1) for each band; a
 * band must only write its own rows).
 */
typedef void (*hd_rows_fn)(void *ctx, int32_t y0, int32_t y1);
void hd_rows(int32_t w, int32_t h, hd_rows_fn fn, void *ctx);

#endif
