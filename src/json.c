/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * A small JSON reader for a package's manifest and levels. Numbers must be
 * integers: game data never needs floats, and the engine keeps away from
 * them. Strings keep \uXXXX escapes as UTF-8. Nesting is limited to 32
 * levels and the text to the package's entry limit.
 */
#include <stdlib.h>
#include <string.h>
#include "pack.h"

typedef struct
{
   const char *p, *end;
   const char *err;
   int depth;
} reader;

static void skip(reader *r)
{
   while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r'))
      r->p++;
}

static json *node(reader *r, int type)
{
   json *j = (json *)calloc(1, sizeof *j);
   if (!j)
      r->err = "out of memory";
   else
      j->type = type;
   return j;
}

static void put_utf8(char *out, size_t *n, uint32_t c)
{
   if (c < 0x80)
      out[(*n)++] = (char)c;
   else if (c < 0x800)
   {
      out[(*n)++] = (char)(0xc0 | c >> 6);
      out[(*n)++] = (char)(0x80 | (c & 0x3f));
   }
   else if (c < 0x10000)
   {
      out[(*n)++] = (char)(0xe0 | c >> 12);
      out[(*n)++] = (char)(0x80 | ((c >> 6) & 0x3f));
      out[(*n)++] = (char)(0x80 | (c & 0x3f));
   }
   else
   {
      out[(*n)++] = (char)(0xf0 | c >> 18);
      out[(*n)++] = (char)(0x80 | ((c >> 12) & 0x3f));
      out[(*n)++] = (char)(0x80 | ((c >> 6) & 0x3f));
      out[(*n)++] = (char)(0x80 | (c & 0x3f));
   }
}

static int hex4(reader *r, uint32_t *v)
{
   int i;
   *v = 0;
   if (r->end - r->p < 4)
      return 0;
   for (i = 0; i < 4; i++)
   {
      char c = *r->p++;
      *v <<= 4;
      if (c >= '0' && c <= '9') *v |= (uint32_t)(c - '0');
      else if (c >= 'a' && c <= 'f') *v |= (uint32_t)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') *v |= (uint32_t)(c - 'A' + 10);
      else return 0;
   }
   return 1;
}

/* A string after its opening quote; a new NUL-terminated copy. */
static char *string(reader *r)
{
   const char *start = r->p;
   char *out;
   size_t n = 0;
   /* an escaped string is never longer than its text */
   while (r->p < r->end && *r->p != '"')
      r->p += (*r->p == '\\' && r->p + 1 < r->end) ? 2 : 1;
   if (r->p >= r->end)
   {
      r->err = "a JSON string is not closed";
      return NULL;
   }
   out = (char *)malloc((size_t)(r->p - start) + 1);
   if (!out)
   {
      r->err = "out of memory";
      return NULL;
   }
   r->p = start;
   while (*r->p != '"')
   {
      char c = *r->p++;
      if ((unsigned char)c < 0x20)
      {
         r->err = "a JSON string has a control character";
         free(out);
         return NULL;
      }
      if (c != '\\')
      {
         out[n++] = c;
         continue;
      }
      c = *r->p++;
      switch (c)
      {
      case '"': case '\\': case '/': out[n++] = c; break;
      case 'b': out[n++] = '\b'; break;
      case 'f': out[n++] = '\f'; break;
      case 'n': out[n++] = '\n'; break;
      case 'r': out[n++] = '\r'; break;
      case 't': out[n++] = '\t'; break;
      case 'u':
      {
         uint32_t v;
         if (!hex4(r, &v) || v == 0)
         {
            r->err = "a JSON string has a bad \\u escape";
            free(out);
            return NULL;
         }
         /* \uXXXX takes 6 bytes of text and at most 3 of UTF-8 (surrogates are kept as is) */
         put_utf8(out, &n, v);
         break;
      }
      default:
         r->err = "a JSON string has a bad escape";
         free(out);
         return NULL;
      }
   }
   r->p++;
   out[n] = 0;
   return out;
}

static json *value(reader *r);

static json *container(reader *r, int type)
{
   json *j = node(r, type), *last = NULL;
   char close = type == JSON_ARRAY ? ']' : '}';
   if (!j)
      return NULL;
   if (++r->depth > 32)
   {
      r->err = "the JSON nests too deep";
      return j;
   }
   skip(r);
   if (r->p < r->end && *r->p == close)
   {
      r->p++;
      r->depth--;
      return j;
   }
   for (;;)
   {
      json *item;
      char *key = NULL;
      skip(r);
      if (type == JSON_OBJECT)
      {
         if (r->p >= r->end || *r->p != '"')
         {
            r->err = "a JSON object's key is not a string";
            return j;
         }
         r->p++;
         key = string(r);
         if (!key)
            return j;
         skip(r);
         if (r->p >= r->end || *r->p != ':')
         {
            free(key);
            r->err = "a JSON object has no ':' after a key";
            return j;
         }
         r->p++;
      }
      item = value(r);
      if (!item)
      {
         free(key);
         return j;
      }
      item->key = key;
      if (last)
         last->next = item;
      else
         j->child = item;
      last = item;
      j->count++;
      if (r->err)
         return j;
      skip(r);
      if (r->p < r->end && *r->p == ',')
      {
         r->p++;
         continue;
      }
      if (r->p < r->end && *r->p == close)
      {
         r->p++;
         r->depth--;
         return j;
      }
      r->err = type == JSON_ARRAY ? "a JSON array is not closed" : "a JSON object is not closed";
      return j;
   }
}

static int word(reader *r, const char *w)
{
   size_t n = strlen(w);
   if ((size_t)(r->end - r->p) >= n && memcmp(r->p, w, n) == 0)
   {
      r->p += n;
      return 1;
   }
   return 0;
}

static json *value(reader *r)
{
   json *j;
   skip(r);
   if (r->p >= r->end)
   {
      r->err = "the JSON ends too soon";
      return NULL;
   }
   switch (*r->p)
   {
   case '{':
      r->p++;
      return container(r, JSON_OBJECT);
   case '[':
      r->p++;
      return container(r, JSON_ARRAY);
   case '"':
      r->p++;
      j = node(r, JSON_STRING);
      if (j && !(j->str = string(r)))
      {
         free(j);
         return NULL;
      }
      return j;
   }
   if (word(r, "true") || word(r, "false"))
   {
      j = node(r, JSON_BOOL);
      if (j)
         j->num = r->p[-1] == 'e' && r->p[-2] == 'u';
      return j;
   }
   if (word(r, "null"))
      return node(r, JSON_NULL);
   if (*r->p == '-' || (*r->p >= '0' && *r->p <= '9'))
   {
      int neg = *r->p == '-';
      int64_t v = 0;
      int digits = 0;
      if (neg)
         r->p++;
      while (r->p < r->end && *r->p >= '0' && *r->p <= '9')
      {
         if (++digits > 15)
         {
            r->err = "a JSON number is too big";
            return NULL;
         }
         v = v * 10 + (*r->p++ - '0');
      }
      if (!digits || (r->p < r->end && (*r->p == '.' || *r->p == 'e' || *r->p == 'E')))
      {
         r->err = "a JSON number is not an integer";
         return NULL;
      }
      j = node(r, JSON_INT);
      if (j)
         j->num = neg ? -v : v;
      return j;
   }
   r->err = "the JSON has an unexpected character";
   return NULL;
}

void hd_json_free(json *j)
{
   while (j)
   {
      json *next = j->next;
      hd_json_free(j->child);
      free(j->str);
      free(j->key);
      free(j);
      j = next;
   }
}

json *hd_json_parse(const char *text, size_t len, const char **err)
{
   reader r;
   json *j;
   r.p = text;
   r.end = text + len;
   r.err = NULL;
   r.depth = 0;
   if (len > PACK_MAX_ENTRY)
   {
      *err = "a JSON file is too big";
      return NULL;
   }
   j = value(&r);
   if (!r.err)
   {
      skip(&r);
      if (r.p != r.end)
         r.err = "the JSON has text after its end";
   }
   if (r.err)
   {
      hd_json_free(j);
      *err = r.err;
      return NULL;
   }
   *err = NULL;
   return j;
}

const json *hd_json_get(const json *obj, const char *key)
{
   const json *c;
   if (!obj || obj->type != JSON_OBJECT)
      return NULL;
   for (c = obj->child; c; c = c->next)
      if (c->key && strcmp(c->key, key) == 0)
         return c;
   return NULL;
}

const json *hd_json_at(const json *arr, int i)
{
   const json *c;
   if (!arr || arr->type != JSON_ARRAY || i < 0)
      return NULL;
   for (c = arr->child; c && i; c = c->next)
      i--;
   return c;
}
