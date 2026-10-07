/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Zip reading: finds an entry through the central directory, unpacks it
 * (stored or deflate) and checks its CRC-32. No zip64, no encryption, no
 * multi-disk archives: a package is one plain zip under 256 MB.
 */
#include <stdlib.h>
#include <string.h>
#include "pack.h"

static uint32_t u16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | u16(p + 2) << 16; }

uint8_t *hd_zip_read(const hd_zip *zip, const char *name, size_t *size, const char **err)
{
   const uint8_t *d = zip->data;
   size_t n = zip->size, eocd, at, i, name_len = strlen(name);
   uint32_t entries, cd_size, cd_at;
   *err = NULL;
   if (n < 22 || n > PACK_MAX_BYTES)
   {
      *err = "the package is not a zip of up to 256 MB";
      return NULL;
   }
   /* the end of central directory record, before an optional comment */
   for (eocd = n - 22;; eocd--)
   {
      if (u32(d + eocd) == 0x06054b50u)
         break;
      if (eocd == 0 || n - eocd > 22 + 65535)
      {
         *err = "the package is not a zip";
         return NULL;
      }
   }
   entries = u16(d + eocd + 10);
   cd_size = u32(d + eocd + 12);
   cd_at = u32(d + eocd + 16);
   if (cd_at > eocd || cd_size > eocd - cd_at)
   {
      *err = "the package's zip directory is damaged";
      return NULL;
   }
   at = cd_at;
   for (i = 0; i < entries; i++)
   {
      uint32_t method, crc, csize, usize, nlen, xlen, clen, local, flags;
      if (at + 46 > cd_at + cd_size || u32(d + at) != 0x02014b50u)
      {
         *err = "the package's zip directory is damaged";
         return NULL;
      }
      flags = u16(d + at + 8);
      method = u16(d + at + 10);
      crc = u32(d + at + 16);
      csize = u32(d + at + 20);
      usize = u32(d + at + 24);
      nlen = u16(d + at + 28);
      xlen = u16(d + at + 30);
      clen = u16(d + at + 32);
      local = u32(d + at + 42);
      if (at + 46 + nlen > cd_at + cd_size)
      {
         *err = "the package's zip directory is damaged";
         return NULL;
      }
      if (nlen == name_len && memcmp(d + at + 46, name, nlen) == 0)
      {
         size_t data_at;
         uint8_t *out;
         size_t got = 0;
         if (flags & 1)
         {
            *err = "the package has an encrypted file";
            return NULL;
         }
         if (usize > PACK_MAX_ENTRY)
         {
            *err = "a file in the package is bigger than 64 MB";
            return NULL;
         }
         if (n < 30 || local > n - 30 || u32(d + local) != 0x04034b50u)
         {
            *err = "the package's zip is damaged";
            return NULL;
         }
         data_at = local + 30 + u16(d + local + 26) + u16(d + local + 28);
         if (data_at > n || csize > n - data_at)
         {
            *err = "the package's zip is damaged";
            return NULL;
         }
         out = (uint8_t *)malloc(usize ? usize : 1);
         if (!out)
         {
            *err = "out of memory";
            return NULL;
         }
         if (method == 0 && csize == usize)
         {
            memcpy(out, d + data_at, usize);
            got = usize;
         }
         else if (method != 8 || !hd_inflate(d + data_at, csize, out, usize, &got))
            got = (size_t)-1;
         if (got != usize || hd_crc32(out, usize) != crc)
         {
            free(out);
            *err = method == 0 || method == 8 ? "a file in the package is damaged" : "the package uses a zip compression other than deflate";
            return NULL;
         }
         *size = usize;
         return out;
      }
      at += 46 + nlen + xlen + clen;
   }
   *err = "a file is missing from the package";
   return NULL;
}
