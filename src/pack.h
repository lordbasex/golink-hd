/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Reading game packages (.glhd): a zip with a manifest, a level and PNG
 * pictures. Everything here checks its input's bounds and size limits, since
 * a package can come from anywhere, and reports a readable error.
 */
#ifndef HD_PACK_H
#define HD_PACK_H

#include <stddef.h>
#include <stdint.h>

/* Limits: a package, one entry once unpacked, one picture's side. */
#define PACK_MAX_BYTES (256u * 1024u * 1024u)
#define PACK_MAX_ENTRY (64u * 1024u * 1024u)
#define PACK_MAX_SIDE 8192 /* a long strip of big frames (a boss's at 1080p); the decoded size stays under PACK_MAX_ENTRY */

/* inflate.c: raw DEFLATE and zlib streams; 1 on success. */
int hd_inflate(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, size_t *out);
int hd_zlib(const uint8_t *src, size_t len, uint8_t *dst, size_t cap, size_t *out);
uint32_t hd_crc32(const uint8_t *p, size_t n);
uint32_t hd_adler32(const uint8_t *p, size_t n);

/* sha256.c */
void hd_sha256(const uint8_t *p, size_t n, uint8_t out[32]);

/* zip.c: an entry of a zip in memory, unpacked into a new buffer (free it). */
typedef struct
{
   const uint8_t *data;
   size_t size;
} hd_zip;
/* Returns NULL and sets *err when the entry is missing, too big or damaged. */
uint8_t *hd_zip_read(const hd_zip *zip, const char *name, size_t *size, const char **err);

/* png.c: a PNG to 0xAARRGGBB pixels (free them); NULL and *err when it cannot. */
uint32_t *hd_png_read(const uint8_t *data, size_t size, int32_t *w, int32_t *h, const char **err);

/* json.c: a small JSON reader. Numbers must be integers (game data never needs floats). */
enum { JSON_NULL, JSON_BOOL, JSON_INT, JSON_STRING, JSON_ARRAY, JSON_OBJECT };
typedef struct json json;
struct json
{
   int type;
   int64_t num;    /* JSON_BOOL, JSON_INT */
   char *str;      /* JSON_STRING; the key of an object member in key */
   char *key;
   json *child;    /* first element or member */
   json *next;     /* next sibling */
   int count;      /* elements or members */
};
/* Parses text (not necessarily NUL-terminated); NULL and *err on bad JSON. Free with hd_json_free. */
json *hd_json_parse(const char *text, size_t len, const char **err);
void hd_json_free(json *j);
const json *hd_json_get(const json *obj, const char *key);
const json *hd_json_at(const json *arr, int i);

#endif
