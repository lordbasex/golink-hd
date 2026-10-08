/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * The demo platformer's rules, one fixed 60 Hz step at a time. Everything it
 * reads or writes is in hd_state (plus the level, art and sounds built once
 * at start), with 16.16 fixed point and the state's own random generator, so
 * the same inputs give the same game on every computer.
 */
#include <string.h>
#include "hd.h"
#include "gfx.h"
#include "text.h"

/* Movement, in 16.16 pixels per frame (and per frame squared). */
#define WALK_MAX FX_FRAC(5, 2)
#define RUN_MAX FX(4)
#define ACCEL_GROUND FX_FRAC(30, 100)
#define ACCEL_AIR FX_FRAC(18, 100)
#define FRICTION_GROUND FX_FRAC(25, 100)
#define FRICTION_AIR FX_FRAC(5, 100)
#define GRAVITY FX_FRAC(45, 100)
#define GRAVITY_HOLD FX_FRAC(28, 100) /* while jump is held on the way up */
#define FALL_MAX FX(7)
#define JUMP_SPEED FX_FRAC(64, 10)
#define JUMP_CUT FX(-2)
#define BOUNCE FX_FRAC(-45, 10)
#define BOUNCE_HELD FX(-7)
#define ENEMY_SPEED FX_FRAC(6, 10)
#define COYOTE 6
#define BUFFER 6
#define RESPAWN_FRAMES 45
#define CLEAR_FRAMES 360

static int initialized;

void hd_static_init(void)
{
   if (initialized)
      return;
   hd_content_builtin();
   hd_audio_build();
   hd_trig_build();
   hd_show_build();
   initialized = 1;
}

int hd_cell(int32_t tx, int32_t ty)
{
   if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H)
      return T_EMPTY;
   return hd_map[ty][tx];
}

static int taken(const hd_state *s, int32_t tx, int32_t ty)
{
   int32_t i = ty * MAP_W + tx;
   return (s->taken[i >> 5] >> (i & 31)) & 1;
}

static void take(hd_state *s, int32_t tx, int32_t ty)
{
   int32_t i = ty * MAP_W + tx;
   s->taken[i >> 5] |= 1u << (i & 31);
}

/* Solid for everyone: ground, bricks and the level's left and right ends. */
static int solid(int32_t tx, int32_t ty)
{
   int32_t t;
   if (tx < 0 || tx >= MAP_W)
      return 1;
   t = hd_cell(tx, ty);
   return t == T_GROUND || t == T_BRICK;
}

static int shelf(int32_t tx, int32_t ty)
{
   return hd_cell(tx, ty) == T_PLATFORM;
}

static void particle(hd_state *s, int32_t x, int32_t y, int32_t vx, int32_t vy, int32_t life, uint32_t color, int32_t flags)
{
   hd_particle *q = &s->part[s->part_next];
   s->part_next = (s->part_next + 1) % MAX_PARTICLES;
   q->life = life;
   q->max = life;
   q->x = FX(x);
   q->y = FX(y);
   q->vx = vx;
   q->vy = vy;
   q->color = color;
   q->flags = flags;
}

static void burst(hd_state *s, int32_t x, int32_t y, int32_t n, uint32_t color, int32_t speed, int32_t flags)
{
   int32_t i;
   for (i = 0; i < n; i++)
   {
      int32_t vx = rng_range(&s->rng, 2 * speed + 1) - speed;
      int32_t vy = -rng_range(&s->rng, speed + 1) - speed / 2;
      particle(s, x, y, vx, vy, 20 + rng_range(&s->rng, 20), color, flags);
   }
}

static int32_t screen_x(const hd_state *s, int32_t x_px)
{
   return x_px - FX_INT(s->cam_x);
}

static void reset_level(hd_state *s)
{
   int32_t i;
   memset(s->taken, 0, sizeof s->taken);
   memset(s->e, 0, sizeof s->e);
   memset(s->part, 0, sizeof s->part);
   for (i = 0; i < hd_enemy_count; i++)
   {
      s->e[i].alive = 1;
      s->e[i].x = FX(hd_enemy_start[i][0]);
      s->e[i].y = FX(hd_enemy_start[i][1]);
      s->e[i].vx = -ENEMY_SPEED;
   }
   s->cam_x = 0;
   s->cam_y = FX(MAP_H * TILE - HD_H);
}

static void place(hd_player *p, int32_t x, int32_t y)
{
   p->x = FX(x);
   p->y = FX(y);
   p->vx = 0;
   p->vy = 0;
   p->ground = 0;
   p->jumping = 0;
}

static void join(hd_state *s, int32_t i)
{
   hd_player *p = &s->p[i];
   int32_t x = hd_start_x + i * 18, y = hd_start_y;
   int32_t j;
   uint32_t pad, prev;
   /* join next to someone already playing */
   for (j = 0; j < MAX_PLAYERS; j++)
      if (j != i && s->p[j].active && !s->p[j].respawn)
      {
         x = s->p[j].check_x;
         y = s->p[j].check_y;
         break;
      }
   pad = p->pad;
   prev = p->prev;
   memset(p, 0, sizeof *p);
   p->pad = pad; /* keep the buttons, or the press that joined would count again */
   p->prev = prev;
   p->active = 1;
   p->facing = 1;
   p->check_x = x;
   p->check_y = y;
   place(p, x, y - 48);
   p->hurt = 60;
   hd_play(s, SFX_JOIN, screen_x(s, x));
}

void hd_reset(hd_state *s)
{
   int32_t i;
   memset(s, 0, sizeof *s);
   s->rng = 0x2f6b9e1du;
   s->phase = PH_TITLE;
   for (i = 0; i < MAX_CHANNELS; i++)
      s->ch[i].sample = -1;
   s->sfx_next = MUSIC_CHANNELS;
   s->zoom = 256;
   hd_audio_effects(s, hd_fx.lowpass, hd_fx.echo_ms, 150, 110);
   reset_level(s);
}

static void move_x(hd_player *p)
{
   int32_t px, py, tx, ty;
   p->x += p->vx;
   px = FX_INT(p->x);
   py = FX_INT(p->y);
   if (p->vx > 0)
   {
      tx = (px + PW - 1) >> 4;
      for (ty = py >> 4; ty <= (py + PH - 1) >> 4; ty++)
         if (solid(tx, ty))
         {
            p->x = FX(tx * TILE - PW);
            p->vx = 0;
            break;
         }
   }
   else if (p->vx < 0)
   {
      tx = px >> 4;
      for (ty = py >> 4; ty <= (py + PH - 1) >> 4; ty++)
         if (solid(tx, ty))
         {
            p->x = FX((tx + 1) * TILE);
            p->vx = 0;
            break;
         }
   }
}

/* Returns 1 when it lands this frame. */
static int move_y(hd_player *p)
{
   int32_t old_bottom = FX_INT(p->y) + PH;
   int32_t px, py, tx, ty;
   int was = p->ground;
   p->y += p->vy;
   px = FX_INT(p->x);
   py = FX_INT(p->y);
   p->ground = 0;
   if (p->vy >= 0)
   {
      ty = (py + PH - 1) >> 4;
      for (tx = px >> 4; tx <= (px + PW - 1) >> 4; tx++)
         if (solid(tx, ty) || (!p->drop && shelf(tx, ty) && old_bottom <= ty * TILE))
         {
            p->y = FX(ty * TILE - PH);
            p->vy = 0;
            p->ground = 1;
            p->jumping = 0;
            break;
         }
   }
   else
   {
      ty = py >> 4;
      for (tx = px >> 4; tx <= (px + PW - 1) >> 4; tx++)
         if (solid(tx, ty))
         {
            p->y = FX((ty + 1) * TILE);
            p->vy = 0;
            break;
         }
   }
   return p->ground && !was;
}

static void hurt(hd_state *s, hd_player *p, int32_t from_x)
{
   int32_t lost = hd_min(3, p->coins), i;
   int32_t cx = FX_INT(p->x) + PW / 2, cy = FX_INT(p->y) + 4;
   p->hurt = HURT_FRAMES;
   p->coins -= lost;
   p->vx = FX_INT(p->x) + PW / 2 < from_x ? FX(-3) : FX(3);
   p->vy = FX_FRAC(-35, 10);
   p->jumping = 0;
   s->shake = 10;
   for (i = 0; i < lost; i++)
      particle(s, cx, cy, FX(i - 1), FX(-4), 40, 0xfff8c838u, 1);
   hd_play(s, SFX_HURT, screen_x(s, cx));
}

/*
 * Checkpoints and the goal count when a player passes their column at any
 * height, even jumping over them.
 */
static void touch_column(hd_state *s, const hd_player *p)
{
   int32_t tx = (FX_INT(p->x) + PW / 2) >> 4, ty, j;
   for (ty = 0; ty < MAP_H; ty++)
   {
      int32_t t = hd_cell(tx, ty);
      if (t == T_CHECK && !taken(s, tx, ty))
      {
         take(s, tx, ty);
         /* everyone comes back here from now on */
         for (j = 0; j < MAX_PLAYERS; j++)
         {
            s->p[j].check_x = tx * TILE + 3;
            s->p[j].check_y = (ty + 1) * TILE - PH;
         }
         burst(s, tx * TILE + 8, ty * TILE, 10, 0xff3ad85au, FX(2), 2);
         hd_play(s, SFX_CHECK, screen_x(s, tx * TILE));
      }
      else if (t == T_FLAG && s->phase == PH_PLAY)
      {
         s->phase = PH_CLEAR;
         s->phase_t = 0;
         burst(s, tx * TILE + 16, ty * TILE - 40, 40, 0xfff8f8f8u, FX(3), 3);
         hd_play(s, SFX_CLEAR, HD_W / 2);
      }
   }
}

static void touch_coins(hd_state *s, hd_player *p)
{
   int32_t px = FX_INT(p->x), py = FX_INT(p->y), tx, ty;
   for (ty = py >> 4; ty <= (py + PH - 1) >> 4; ty++)
      for (tx = px >> 4; tx <= (px + PW - 1) >> 4; tx++)
         if (hd_cell(tx, ty) == T_COIN && !taken(s, tx, ty))
         {
            take(s, tx, ty);
            p->coins++;
            burst(s, tx * TILE + 8, ty * TILE + 8, 6, 0xfffff0a0u, FX(2), 2);
            hd_play(s, SFX_COIN, screen_x(s, tx * TILE));
         }
}

static void player_step(hd_state *s, int32_t i)
{
   hd_player *p = &s->p[i];
   uint32_t pad = p->pad, pressed = pad & ~p->prev;
   int32_t dir = ((pad & PAD_RIGHT) ? 1 : 0) - ((pad & PAD_LEFT) ? 1 : 0);
   /* the left stick moves too, when the D-pad does not */
   if (!dir && hd_abs(p->lx) > STICK_DEAD)
      dir = p->lx > 0 ? 1 : -1;
   int32_t max = (pad & PAD_RUN) ? RUN_MAX : WALK_MAX;
   int32_t was_vy;

   if (p->respawn)
   {
      if (--p->respawn == 0)
      {
         place(p, p->check_x, p->check_y - 32);
         p->hurt = 60;
      }
      return;
   }
   if (p->hurt)
      p->hurt--;

   /* walking and running */
   if (dir)
   {
      p->vx += dir * (p->ground ? ACCEL_GROUND : ACCEL_AIR);
      if (p->vx > max)
         p->vx = hd_max(max, p->vx - FRICTION_GROUND);
      if (p->vx < -max)
         p->vx = hd_min(-max, p->vx + FRICTION_GROUND);
      if (p->ground && dir * p->vx < 0 && (s->frame & 3) == 0)
         particle(s, FX_INT(p->x) + PW / 2, FX_INT(p->y) + PH, 0, FX_FRAC(-1, 2), 14, 0xffe0d0b0u, 0);
      p->facing = dir;
   }
   else
   {
      int32_t f = p->ground ? FRICTION_GROUND : FRICTION_AIR;
      if (p->vx > 0)
         p->vx = hd_max(0, p->vx - f);
      else if (p->vx < 0)
         p->vx = hd_min(0, p->vx + f);
   }

   /* jumping: a press shortly before landing or shortly after leaving an edge still counts */
   if (pressed & PAD_JUMP)
      p->buffer = BUFFER;
   else if (p->buffer)
      p->buffer--;
   if (p->ground)
      p->coyote = COYOTE;
   else if (p->coyote)
      p->coyote--;
   if (p->drop)
      p->drop--;
   if (p->buffer && p->coyote)
   {
      if ((pad & PAD_DOWN) && p->ground && shelf(FX_INT(p->x + FX(PW / 2)) >> 4, (FX_INT(p->y) + PH) >> 4))
      {
         p->drop = 10; /* down + jump on a one-way platform drops through it */
      }
      else
      {
         p->vy = -JUMP_SPEED - hd_abs(p->vx) / 6;
         p->jumping = 1;
         hd_play(s, SFX_JUMP, screen_x(s, FX_INT(p->x)));
      }
      p->buffer = 0;
      p->coyote = 0;
   }
   if (p->jumping && !(pad & PAD_JUMP) && p->vy < JUMP_CUT)
      p->vy = JUMP_CUT;

   p->vy += ((pad & PAD_JUMP) && p->vy < 0) ? GRAVITY_HOLD : GRAVITY;
   if (p->vy > FALL_MAX)
      p->vy = FALL_MAX;

   move_x(p);
   was_vy = p->vy;
   if (move_y(p))
   {
      p->landed = 0;
      if (was_vy > FX(4))
      {
         particle(s, FX_INT(p->x), FX_INT(p->y) + PH, FX(-1), FX_FRAC(-1, 2), 16, 0xffe0d0b0u, 0);
         particle(s, FX_INT(p->x) + PW, FX_INT(p->y) + PH, FX(1), FX_FRAC(-1, 2), 16, 0xffe0d0b0u, 0);
      }
   }
   else
      p->landed++;

   /* fell into a pit */
   if (FX_INT(p->y) > MAP_H * TILE + 48)
   {
      p->respawn = RESPAWN_FRAMES;
      p->coins -= hd_min(3, p->coins);
      hd_play(s, SFX_HURT, screen_x(s, FX_INT(p->x)));
      return;
   }

   p->anim += p->ground ? hd_abs(p->vx) >> 14 : 0;
   touch_coins(s, p);
   touch_column(s, p);
}

static void enemy_step(hd_state *s, hd_enemy *e)
{
   int32_t ex, ey, tx, ty, front;
   if (e->alive == 2)
   {
      if (--e->squash <= 0)
         e->alive = 0;
      return;
   }
   /* enemies wake when they come near the screen and stay awake */
   if (!e->awake)
   {
      if (FX_INT(e->x) < FX_INT(s->cam_x) + HD_W + 64)
         e->awake = 1;
      else
         return;
   }
   e->anim++;
   e->vy = hd_min(e->vy + GRAVITY, FALL_MAX);
   e->x += e->vx;
   ex = FX_INT(e->x);
   ey = FX_INT(e->y);
   front = e->vx > 0 ? ex + EW : ex - 1;
   if (solid(front >> 4, (ey + EH - 1) >> 4) || solid(front >> 4, ey >> 4))
   {
      e->vx = -e->vx;
      e->x = e->vx > 0 ? FX(((front >> 4) + 1) * TILE) : FX((front >> 4) * TILE - EW);
   }
   e->y += e->vy;
   ex = FX_INT(e->x);
   ey = FX_INT(e->y);
   ty = (ey + EH - 1) >> 4;
   for (tx = ex >> 4; tx <= (ex + EW - 1) >> 4; tx++)
      if (e->vy >= 0 && (solid(tx, ty) || shelf(tx, ty)))
      {
         e->y = FX(ty * TILE - EH);
         e->vy = 0;
         /* turn at the edge of what it stands on */
         front = e->vx > 0 ? ex + EW : ex - 1;
         if (!solid(front >> 4, ty) && !shelf(front >> 4, ty))
            e->vx = -e->vx;
         break;
      }
   if (ey > MAP_H * TILE + 32)
      e->alive = 0;
}

static int overlap(int32_t ax, int32_t ay, int32_t aw, int32_t ah, int32_t bx, int32_t by, int32_t bw, int32_t bh)
{
   return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void fights(hd_state *s)
{
   int32_t i, j;
   for (i = 0; i < MAX_PLAYERS; i++)
   {
      hd_player *p = &s->p[i];
      int32_t px, py;
      if (!p->active || p->respawn)
         continue;
      px = FX_INT(p->x);
      py = FX_INT(p->y);
      for (j = 0; j < MAX_ENEMIES; j++)
      {
         hd_enemy *e = &s->e[j];
         int32_t ex = FX_INT(e->x), ey = FX_INT(e->y);
         if (e->alive != 1 || !overlap(px, py, PW, PH, ex, ey, EW, EH))
            continue;
         if (p->vy > 0 && py + PH - FX_INT(p->vy) <= ey + 6)
         {
            e->alive = 2;
            e->squash = 30;
            p->y = FX(ey - PH);
            p->vy = (p->pad & PAD_JUMP) ? BOUNCE_HELD : BOUNCE;
            p->jumping = 1;
            s->hitstop = 4;
            burst(s, ex + EW / 2, ey + EH / 2, 8, 0xffb070f0u, FX(2), 1);
            hd_play(s, SFX_STOMP, screen_x(s, ex));
         }
         else if (!p->hurt)
            hurt(s, p, ex + EW / 2);
      }
   }
}

static void camera(hd_state *s)
{
   int32_t i, n = 0, lo = 1 << 30, hi = -(1 << 30), top = 1 << 30, bottom = -(1 << 30);
   int32_t tx, ty, cx, cy, vw, vh;
   for (i = 0; i < MAX_PLAYERS; i++)
   {
      const hd_player *p = &s->p[i];
      if (!p->active || p->respawn)
         continue;
      n++;
      lo = hd_min(lo, FX_INT(p->x));
      hi = hd_max(hi, FX_INT(p->x) + PW);
      top = hd_min(top, FX_INT(p->y));
      bottom = hd_max(bottom, FX_INT(p->y) + PH);
   }
   if (!n)
      return;
   if (hd_fx.zoom_auto)
   {
      /* zoom out (down to 0.5x) when the players spread apart, back in when they gather */
      int32_t spread = hd_max(hi - lo + 200, (bottom - top + 120) * HD_W / HD_H);
      int32_t want = hd_clamp(HD_W * 256 / hd_max(spread, 1), 128, 256);
      want = hd_max(want, hd_max(HD_W * 256 / (MAP_W * TILE), HD_H * 256 / (MAP_H * TILE)));
      s->zoom += (want - s->zoom) / 8;
   }
   if (s->zoom < 128 || s->zoom > 512)
      s->zoom = 256;
   vw = HD_W * 256 / s->zoom;
   vh = HD_H * 256 / s->zoom;
   tx = (lo + hi) / 2 - vw / 2;
   ty = (top + bottom) / 2 - vh * 3 / 5;
   tx = hd_clamp(tx, 0, hd_max(0, MAP_W * TILE - vw));
   ty = hd_clamp(ty, 0, hd_max(0, MAP_H * TILE - vh));
   s->cam_x += (FX(tx) - s->cam_x) / 6;
   s->cam_y += (FX(ty) - s->cam_y) / 8;
   /* every player stays on screen */
   cx = FX_INT(s->cam_x);
   cy = FX_INT(s->cam_y);
   (void)cy;
   for (i = 0; i < MAX_PLAYERS; i++)
   {
      hd_player *p = &s->p[i];
      if (!p->active || p->respawn)
         continue;
      if (FX_INT(p->x) < cx)
      {
         p->x = FX(cx);
         p->vx = hd_max(p->vx, 0);
      }
      if (FX_INT(p->x) + PW > cx + vw)
      {
         p->x = FX(cx + vw - PW);
         p->vx = hd_min(p->vx, 0);
      }
   }
}

/* A dialog opens when a player reaches its column. */
static void dialogs(hd_state *s)
{
   int32_t d, i;
   for (d = 0; d < hd_fx.dialogs && !s->dlg; d++)
   {
      if (s->dlg_done & (1u << d))
         continue;
      for (i = 0; i < MAX_PLAYERS; i++)
         if (s->p[i].active && !s->p[i].respawn && ((FX_INT(s->p[i].x) + PW / 2) >> 4) >= hd_fx.dialog_col[d])
         {
            s->dlg = d + 1;
            s->dlg_chars = 0;
            hd_play(s, SFX_PAUSE, HD_W / 2);
            break;
         }
   }
}

static void particles_step(hd_state *s)
{
   int32_t i;
   for (i = 0; i < MAX_PARTICLES; i++)
   {
      hd_particle *q = &s->part[i];
      if (!q->life)
         continue;
      q->life--;
      q->x += q->vx;
      q->y += q->vy;
      if (q->flags & 1)
         q->vy += FX_FRAC(25, 100);
      else
         q->vy = q->vy * 7 / 8;
   }
}

void hd_step(hd_state *s, const hd_input in[MAX_PLAYERS])
{
   int32_t i;
   uint32_t any = 0;
   s->frame++;
   if (s->show.on)
   {
      hd_music_step(s);
      hd_show_step(s, in);
      return;
   }
   for (i = 0; i < MAX_PLAYERS; i++)
   {
      uint32_t b = i < hd_players ? in[i].buttons : 0; /* only the game's players count */
      /* a stick held down is the D-pad's down (drop through a platform) */
      if (i < hd_players && in[i].ly > STICK_DEAD)
         b |= PAD_DOWN;
      s->p[i].prev = s->p[i].pad;
      s->p[i].pad = b;
      s->p[i].lx = i < hd_players ? hd_clamp(in[i].lx, -32768, 32767) : 0;
      s->p[i].ly = i < hd_players ? hd_clamp(in[i].ly, -32768, 32767) : 0;
      any |= b & ~s->p[i].prev;
   }
   hd_music_step(s);

   if (s->phase == PH_TITLE)
   {
      /* the title scrolls through the level until someone presses start or jump */
      s->cam_x += FX_FRAC(1, 2);
      if (FX_INT(s->cam_x) > MAP_W * TILE - HD_W)
         s->cam_x = 0;
      if (any & (PAD_START | PAD_JUMP))
      {
         reset_level(s);
         s->phase = PH_PLAY;
         for (i = 0; i < MAX_PLAYERS; i++)
            if ((s->p[i].pad & ~s->p[i].prev) & (PAD_START | PAD_JUMP))
               join(s, i);
      }
      particles_step(s);
      return;
   }

   /* a dialog stops the game: letters appear one by one; jump or start shows them all, then closes it */
   if (s->dlg > 0 && s->dlg <= hd_fx.dialogs)
   {
      int32_t total = text_glyphs(hd_fx.dialog_text[s->dlg - 1][hd_lang]);
      if (s->dlg_chars < total)
         s->dlg_chars++;
      if (any & (PAD_JUMP | PAD_START))
      {
         if (s->dlg_chars < total)
            s->dlg_chars = total;
         else
         {
            s->dlg_done |= 1u << (s->dlg - 1);
            s->dlg = 0;
         }
      }
      particles_step(s);
      return;
   }

   /* start pauses for whoever is playing; it brings in whoever is not */
   for (i = 0; i < MAX_PLAYERS; i++)
   {
      uint32_t pressed = s->p[i].pad & ~s->p[i].prev;
      if (!s->p[i].active && (pressed & (PAD_START | PAD_JUMP)) && !s->paused && s->phase == PH_PLAY)
         join(s, i);
      else if (s->p[i].active && (pressed & PAD_START) && s->phase == PH_PLAY)
      {
         s->paused = !s->paused;
         hd_play(s, SFX_PAUSE, HD_W / 2);
      }
   }
   if (s->paused)
      return;

   if (s->shake)
   {
      s->shake--;
      s->shake_x = rng_range(&s->rng, 5) - 2;
      s->shake_y = rng_range(&s->rng, 5) - 2;
   }
   else
      s->shake_x = s->shake_y = 0;

   if (s->hitstop)
   {
      s->hitstop--;
      return;
   }

   if (s->phase == PH_CLEAR)
   {
      s->phase_t++;
      if (s->phase_t % 20 == 0 && s->phase_t < 200)
         burst(s, FX_INT(s->cam_x) + 80 + rng_range(&s->rng, HD_W - 160), 60 + rng_range(&s->rng, 120), 16,
               hd_player_color[rng_range(&s->rng, hd_players)], FX(3), 3);
      if (s->phase_t >= CLEAR_FRAMES)
      {
         uint32_t rng = s->rng;
         hd_channel ch[MAX_CHANNELS];
         int32_t row = s->music_row, tick = s->music_tick, sfx = s->sfx_next;
         memcpy(ch, s->ch, sizeof ch);
         hd_reset(s);
         s->rng = rng;
         memcpy(s->ch, ch, sizeof ch);
         s->music_row = row;
         s->music_tick = tick;
         s->sfx_next = sfx;
         return;
      }
   }
   else
      for (i = 0; i < MAX_PLAYERS; i++)
         if (s->p[i].active)
            player_step(s, i);

   for (i = 0; i < MAX_ENEMIES; i++)
      if (s->e[i].alive)
         enemy_step(s, &s->e[i]);
   if (s->phase == PH_PLAY)
   {
      fights(s);
      dialogs(s);
   }
   camera(s);
   particles_step(s);
}
