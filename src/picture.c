/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * A package's pictures as they are loaded: their transparency cleaned and,
 * when they were painted bigger than the drawing (format 3's "art_scale"),
 * made smaller to it. Everything is integer math, so every computer gets
 * the same pixels.
 */
#include <stdlib.h>
#include "sprite.h"

int32_t hd_art = 1;

/*
 * Image AIs cut their pictures out of a background themselves: the inside
 * of a character comes almost opaque (alpha 250-254, the background showing
 * through a little) and the space around it almost empty. Those become
 * fully opaque and fully empty: the picture as it was meant, drawn without
 * blending. The soft edge in between (the outline's anti-aliasing) stays.
 */
#define ALPHA_SOLID 247
#define ALPHA_EMPTY 8

void hd_pic_clean(uint32_t *px, int64_t n)
{
   int64_t k;
   for (k = 0; k < n; k++)
   {
      uint32_t a = px[k] >> 24;
      if (a >= ALPHA_SOLID)
         px[k] |= 0xff000000u;
      else if (a <= ALPHA_EMPTY)
         px[k] = 0;
   }
}

int32_t hd_art_px(int32_t v)
{
   return (int32_t)(((int64_t)v * hd_res * 2 + hd_art) / (hd_art * 2));
}

/*
 * The sw x sh pixels at px (rows of `pitch`) made dw x dh (no bigger): each
 * new pixel is the average of the old ones it covers, parts of a pixel
 * counted by how much of it lies inside, the colors weighted by their alpha
 * (a see-through pixel's color does not darken the edge).
 */
uint32_t *hd_pic_shrink(const uint32_t *px, int32_t pitch, int32_t sw, int32_t sh, int32_t dw, int32_t dh)
{
   uint32_t *out = (uint32_t *)malloc((size_t)dw * dh * 4);
   int32_t x, y, sx, sy;
   if (!out)
      return NULL;
   for (y = 0; y < dh; y++)
   {
      /* in 1/dh of a source row: the new row covers [y0, y1) */
      int64_t y0 = (int64_t)y * sh, y1 = (int64_t)(y + 1) * sh;
      for (x = 0; x < dw; x++)
      {
         int64_t x0 = (int64_t)x * sw, x1 = (int64_t)(x + 1) * sw;
         int64_t sa = 0, sr = 0, sg = 0, sb = 0, tw = 0;
         uint32_t a, r = 0, g = 0, b = 0;
         for (sy = (int32_t)(y0 / dh); sy < sh && (int64_t)sy * dh < y1; sy++)
         {
            int64_t wy = (y1 < (int64_t)(sy + 1) * dh ? y1 : (int64_t)(sy + 1) * dh) - (y0 > (int64_t)sy * dh ? y0 : (int64_t)sy * dh);
            const uint32_t *row = px + (size_t)sy * pitch;
            for (sx = (int32_t)(x0 / dw); sx < sw && (int64_t)sx * dw < x1; sx++)
            {
               int64_t wx = (x1 < (int64_t)(sx + 1) * dw ? x1 : (int64_t)(sx + 1) * dw) - (x0 > (int64_t)sx * dw ? x0 : (int64_t)sx * dw);
               int64_t w = wx * wy, aw = (int64_t)(row[sx] >> 24) * w;
               uint32_t c = row[sx];
               tw += w;
               sa += aw;
               sr += (int64_t)(c >> 16 & 255) * aw;
               sg += (int64_t)(c >> 8 & 255) * aw;
               sb += (int64_t)(c & 255) * aw;
            }
         }
         a = tw ? (uint32_t)((sa + tw / 2) / tw) : 0;
         if (sa)
         {
            r = (uint32_t)((sr + sa / 2) / sa);
            g = (uint32_t)((sg + sa / 2) / sa);
            b = (uint32_t)((sb + sa / 2) / sa);
         }
         out[(size_t)y * dw + x] = a << 24 | r << 16 | g << 8 | b;
      }
   }
   hd_pic_clean(out, (int64_t)dw * dh);
   return out;
}
