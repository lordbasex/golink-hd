/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Format 3's "sprites": the heroes' own pictures (sprite.h).
 *
 *   "sprites": {"hero": {
 *      "players": ["red", "blue"],            a skin per player, repeated for the rest
 *      "skins": {"red": {
 *         "idle": {"file": "red_idle.png", "frame": [w, h], "fps": 10, "feet": 2},
 *         "run": {"file": "red_run.png", "frame": [w, h], "stride": 120},
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
hd_anim hd_objects[OBJ_COUNT];
hd_image hd_textures[TEX_COUNT];
hd_image hd_screens[SCREEN_COUNT];
int32_t hd_intro_frames;
hd_layer hd_layers[MAX_LAYERS];
int32_t hd_layer_count;
int32_t hd_skin_count;
int32_t hd_skin_of[MAX_PLAYERS];

static const char *const anim_names[ANIM_COUNT] = { "idle", "run", "jump", "hurt", "bored", "win", "shot", "shot_hit", "knockout", "super", "granule", "dash" };

/* Every picture of the package together (every level's too) may hold this many pixels (384 MB). */
#define SPRITE_PIXELS_MAX (96 * 1024 * 1024)

static int64_t pixels_used;

void hd_sprites_free(void)
{
   int32_t s, a;
   for (s = 0; s < MAX_SKINS; s++)
   {
      hd_anim *parts[4] = { &hd_skins[s].rig.body, &hd_skins[s].rig.hand, &hd_skins[s].rig.foot, &hd_skins[s].rig.worn };
      for (a = 0; a < 4; a++)
         if (parts[a]->frames)
         {
            free(parts[a]->frames[0].px);
            free(parts[a]->frames);
         }
   }
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
   for (s = 0; s < OBJ_COUNT; s++)
      if (hd_objects[s].frames)
      {
         free(hd_objects[s].frames[0].px);
         free(hd_objects[s].frames);
      }
   memset(hd_objects, 0, sizeof hd_objects);
   for (s = 0; s < TEX_COUNT; s++)
      free(hd_textures[s].px);
   for (s = 0; s < SCREEN_COUNT; s++)
      free(hd_screens[s].px);
   memset(hd_screens, 0, sizeof hd_screens);
   hd_intro_frames = 0;
   memset(hd_textures, 0, sizeof hd_textures);
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

void hd_anim_free(hd_anim *an)
{
   if (an->frames)
   {
      free(an->frames[0].px); /* one block for all the frames */
      free(an->frames);
   }
   memset(an, 0, sizeof *an);
}

const char *hd_anim_load(const hd_zip *zip, const json *def, hd_anim *an, const char *skin, const char *name)
{
   static char msg[200];
   const json *file = hd_json_get(def, "file"), *frame = hd_json_get(def, "frame");
   const char *err;
   uint8_t *png;
   size_t size;
   uint32_t *px, *block;
   int32_t w, h, fw, fh, dw, dh, n, i, y, first;
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
   an->feet = hd_art_px(get_int(def, "feet", 0, 64 * hd_art, 0, &bad));
   an->stride = get_int(def, "stride", 0, 4096, 0, &bad); /* walked in the game's pixels */
   if (bad || fw < 4 || fw > 512 * hd_art || fh < 4 || fh > 512 * hd_art)
   {
      snprintf(msg, sizeof msg, "the sprite %s's %s: frames of 4 to 512 pixels (times the art_scale), fps 1 to 60, feet 0 to 64", skin, name);
      return msg;
   }
   /* the frames as they are drawn */
   dw = hd_max(1, hd_art_px(fw));
   dh = hd_max(1, hd_art_px(fh));
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
   /* "from" and "frames": only some of the picture's frames */
   first = get_int(def, "from", 0, w / fw - 1, 0, &bad);
   n = get_int(def, "frames", 1, 64, w / fw - first, &bad);
   if (bad || first + n > w / fw)
   {
      free(px);
      snprintf(msg, sizeof msg, "%s: \"from\" and \"frames\" must stay inside its %d frames", file->str, (int)(w / fw));
      return msg;
   }
   if (pixels_used + (int64_t)n * dw * dh > SPRITE_PIXELS_MAX)
   {
      free(px);
      return "the sprites are bigger than go-link HD keeps (96 million pixels)";
   }
   hd_pic_clean(px, (int64_t)w * h);
   block = (uint32_t *)malloc((size_t)n * dw * dh * 4);
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
      const uint32_t *src = px + (size_t)(first + i) * fw;
      an->frames[i].w = dw;
      an->frames[i].h = dh;
      an->frames[i].px = block + (size_t)i * dw * dh;
      if (dw == fw && dh == fh)
         for (y = 0; y < fh; y++)
            memcpy(an->frames[i].px + y * fw, src + (size_t)y * w, (size_t)fw * 4);
      else
      {
         uint32_t *small = hd_pic_shrink(src, w, fw, fh, dw, dh);
         if (!small)
         {
            free(block);
            free(an->frames);
            an->frames = NULL;
            free(px);
            return "not enough memory for the sprites";
         }
         memcpy(an->frames[i].px, small, (size_t)dw * dh * 4);
         free(small);
      }
   }
   an->count = n;
   pixels_used += (int64_t)n * dw * dh;
   free(px);
   return NULL;
}

static const char *load_rig(const hd_zip *zip, const json *rig, hd_rig *r, const char *skin)
{
   static char msg[200];
   static const char *const parts[3] = { "body", "hand", "foot" };
   hd_anim *an[3];
   int32_t k;
   int bad = 0;
   an[0] = &r->body;
   an[1] = &r->hand;
   an[2] = &r->foot;
   if (rig->type != JSON_OBJECT)
      return "a skin's rig must be an object";
   for (k = 0; k < 3; k++)
   {
      const json *def = hd_json_get(rig, parts[k]);
      const char *err;
      if (!def)
      {
         snprintf(msg, sizeof msg, "the rig of %s needs a \"%s\"", skin, parts[k]);
         return msg;
      }
      err = hd_anim_load(zip, def, an[k], skin, parts[k]);
      if (err)
         return err;
   }
   if (hd_json_get(rig, "worn"))
   {
      const char *err = hd_anim_load(zip, hd_json_get(rig, "worn"), &r->worn, skin, "worn");
      if (err)
         return err;
   }
   /* the hoses are drawn at the pictures' scale (art_scale times the numbers below); the stride is walked in the game's pixels */
   r->limb = hd_max(1, hd_art_px(get_int(rig, "limb", 1, 32 * hd_art, 5 * hd_art, &bad)));
   r->leg = hd_art_px(get_int(rig, "leg", 4, 200 * hd_art, 24 * hd_art, &bad));
   r->arm = hd_art_px(get_int(rig, "arm", 4, 200 * hd_art, 22 * hd_art, &bad));
   r->stride = get_int(rig, "stride", 0, 4096, 110, &bad);
   r->lift = hd_art_px(get_int(rig, "lift", 0, 100 * hd_art, 9 * hd_art, &bad));
   r->bob = hd_art_px(get_int(rig, "bob", 0, 50 * hd_art, 3 * hd_art, &bad));
   if (bad)
      return "a rig's sizes are out of range (limb 1-32, leg and arm 4-200, lift 0-100, bob 0-50, all times the art_scale; stride 0-4096)";
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
   {
      /* the level's things: an animation each, or two for the checkpoint and the enemy */
      static const struct { const char *key, *sub; int32_t obj; } objs[] = {
         { "coin", NULL, OBJ_COIN }, { "goal", NULL, OBJ_GOAL },
         { "checkpoint", "off", OBJ_CHECK_OFF }, { "checkpoint", "on", OBJ_CHECK_ON },
         { "enemy", "walk", OBJ_ENEMY_WALK }, { "enemy", "squashed", OBJ_ENEMY_SQUASHED },
         { "spore", "fly", OBJ_SPORE_FLY }, { "spore", "pop", OBJ_SPORE_POP },
         { "spitter", "idle", OBJ_SPITTER_IDLE }, { "spitter", "spit", OBJ_SPITTER_SPIT }, { "spitter", "squashed", OBJ_SPITTER_SQUASHED },
         { "spit", NULL, OBJ_SPIT },
      };
      size_t k;
      for (k = 0; k < sizeof objs / sizeof objs[0]; k++)
      {
         const json *def = hd_json_get(sprites, objs[k].key);
         const char *err;
         if (def && objs[k].sub)
            def = hd_json_get(def, objs[k].sub);
         if (!def)
            continue;
         err = hd_anim_load(zip, def, &hd_objects[objs[k].obj], objs[k].key, objs[k].sub ? objs[k].sub : "animation");
         if (err)
         {
            hd_sprites_free();
            return err;
         }
      }
   }
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
         err = hd_anim_load(zip, def, &hd_skins[i].anim[a], p->str, anim_names[a]);
         if (err)
         {
            hd_sprites_free();
            return err;
         }
      }
      {
         const json *rig = hd_json_get(sk, "rig");
         if (rig)
         {
            const char *err = load_rig(zip, rig, &hd_skins[i].rig, p->str);
            if (err)
            {
               hd_sprites_free();
               return err;
            }
            hd_skins[i].has_rig = 1;
            continue; /* a puppet needs no animations */
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
      hd_pic_clean(l->img.px, (int64_t)l->img.w * l->img.h);
      if (hd_art != hd_res)
      {
         int32_t dw = hd_max(1, hd_art_px(l->img.w)), dh = hd_max(1, hd_art_px(l->img.h));
         uint32_t *small = hd_pic_shrink(l->img.px, l->img.w, l->img.w, l->img.h, dw, dh);
         free(l->img.px);
         l->img.px = small;
         l->img.w = dw;
         l->img.h = dh;
         if (!small)
            return "not enough memory for the layers";
      }
      if (pixels_used + (int64_t)l->img.w * l->img.h > SPRITE_PIXELS_MAX)
      {
         free(l->img.px);
         l->img.px = NULL;
         return "the layers and sprites are bigger than go-link HD keeps (96 million pixels)";
      }
      pixels_used += (int64_t)l->img.w * l->img.h;
      {
         int64_t k, n = (int64_t)l->img.w * l->img.h;
         for (k = 0; k < n && (l->img.px[k] >> 24) == 255; k++)
            ;
         l->opaque = k == n;
      }
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

int hd_layers_cover(int32_t h, int32_t cy)
{
   int32_t i;
   for (i = 0; i < hd_layer_count; i++)
   {
      const hd_layer *l = &hd_layers[i];
      int32_t top = l->y * hd_res - (int32_t)((int64_t)cy * l->speed / 100);
      if (!l->front && l->opaque && top <= 0 && top + l->img.h >= h)
         return 1;
   }
   return 0;
}

typedef struct
{
   uint32_t *px;
   int32_t w, h, cx, cy, front;
} layers_job;

/* The layers on rows y0..y1 of the surface. */
static void layers_rows(void *ctx, int32_t y0, int32_t y1)
{
   const layers_job *j = (const layers_job *)ctx;
   uint32_t *px = j->px;
   int32_t w = j->w, h = y1, cx = j->cx, cy = j->cy, front = j->front;
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
      top = l->y * hd_res - (int32_t)((int64_t)cy * l->speed / 100); /* y in logical pixels, the picture at the drawing's scale */
      for (y = hd_max(top, y0); y < hd_min(top + l->img.h, h); y++)
      {
         const uint32_t *src = l->img.px + (size_t)(y - top) * lw;
         uint32_t *dst = px + (size_t)y * w;
         int32_t sx = ox;
         if (l->opaque)
         {
            /* solid: the row copied, a run at a time where the picture repeats */
            for (x = 0; x < w;)
            {
               int32_t run = hd_min(w - x, lw - sx), k;
               for (k = 0; k < run; k++)
                  dst[x + k] = src[sx + k] & 0xffffffu;
               x += run;
               sx = 0;
            }
            continue;
         }
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

void hd_layers_draw(uint32_t *px, int32_t w, int32_t h, int32_t cx, int32_t cy, int32_t front)
{
   layers_job j;
   j.px = px;
   j.w = w;
   j.h = h;
   j.cx = cx;
   j.cy = cy;
   j.front = front;
   hd_rows(w, h, layers_rows, &j);
}

/*
 *   "textures": {"ground_top": "flesh_top.png", "ground": "flesh.png",
 *                "brick": "cell.png", "platform": "valve.png"}
 *
 * Each picture's sides are multiples of 16 (16 to 1024): it is laid over
 * the level and each cell of that kind shows its own 16 x 16 piece, so a
 * floor keeps a painting's detail on the level's grid. See-through pixels
 * stay see-through (a platform's edge).
 *
 * With a top band of their own ("ground_top", "brick_top") a floor and a
 * wall are drawn in two layers: the inside ("ground", "brick") over every
 * cell, and the band over the top cells, laid on it (its lower edge is part
 * of its picture), so cells next to each other always meet whatever their
 * height. "brick_bottom" (optional) is the wall's inside with each cell's
 * bottom edged, for a wall nothing holds up.
 *
 * "ground_left", "ground_right", "ground_top_left", ... "brick_bottom_right":
 * the same picture with each cell's piece cut as the end of a run (its
 * outer side rounded off and inked), drawn in the cell where a floor or a
 * platform stops; the same size as the kind's own texture, so the end
 * leads into the next cell's piece.
 */
const char *hd_textures_load(const hd_zip *zip, const json *tex)
{
   static char msg[200];
   static const char *const kinds[TX_KINDS] = { "ground_top", "ground", "brick", "platform", "brick_top", "brick_bottom" };
   static const char *const sides[3] = { "", "_left", "_right" };
   int32_t i;
   if (!tex)
      return NULL;
   if (tex->type != JSON_OBJECT)
      return "manifest.json's textures must be an object";
   for (i = 0; i < TEX_COUNT; i++)
   {
      char name[32];
      const json *f;
      const char *err;
      uint8_t *png;
      size_t size;
      hd_image *im = &hd_textures[i];
      snprintf(name, sizeof name, "%s%s", kinds[i % TX_KINDS], sides[i / TX_KINDS]);
      f = hd_json_get(tex, name);
      if (!f)
         continue;
      if (f->type != JSON_STRING)
         return "each texture must be a file name";
      png = hd_zip_read(zip, f->str, &size, &err);
      if (!png)
      {
         snprintf(msg, sizeof msg, "%s: %s", f->str, err);
         return msg;
      }
      im->px = hd_png_read(png, size, &im->w, &im->h, &err);
      free(png);
      if (!im->px)
      {
         snprintf(msg, sizeof msg, "%s: %s", f->str, err);
         return msg;
      }
      if (im->w % (16 * hd_art) || im->h % (16 * hd_art) || im->w > 1024 * hd_art || im->h > 1024 * hd_art)
      {
         free(im->px);
         im->px = NULL;
         snprintf(msg, sizeof msg, "%s: a texture's sides must be multiples of 16, up to 1024 (times the art_scale: 2 at 720p, 3 at 1080p)", f->str);
         return msg;
      }
      hd_pic_clean(im->px, (int64_t)im->w * im->h);
      if (hd_art != hd_res)
      {
         /* a 16 * art_scale piece per cell becomes 16 * hd_res: exact, the cells stay whole */
         int32_t dw = im->w / hd_art * hd_res, dh = im->h / hd_art * hd_res;
         uint32_t *small = hd_pic_shrink(im->px, im->w, im->w, im->h, dw, dh);
         free(im->px);
         im->px = small;
         im->w = dw;
         im->h = dh;
         if (!small)
            return "not enough memory for the textures";
      }
      if (pixels_used + (int64_t)im->w * im->h > SPRITE_PIXELS_MAX)
      {
         free(im->px);
         im->px = NULL;
         return "the textures, layers and sprites are bigger than go-link HD keeps (96 million pixels)";
      }
      pixels_used += (int64_t)im->w * im->h;
   }
   /* an end is its kind's picture cut: the same size, laid the same way */
   for (i = TX_KINDS; i < TEX_COUNT; i++)
   {
      const hd_image *im = &hd_textures[i], *own = &hd_textures[i % TX_KINDS];
      if (im->px && (!own->px || im->w != own->w || im->h != own->h))
      {
         snprintf(msg, sizeof msg, "the texture %s%s must be the same size as %s", kinds[i % TX_KINDS], sides[i / TX_KINDS], kinds[i % TX_KINDS]);
         return msg;
      }
   }
   /* a wall's bottom is its inside cut: the same size */
   if (hd_textures[TX_BRICK_BOTTOM].px && (!hd_textures[TL_BRICK].px || hd_textures[TX_BRICK_BOTTOM].w != hd_textures[TL_BRICK].w ||
                                           hd_textures[TX_BRICK_BOTTOM].h != hd_textures[TL_BRICK].h))
      return "the texture brick_bottom must be the same size as brick";
   return NULL;
}

/* A picture scaled to w x h, cropped to that shape from its middle (bilinear, in integers: the same pixels everywhere). */
static uint32_t *fit(const uint32_t *px, int32_t pw, int32_t ph, int32_t w, int32_t h)
{
   uint32_t *out = (uint32_t *)malloc((size_t)w * h * 4);
   int32_t cw = pw, ch = ph, x0 = 0, y0 = 0, x, y;
   if (!out)
      return NULL;
   /* the biggest part of the picture with the screen's shape */
   if ((int64_t)pw * h > (int64_t)ph * w)
   {
      cw = (int32_t)((int64_t)ph * w / h);
      x0 = (pw - cw) / 2;
   }
   else
   {
      ch = (int32_t)((int64_t)pw * h / w);
      y0 = (ph - ch) / 2;
   }
   /* never an empty crop (a very thin picture): rows and columns stay inside it */
   cw = hd_clamp(cw, 1, pw);
   ch = hd_clamp(ch, 1, ph);
   x0 = hd_clamp(x0, 0, pw - cw);
   y0 = hd_clamp(y0, 0, ph - ch);
   for (y = 0; y < h; y++)
   {
      int32_t sy = (int32_t)(((int64_t)y * 2 + 1) * ch * 128 / h) - 128; /* 1/256 pixels, centers aligned */
      int32_t iy = hd_clamp(sy >> 8, 0, ch - 1), fy = sy < 0 ? 0 : sy & 255, iy2 = hd_min(iy + 1, ch - 1);
      for (x = 0; x < w; x++)
      {
         int32_t sx = (int32_t)(((int64_t)x * 2 + 1) * cw * 128 / w) - 128;
         int32_t ix = hd_clamp(sx >> 8, 0, cw - 1), fx = sx < 0 ? 0 : sx & 255, ix2 = hd_min(ix + 1, cw - 1);
         const uint32_t *r1 = px + (size_t)(y0 + iy) * pw + x0, *r2 = px + (size_t)(y0 + iy2) * pw + x0;
         uint32_t c = 0;
         int32_t k;
         for (k = 0; k < 32; k += 8)
         {
            int32_t a = (int32_t)(r1[ix] >> k & 255), b = (int32_t)(r1[ix2] >> k & 255);
            int32_t cc = (int32_t)(r2[ix] >> k & 255), d = (int32_t)(r2[ix2] >> k & 255);
            int32_t top = a * (256 - fx) + b * fx, bot = cc * (256 - fx) + d * fx;
            c |= (uint32_t)((top * (256 - fy) + bot * fy) >> 16) << k;
         }
         out[(size_t)y * w + x] = c | 0xff000000u;
      }
   }
   return out;
}

/*
 *   "screens": {"title": "start.png", "intro": "level1.png", "ending": "end.png", "intro_seconds": 5}
 */
const char *hd_screens_load(const hd_zip *zip, const json *screens)
{
   static const char *const keys[SCREEN_COUNT] = { "title", "intro", "ending" };
   const json *secs;
   int32_t i;
   hd_intro_frames = 5 * HD_FPS;
   if (!screens)
      return NULL;
   if (screens->type != JSON_OBJECT)
      return "manifest.json's screens must be an object";
   if ((secs = hd_json_get(screens, "intro_seconds")))
   {
      if (secs->type != JSON_INT || secs->num < 1 || secs->num > 30)
         return "the screens' intro_seconds must be 1 to 30";
      hd_intro_frames = (int32_t)secs->num * HD_FPS;
   }
   for (i = 0; i < SCREEN_COUNT; i++)
   {
      const json *f = hd_json_get(screens, keys[i]);
      const char *err;
      if (f && (err = hd_screen_load(zip, f, i)))
         return err;
   }
   return NULL;
}

const char *hd_screen_load(const hd_zip *zip, const json *f, int32_t i)
{
   static char msg[200];
   {
      const char *err;
      uint8_t *png;
      size_t size;
      uint32_t *px;
      int32_t w, h;
      if (f->type != JSON_STRING)
         return "each screen must be a file name";
      png = hd_zip_read(zip, f->str, &size, &err);
      if (!png)
      {
         snprintf(msg, sizeof msg, "%s: %s", f->str, err);
         return msg;
      }
      px = hd_png_read(png, size, &w, &h, &err);
      free(png);
      if (!px)
      {
         snprintf(msg, sizeof msg, "%s: %s", f->str, err);
         return msg;
      }
      if (w < 16 || h < 16)
      {
         free(px);
         snprintf(msg, sizeof msg, "%s: a screen must be at least 16 x 16 pixels", f->str);
         return msg;
      }
      hd_screens[i].px = fit(px, w, h, HD_OUT_W, HD_OUT_H);
      free(px);
      if (!hd_screens[i].px)
         return "not enough memory for the screens";
      hd_screens[i].w = HD_OUT_W;
      hd_screens[i].h = HD_OUT_H;
   }
   return NULL;
}
