/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * DEFLATE (RFC 1951) and zlib (RFC 1950) decoding, for zip entries and PNG
 * pictures. Packages come from anywhere, so every read and write is bounds
 * checked: bad data returns an error, never reads or writes out of range.
 */
#include <string.h>
#include "pack.h"

typedef struct
{
   const uint8_t *src;
   size_t len, pos;
   uint32_t bits, nbits;
   uint8_t *dst;
   size_t cap, out;
   int err;
} stream;

/* A Huffman table: counts per code length and the symbols in code order. */
typedef struct
{
   uint16_t count[16];
   uint16_t symbol[320];
} huff;

static uint32_t bits(stream *s, uint32_t n)
{
   uint32_t v;
   while (s->nbits < n)
   {
      if (s->pos >= s->len)
      {
         s->err = 1;
         return 0;
      }
      s->bits |= (uint32_t)s->src[s->pos++] << s->nbits;
      s->nbits += 8;
   }
   v = s->bits & ((1u << n) - 1u);
   s->bits >>= n;
   s->nbits -= n;
   return v;
}

/* Builds a table from code lengths; returns 0 when they are not a valid code. */
static int build(huff *h, const uint8_t *lengths, int n)
{
   uint16_t offs[16];
   int i, left = 1;
   memset(h->count, 0, sizeof h->count);
   for (i = 0; i < n; i++)
      h->count[lengths[i]]++;
   if (h->count[0] == n)
      return 1; /* no codes: allowed, decoding with it fails */
   for (i = 1; i < 16; i++)
   {
      left <<= 1;
      left -= h->count[i];
      if (left < 0)
         return 0; /* over-subscribed */
   }
   offs[1] = 0;
   for (i = 1; i < 15; i++)
      offs[i + 1] = (uint16_t)(offs[i] + h->count[i]);
   for (i = 0; i < n; i++)
      if (lengths[i])
         h->symbol[offs[lengths[i]]++] = (uint16_t)i;
   return 1;
}

static int decode(stream *s, const huff *h)
{
   int code = 0, first = 0, index = 0, len;
   for (len = 1; len < 16; len++)
   {
      code |= (int)bits(s, 1);
      if (s->err)
         return -1;
      if (code - h->count[len] < first)
         return h->symbol[index + (code - first)];
      index += h->count[len];
      first += h->count[len];
      first <<= 1;
      code <<= 1;
   }
   s->err = 1;
   return -1;
}

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(stream *s, const huff *lit, const huff *dist)
{
   for (;;)
   {
      int sym = decode(s, lit);
      if (sym < 0)
         return 0;
      if (sym < 256)
      {
         if (s->out >= s->cap)
            return 0;
         s->dst[s->out++] = (uint8_t)sym;
      }
      else if (sym == 256)
         return 1;
      else
      {
         size_t len, d, i;
         sym -= 257;
         if (sym >= 29)
            return 0;
         len = len_base[sym] + bits(s, len_extra[sym]);
         sym = decode(s, dist);
         if (sym < 0 || sym >= 30)
            return 0;
         d = dist_base[sym] + bits(s, dist_extra[sym]);
         if (s->err || d > s->out || len > s->cap - s->out)
            return 0;
         for (i = 0; i < len; i++, s->out++)
            s->dst[s->out] = s->dst[s->out - d];
      }
   }
}

static int fixed_block(stream *s)
{
   static huff lit, dist;
   static int ready;
   if (!ready)
   {
      uint8_t l[288];
      int i;
      for (i = 0; i < 144; i++) l[i] = 8;
      for (; i < 256; i++) l[i] = 9;
      for (; i < 280; i++) l[i] = 7;
      for (; i < 288; i++) l[i] = 8;
      build(&lit, l, 288);
      for (i = 0; i < 30; i++) l[i] = 5;
      build(&dist, l, 30);
      ready = 1;
   }
   return codes(s, &lit, &dist);
}

static int dynamic_block(stream *s)
{
   static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
   uint8_t lengths[320];
   huff lens, lit, dist;
   int nlen, ndist, ncode, i;
   nlen = (int)bits(s, 5) + 257;
   ndist = (int)bits(s, 5) + 1;
   ncode = (int)bits(s, 4) + 4;
   if (s->err || nlen > 286 || ndist > 30)
      return 0;
   memset(lengths, 0, sizeof lengths);
   for (i = 0; i < ncode; i++)
      lengths[order[i]] = (uint8_t)bits(s, 3);
   if (s->err || !build(&lens, lengths, 19))
      return 0;
   for (i = 0; i < nlen + ndist;)
   {
      int sym = decode(s, &lens), rep, val = 0;
      if (sym < 0)
         return 0;
      if (sym < 16)
      {
         lengths[i++] = (uint8_t)sym;
         continue;
      }
      if (sym == 16)
      {
         if (i == 0)
            return 0;
         val = lengths[i - 1];
         rep = 3 + (int)bits(s, 2);
      }
      else if (sym == 17)
         rep = 3 + (int)bits(s, 3);
      else
         rep = 11 + (int)bits(s, 7);
      if (s->err || i + rep > nlen + ndist)
         return 0;
      while (rep--)
         lengths[i++] = (uint8_t)val;
   }
   if (lengths[256] == 0)
      return 0; /* no end of block code */
   if (!build(&lit, lengths, nlen) || !build(&dist, lengths + nlen, ndist))
      return 0;
   return codes(s, &lit, &dist);
}

static int stored_block(stream *s)
{
   size_t len, nlen;
   s->bits = 0;
   s->nbits = 0; /* to the next byte */
   if (s->len - s->pos < 4)
      return 0;
   len = s->src[s->pos] | (size_t)s->src[s->pos + 1] << 8;
   nlen = s->src[s->pos + 2] | (size_t)s->src[s->pos + 3] << 8;
   s->pos += 4;
   if (len != (~nlen & 0xffff) || len > s->len - s->pos || len > s->cap - s->out)
      return 0;
   memcpy(s->dst + s->out, s->src + s->pos, len);
   s->pos += len;
   s->out += len;
   return 1;
}

int hd_inflate(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, size_t *out)
{
   stream s;
   uint32_t last;
   memset(&s, 0, sizeof s);
   s.src = src;
   s.len = len;
   s.dst = dst;
   s.cap = cap;
   do
   {
      uint32_t type;
      int ok;
      last = bits(&s, 1);
      type = bits(&s, 2);
      if (s.err)
         return 0;
      if (type == 0)
         ok = stored_block(&s);
      else if (type == 1)
         ok = fixed_block(&s);
      else if (type == 2)
         ok = dynamic_block(&s);
      else
         ok = 0;
      if (!ok || s.err)
         return 0;
   } while (!last);
   *out = s.out;
   return 1;
}

uint32_t hd_adler32(const uint8_t *p, size_t n)
{
   uint32_t a = 1, b = 0;
   size_t i;
   for (i = 0; i < n; i++)
   {
      a = (a + p[i]) % 65521u;
      b = (b + a) % 65521u;
   }
   return b << 16 | a;
}

int hd_zlib(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, size_t *out)
{
   uint32_t want;
   if (len < 6 || (src[0] & 0x0f) != 8 || ((src[0] << 8) | src[1]) % 31 != 0 || (src[1] & 0x20))
      return 0; /* not deflate, bad check, or a preset dictionary */
   if (!hd_inflate(src + 2, len - 6, dst, cap, out))
      return 0;
   want = (uint32_t)src[len - 4] << 24 | (uint32_t)src[len - 3] << 16 | (uint32_t)src[len - 2] << 8 | src[len - 1];
   return hd_adler32(dst, *out) == want;
}

uint32_t hd_crc32(const uint8_t *p, size_t n)
{
   static uint32_t table[256];
   uint32_t c = 0xffffffffu;
   size_t i;
   if (!table[1])
   {
      uint32_t k, j;
      for (k = 0; k < 256; k++)
      {
         uint32_t v = k;
         for (j = 0; j < 8; j++)
            v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1;
         table[k] = v;
      }
   }
   for (i = 0; i < n; i++)
      c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
   return c ^ 0xffffffffu;
}
