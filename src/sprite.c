/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Format 3's "sprites": the heroes' own pictures (sprite.h).
 *
 *   "sprites": {"hero": {
 *      "players": ["red", "blue"],            a skin per player, repeated for the rest
 *      "skins": {"red": {
 *         "idle": {"file": "red_idle.png", "frame": [w, h], "fps": 10, "feet": 2},
 *         "run": {...}, "jump": {...}, "hurt": {...}, "bored": {...}, "win": {...}}}}}
 *
 * Each animation is one PNG with its frames in a row, all w x h: the frame
 * count is the picture's width / w. "feet" is the empty space under the
 * character's feet in each frame (0 by default). Every animation but idle
 * is optional.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sprite.h"

hd_skin hd_skins[MAX_SKINS];
hd_layer hd_layers[MAX_LAYERS];
int32_t hd_layer_count;
int32_t hd_skin_count;
int32_t hd_skin_of[MAX_PLAYERS];

static const char *const anim_names[ANIM_COUNT] = { "idle", "run", "jump", "hurt", "bored", "win" };

/* Every picture of every skin together may hold this many pixels (256 MB). */
#define SPRITE_PIXELS_MAX (64 * 1024 * 1024)

static int64_t pixels_used;

void hd_sprites_free(void)
{
   int32_t s, a;
   for (s = 0; s < MAX_SKINS; s++)
      for (a = 0; a < ANIM_COUNT; a++)
      {
         hd_anim *an = &hd_skins[s].anim[a];
         if (an->frames)
         {
            free(an->frames[0].px); /* one block for all the frames */
            free(an->frames);
         }
      }
   for (s = 0; s < hd_layer_count; s++)
      free(hd_layers[s].img.px);
   memset(hd_layers, 0, sizeof hd_layers);
   hd_layer_count = 0;
   memset(hd_skins, 0, sizeof hd_skins);
   memset(hd_skin_of, 0, sizeof hd_skin_of);
   hd_skin_count = 0;
   pixels_used = 0;
}

const hd_anim *hd_skin_anim(const hd_skin *sk, int32_t state)
{
   /* what stands in for a missing animation */
   static const int32_t fallback[ANIM_COUNT] = { ANIM_IDLE, ANIM_IDLE, ANIM_IDLE, ANIM_IDLE, ANIM_IDLE, ANIM_IDLE };
   if (state < 0 || state >= ANIM_COUNT)
      state = ANIM_IDLE;
   if (sk->anim[state].frames)
      return &sk->anim[state];
   state = fallback[state];
   return sk->anim[state].frames ? &sk->anim[state] : NULL;
}

static int32_t get_int(const json *obj, const char *key, int32_t lo, int32_t hi, int32_t fallback, int *bad)
{
   const json *j = hd_json_get(obj, key);
   if (!j)
      return fallback;
   if (j->type != JSON_INT || j->num < lo || j->num > hi)
   {
      *bad = 1;
      return fallback;
   }
   return (int32_t)j->num;
}

static const char *load_anim(const hd_zip *zip, const json *def, hd_anim *an, const char *skin, const char *name)
{
   static char msg[200];
   const json *file = hd_json_get(def, "file"), *frame = hd_json_get(def, "frame");
   const char *err;
   uint8_t *png;
   size_t size;
   uint32_t *px, *block;
   int32_t w, h, fw, fh, n, i, y;
   int bad = 0;
   if (def->type != JSON_OBJECT || !file || file->type != JSON_STRING || !frame || frame->type != JSON_ARRAY || frame->count != 2 ||
       hd_json_at(frame, 0)->type != JSON_INT || hd_json_at(frame, 1)->type != JSON_INT)
   {
      snprintf(msg, sizeof msg, "the sprite %s's %s needs a \"file\" and a \"frame\": [width, height]", skin, name);
      return msg;
   }
   fw = (int32_t)hd_json_at(frame, 0)->num;
   fh = (int32_t)hd_json_at(frame, 1)->num;
   an->fps = get_int(def, "fps", 1, 60, 10, &bad);
   an->feet = get_int(def, "feet", 0, 64, 0, &bad);
   if (bad || fw < 4 || fw > 512 || fh < 4 || fh > 512)
   {
      snprintf(msg, sizeof msg, "the sprite %s's %s: frames of 4 to 512 pixels, fps 1 to 60, feet 0 to 64", skin, name);
      return msg;
   }
   png = hd_zip_read(zip, file->str, &size, &err);
   if (!png)
   {
      snprintf(msg, sizeof msg, "%s: %s", file->str, err);
      return msg;
   }
   px = hd_png_read(png, size, &w, &h, &err);
   free(png);
   if (!px)
   {
      snprintf(msg, sizeof msg, "%s: %s", file->str, err);
      return msg;
   }
   if (h != fh || w % fw || w / fw < 1 || w / fw > 64)
   {
      free(px);
      snprintf(msg, sizeof msg, "%s must be one row of 1 to 64 frames of %d x %d pixels", file->str, (int)fw, (int)fh);
      return msg;
   }
   n = w / fw;
   if (pixels_used + (int64_t)w * h > SPRITE_PIXELS_MAX)
   {
      free(px);
      return "the sprites are bigger than go-link HD keeps (64 million pixels)";
   }
   block = (uint32_t *)malloc((size_t)w * h * 4);
   an->frames = (hd_image *)calloc((size_t)n, sizeof *an->frames);
   if (!block || !an->frames)
   {
      free(block);
      free(an->frames);
      an->frames = NULL;
      free(px);
      return "not enough memory for the sprites";
   }
   for (i = 0; i < n; i++)
   {
      an->frames[i].w = fw;
      an->frames[i].h = fh;
      an->frames[i].px = block + (size_t)i * fw * fh;
      for (y = 0; y < fh; y++)
         memcpy(an->frames[i].px + y * fw, px + (size_t)y * w + i * fw, (size_t)fw * 4);
   }
   an->count = n;
   pixels_used += (int64_t)w * h;
   free(px);
   return NULL;
}

const char *hd_sprites_load(const hd_zip *zip, const json *sprites)
{
   static char msg[200];
   const json *hero, *players, *skins, *sk, *p;
   int32_t i, a;
   hd_sprites_free();
   if (!sprites)
      return NULL;
   if (sprites->type != JSON_OBJECT)
      return "manifest.json's sprites must be an object";
   hero = hd_json_get(sprites, "hero");
   if (!hero)
      return NULL;
   players = hd_json_get(hero, "players");
   skins = hd_json_get(hero, "skins");
   if (hero->type != JSON_OBJECT || !players || players->type != JSON_ARRAY || players->count < 1 || players->count > MAX_SKINS ||
       !skins || skins->type != JSON_OBJECT)
      return "the hero's sprites need \"players\" (1 to 8 skin names) and \"skins\"";
   for (p = players->child, i = 0; p; p = p->next, i++)
   {
      if (p->type != JSON_STRING || !(sk = hd_json_get(skins, p->str)))
      {
         snprintf(msg, sizeof msg, "the hero's player %d wears a skin that is not in \"skins\"", (int)i + 1);
         hd_sprites_free();
         return msg;
      }
      for (a = 0; a < ANIM_COUNT; a++)
      {
         const json *def = hd_json_get(sk, anim_names[a]);
         const char *err;
         if (!def)
            continue;
         err = load_anim(zip, def, &hd_skins[i].anim[a], p->str, anim_names[a]);
         if (err)
         {
            hd_sprites_free();
            return err;
         }
      }
      if (!hd_skins[i].anim[ANIM_IDLE].frames)
      {
         snprintf(msg, sizeof msg, "the skin %s has no \"idle\"", p->str);
         hd_sprites_free();
         return msg;
      }
   }
   hd_skin_count = i;
   for (i = 0; i < MAX_PLAYERS; i++)
      hd_skin_of[i] = i % hd_skin_count;
   return NULL;
}

/*
 *   "layers": [{"file": "far.png", "speed": 25, "y": 0},
 *              {"file": "mid.png", "speed": 50, "y": 120},
 *              {"file": "front.png", "speed": 130, "y": 0, "front": true}]
 *
 * Back to front, in the list's order. A layer repeats across the level; its
 * picture may be opaque (a far sky) or see-through. They replace the
 * built-in clouds and hills; the manifest's sky still fills what no layer
 * covers.
 */
const char *hd_layers_load(const hd_zip *zip, const json *layers)
{
   static char msg[200];
   const json *it;
   int32_t i = 0;
   if (!layers)
      return NULL;
   if (layers->type != JSON_ARRAY || layers->count > MAX_LAYERS)
      return "manifest.json's layers must be a list of at most 8";
   for (it = layers->child; it; it = it->next, i++)
   {
      hd_layer *l = &hd_layers[i];
      const json *file = hd_json_get(it, "file"), *front = hd_json_get(it, "front");
      const char *err;
      uint8_t *png;
      size_t size;
      int bad = 0;
      if (it->type != JSON_OBJECT || !file || file->type != JSON_STRING)
         return "each layer needs a \"file\"";
      l->speed = get_int(it, "speed", 0, 400, 50, &bad);
      l->y = get_int(it, "y", -4096, 16384, 0, &bad);
      l->front = front && front->type == JSON_BOOL && front->num;
      if (bad)
         return "a layer's speed must be 0 to 400 and its y -4096 to 16384";
      png = hd_zip_read(zip, file->str, &size, &err);
      if (!png)
      {
         snprintf(msg, sizeof msg, "%s: %s", file->str, err);
         return msg;
      }
      l->img.px = hd_png_read(png, size, &l->img.w, &l->img.h, &err);
      free(png);
      if (!l->img.px)
      {
         snprintf(msg, sizeof msg, "%s: %s", file->str, err);
         return msg;
      }
      if (pixels_used + (int64_t)l->img.w * l->img.h > SPRITE_PIXELS_MAX)
      {
         free(l->img.px);
         l->img.px = NULL;
         return "the layers and sprites are bigger than go-link HD keeps (64 million pixels)";
      }
      pixels_used += (int64_t)l->img.w * l->img.h;
      hd_layer_count = i + 1;
   }
   return NULL;
}

static uint32_t blend(uint32_t dst, uint32_t src)
{
   uint32_t a = src >> 24, rb, g;
   if (a == 255)
      return src & 0xffffffu;
   rb = ((src & 0xff00ffu) * a + (dst & 0xff00ffu) * (255 - a)) >> 8;
   g = ((src & 0xff00u) * a + (dst & 0xff00u) * (255 - a)) >> 8;
   return (rb & 0xff00ffu) | (g & 0xff00u);
}

void hd_layers_draw(uint32_t *px, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t front)
{
   int32_t i, x, y;
   for (i = 0; i < hd_layer_count; i++)
   {
      const hd_layer *l = &hd_layers[i];
      int32_t lw = l->img.w, ox, top;
      if (l->front != front)
         continue;
      /* where the camera is, in the layer's own pixels */
      ox = (int32_t)(((int64_t)cx * l->speed / 100) % lw);
      if (ox < 0)
         ox += lw;
      top = l->y - (int32_t)((int64_t)cy * l->speed / 100);
      for (y = hd_max(top, 0); y < hd_min(top + l->img.h, h); y++)
      {
         const uint32_t *src = l->img.px + (size_t)(y - top) * lw;
         uint32_t *dst = px + (size_t)y * w;
         int32_t sx = ox;
         for (x = 0; x < w; x++)
         {
            uint32_t c = src[sx];
            if (c >> 24)
               dst[x] = blend(dst[x], c);
            if (++sx == lw)
               sx = 0;
         }
      }
   }
}
