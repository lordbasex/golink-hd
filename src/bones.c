/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/* Skeletal animation (bones.h). */
#include <string.h>
#include "bones.h"

/* The short way from a to b, in angle units (-2048..2047). */
static int32_t turn_between(int32_t a, int32_t b)
{
   return ((b - a + 2048) & (ANGLE_FULL - 1)) - 2048;
}

void bones_pose(const hd_anim *a, int32_t t, hd_pose *out)
{
   int32_t i, k = 0;
   const hd_key *k0, *k1;
   int32_t span, at;
   memset(out, 0, sizeof *out);
   if (a->count <= 0)
      return;
   if (a->loop && a->length > 0)
      t = ((t % a->length) + a->length) % a->length;
   else
      t = hd_clamp(t, 0, a->length);
   while (k + 1 < a->count && a->key[k + 1].frame <= t)
      k++;
   k0 = &a->key[k];
   if (k + 1 < a->count)
   {
      k1 = &a->key[k + 1];
      span = k1->frame - k0->frame;
   }
   else if (a->loop)
   {
      k1 = &a->key[0];
      span = a->length - k0->frame;
   }
   else
   {
      k1 = k0;
      span = 1;
   }
   at = span > 0 ? (t - k0->frame) * 256 / span : 0;
   for (i = 0; i < BONES_MAX; i++)
      out->angle[i] = k0->angle[i] + turn_between(k0->angle[i], k1->angle[i]) * at / 256;
   out->lift = k0->lift + (k1->lift - k0->lift) * at / 256;
}

void bones_blend(const hd_pose *a, const hd_pose *b, int32_t t, hd_pose *out)
{
   int32_t i;
   for (i = 0; i < BONES_MAX; i++)
      out->angle[i] = a->angle[i] + turn_between(a->angle[i], b->angle[i]) * t / 256;
   out->lift = a->lift + (b->lift - a->lift) * t / 256;
}

void bones_draw(hd_surface *s, const hd_skeleton *sk, const hd_pose *pose, int32_t x, int32_t y, int32_t scale, int32_t flip, const hd_style *style)
{
   int32_t world[BONES_MAX];          /* each bone's direction */
   int64_t bx[BONES_MAX], by[BONES_MAX]; /* where each bone starts, 16.16 */
   int32_t order[BONES_MAX], i, j;
   hd_style st;
   memset(&st, 0, sizeof st);
   if (style)
      st = *style;
   /* bones come parents first: each starts where its parent ends */
   for (i = 0; i < sk->bones; i++)
   {
      const hd_bone *b = &sk->bone[i];
      int32_t p = b->parent;
      if (p < 0 || p >= i)
      {
         world[i] = b->angle + pose->angle[i];
         bx[i] = (int64_t)x << 16;
         by[i] = ((int64_t)y << 16) - ((int64_t)pose->lift * scale);
      }
      else
      {
         int64_t len = (int64_t)sk->bone[p].length * scale; /* 16.16 */
         world[i] = world[p] + b->angle + pose->angle[i];
         bx[i] = bx[p] + ((len * hd_cos(world[p])) >> 14) * (flip ? -1 : 1);
         by[i] = by[p] + ((len * hd_sin(world[p])) >> 14);
      }
   }
   /* parts back to front */
   for (i = 0; i < sk->parts; i++)
      order[i] = i;
   for (i = 1; i < sk->parts; i++)
      for (j = i; j > 0 && sk->part[order[j]].z < sk->part[order[j - 1]].z; j--)
      {
         int32_t t = order[j];
         order[j] = order[j - 1];
         order[j - 1] = t;
      }
   for (i = 0; i < sk->parts; i++)
   {
      const hd_part *pt = &sk->part[order[i]];
      int32_t b = pt->bone, rot;
      if (b < 0 || b >= sk->bones || !pt->im)
         continue;
      /* a picture drawn lying along +x is turned to the bone; mirrored, it turns the other way */
      rot = world[b] + pt->turn;
      st.flags = (style ? style->flags : 0) ^ (flip ? DRAW_FLIP_X : 0);
      gfx_blit_rot(s, pt->im, (int32_t)(bx[b] >> 16), (int32_t)(by[b] >> 16), flip ? pt->im->w - 1 - pt->px : pt->px, pt->py,
                   flip ? -rot : rot, scale, scale, &st);
   }
}
