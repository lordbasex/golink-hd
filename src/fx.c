/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Whole-screen effects, run on the finished frame: fades, color grading,
 * bloom, blur, waves, pixelate and 2D lights. Each one is integer math over
 * the surface, measured against the frame budget by the tests.
 */
#include <string.h>
#include "gfx.h"

void fx_fade(hd_surface *s, uint32_t color, int32_t amount)
{
   int32_t i, n = s->w * s->h;
   if (amount <= 0)
      return;
   amount = hd_min(amount, 256);
   for (i = 0; i < n; i++)
      s->px[i] = gfx_mix(s->px[i], color, amount);
}

/* Color grading: a 64 x 64 x 64 table from every color to its graded one. */
#define LUT_N 64
static uint32_t lut[LUT_N * LUT_N * LUT_N];
static int32_t lut_ready;

static int32_t clamp255(int32_t v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* A preset as a 3x3 matrix in 1/256 and an offset. */
static const int32_t presets[][12] = {
   /* none */ { 256, 0, 0, 0, 256, 0, 0, 0, 256, 0, 0, 0 },
   /* night: dark, blue, little red */ { 70, 30, 10, 20, 105, 20, 25, 55, 185, 0, 4, 18 },
   /* sepia */ { 101, 197, 48, 89, 176, 43, 70, 137, 34, 0, 0, 0 },
   /* underwater: red goes first, a green-blue cast */ { 140, 20, 10, 30, 210, 30, 20, 50, 220, 0, 10, 30 },
   /* sunset: warm, the blues pushed to purple */ { 290, 20, 0, 0, 225, 0, 20, 0, 190, 10, 0, 0 },
   /* grey */ { 77, 150, 29, 77, 150, 29, 77, 150, 29, 0, 0, 0 },
};

void fx_grade_build(int32_t preset, const uint32_t *strip)
{
   static int32_t built = -1;
   int32_t r, g, b;
   /* a preset is built once; a LUT picture is always built again */
   if (!strip && preset == built)
      return;
   built = strip ? -1 : preset;
   for (r = 0; r < LUT_N; r++)
      for (g = 0; g < LUT_N; g++)
         for (b = 0; b < LUT_N; b++)
         {
            int32_t vr = r * 255 / (LUT_N - 1), vg = g * 255 / (LUT_N - 1), vb = b * 255 / (LUT_N - 1);
            int32_t orr, og, ob;
            if (preset == GRADE_LUT && strip)
            {
               /* trilinear over the 16x16x16 strip (256 x 16: x = blue slice * 16 + red, y = green) */
               int32_t fr = vr * 15, fg = vg * 15, fb = vb * 15; /* in 1/255 steps of the 0..15 grid */
               int32_t r0 = fr / 255, g0 = fg / 255, b0 = fb / 255;
               int32_t r1 = hd_min(r0 + 1, 15), g1 = hd_min(g0 + 1, 15), b1 = hd_min(b0 + 1, 15);
               int32_t tr = fr % 255, tg = fg % 255, tb = fb % 255, ch, k;
               int32_t out[3];
               for (k = 0; k < 3; k++)
               {
                  int32_t sh = 16 - 8 * k, c[8], i;
                  const int32_t idx[8][3] = { { r0, g0, b0 }, { r1, g0, b0 }, { r0, g1, b0 }, { r1, g1, b0 }, { r0, g0, b1 }, { r1, g0, b1 }, { r0, g1, b1 }, { r1, g1, b1 } };
                  for (i = 0; i < 8; i++)
                     c[i] = (int32_t)((strip[idx[i][1] * 256 + idx[i][2] * 16 + idx[i][0]] >> sh) & 0xff);
                  ch = ((c[0] * (255 - tr) + c[1] * tr) * (255 - tg) + (c[2] * (255 - tr) + c[3] * tr) * tg) / 255 * (255 - tb) +
                       ((c[4] * (255 - tr) + c[5] * tr) * (255 - tg) + (c[6] * (255 - tr) + c[7] * tr) * tg) / 255 * tb;
                  out[k] = ch / (255 * 255);
               }
               orr = out[0];
               og = out[1];
               ob = out[2];
            }
            else
            {
               const int32_t *m = presets[preset >= GRADE_NONE && preset < GRADE_LUT ? preset : GRADE_NONE];
               orr = (m[0] * vr + m[1] * vg + m[2] * vb) / 256 + m[9];
               og = (m[3] * vr + m[4] * vg + m[5] * vb) / 256 + m[10];
               ob = (m[6] * vr + m[7] * vg + m[8] * vb) / 256 + m[11];
            }
            lut[(r * LUT_N + g) * LUT_N + b] = (uint32_t)clamp255(orr) << 16 | (uint32_t)clamp255(og) << 8 | (uint32_t)clamp255(ob);
         }
   lut_ready = 1;
}

void fx_grade(hd_surface *s, int32_t amount)
{
   int32_t i, n = s->w * s->h;
   if (!lut_ready || amount <= 0)
      return;
   for (i = 0; i < n; i++)
   {
      uint32_t c = s->px[i];
      uint32_t g = lut[(((c >> 18) & 63) * LUT_N + ((c >> 10) & 63)) * LUT_N + ((c >> 2) & 63)];
      s->px[i] = amount >= 256 ? g : gfx_mix(c, g, amount);
   }
}

/* Separable box blur of radius r over a w x h buffer of 0xRRGGBB, in place. */
static void box_blur(uint32_t *px, int32_t w, int32_t h, int32_t r)
{
   static uint32_t line[HD_MAX_W > HD_MAX_H ? HD_MAX_W : HD_MAX_H];
   int32_t x, y, k, n = 2 * r + 1;
   uint32_t recip = (65536u + (uint32_t)n / 2) / (uint32_t)n; /* a multiply instead of a divide per pixel */
   if (r <= 0)
      return;
   for (y = 0; y < h; y++)
   {
      int32_t sr = 0, sg = 0, sb = 0;
      uint32_t *row = px + y * w;
      for (k = -r; k <= r; k++)
      {
         uint32_t c = row[hd_clamp(k, 0, w - 1)];
         sr += (c >> 16) & 0xff;
         sg += (c >> 8) & 0xff;
         sb += c & 0xff;
      }
      for (x = 0; x < w; x++)
      {
         uint32_t in = row[hd_min(x + r + 1, w - 1)], out = row[hd_max(x - r, 0)];
         line[x] = ((uint32_t)sr * recip >> 16) << 16 | ((uint32_t)sg * recip >> 16) << 8 | ((uint32_t)sb * recip >> 16);
         sr += (int32_t)((in >> 16) & 0xff) - (int32_t)((out >> 16) & 0xff);
         sg += (int32_t)((in >> 8) & 0xff) - (int32_t)((out >> 8) & 0xff);
         sb += (int32_t)(in & 0xff) - (int32_t)(out & 0xff);
      }
      memcpy(row, line, (size_t)w * 4);
   }
   /* down the columns, row by row: a running sum per column, so memory is read in order */
   {
      static int32_t cr[HD_MAX_W > HD_MAX_H ? 2 * HD_MAX_W : 2 * HD_MAX_H], cg[HD_MAX_W > HD_MAX_H ? 2 * HD_MAX_W : 2 * HD_MAX_H], cb[HD_MAX_W > HD_MAX_H ? 2 * HD_MAX_W : 2 * HD_MAX_H];
      static uint32_t tmp[(HD_MAX_W / 4) * (HD_MAX_H / 4) > HD_MAX_W * HD_MAX_H ? 1 : HD_MAX_W * HD_MAX_H];
      memcpy(tmp, px, (size_t)w * h * 4);
      for (x = 0; x < w; x++)
      {
         cr[x] = cg[x] = cb[x] = 0;
         for (k = -r; k <= r; k++)
         {
            uint32_t c = tmp[hd_clamp(k, 0, h - 1) * w + x];
            cr[x] += (c >> 16) & 0xff;
            cg[x] += (c >> 8) & 0xff;
            cb[x] += c & 0xff;
         }
      }
      for (y = 0; y < h; y++)
      {
         const uint32_t *in = tmp + hd_min(y + r + 1, h - 1) * w, *out = tmp + hd_max(y - r, 0) * w;
         uint32_t *dst = px + y * w;
         for (x = 0; x < w; x++)
         {
            dst[x] = ((uint32_t)cr[x] * recip >> 16) << 16 | ((uint32_t)cg[x] * recip >> 16) << 8 | ((uint32_t)cb[x] * recip >> 16);
            cr[x] += (int32_t)((in[x] >> 16) & 0xff) - (int32_t)((out[x] >> 16) & 0xff);
            cg[x] += (int32_t)((in[x] >> 8) & 0xff) - (int32_t)((out[x] >> 8) & 0xff);
            cb[x] += (int32_t)(in[x] & 0xff) - (int32_t)(out[x] & 0xff);
         }
      }
   }
}

void fx_blur(hd_surface *s, int32_t radius)
{
   box_blur(s->px, s->w, s->h, hd_clamp(radius, 0, 8));
}

/* Bloom: the bright parts, at a quarter of the size, blurred and added back. */
void fx_bloom(hd_surface *s, int32_t threshold, int32_t strength)
{
   static uint32_t small[(HD_MAX_W / 4) * (HD_MAX_H / 4)];
   int32_t sw = s->w / 4, sh = s->h / 4, x, y, i, j;
   for (y = 0; y < sh; y++)
      for (x = 0; x < sw; x++)
      {
         /* 4 of the block's 16 pixels: the blur that follows hides the difference */
         int32_t r = 0, g = 0, b = 0;
         for (j = 0; j < 4; j += 2)
            for (i = 0; i < 4; i += 2)
            {
               uint32_t c = s->px[(y * 4 + j + 1) * s->w + x * 4 + i + 1];
               r += hd_max(0, (int32_t)((c >> 16) & 0xff) - threshold);
               g += hd_max(0, (int32_t)((c >> 8) & 0xff) - threshold);
               b += hd_max(0, (int32_t)(c & 0xff) - threshold);
            }
         small[y * sw + x] = (uint32_t)clamp255(r / 4) << 16 | (uint32_t)clamp255(g / 4) << 8 | (uint32_t)clamp255(b / 4);
      }
   box_blur(small, sw, sh, 2);
   box_blur(small, sw, sh, 2);
   /* back to full size, bilinear (two rows of glow kept per band of 4), added */
   {
      static uint32_t row_a[HD_MAX_W], row_b[HD_MAX_W];
      int32_t cached = -1;
      for (y = 0; y < s->h; y++)
      {
         int32_t fy = hd_max(0, y * 64 - 128), y0 = hd_min(fy >> 8, sh - 1), y1 = hd_min(y0 + 1, sh - 1), ty = fy & 255;
         uint32_t *out = s->px + y * s->w;
         if (y0 != cached)
         {
            for (x = 0; x < s->w; x++)
            {
               int32_t fx = hd_max(0, x * 64 - 128), x0 = hd_min(fx >> 8, sw - 1), x1 = hd_min(x0 + 1, sw - 1), tx = fx & 255;
               row_a[x] = gfx_mix(small[y0 * sw + x0], small[y0 * sw + x1], tx);
               row_b[x] = gfx_mix(small[y1 * sw + x0], small[y1 * sw + x1], tx);
            }
            cached = y0;
         }
         for (x = 0; x < s->w; x++)
         {
            uint32_t glow = gfx_mix(row_a[x], row_b[x], ty), c = out[x];
            uint32_t r = ((c >> 16) & 0xff) + ((((glow >> 16) & 0xff) * (uint32_t)strength) >> 8);
            uint32_t g = ((c >> 8) & 0xff) + ((((glow >> 8) & 0xff) * (uint32_t)strength) >> 8);
            uint32_t b2 = (c & 0xff) + (((glow & 0xff) * (uint32_t)strength) >> 8);
            out[x] = (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b2 > 255 ? 255 : b2);
         }
      }
   }
}

void fx_waves(hd_surface *s, int32_t y0, int32_t y1, int32_t amplitude, int32_t wavelength, int32_t phase)
{
   static uint32_t line[HD_MAX_W];
   int32_t y, x;
   if (wavelength <= 0)
      return;
   for (y = hd_max(y0, 0); y <= hd_min(y1, s->h - 1); y++)
   {
      int32_t off = amplitude * hd_sin(phase + y * ANGLE_FULL / wavelength) / TRIG_ONE;
      uint32_t *row = s->px + y * s->w;
      if (!off)
         continue;
      for (x = 0; x < s->w; x++)
         line[x] = row[hd_clamp(x - off, 0, s->w - 1)];
      memcpy(row, line, (size_t)s->w * 4);
   }
}

void fx_pixelate(hd_surface *s, int32_t block)
{
   int32_t x, y, i, j;
   if (block <= 1)
      return;
   for (y = 0; y < s->h; y += block)
      for (x = 0; x < s->w; x += block)
      {
         int32_t r = 0, g = 0, b = 0, n = 0;
         uint32_t c;
         for (j = y; j < hd_min(y + block, s->h); j++)
            for (i = x; i < hd_min(x + block, s->w); i++)
            {
               c = s->px[j * s->w + i];
               r += (c >> 16) & 0xff;
               g += (c >> 8) & 0xff;
               b += c & 0xff;
               n++;
            }
         c = (uint32_t)(r / n) << 16 | (uint32_t)(g / n) << 8 | (uint32_t)(b / n);
         for (j = y; j < hd_min(y + block, s->h); j++)
            for (i = x; i < hd_min(x + block, s->w); i++)
               s->px[j * s->w + i] = c;
      }
}

/*
 * Lights: a light map per channel at half the screen's resolution (soft
 * light needs no more), 256 = the picture as it is, up to 512 (twice as
 * bright); then every pixel is multiplied by its spot of the map.
 */
void fx_lights(hd_surface *s, int32_t darkness, const hd_light *lights, int32_t count)
{
   static uint16_t map[3][(HD_MAX_W / 2 + 1) * (HD_MAX_H / 2 + 1)];
   int32_t mw = (s->w + 1) / 2, mh = (s->h + 1) / 2, n = mw * mh, i, k, x, y;
   uint16_t ambient = (uint16_t)(256 - hd_clamp(darkness, 0, 256));
   if (darkness <= 0 && count == 0)
      return;
   for (i = 0; i < n; i++)
      map[0][i] = map[1][i] = map[2][i] = ambient;
   for (k = 0; k < count; k++)
   {
      const hd_light *l = &lights[k];
      int32_t r = l->radius / 2, lx = l->x / 2, ly = l->y / 2, rr, inv;
      int32_t cr = (int32_t)((l->color >> 16) & 0xff), cg = (int32_t)((l->color >> 8) & 0xff), cb = (int32_t)(l->color & 0xff);
      if (r <= 0)
         continue;
      rr = r * r;
      inv = (256 << 16) / rr;
      for (y = hd_max(ly - r, 0); y <= hd_min(ly + r, mh - 1); y++)
      {
         int32_t dy2 = (y - ly) * (y - ly);
         uint16_t *m0 = map[0] + y * mw, *m1 = map[1] + y * mw, *m2 = map[2] + y * mw;
         for (x = hd_max(lx - r, 0); x <= hd_min(lx + r, mw - 1); x++)
         {
            int32_t d = (x - lx) * (x - lx) + dy2, f, v;
            if (d >= rr)
               continue;
            /* (1 - d/r^2)^2: bright in the middle, soft at the edge */
            f = 256 - ((d * inv) >> 16);
            f = ((f * f) >> 8) * l->strength >> 8;
            v = m0[x] + ((cr * f) >> 8);
            m0[x] = (uint16_t)(v > 512 ? 512 : v);
            v = m1[x] + ((cg * f) >> 8);
            m1[x] = (uint16_t)(v > 512 ? 512 : v);
            v = m2[x] + ((cb * f) >> 8);
            m2[x] = (uint16_t)(v > 512 ? 512 : v);
         }
      }
   }
   for (y = 0; y < s->h; y++)
   {
      uint32_t *row = s->px + y * s->w;
      const uint16_t *m0 = map[0] + (y >> 1) * mw, *m1 = map[1] + (y >> 1) * mw, *m2 = map[2] + (y >> 1) * mw;
      for (x = 0; x < s->w; x++)
      {
         uint32_t p = row[x], h = (uint32_t)x >> 1;
         uint32_t r = (((p >> 16) & 0xff) * m0[h]) >> 8, g = (((p >> 8) & 0xff) * m1[h]) >> 8, b = ((p & 0xff) * m2[h]) >> 8;
         row[x] = (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
      }
   }
}
