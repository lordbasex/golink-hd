/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * PNG decoding to 0xAARRGGBB: 8-bit grey, grey with alpha, RGB and RGBA,
 * palettes of 1, 2, 4 or 8 bits (with tRNS transparency), every filter, not
 * interlaced. That covers what image editors and browsers save. Each chunk's
 * CRC is checked; anything else is refused with a readable error.
 */
#include <stdlib.h>
#include <string.h>
#include "pack.h"

static uint32_t be32(const uint8_t *p)
{
   return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int paeth(int a, int b, int c)
{
   int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
   return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
}

uint32_t *hd_png_read(const uint8_t *data, size_t size, int32_t *out_w, int32_t *out_h, const char **err)
{
   static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
   uint32_t w = 0, h = 0, palette[256], npal = 0, i, x, y;
   int depth = 0, color = 0, channels, bpp, seen_ihdr = 0;
   uint8_t *z = NULL, *raw = NULL, *prev, *cur;
   size_t zlen = 0, zcap = 0, at = 8, stride, rawlen, got;
   uint32_t *px = NULL;
   int has_trns_grey = 0, has_trns_rgb = 0;
   uint32_t trns_grey = 0, trns_r = 0, trns_g = 0, trns_b = 0;

   *err = NULL;
   if (size < 8 || memcmp(data, sig, 8) != 0)
   {
      *err = "a picture is not a PNG";
      return NULL;
   }
   for (i = 0; i < 256; i++)
      palette[i] = 0xff000000u;
   for (;;)
   {
      uint32_t len;
      const uint8_t *type, *body;
      if (size - at < 12)
         goto bad;
      len = be32(data + at);
      if (len > size - at - 12)
         goto bad;
      type = data + at + 4;
      body = data + at + 8;
      if (hd_crc32(type, len + 4) != be32(body + len))
         goto bad;
      if (!memcmp(type, "IHDR", 4))
      {
         if (len != 13)
            goto bad;
         w = be32(body);
         h = be32(body + 4);
         depth = body[8];
         color = body[9];
         if (w == 0 || h == 0 || w > PACK_MAX_SIDE || h > PACK_MAX_SIDE)
         {
            *err = "a picture is larger than 8192 pixels on a side";
            return NULL;
         }
         if (body[10] != 0 || body[11] != 0 || body[12] != 0)
         {
            *err = "a picture is interlaced: save it without interlacing";
            return NULL;
         }
         if (!(color == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) &&
             !((color == 0 || color == 2 || color == 4 || color == 6) && depth == 8))
         {
            *err = "a picture uses 16-bit color or an unusual format: save it as 8-bit RGBA";
            return NULL;
         }
         seen_ihdr = 1;
      }
      else if (!memcmp(type, "PLTE", 4))
      {
         if (len % 3 || len > 768)
            goto bad;
         npal = len / 3;
         for (i = 0; i < npal; i++)
            palette[i] = 0xff000000u | (uint32_t)body[3 * i] << 16 | (uint32_t)body[3 * i + 1] << 8 | body[3 * i + 2];
      }
      else if (!memcmp(type, "tRNS", 4))
      {
         if (color == 3)
         {
            for (i = 0; i < len && i < 256; i++)
               palette[i] = (palette[i] & 0xffffffu) | (uint32_t)body[i] << 24;
         }
         else if (color == 0 && len == 2)
         {
            has_trns_grey = 1;
            trns_grey = body[1];
         }
         else if (color == 2 && len == 6)
         {
            has_trns_rgb = 1;
            trns_r = body[1];
            trns_g = body[3];
            trns_b = body[5];
         }
      }
      else if (!memcmp(type, "IDAT", 4))
      {
         if (zlen + len > zcap)
         {
            size_t cap = (zlen + len) * 2;
            uint8_t *nz;
            if (cap > PACK_MAX_ENTRY)
               goto bad;
            nz = (uint8_t *)realloc(z, cap);
            if (!nz)
               goto bad;
            z = nz;
            zcap = cap;
         }
         memcpy(z + zlen, body, len);
         zlen += len;
      }
      else if (!memcmp(type, "IEND", 4))
         break;
      at += 12 + len;
   }
   if (!seen_ihdr || !z || (color == 3 && npal == 0))
      goto bad;

   channels = color == 0 ? 1 : color == 2 ? 3 : color == 4 ? 2 : color == 6 ? 4 : 1;
   stride = ((size_t)w * channels * depth + 7) / 8;
   bpp = (channels * depth + 7) / 8;
   rawlen = (stride + 1) * h;
   raw = (uint8_t *)malloc(rawlen);
   px = (uint32_t *)malloc((size_t)w * h * 4);
   prev = (uint8_t *)calloc(stride, 1);
   if (!raw || !px || !prev || !hd_zlib(z, zlen, raw, rawlen, &got) || got != rawlen)
   {
      free(prev);
      goto bad;
   }
   for (y = 0; y < h; y++)
   {
      uint8_t filter = raw[y * (stride + 1)];
      size_t k;
      cur = raw + y * (stride + 1) + 1;
      if (filter > 4)
      {
         free(prev);
         goto bad;
      }
      for (k = 0; k < stride; k++)
      {
         int a = k >= (size_t)bpp ? cur[k - bpp] : 0, b = prev[k], c = k >= (size_t)bpp ? prev[k - bpp] : 0;
         int v = cur[k];
         switch (filter)
         {
         case 1: v += a; break;
         case 2: v += b; break;
         case 3: v += (a + b) / 2; break;
         case 4: v += paeth(a, b, c); break;
         }
         cur[k] = (uint8_t)v;
      }
      for (x = 0; x < w; x++)
      {
         uint32_t r, g, bl, al = 255;
         switch (color)
         {
         case 0:
            r = g = bl = cur[x];
            if (has_trns_grey && cur[x] == trns_grey)
               al = 0;
            break;
         case 4:
            r = g = bl = cur[2 * x];
            al = cur[2 * x + 1];
            break;
         case 2:
            r = cur[3 * x];
            g = cur[3 * x + 1];
            bl = cur[3 * x + 2];
            if (has_trns_rgb && r == trns_r && g == trns_g && bl == trns_b)
               al = 0;
            break;
         case 6:
            r = cur[4 * x];
            g = cur[4 * x + 1];
            bl = cur[4 * x + 2];
            al = cur[4 * x + 3];
            break;
         default: /* palette, depth bits per pixel */
         {
            uint32_t bit = x * (uint32_t)depth, idx = (cur[bit >> 3] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1u);
            if (idx >= npal)
            {
               free(prev);
               goto bad;
            }
            px[y * w + x] = palette[idx];
            continue;
         }
         }
         px[y * w + x] = al << 24 | r << 16 | g << 8 | bl;
      }
      memcpy(prev, cur, stride);
   }
   free(prev);
   free(raw);
   free(z);
   *out_w = (int32_t)w;
   *out_h = (int32_t)h;
   return px;

bad:
   free(z);
   free(raw);
   free(px);
   if (!*err)
      *err = "a picture is a damaged PNG";
   return NULL;
}
