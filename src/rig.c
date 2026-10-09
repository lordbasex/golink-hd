/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * A rubber-hose puppet (format 3's "rig"): a body picture with faces,
 * gloves and shoes, and arms and legs drawn as black hoses, posed again on
 * every frame from the player's state, so the movement is as smooth as
 * the screen (60 frames a second) with a handful of pictures.
 *
 *   "rig": {
 *     "body": {"file": "red_body.png", "frame": [w, h]},     faces in a row: normal, blink, shout, hurt, tired, yawn
 *     "hand": {"file": "red_hands.png", "frame": [w, h]},    gloves in a row: open, fist, gun, wave, up, palm (wrist on the left)
 *     "foot": {"file": "red_feet.png", "frame": [w, h]},     shoes in a row: flat, toe down, heel down, in the air (toe on the right)
 *     "limb": 5, "leg": 24, "arm": 22, "stride": 110, "lift": 9, "bob": 3
 *   }
 *
 * limb is the hoses' width, leg and arm their length, stride the pixels of
 * one walk cycle (two steps), lift how high a foot rises, bob how much the
 * body bounces. Everything is integer math on the engine's sine table.
 */
#include <stdlib.h>
#include <string.h>
#include "sprite.h"
#include "gfx.h"

enum { FACE_NORMAL = 0, FACE_BLINK, FACE_SHOUT, FACE_HURT, FACE_TIRED, FACE_YAWN };
enum { HAND_OPEN = 0, HAND_FIST, HAND_GUN, HAND_WAVE, HAND_UP, HAND_PALM };
enum { FOOT_FLAT = 0, FOOT_TOE, FOOT_HEEL, FOOT_AIR };

/* A filled disc. */
static void disc(hd_surface *s, int32_t cx, int32_t cy, int32_t r, uint32_t c)
{
   int32_t x, y;
   for (y = hd_max(cy - r, 0); y <= hd_min(cy + r, s->h - 1); y++)
   {
      int32_t dy = y - cy;
      for (x = hd_max(cx - r, 0); x <= hd_min(cx + r, s->w - 1); x++)
      {
         int32_t dx = x - cx;
         if (dx * dx + dy * dy <= r * r + r)
            s->px[y * s->w + x] = c;
      }
   }
}

/* A hose from (x0, y0) to (x1, y1) bending through (bx, by): a quadratic curve of discs. */
static void hose(hd_surface *s, int32_t x0, int32_t y0, int32_t bx, int32_t by, int32_t x1, int32_t y1, int32_t width)
{
   int32_t len = hd_abs(x1 - x0) + hd_abs(y1 - y0) + hd_abs(bx - x0) + hd_abs(by - y0);
   int32_t steps = hd_max(8, len), i, r = hd_max(1, width / 2);
   for (i = 0; i <= steps; i++)
   {
      /* (1-t)^2 p0 + 2 t (1-t) b + t^2 p1, t = i / steps */
      int64_t t = i, u = steps - i, n = (int64_t)steps * steps;
      int32_t x = (int32_t)((u * u * x0 + 2 * t * u * bx + t * t * x1) / n);
      int32_t y = (int32_t)((u * u * y0 + 2 * t * u * by + t * t * y1) / n);
      disc(s, x, y, r, 0x101010u);
   }
}

/* One picture of a parts sheet. */
static const hd_image *part(const hd_anim *an, int32_t k)
{
   return &an->frames[hd_clamp(k, 0, an->count - 1)];
}

/* A part drawn with its pivot (px, py, in the picture) at (x, y), mirrored when facing left, scaled (16.16). */
static void place(hd_surface *s, const hd_image *im, int32_t x, int32_t y, int32_t px, int32_t py, int32_t left, int32_t sx, int32_t sy, int32_t white)
{
   hd_style st;
   memset(&st, 0, sizeof st);
   st.flags = (left ? DRAW_FLIP_X : 0) | (white ? DRAW_WHITE : 0);
   gfx_blit_rot(s, im, x, y, left ? im->w - px : px, py, 0, sx, sy, &st);
}

/* x of a point `dx` in front of the body (mirrored when facing left). */
#define FWD(dx) (left ? -(dx) : (dx))

void hd_rig_draw(hd_surface *s, const hd_rig *rig, const hd_state *st, const hd_player *p, int32_t i,
                 int32_t feet_x, int32_t feet_y, int32_t white)
{
   const hd_image *body = part(&rig->body, FACE_NORMAL);
   int32_t left = p->facing < 0, k;
   int32_t moving = p->ground && hd_abs(p->vx) >= FX_FRAC(1, 4);
   int32_t phase = 0, bob = 0, squash = FX_ONE, stretch = FX_ONE, face = FACE_NORMAL;
   int32_t hip_y, body_bottom, shoulder_y, hand_pose = HAND_FIST;
   int32_t foot_x[2], foot_y[2], foot_pose[2], hand_x[2], hand_y[2], knee[2], elbow[2];
   int32_t t = st->frame + i * 37;

   /* the walk: one cycle every `stride` pixels walked (p->anim grows by 4 a pixel) */
   if (rig->stride > 0)
      phase = (int32_t)(((int64_t)p->anim * 4096 / ((int64_t)rig->stride * 4)) & 4095);

   /* faces: a blink now and then, the state's own face otherwise */
   if (p->hurt > HURT_FRAMES - 30)
      face = FACE_HURT;
   else if (p->still >= BORED_AFTER && (p->still - BORED_AFTER) % 400 < 120)
      face = FACE_YAWN;
   else if (!p->ground && p->vy < 0)
      face = FACE_SHOUT;
   else if ((t % 200) < 7)
      face = FACE_BLINK;
   body = part(&rig->body, face);

   /* the body: bounce when walking, breathe when still, squash on landing, stretch in the air */
   if (moving)
      bob = (int32_t)(((int64_t)hd_abs(hd_sin(phase * 2)) * rig->bob) >> 14);
   else if (p->ground)
   {
      squash = FX_ONE + (int32_t)(((int64_t)hd_sin((t * 4096 / 150) & 4095) * (FX_ONE / 50)) >> 14);
      stretch = FX_ONE - (squash - FX_ONE);
   }
   if (p->ground && p->landed < 8)
   {
      int32_t k8 = 8 - p->landed; /* just landed: flatter, wider */
      squash = FX_ONE - k8 * (FX_ONE / 40);
      stretch = FX_ONE + k8 * (FX_ONE / 50);
   }
   else if (!p->ground)
   {
      squash = FX_ONE + FX_ONE / 14;
      stretch = FX_ONE - FX_ONE / 20;
   }

   hip_y = feet_y - rig->leg + bob / 2;
   body_bottom = hip_y + rig->limb;
   shoulder_y = body_bottom - (int32_t)(((int64_t)body->h * squash >> 16) * 45 / 100);

   /* legs: the two feet half a cycle apart */
   for (k = 0; k < 2; k++)
   {
      int32_t ph = (phase + k * 2048) & 4095;
      int32_t hip_x = feet_x + FWD(k ? -rig->limb / 2 : rig->limb / 2);
      if (moving)
      {
         int32_t swing = (int32_t)(((int64_t)hd_sin(ph) * (rig->stride / 4)) >> 14);
         int32_t up = hd_max(0, (int32_t)(((int64_t)hd_cos(ph) * rig->lift) >> 14));
         foot_x[k] = hip_x + FWD(swing);
         foot_y[k] = feet_y - up;
         foot_pose[k] = up > rig->lift / 2 ? FOOT_AIR : (swing > rig->stride / 10 ? FOOT_HEEL : (swing < -rig->stride / 10 ? FOOT_TOE : FOOT_FLAT));
      }
      else if (!p->ground)
      {
         /* in the air: rising, feet tucked up; falling, reaching down */
         int32_t tuck = p->vy < 0 ? rig->leg / 3 : 0;
         foot_x[k] = hip_x + FWD(k ? -rig->limb : rig->limb);
         foot_y[k] = hip_y + rig->leg - tuck - k * 3;
         foot_pose[k] = FOOT_AIR;
      }
      else
      {
         foot_x[k] = hip_x + FWD(k ? -rig->limb / 2 : rig->limb / 2);
         foot_y[k] = feet_y;
         foot_pose[k] = FOOT_FLAT;
      }
      knee[k] = FWD(rig->leg / 4); /* knees bend forward */
   }

   /* arms: swing against the legs; still, hands on the hips; in the air, up */
   for (k = 0; k < 2; k++)
   {
      int32_t sh_x = feet_x + FWD(k ? -body->w * 2 / 5 : body->w * 2 / 5);
      if (moving)
      {
         int32_t ph = (phase + (k ? 0 : 2048)) & 4095;
         int32_t swing = (int32_t)(((int64_t)hd_sin(ph) * (rig->arm * 3 / 5)) >> 14);
         hand_x[k] = sh_x + FWD(swing);
         hand_y[k] = shoulder_y + rig->arm * 4 / 5 - hd_abs(swing) / 3;
         hand_pose = HAND_FIST;
         elbow[k] = FWD(-rig->arm / 4);
      }
      else if (!p->ground)
      {
         hand_x[k] = sh_x + FWD(k ? -rig->arm / 2 : rig->arm / 2);
         hand_y[k] = shoulder_y - rig->arm * 3 / 4;
         hand_pose = HAND_OPEN;
         elbow[k] = FWD(k ? -rig->arm / 3 : rig->arm / 3);
      }
      else if (face == FACE_YAWN)
      {
         hand_x[k] = sh_x + FWD(k ? -rig->arm / 3 : rig->arm / 3);
         hand_y[k] = shoulder_y - rig->arm;
         hand_pose = HAND_OPEN;
         elbow[k] = FWD(k ? -rig->arm / 2 : rig->arm / 2);
      }
      else
      {
         /* on the hips: the elbows out, the hands at the body's sides */
         hand_x[k] = sh_x + FWD(k ? rig->arm / 6 : -rig->arm / 6);
         hand_y[k] = body_bottom - rig->limb * 2;
         hand_pose = HAND_FIST;
         elbow[k] = FWD(k ? -rig->arm / 2 : rig->arm / 2);
      }
   }

   /* back to front: the far arm and leg, the body, the near leg and arm */
   for (k = 1; k >= 0; k--)
   {
      int32_t hip_x = feet_x + FWD(k ? -rig->limb / 2 : rig->limb / 2);
      int32_t bx = (hip_x + foot_x[k]) / 2 + knee[k], by = (hip_y + foot_y[k]) / 2;
      const hd_image *shoe = part(&rig->foot, foot_pose[k]);
      if (k == 1)
      {
         int32_t sh_x = feet_x + FWD(-body->w * 2 / 5);
         hose(s, sh_x, shoulder_y, (sh_x + hand_x[1]) / 2 + elbow[1], (shoulder_y + hand_y[1]) / 2, hand_x[1], hand_y[1], rig->limb);
         place(s, part(&rig->hand, hand_pose), hand_x[1], hand_y[1], 0, part(&rig->hand, hand_pose)->h / 2, left, FX_ONE, FX_ONE, white);
      }
      hose(s, hip_x, hip_y, bx, by, foot_x[k], foot_y[k] - shoe->h / 2, rig->limb);
      place(s, shoe, foot_x[k], foot_y[k], shoe->w / 3, shoe->h - rig->foot.feet, left, FX_ONE, FX_ONE, white);
      if (k == 1)
         place(s, body, feet_x, body_bottom, body->w / 2, body->h - rig->body.feet, left, stretch, squash, white);
   }
   {
      int32_t sh_x = feet_x + FWD(body->w * 2 / 5);
      hose(s, sh_x, shoulder_y, (sh_x + hand_x[0]) / 2 + elbow[0], (shoulder_y + hand_y[0]) / 2, hand_x[0], hand_y[0], rig->limb);
      place(s, part(&rig->hand, hand_pose), hand_x[0], hand_y[0], 0, part(&rig->hand, hand_pose)->h / 2, left, FX_ONE, FX_ONE, white);
   }
}
