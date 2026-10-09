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

/* The direction of (dx, dy) on the screen, 0..4095 clockwise from the right (y grows down), found on the
   engine's own sine table so it is the same on every computer. */
static int32_t turn(int32_t dx, int32_t dy)
{
   int32_t ax = hd_abs(dx), ay = hd_abs(dy), lo = 0, hi = ANGLE_FULL / 4, a;
   if (!ax && !ay)
      return 0;
   /* the first quarter: the angle where sin * ax meets cos * ay */
   while (lo < hi)
   {
      int32_t mid = (lo + hi) / 2;
      if ((int64_t)hd_sin(mid) * ax < (int64_t)hd_cos(mid) * ay)
         lo = mid + 1;
      else
         hi = mid;
   }
   a = lo;
   if (dx < 0)
      a = ANGLE_FULL / 2 - a;
   if (dy < 0)
      a = ANGLE_FULL - a;
   return a & (ANGLE_FULL - 1);
}

/* One picture of a parts sheet. */
static const hd_image *part(const hd_anim *an, int32_t k)
{
   return &an->frames[hd_clamp(k, 0, an->count - 1)];
}

/* A part drawn with its pivot (px, py, in the picture) at (x, y), mirrored when facing left, turned by
   `angle` on the screen (its right side then points that way; facing left, its mirrored left side does),
   scaled (16.16). */
static void place(hd_surface *s, const hd_image *im, int32_t x, int32_t y, int32_t px, int32_t py, int32_t left,
                  int32_t angle, int32_t sx, int32_t sy, int32_t white)
{
   hd_style st;
   memset(&st, 0, sizeof st);
   st.flags = (left ? DRAW_FLIP_X : 0) | (white ? DRAW_WHITE : 0);
   gfx_blit_rot(s, im, x, y, left ? im->w - px : px, py, angle & (ANGLE_FULL - 1), sx, sy, &st);
}

/* A glove at the end of an arm bending through (bx, by): pointing the way the forearm goes, or straight
   ahead when `level` (the finger pistol fires where the shots go). */
static void glove(hd_surface *s, const hd_image *im, int32_t x, int32_t y, int32_t bx, int32_t by, int32_t left,
                  int32_t level, int32_t white)
{
   int32_t a = level ? (left ? ANGLE_FULL / 2 : 0) : turn(x - bx, y - by);
   place(s, im, x, y, 0, im->h / 2, left, left ? a - ANGLE_FULL / 2 : a, FX_ONE, FX_ONE, white);
}

/* x of a point `dx` in front of the body (mirrored when facing left). */
#define FWD(dx) (left ? -(dx) : (dx))
/* The shoulders, seen from the side: both behind the middle (the far one, k 1, further back), so the arms
   swing over the back of the white half and leave the face, on the front, clear. */
#define SHOULDER_X(k) (feet_x + FWD((k) ? -body->w * 2 / 5 : -body->w / 6))

void hd_rig_draw(hd_surface *s, const hd_rig *rig, const hd_state *st, const hd_player *p, int32_t i,
                 int32_t feet_x, int32_t feet_y, int32_t white)
{
   const hd_image *body = part(&rig->body, FACE_NORMAL);
   int32_t left = p->facing < 0, k;
   int32_t moving = p->ground && hd_abs(p->vx) >= FX_FRAC(1, 4);
   int32_t phase = 0, bob = 0, squash = FX_ONE, stretch = FX_ONE, face = FACE_NORMAL;
   int32_t hip_y, body_bottom, shoulder_y, hand_pose[2] = { HAND_FIST, HAND_FIST };
   /* aiming: the near arm straight forward at the hip with the finger pistol, in any state (set by shooting) */
   int32_t aim = 0;
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
      squash = FX_ONE + (int32_t)(((int64_t)hd_sin((t % 150 * 4096 / 150) & 4095) * (FX_ONE / 50)) >> 14);
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
   shoulder_y = body_bottom - (int32_t)(((int64_t)body->h * squash >> 16) * 46 / 100);

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

   /* arms: swing against the legs; standing, they hang; in the air, the near one forward and the far one back */
   for (k = 0; k < 2; k++)
   {
      int32_t sh_x = SHOULDER_X(k);
      if (moving)
      {
         int32_t ph = (phase + (k ? 0 : 2048)) & 4095;
         int32_t swing = (int32_t)(((int64_t)hd_sin(ph) * (rig->arm * 3 / 5)) >> 14);
         if (swing > 0)
            swing = swing * 2 / 3; /* forward less than back: the hand stops before the mouth */
         hand_x[k] = sh_x + FWD(swing);
         hand_y[k] = shoulder_y + rig->arm * 4 / 5 - hd_abs(swing) / 3;
         hand_pose[k] = HAND_FIST;
         elbow[k] = FWD(-rig->arm / 4);
      }
      else if (!p->ground)
      {
         /* in the air: the near arm forward at the hip, ready to shoot, the far one back for balance;
            rising they lift a little, falling they drop and wave */
         int32_t rise = p->vy < 0 ? rig->arm / 5 : -rig->arm / 6;
         int32_t wave = p->vy > 0 ? (int32_t)(((int64_t)hd_sin((t * 64 + k * 1024) & 4095) * 2) >> 14) : 0;
         hand_x[k] = sh_x + FWD(k ? -rig->arm * 3 / 4 : rig->arm * 2 / 3);
         hand_y[k] = shoulder_y + (k ? rig->arm / 3 : rig->arm * 4 / 5) - rise + wave;
         hand_pose[k] = k ? HAND_OPEN : HAND_FIST;
         elbow[k] = k ? FWD(-rig->arm / 6) : 0;
      }
      else if (face == FACE_YAWN)
      {
         hand_x[k] = sh_x + FWD(k ? -rig->arm / 3 : rig->arm / 3);
         hand_y[k] = shoulder_y - rig->arm;
         hand_pose[k] = HAND_OPEN;
         elbow[k] = FWD(k ? -rig->arm / 2 : rig->arm / 2);
      }
      else
      {
         /* standing: the arms hang, swaying a little with the breath, the elbows a bit back */
         int32_t sway = (int32_t)(((int64_t)hd_sin(((t % 150 * 4096 / 150) + k * 700) & 4095) * 2) >> 14);
         hand_x[k] = sh_x + FWD((k ? -2 : 3) + sway);
         hand_y[k] = shoulder_y + rig->arm * 9 / 10;
         hand_pose[k] = HAND_FIST;
         elbow[k] = FWD(-rig->arm / 5);
      }
      if (aim && k == 0)
      {
         hand_x[0] = sh_x + FWD(rig->arm * 9 / 10);
         hand_y[0] = shoulder_y + rig->arm * 3 / 5;
         hand_pose[0] = HAND_GUN;
         elbow[0] = FWD(0);
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
         int32_t sh_x = SHOULDER_X(1);
         int32_t ex = (sh_x + hand_x[1]) / 2 + elbow[1], ey = (shoulder_y + hand_y[1]) / 2;
         hose(s, sh_x, shoulder_y, ex, ey, hand_x[1], hand_y[1], rig->limb);
         glove(s, part(&rig->hand, hand_pose[1]), hand_x[1], hand_y[1], ex, ey, left, hand_pose[1] == HAND_GUN, white);
      }
      hose(s, hip_x, hip_y, bx, by, foot_x[k], foot_y[k] - shoe->h / 2, rig->limb);
      place(s, shoe, foot_x[k], foot_y[k], shoe->w / 3, shoe->h - rig->foot.feet, left, 0, FX_ONE, FX_ONE, white);
      if (k == 1)
         place(s, body, feet_x, body_bottom, body->w / 2, body->h - rig->body.feet, left, 0, stretch, squash, white);
   }
   {
      int32_t sh_x = SHOULDER_X(0);
      int32_t ex = (sh_x + hand_x[0]) / 2 + elbow[0], ey = (shoulder_y + hand_y[0]) / 2;
      hose(s, sh_x, shoulder_y, ex, ey, hand_x[0], hand_y[0], rig->limb);
      glove(s, part(&rig->hand, hand_pose[0]), hand_x[0], hand_y[0], ex, ey, left, hand_pose[0] == HAND_GUN, white);
   }
}
