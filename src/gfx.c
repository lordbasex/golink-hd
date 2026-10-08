/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Sprites: blend modes, opacity, tint, flash, outline, rotation and scale,
 * shadows; and the sine table. Integer math only (see gfx.h).
 */
#include <string.h>
#include "gfx.h"

/* A quarter wave, 0..1024 (inclusive), 16384 = 1.0. */
static int32_t quarter[1025];

/*
 * sin(x) by its Taylor series to x^11 in 2.30 fixed point: the same integer
 * steps on every CPU, so the table is the same everywhere (libm's sin may
 * differ in the last bit between platforms). Error under 1e-6 on [0, pi/2].
 */
void hd_trig_build(void)
{
   const int64_t half_pi = 1686629713; /* pi/2 * 2^30 */
   int32_t a;
   for (a = 0; a <= 1024; a++)
   {
      int64_t x = half_pi * a / 1024, x2 = (x * x) >> 30, term = x, sum = x;
      int32_t k;
      for (k = 1; k <= 5; k++)
      {
         term = -((term * x2) >> 30) / ((2 * k) * (2 * k + 1));
         sum += term;
      }
      quarter[a] = (int32_t)((sum * TRIG_ONE + (1 << 29)) >> 30);
   }
}

int32_t hd_sin(int32_t angle)
{
   int32_t a = angle & (ANGLE_FULL - 1);
   if (a < 1024)
      return quarter[a];
   if (a < 2048)
      return quarter[2048 - a];
   if (a < 3072)
      return -quarter[a - 2048];
   return -quarter[4096 - a];
}

int32_t hd_cos(int32_t angle)
{
   return hd_sin(angle + 1024);
}

void gfx_scale(hd_surface *dst, const hd_surface *src)
{
   static int32_t col[HD_MAX_W];
   int32_t x, y;
   for (x = 0; x < dst->w; x++)
      col[x] = hd_min(x * src->w / dst->w, src->w - 1);
   for (y = 0; y < dst->h; y++)
   {
      const uint32_t *in = src->px + hd_min(y * src->h / dst->h, src->h - 1) * src->w;
      uint32_t *out = dst->px + y * dst->w;
      for (x = 0; x < dst->w; x++)
         out[x] = in[col[x]];
   }
}

void gfx_fill(hd_surface *s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c)
{
   int32_t i, j;
   for (j = hd_max(y, 0); j < hd_min(y + h, s->h); j++)
      for (i = hd_max(x, 0); i < hd_min(x + w, s->w); i++)
         s->px[j * s->w + i] = c;
}

static uint32_t channel_op(uint32_t d, uint32_t c, int32_t mode)
{
   uint32_t out = 0;
   int32_t k;
   for (k = 0; k < 24; k += 8)
   {
      uint32_t x = (d >> k) & 0xff, y = (c >> k) & 0xff, v;
      if (mode == BLEND_ADD)
         v = x + y > 255 ? 255 : x + y;
      else if (mode == BLEND_MULTIPLY)
         v = x * y / 255;
      else /* screen */
         v = 255 - (255 - x) * (255 - y) / 255;
      out |= v << k;
   }
   return out;
}

/* One pixel of color c (0xRRGGBB) over d with weight a (0..256) in a blend mode. */
static uint32_t blend_px(uint32_t d, uint32_t c, int32_t a, int32_t mode)
{
   if (mode == BLEND_NORMAL)
      return a >= 256 ? c : gfx_mix(d, c, a);
   if (mode == BLEND_ADD)
   {
      uint32_t scaled = (((c >> 16) & 0xff) * (uint32_t)a >> 8) << 16 | (((c >> 8) & 0xff) * (uint32_t)a >> 8) << 8 | ((c & 0xff) * (uint32_t)a >> 8);
      return channel_op(d, scaled, BLEND_ADD);
   }
   return gfx_mix(d, channel_op(d, c, mode), a);
}

/* A picture's pixel with its flips, or 0 (transparent) outside it. */
static uint32_t pick(const hd_image *im, int32_t u, int32_t v, int32_t flags)
{
   if (u < 0 || v < 0 || u >= im->w || v >= im->h)
      return 0;
   if (flags & DRAW_FLIP_X)
      u = im->w - 1 - u;
   if (flags & DRAW_FLIP_Y)
      v = im->h - 1 - v;
   return im->px[v * im->w + u];
}

/* The color and weight a style gives a picture's pixel; weight 0 draws nothing. */
static int32_t styled(uint32_t *c, const hd_style *d)
{
   uint32_t a = *c >> 24;
   int32_t w;
   if (!a)
      return 0;
   w = a == 255 ? 256 : (int32_t)a;
   if (d)
   {
      if (d->opacity > 0 && d->opacity < 256)
         w = w * d->opacity >> 8;
      if (d->flags & DRAW_WHITE)
         *c = 0xffffffffu;
      else if (d->tint >> 24)
         *c = 0xff000000u | gfx_mix(*c & 0xffffffu, d->tint & 0xffffffu, (int32_t)(d->tint >> 24) + 1);
   }
   *c &= 0xffffffu;
   return w;
}

void gfx_blit(hd_surface *s, const hd_image *im, int32_t x, int32_t y, const hd_style *d)
{
   int32_t flags = d ? d->flags : 0, mode = d ? d->blend : BLEND_NORMAL;
   int32_t i, j;
   if (d && d->outline >> 24)
   {
      /* the outline: transparent pixels touching an opaque one, one pixel around the picture too */
      int32_t oa = (int32_t)(d->outline >> 24) + 1;
      for (j = -1; j <= im->h; j++)
         for (i = -1; i <= im->w; i++)
         {
            int32_t sx = x + i, sy = y + j;
            if ((uint32_t)sx >= (uint32_t)s->w || (uint32_t)sy >= (uint32_t)s->h || pick(im, i, j, flags) >> 24)
               continue;
            if (pick(im, i - 1, j, flags) >> 24 || pick(im, i + 1, j, flags) >> 24 || pick(im, i, j - 1, flags) >> 24 || pick(im, i, j + 1, flags) >> 24)
               s->px[sy * s->w + sx] = gfx_mix(s->px[sy * s->w + sx], d->outline & 0xffffffu, oa);
         }
   }
   {
      int32_t x0 = hd_max(0, -x), x1 = hd_min(im->w, s->w - x);
      int32_t y0 = hd_max(0, -y), y1 = hd_min(im->h, s->h - y);
      for (j = y0; j < y1; j++)
      {
         uint32_t *row = s->px + (y + j) * s->w + x;
         const uint32_t *src = im->px + ((flags & DRAW_FLIP_Y) ? im->h - 1 - j : j) * im->w;
         for (i = x0; i < x1; i++)
         {
            uint32_t c = src[(flags & DRAW_FLIP_X) ? im->w - 1 - i : i];
            int32_t w = styled(&c, d);
            if (w)
               row[i] = blend_px(row[i], c, w, mode);
         }
      }
   }
}

void gfx_blit_rot(hd_surface *s, const hd_image *im, int32_t x, int32_t y, int32_t px, int32_t py,
                  int32_t angle, int32_t sx, int32_t sy, const hd_style *d)
{
   int32_t c = hd_cos(angle), sn = hd_sin(angle);
   int32_t flags = d ? d->flags : 0, mode = d ? d->blend : BLEND_NORMAL;
   int32_t minx = 1 << 30, maxx = -(1 << 30), miny = 1 << 30, maxy = -(1 << 30), k, i, j;
   int64_t isx, isy;
   if (sx <= 0 || sy <= 0)
      return;
   isx = ((int64_t)FX_ONE << 16) / sx; /* 1/scale, 16.16 */
   isy = ((int64_t)FX_ONE << 16) / sy;
   /* the corners, turned and scaled, give the box to fill */
   for (k = 0; k < 4; k++)
   {
      int64_t u = (int64_t)((k & 1) ? im->w - px : -px) * sx, v = (int64_t)((k & 2) ? im->h - py : -py) * sy; /* 16.16 */
      int32_t rx = (int32_t)((u * c - v * sn) >> (14 + 16)), ry = (int32_t)((u * sn + v * c) >> (14 + 16));
      minx = hd_min(minx, rx);
      maxx = hd_max(maxx, rx);
      miny = hd_min(miny, ry);
      maxy = hd_max(maxy, ry);
   }
   for (j = hd_max(y + miny - 1, 0); j <= hd_min(y + maxy + 1, s->h - 1); j++)
   {
      int64_t dy = (int64_t)(j - y) * 2 + 1; /* pixel centres, in halves */
      for (i = hd_max(x + minx - 1, 0); i <= hd_min(x + maxx + 1, s->w - 1); i++)
      {
         int64_t dx = (int64_t)(i - x) * 2 + 1;
         /* back into the picture: turn the other way, undo the scale (halves * 2^14 * 2^16 >> 31 = pixels << 16) */
         int64_t u = ((dx * c + dy * sn) * isx) >> 15, v = ((dy * c - dx * sn) * isy) >> 15;
         /* >> on a negative number rounds down on every compiler the engine is built with (see fixed.h) */
         int32_t pu = px + (int32_t)(u >> 16), pv = py + (int32_t)(v >> 16);
         uint32_t col = pick(im, pu, pv, flags);
         int32_t w;
         w = styled(&col, d);
         if (w)
            s->px[j * s->w + i] = blend_px(s->px[j * s->w + i], col, w, mode);
      }
   }
}

void gfx_shadow(hd_surface *s, int32_t cx, int32_t cy, int32_t rx, int32_t ry, int32_t strength)
{
   int32_t i, j;
   if (rx <= 0 || ry <= 0)
      return;
   for (j = -ry; j <= ry; j++)
      for (i = -rx; i <= rx; i++)
      {
         int32_t x = cx + i, y = cy + j;
         int64_t d = (int64_t)i * i * ry * ry + (int64_t)j * j * rx * rx, r = (int64_t)rx * rx * ry * ry;
         if ((uint32_t)x >= (uint32_t)s->w || (uint32_t)y >= (uint32_t)s->h || d > r)
            continue;
         /* darker at the middle */
         s->px[y * s->w + x] = gfx_mix(s->px[y * s->w + x], 0, (int32_t)((int64_t)strength * (r - d) / r));
      }
}
