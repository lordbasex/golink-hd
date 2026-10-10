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
#include "sprite.h"

/* Movement, in 16.16 pixels per frame (and per frame squared): hd_phys. */
#define WALK_MAX (hd_phys.walk_max)
#define RUN_MAX (hd_phys.run_max)
#define ACCEL_GROUND (hd_phys.accel_ground)
#define ACCEL_AIR (hd_phys.accel_air)
#define FRICTION_GROUND (hd_phys.friction_ground)
#define FRICTION_AIR (hd_phys.friction_air)
#define GRAVITY (hd_phys.gravity)
#define GRAVITY_HOLD (hd_phys.gravity_hold) /* while jump is held on the way up */
#define FALL_MAX (hd_phys.fall_max)
#define JUMP_SPEED (hd_phys.jump_speed)
#define JUMP_CUT (hd_phys.jump_cut)
#define BOUNCE (hd_phys.bounce)
#define BOUNCE_HELD (hd_phys.bounce_held)
/* Enemies keep the built-in game's gravity and fall. */
#define ENEMY_GRAVITY FX_FRAC(45, 100)
#define ENEMY_FALL_MAX FX(7)
#define ENEMY_SPEED FX_FRAC(6, 10)
#define COYOTE 6
#define BUFFER 6
#define RESPAWN_FRAMES 45
#define CLEAR_FRAMES 360

hd_physics hd_phys;
hd_weapon_config hd_weapon;
hd_health_config hd_health;
hd_dash_config hd_dash;

void hd_weapon_default(void)
{
   memset(&hd_weapon, 0, sizeof hd_weapon);
   hd_weapon.enemy_health = 1;
   memset(&hd_health, 0, sizeof hd_health);
   hd_health.invulnerable = 90;
   hd_health.cont = 10;
}

void hd_physics_default(void)
{
   hd_phys.pw = 10;
   hd_phys.ph = 22;
   hd_phys.ew = 14;
   hd_phys.eh = 12;
   hd_phys.walk_max = FX_FRAC(5, 2);
   hd_phys.run_max = FX(4);
   hd_phys.accel_ground = FX_FRAC(30, 100);
   hd_phys.accel_air = FX_FRAC(18, 100);
   hd_phys.friction_ground = FX_FRAC(25, 100);
   hd_phys.friction_air = FX_FRAC(5, 100);
   hd_phys.gravity = FX_FRAC(45, 100);
   hd_phys.gravity_hold = FX_FRAC(28, 100);
   hd_phys.fall_max = FX(7);
   hd_phys.jump_speed = FX_FRAC(64, 10);
   hd_phys.jump_cut = FX(-2);
   hd_phys.bounce = FX_FRAC(-45, 10);
   hd_phys.bounce_held = FX(-7);
}

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
   memset(s->shot, 0, sizeof s->shot);
   s->shot_next = 0;
   for (i = 0; i < hd_enemy_count; i++)
   {
      s->e[i].alive = 1;
      s->e[i].hp = hd_weapon.enemy_health;
      s->e[i].x = FX(hd_enemy_start[i][0]);
      s->e[i].y = FX(hd_enemy_start[i][1]);
      s->e[i].vx = -ENEMY_SPEED;
      s->e[i].kind = hd_enemy_kind_of[i];
      s->e[i].face = -1;
      s->e[i].act = -1;
      s->e[i].home_x = hd_enemy_start[i][0];
      s->e[i].home_y = hd_enemy_start[i][1];
      if (s->e[i].kind != EK_WALKER)
      {
         /* format 3's kinds: their own health; they stand until they see a player */
         s->e[i].hp = hd_kinds[s->e[i].kind].health;
         s->e[i].vx = 0;
      }
   }
   memset(s->bolt, 0, sizeof s->bolt);
   s->bolt_next = 0;
   s->boss = s->boss_max = s->boss_angry = s->boss_beaten = s->arena = 0;
   s->music_boss = 0;
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
   p->hp = hd_health.hits;
   p->lives = hd_health.lives;
   place(p, x, y - 48);
   p->hurt = 60;
   hd_play(s, SFX_JOIN, screen_x(s, x));
}

/* A player out of time to continue leaves the game (start brings it back); with nobody left, the game is over. */
static void leave(hd_state *s, int32_t i)
{
   int32_t j, playing = 0;
   s->p[i].active = 0;
   s->p[i].cont = 0;
   s->p[i].respawn = 0;
   for (j = 0; j < MAX_PLAYERS; j++)
      playing |= s->p[j].active;
   if (!playing && s->phase == PH_PLAY)
   {
      s->phase = PH_OVER;
      s->phase_t = 0;
   }
}

/* A knockout, or a fall with no health left: with lives, one less; at none the player waits to continue. */
static void life_lost(hd_state *s, int32_t i)
{
   hd_player *p = &s->p[i];
   if (!hd_health.lives || --p->lives > 0)
      return;
   p->lives = 0;
   p->cont = hd_health.cont * 60;
   p->respawn = 1; /* held while it waits: out of the game's way */
   if (!p->cont)
      leave(s, i);
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
   hd_stage_select(0); /* a package of several levels starts at the first */
   hd_audio_effects(s, hd_fx.lowpass, hd_fx.echo_ms, 150, 110);
   reset_level(s);
}

/* The next level of a package of several: its intro (if it has one), then the players at its start, keeping their coins and health. */
static void next_stage(hd_state *s)
{
   int32_t i;
   s->stage++;
   hd_stage_select(s->stage);
   hd_audio_effects(s, hd_fx.lowpass, hd_fx.echo_ms, 150, 110);
   reset_level(s);
   s->music_pos = 0;
   s->phase = hd_screens[SCREEN_INTRO].px ? PH_INTRO : PH_PLAY;
   s->phase_t = 0;
   s->skip_hold = 0;
   s->intro_join = 0;
   for (i = 0; i < MAX_PLAYERS; i++)
   {
      hd_player *p = &s->p[i];
      if (!p->active)
         continue;
      p->check_x = hd_start_x + i * 18;
      p->check_y = hd_start_y;
      place(p, p->check_x, p->check_y - 48);
      p->respawn = p->ko = p->super_t = p->shot_wait = p->aim = 0;
      if (p->cont)
      {
         p->cont = 0; /* a player waiting to continue goes on to the next level too */
         p->lives = hd_health.lives;
      }
      if (p->hp <= 0)
         p->hp = hd_health.hits;
      p->hurt = 60;
   }
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
      /* the row of the feet's lowest pixel, with the fraction: half a pixel
       * into the floor touches it (rounded down, a standing player was in
       * the air every other frame, sinking half a pixel and then one) */
      ty = FX_INT(p->y + FX(PH) - 1) >> 4;
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
   int32_t lost = hd_health.on ? 0 : hd_min(3, p->coins), i; /* with health, a hit costs health, not coins */
   int32_t cx = FX_INT(p->x) + PW / 2, cy = FX_INT(p->y) + 4;
   if (hd_health.on && --p->hp <= 0)
   {
      /* knocked out: it stops where it is and dissolves, then comes back at the checkpoint */
      p->hp = 0;
      p->ko = hd_health.knockout;
      p->vx = 0;
      p->vy = 0;
      p->hurt = 0;
      p->jumping = 0;
      s->shake = 16;
      burst(s, cx, cy + PH / 2, 16, 0xfff0e8e0u, FX(2), 1);
      hd_play(s, SFX_KO, screen_x(s, cx));
      return;
   }
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
 * Checkpoints and the goal count when a player passes their column near
 * their height: from a little below to well above, so jumping over them
 * counts, but passing far under them (a level that climbs) does not.
 */
#define TOUCH_ABOVE 8 /* rows above the cell that still count */
#define TOUCH_BELOW 2
static void touch_column(hd_state *s, const hd_player *p)
{
   int32_t tx = (FX_INT(p->x) + PW / 2) >> 4, ty, j;
   int32_t top = FX_INT(p->y) >> 4, feet = (FX_INT(p->y) + PH - 1) >> 4;
   for (ty = 0; ty < MAP_H; ty++)
   {
      int32_t t = hd_cell(tx, ty);
      if (feet < ty - TOUCH_ABOVE || top > ty + TOUCH_BELOW)
         continue;
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
      else if (t == T_FLAG && s->phase == PH_PLAY && hd_goal_open(s))
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

/* A super attack's granules: a fan forward, a little upward, each a bit faster or slower, falling in arcs. */
static void granules(hd_state *s, int32_t i)
{
   hd_player *p = &s->p[i];
   int32_t k, n = hd_weapon.super_count;
   int32_t x = FX_INT(p->x) + PW / 2 + p->facing * hd_weapon.muzzle_x / 2, y = FX_INT(p->y) + PH / 3;
   for (k = 0; k < n; k++)
   {
      hd_shot *q = &s->shot[s->shot_next];
      /* from the fan's top to its bottom, centered a little above straight ahead */
      int32_t a = (n > 1 ? -hd_weapon.super_spread / 2 + hd_weapon.super_spread * k / (n - 1) : 0) - ANGLE_FULL / 48;
      int32_t speed = hd_weapon.super_speed * (80 + rng_range(&s->rng, 41)) / 100;
      s->shot_next = (s->shot_next + 1) % MAX_SHOTS;
      memset(q, 0, sizeof *q);
      q->life = hd_weapon.super_life;
      q->x = FX(x);
      q->y = FX(y);
      q->vx = p->facing * (int32_t)(((int64_t)hd_cos(a & (ANGLE_FULL - 1)) * speed) >> 14);
      q->vy = (int32_t)(((int64_t)hd_sin(a & (ANGLE_FULL - 1)) * speed) >> 14);
      q->owner = i;
      q->damage = hd_weapon.super_damage;
      q->granule = 1;
      q->age = rng_range(&s->rng, 8); /* not all twinkling together */
   }
   burst(s, x, y, 14, 0xffff6060u, FX(3), 3);
   s->shake = 12;
}

/* The weapon: held, it fires every hd_weapon.rate frames, straight ahead from the muzzle. */
static void fire(hd_state *s, int32_t i)
{
   hd_player *p = &s->p[i];
   hd_shot *q;
   int32_t x, y;
   if (p->shot_wait)
      p->shot_wait--;
   if (p->aim)
      p->aim--;
   if (!(p->pad & hd_weapon.button) || p->shot_wait)
      return;
   x = FX_INT(p->x) + PW / 2 + p->facing * hd_weapon.muzzle_x;
   y = FX_INT(p->y) + PH + hd_weapon.muzzle_y;
   q = &s->shot[s->shot_next];
   s->shot_next = (s->shot_next + 1) % MAX_SHOTS;
   memset(q, 0, sizeof *q);
   q->life = hd_weapon.life;
   q->x = FX(x);
   q->y = FX(y);
   q->vx = p->facing * hd_weapon.speed;
   q->owner = i;
   q->damage = 1;
   p->shot_wait = hd_weapon.rate;
   p->aim = hd_weapon.rate + 14; /* the pose stays a little after the last shot */
   p->still = 0;
   particle(s, x, y, 0, 0, 5, 0xfffff0e0u, 2); /* the muzzle's flash */
   hd_play(s, SFX_SHOOT, screen_x(s, x));
}

static void player_step(hd_state *s, int32_t i)
{
   hd_player *p = &s->p[i];
   uint32_t pad = p->pad, pressed = pad & ~p->prev;
   int32_t dir = ((pad & PAD_RIGHT) ? 1 : 0) - ((pad & PAD_LEFT) ? 1 : 0);
   /* the left stick moves too, when the D-pad does not */
   if (!dir && hd_abs(p->lx) > STICK_DEAD)
      dir = p->lx > 0 ? 1 : -1;
   /* the run button runs, unless the game has a weapon (its buttons shoot) */
   int32_t max = (pad & PAD_RUN) && !hd_weapon.on ? RUN_MAX : WALK_MAX;
   int32_t was_vy;

   if (p->cont)
   {
      /* out of lives: start (or jump) continues with all of them, else it leaves when the time is up */
      if (pressed & (PAD_START | PAD_JUMP))
      {
         p->cont = 0;
         p->lives = hd_health.lives;
         p->respawn = 1;
         hd_play(s, SFX_JOIN, HD_W / 2);
      }
      else if (--p->cont == 0)
         leave(s, i);
      return;
   }
   if (p->ko)
   {
      /* knocked out: nothing moves it; then it is gone for a moment and comes back */
      if (--p->ko == 0)
      {
         p->respawn = RESPAWN_FRAMES;
         life_lost(s, i);
      }
      return;
   }
   if (p->respawn)
   {
      if (--p->respawn == 0)
      {
         place(p, p->check_x, p->check_y - 32);
         p->hurt = 60;
         if (p->hp <= 0)
            p->hp = hd_health.hits; /* back with all its health */
      }
      return;
   }
   if (p->hurt)
      p->hurt--;

   if (p->super_t)
   {
      /* a super attack: standing still (in the air too), the granules leave at the release */
      p->vx = 0;
      p->vy = 0;
      if (++p->super_t == hd_weapon.super_release)
         granules(s, i);
      if (p->super_t >= hd_weapon.super_frames)
         p->super_t = 0;
      return;
   }
   if (hd_weapon.super_on && p->charge >= hd_weapon.super_charge && (pressed & hd_weapon.super_button))
   {
      p->super_t = 1;
      p->charge = 0;
      p->aim = 0;
      s->shake = 6;
      hd_play(s, SFX_SUPER, screen_x(s, FX_INT(p->x)));
      return;
   }

   /* the dash: straight ahead, no gravity, until its frames are done or a wall stops it */
   if (p->dash_wait)
      p->dash_wait--;
   if (p->ground)
      p->dash_air = 0;
   if (p->dash_t)
   {
      p->vx = p->facing * hd_dash.speed;
      p->vy = 0;
      move_x(p);
      if ((p->dash_t & 1) == 0)
         particle(s, FX_INT(p->x) + PW / 2 - p->facing * PW, FX_INT(p->y) + PH * 2 / 3, -p->facing * FX(1), 0, 14, 0xffe8e0d0u, 0);
      if (++p->dash_t > hd_dash.frames || !p->vx)
      {
         p->dash_t = 0;
         p->dash_wait = hd_dash.cooldown;
         p->vx = p->facing * max;
      }
      touch_coins(s, p);
      touch_column(s, p);
      return;
   }
   if (hd_dash.on && (pressed & hd_dash.button) && !p->dash_wait && !p->dash_air)
   {
      p->dash_t = 1;
      p->dash_air = !p->ground;
      p->jumping = 0;
      p->aim = 0;
      p->still = 0;
      burst(s, FX_INT(p->x) + PW / 2, FX_INT(p->y) + PH, 6, 0xffe0d0b0u, FX(1), 0);
      hd_play(s, SFX_DASH, screen_x(s, FX_INT(p->x)));
      return;
   }

   /* walking and running */
   if (dir)
   {
      /* up to the top speed, never past it; faster than it (after running), slowing down to it */
      int32_t along = dir * p->vx;
      if (along < max)
         along = hd_min(max, along + (p->ground ? ACCEL_GROUND : ACCEL_AIR));
      else
         along = hd_max(max, along - FRICTION_GROUND);
      p->vx = dir * along;
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
      if (hd_health.on && --p->hp <= 0)
         life_lost(s, i); /* a fall costs a hit; with none left a life, and it comes back with all of them */
      else if (!hd_health.on)
         p->coins -= hd_min(3, p->coins);
      hd_play(s, SFX_HURT, screen_x(s, FX_INT(p->x)));
      return;
   }

   if (hd_weapon.on)
      fire(s, i);

   p->anim += p->ground ? hd_abs(p->vx) >> 14 : 0;
   if (p->ground && hd_abs(p->vx) < FX_FRAC(1, 4) && !(pad & (PAD_LEFT | PAD_RIGHT | PAD_JUMP | PAD_RUN | PAD_DOWN)) && !dir)
   {
      p->still = hd_min(p->still + 1, 1 << 20);
      /* a package's hero yawns when the bored look starts, and again with each one (as a puppet does: every 400 frames) */
      if (hd_skin_count && p->still >= BORED_AFTER && (p->still - BORED_AFTER) % 400 == 0)
         hd_play(s, SFX_YAWN, screen_x(s, FX_INT(p->x)));
   }
   else
      p->still = 0;
   touch_coins(s, p);
   touch_column(s, p);
}

/* An enemy's hitbox: the built-in walker's, or its kind's (format 3). */
static int32_t enemy_w(const hd_enemy *e)
{
   return e->kind == EK_WALKER ? EW : hd_kinds[e->kind].w;
}

static int32_t enemy_h(const hd_enemy *e)
{
   return e->kind == EK_WALKER ? EH : hd_kinds[e->kind].h;
}

/* The nearest player in the game, -1 when none; *dx and *dy from the enemy's middle to the player's. */
static int32_t nearest(const hd_state *s, const hd_enemy *e, int32_t *dx, int32_t *dy)
{
   int32_t i, best = -1, far = 1 << 30;
   int32_t cx = FX_INT(e->x) + enemy_w(e) / 2, cy = FX_INT(e->y) + enemy_h(e) / 2;
   for (i = 0; i < MAX_PLAYERS; i++)
   {
      const hd_player *p = &s->p[i];
      int32_t x, y;
      if (!p->active || p->respawn || p->ko)
         continue;
      x = FX_INT(p->x) + PW / 2 - cx;
      y = FX_INT(p->y) + PH / 2 - cy;
      if (hd_abs(x) + hd_abs(y) < far)
      {
         far = hd_abs(x) + hd_abs(y);
         best = i;
         *dx = x;
         *dy = y;
      }
   }
   return best;
}

/* Falling, landing and walls for a body of the enemy's hitbox; a wall it walks into stops it (vx 0). */
static void body_move(hd_enemy *e)
{
   int32_t w = enemy_w(e), h = enemy_h(e), ex, ey, tx, ty, front;
   e->vy = hd_min(e->vy + ENEMY_GRAVITY, ENEMY_FALL_MAX);
   if (e->vx)
   {
      e->x += e->vx;
      ex = FX_INT(e->x);
      ey = FX_INT(e->y);
      front = e->vx > 0 ? ex + w : ex - 1;
      for (ty = ey >> 4; ty <= (ey + h - 1) >> 4; ty++)
         if (solid(front >> 4, ty))
         {
            e->x = e->vx > 0 ? FX((front >> 4) * TILE - w) : FX(((front >> 4) + 1) * TILE);
            e->vx = 0;
            break;
         }
   }
   e->y += e->vy;
   ex = FX_INT(e->x);
   ey = FX_INT(e->y);
   e->ground = 0;
   if (e->vy >= 0)
   {
      ty = (ey + h - 1) >> 4;
      for (tx = ex >> 4; tx <= (ex + w - 1) >> 4; tx++)
         if (solid(tx, ty) || shelf(tx, ty))
         {
            e->y = FX(ty * TILE - h);
            e->vy = 0;
            e->ground = 1;
            break;
         }
   }
   else
   {
      ty = ey >> 4;
      for (tx = ex >> 4; tx <= (ex + w - 1) >> 4; tx++)
         if (solid(tx, ty))
         {
            e->y = FX((ty + 1) * TILE);
            e->vy = 0;
            break;
         }
   }
}

/* An enemy's shot from (x, y) pixels. */
static void bolt(hd_state *s, int32_t x, int32_t y, int32_t vx, int32_t vy, int32_t gravity, int32_t life, int32_t big)
{
   hd_bolt *b = &s->bolt[s->bolt_next];
   s->bolt_next = (s->bolt_next + 1) % MAX_BOLTS;
   memset(b, 0, sizeof *b);
   b->life = life;
   b->x = FX(x);
   b->y = FX(y);
   b->vx = vx;
   b->vy = vy;
   b->gravity = gravity;
   b->big = big;
}

/* A spore flies, bobbing: after the nearest player within its range (slowly up or down to them), else to and fro. */
static void spore_step(hd_state *s, hd_enemy *e)
{
   const hd_enemy_kind *k = &hd_kinds[EK_SPORE];
   int32_t dx = 0, dy = 0, who = nearest(s, e, &dx, &dy), nx, my;
   e->t++;
   if (who >= 0 && hd_abs(dx) < k->range && hd_abs(dy) < k->range)
   {
      e->face = dx > 0 ? 1 : -1;
      e->vx = e->face * k->speed;
      if ((e->t & 3) == 0 && hd_abs(dy) > 4)
         e->home_y += dy > 0 ? 1 : -1;
   }
   else
   {
      if (e->t % 150 == 0)
         e->face = -e->face;
      e->vx = e->face * k->speed / 2;
   }
   nx = FX_INT(e->x + e->vx);
   my = (FX_INT(e->y) + k->h / 2) >> 4;
   if (solid((e->vx > 0 ? nx + k->w - 1 : nx) >> 4, my))
   {
      e->face = -e->face; /* a wall turns it back */
      e->vx = 0;
   }
   e->x += e->vx;
   e->home_y = hd_clamp(e->home_y, 0, MAP_H * TILE - k->h);
   /* hd_sin is 16384 a whole: 4 times that is one 16.16 pixel */
   e->y = FX(e->home_y) + hd_sin((e->t * (ANGLE_FULL / 120)) & (ANGLE_FULL - 1)) * 4 * k->bob;
}

/* A spitter stands; it turns to the nearest player within its range and spits an arc at them every `rate` frames. */
static void spitter_step(hd_state *s, hd_enemy *e)
{
   const hd_enemy_kind *k = &hd_kinds[EK_SPITTER];
   int32_t dx = 0, dy = 0, who = nearest(s, e, &dx, &dy);
   int32_t seen = who >= 0 && hd_abs(dx) < k->range && hd_abs(dy) < 120;
   e->vx = 0;
   body_move(e);
   if (e->act < 0)
   {
      if (seen)
         e->face = dx > 0 ? 1 : -1;
      if (++e->t >= k->rate && seen)
      {
         e->act = 0;
         e->act_t = 0;
         e->t = 0;
      }
      return;
   }
   if (++e->act_t == SPIT_AT)
   {
      int32_t x = FX_INT(e->x) + k->w / 2 + e->face * k->w / 2, y = FX_INT(e->y) + k->h * 2 / 5;
      bolt(s, x, y, e->face * k->shot_speed, FX(-2), FX_FRAC(8, 100), 240, 0);
      hd_play(s, SFX_SPIT, screen_x(s, x));
   }
   if (e->act_t >= SPIT_END)
      e->act = -1;
   if (FX_INT(e->y) > MAP_H * TILE + 32)
      e->alive = 0;
}

/* A boss's minion runs at the nearest player, hopping now and then and over what is in its way. */
static void minion_step(hd_state *s, hd_enemy *e)
{
   const hd_enemy_kind *k = &hd_kinds[EK_MINION];
   int32_t dx = 0, dy = 0, who = nearest(s, e, &dx, &dy), want;
   e->t++;
   if (who >= 0 && e->ground)
      e->face = dx > 0 ? 1 : -1;
   want = e->face * k->speed;
   if (e->ground)
      e->vx = want; /* in the air it keeps the speed it was thrown or jumped with */
   body_move(e);
   if (e->ground && (!e->vx || e->t % 70 == 0))
   {
      e->vy = e->vx ? FX(-4) : FX(-6);
      e->vx = want;
      e->ground = 0;
   }
   if (FX_INT(e->y) > MAP_H * TILE + 32)
      e->alive = 0;
}

/* Lets out a boss's brood: thrown from it in a fan, dropping from above the screen, leaping in from both sides. */
static void brood(hd_state *s, hd_enemy *boss)
{
   const hd_enemy_kind *k = &hd_kinds[EK_MINION];
   int32_t n = hd_boss.brood + (s->boss_angry ? (hd_boss.brood + 1) / 2 : 0), i, alive = 0, made = 0;
   int32_t bx = FX_INT(boss->x) + hd_kinds[EK_BOSS].w / 2, by = FX_INT(boss->y) + hd_kinds[EK_BOSS].h / 3;
   int32_t left = FX_INT(s->cam_x), top = FX_INT(s->cam_y);
   for (i = 0; i < MAX_ENEMIES; i++)
      alive += s->e[i].alive == 1 && s->e[i].kind == EK_MINION;
   for (i = 0; i < MAX_ENEMIES && made < n && alive + made < 24; i++)
   {
      hd_enemy *m = &s->e[i];
      int32_t from = made % 4, x, y, vx, vy;
      if (m->alive)
         continue;
      if (from == 1)
      {
         /* dropping from above the screen, anywhere over it */
         x = left + 24 + rng_range(&s->rng, HD_W - 48 - k->w);
         y = top - k->h - rng_range(&s->rng, 40);
         vx = 0;
         vy = FX(1);
      }
      else if (from == 3)
      {
         /* leaping in from one side of the screen or the other */
         int32_t side = (made / 4) & 1;
         x = side ? left + HD_W : left - k->w;
         y = top + HD_H / 3 + rng_range(&s->rng, HD_H / 4);
         vx = (side ? -1 : 1) * (FX(2) + rng_range(&s->rng, FX(2)));
         vy = FX(-3) - rng_range(&s->rng, FX(2));
      }
      else
      {
         /* thrown from the boss, up and to either side */
         x = bx - k->w / 2;
         y = by;
         vx = (rng_range(&s->rng, 2) ? 1 : -1) * (FX(1) + rng_range(&s->rng, FX(3)));
         vy = FX(-4) - rng_range(&s->rng, FX(3));
      }
      memset(m, 0, sizeof *m);
      m->alive = 1;
      m->awake = 1;
      m->kind = EK_MINION;
      m->hp = k->health;
      m->x = FX(x);
      m->y = FX(y);
      m->vx = vx;
      m->vy = vy;
      m->face = vx > 0 ? 1 : -1;
      m->act = -1;
      m->t = rng_range(&s->rng, 70);
      made++;
   }
   burst(s, bx, by, 18, 0xffe8d8f0u, FX(3), 1);
}

/* How far a boss's picture reaches past its hitbox on each side (a sneeze, a cape), in the game's pixels. */
static int32_t boss_overhang(void)
{
   int32_t a, wide = 0;
   for (a = BOSS_IDLE; a <= BOSS_DOWN; a++)
      if (hd_boss_anim[a].frames)
         wide = hd_max(wide, hd_boss_anim[a].frames[0].w / hd_max(1, hd_res));
   return hd_max(0, (wide - hd_kinds[EK_BOSS].w) / 2);
}

/* The arena's left edge for a boss: the screen with the whole boss (its picture too) on its right, inside the level. */
static int32_t arena_of(const hd_enemy *e)
{
   return hd_clamp(FX_INT(e->x) + hd_kinds[EK_BOSS].w + boss_overhang() + 8 - HD_W, 0, hd_max(0, MAP_W * TILE - HD_W));
}

int hd_goal_open(const hd_state *s)
{
   int32_t i;
   if (s->boss_beaten)
      return 1;
   for (i = 0; i < hd_enemy_count; i++)
      if (hd_enemy_kind_of[i] == EK_BOSS)
         return 0;
   return 1;
}

static void boss_down(hd_state *s, hd_enemy *e)
{
   int32_t i, cx = FX_INT(e->x) + hd_kinds[EK_BOSS].w / 2, cy = FX_INT(e->y) + hd_kinds[EK_BOSS].h / 2;
   e->alive = 2;
   e->squash = 150; /* its "down" picture, then it is gone */
   e->vx = 0;
   e->act = -1;
   s->boss_beaten = 1;
   s->arena = 0;
   s->shake = 40;
   s->hitstop = 20;
   /* its brood and its shots go with it */
   for (i = 0; i < MAX_ENEMIES; i++)
      if (s->e[i].alive == 1 && s->e[i].kind == EK_MINION)
      {
         s->e[i].alive = 2;
         s->e[i].squash = 30;
      }
   memset(s->bolt, 0, sizeof s->bolt);
   for (i = 0; i < 4; i++)
      burst(s, cx + (i - 2) * 20, cy + (i & 1) * 30 - 15, 24, i & 1 ? 0xfff8f0a0u : 0xffffffffu, FX(4), 3);
   if (s->music_boss)
   {
      s->music_boss = 0; /* the level's music again */
      s->music_pos = 0;
   }
   hd_play(s, SFX_BOSS_DOWN, screen_x(s, cx));
}

/* A hit on an enemy of a format 3 kind (a walker's stays in fights and shots_step): `damage` of its health. */
static void kind_hit(hd_state *s, hd_enemy *e, int32_t damage)
{
   int32_t ex = FX_INT(e->x), ey = FX_INT(e->y), w = enemy_w(e), h = enemy_h(e);
   e->flash = e->kind == EK_BOSS ? 10 : 6;
   e->hp -= hd_max(1, damage);
   if (e->kind == EK_BOSS)
   {
      if (e->hp <= 0)
      {
         boss_down(s, e);
         return;
      }
      if (!s->boss_angry && e->hp * 2 <= s->boss_max)
      {
         /* half its health gone: it gets angry (faster, shorter rests, a bigger brood) */
         s->boss_angry = 1;
         s->shake = 24;
         hd_play(s, SFX_ROAR, screen_x(s, ex));
      }
      else
         hd_play(s, SFX_BOSS_HIT, screen_x(s, ex));
      return;
   }
   if (e->hp <= 0)
   {
      e->alive = 2;
      e->squash = 30;
      burst(s, ex + w / 2, ey + h / 2, 10, 0xffb070f0u, FX(2), 1);
      hd_play(s, SFX_STOMP, screen_x(s, ex));
   }
   else
      hd_play(s, SFX_HIT, screen_x(s, ex));
}

/* A boss stays in its arena, its whole picture on the screen (on what the camera shows, while it still moves there). */
static void boss_on_screen(hd_state *s, hd_enemy *e)
{
   int32_t w = hd_kinds[EK_BOSS].w, over = boss_overhang();
   int32_t lo = hd_max(s->arena, FX_INT(s->cam_x)) + over;
   int32_t hi = hd_min(s->arena + HD_W, FX_INT(s->cam_x) + HD_W * 256 / (s->zoom ? s->zoom : 256)) - w - over;
   if (FX_INT(e->x) < lo || FX_INT(e->x) > hi)
   {
      e->x = FX(hd_clamp(FX_INT(e->x), lo, hd_max(lo, hi)));
      if (e->act == BA_CHARGE)
         e->vx = 0;
   }
}

/*
 * A boss waits off screen; once it is mostly on it, it wakes up (its music,
 * a roar) and the camera stays on its arena. Then it rests, pacing at the
 * players, and does its attacks in turn: each one a windup and the attack.
 */
static void boss_step(hd_state *s, hd_enemy *e)
{
   const hd_enemy_kind *k = &hd_kinds[EK_BOSS];
   int32_t dx = 0, dy = 0, who, windup = BOSS_WINDUP(s);
   int32_t speed = s->boss_angry ? k->speed * 4 / 3 : k->speed, rest = s->boss_angry ? hd_boss.rest * 2 / 3 : hd_boss.rest;
   int32_t done = 0;
   if (!s->boss)
   {
      int32_t z = s->zoom ? s->zoom : 256;
      if (FX_INT(e->x) + k->w + boss_overhang() > FX_INT(s->cam_x) + HD_W * 256 / z ||
          FX_INT(e->y) + k->h > FX_INT(s->cam_y) + HD_H * 256 / z)
         return; /* until its whole picture is on the screen */
      s->boss = (int32_t)(e - s->e) + 1;
      s->boss_max = e->hp;
      s->arena = arena_of(e);
      e->home_x = FX_INT(e->x);
      e->t = rest;
      e->act = -1;
      s->shake = 30;
      if (hd_pkg_boss_music)
      {
         s->music_boss = 1;
         s->music_pos = 0;
      }
      hd_play(s, SFX_ROAR, screen_x(s, FX_INT(e->x)));
   }
   who = nearest(s, e, &dx, &dy);
   if (e->act < 0)
   {
      /* resting: it paces at the players */
      if (who >= 0)
         e->face = dx > 0 ? 1 : -1;
      if (e->ground)
         e->vx = who >= 0 && hd_abs(dx) > k->w ? e->face * speed / 3 : 0;
      body_move(e);
      if (--e->t <= 0)
      {
         e->act = hd_boss.attack[e->seq % hd_boss.attacks];
         e->seq++;
         e->act_t = 0;
         e->vx = 0;
      }
   }
   else
   {
      int32_t at = ++e->act_t - windup; /* frames into the attack itself (<= 0: the windup) */
      if (at <= 0 && e->ground)
         e->vx = 0;
      switch (e->act)
      {
      case BA_JUMP:
         if (at == 1 && e->ground)
         {
            /* a leap that lands near the player: about 40 frames in the air */
            e->vy = FX(-9);
            e->vx = hd_clamp((int32_t)((int64_t)FX(dx) / 40), -speed * 3, speed * 3);
            e->ground = 0;
         }
         body_move(e);
         if (at > 4 && e->ground)
         {
            /* the landing shakes the floor: a shock wave runs out both ways */
            int32_t fx = FX_INT(e->x), fy = FX_INT(e->y) + k->h - 10;
            s->shake = 16;
            burst(s, fx + k->w / 2, fy + 8, 16, 0xffe0d0b0u, FX(3), 1);
            bolt(s, fx, fy, -k->shot_speed, 0, 0, 60, 1);
            bolt(s, fx + k->w, fy, k->shot_speed, 0, 0, 60, 1);
            hd_play(s, SFX_STOMP, screen_x(s, fx));
            done = 1;
         }
         else if (at > 120)
            done = 1;
         break;
      case BA_CHARGE:
         if (at == 1)
            e->vx = e->face * speed * 2;
         if (at > 0 && e->vx == 0)
         {
            s->shake = 12; /* into a wall */
            done = 1;
         }
         if (at > 0 && (at & 3) == 0)
            particle(s, FX_INT(e->x) + k->w / 2, FX_INT(e->y) + k->h, 0, FX_FRAC(-1, 2), 16, 0xffe0d0b0u, 0);
         body_move(e);
         if (at > 90)
            done = 1;
         break;
      case BA_ADVANCE:
         /* it creeps at the players for a while, then back to where it stood */
         if (at > 0 && at <= 70)
            e->vx = e->face * speed;
         else if (at > 70)
         {
            int32_t back = e->home_x - FX_INT(e->x);
            e->vx = hd_abs(back) < 4 ? 0 : (back > 0 ? speed : -speed);
            if (!e->vx || at > 200)
               done = 1;
         }
         body_move(e);
         break;
      case BA_SPIT:
         body_move(e);
         if (at == 1)
         {
            /* a fan of big shots at the player, falling a little */
            int32_t n = hd_boss.spit + (s->boss_angry ? 2 : 0), j;
            int32_t x = FX_INT(e->x) + k->w / 2 + e->face * k->w / 3, y = FX_INT(e->y) + k->h / 3;
            for (j = 0; j < n; j++)
               bolt(s, x, y, e->face * k->shot_speed, n > 1 ? FX(-3) + (int32_t)((int64_t)FX(4) * j / (n - 1)) : FX(-1),
                    FX_FRAC(6, 100), 240, 1);
            hd_play(s, SFX_SPIT, screen_x(s, x));
         }
         if (at > 30)
            done = 1;
         break;
      default: /* BA_BROOD */
         body_move(e);
         if (at == 1)
         {
            brood(s, e);
            s->shake = 10;
            hd_play(s, SFX_ROAR, screen_x(s, FX_INT(e->x)));
         }
         if (at > 40)
            done = 1;
         break;
      }
      if (done)
      {
         e->act = -1;
         e->t = rest;
      }
   }
   boss_on_screen(s, e);
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
   if (e->flash)
      e->flash--;
   e->anim++;
   switch (e->kind)
   {
   case EK_SPORE: spore_step(s, e); return;
   case EK_SPITTER: spitter_step(s, e); return;
   case EK_BOSS: boss_step(s, e); return;
   case EK_MINION: minion_step(s, e); return;
   default: break;
   }
   e->vy = hd_min(e->vy + ENEMY_GRAVITY, ENEMY_FALL_MAX);
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
      if (!p->active || p->respawn || p->ko || p->super_t)
         continue; /* nothing hurts a player knocked out or in a super attack */
      px = FX_INT(p->x);
      py = FX_INT(p->y);
      for (j = 0; j < MAX_ENEMIES; j++)
      {
         hd_enemy *e = &s->e[j];
         int32_t ex = FX_INT(e->x), ey = FX_INT(e->y);
         if (e->alive != 1 || !overlap(px, py, PW, PH, ex, ey, enemy_w(e), enemy_h(e)))
            continue;
         if (p->dash_t)
            continue; /* a dash goes through */
         if (p->vy > 0 && py + PH - FX_INT(p->vy) <= ey + 6 && e->kind == EK_BOSS)
         {
            /* stomping on a boss hurts it and bounces off */
            p->y = FX(ey - PH);
            p->vy = (p->pad & PAD_JUMP) ? BOUNCE_HELD : BOUNCE;
            p->jumping = 1;
            s->hitstop = 4;
            kind_hit(s, e, 2);
            px = FX_INT(p->x);
            py = FX_INT(p->y);
         }
         else if (p->vy > 0 && py + PH - FX_INT(p->vy) <= ey + 6)
         {
            e->alive = 2;
            e->squash = 30;
            p->y = FX(ey - PH);
            p->vy = (p->pad & PAD_JUMP) ? BOUNCE_HELD : BOUNCE;
            p->jumping = 1;
            s->hitstop = 4;
            burst(s, ex + enemy_w(e) / 2, ey + enemy_h(e) / 2, 8, 0xffb070f0u, FX(2), 1);
            hd_play(s, SFX_STOMP, screen_x(s, ex));
         }
         else if (!p->hurt)
            hurt(s, p, ex + enemy_w(e) / 2);
      }
   }
}

/* Shots fly, stop at walls and hit enemies: a hit costs one of its hits, the last one pops it. */
static void shots_step(hd_state *s)
{
   int32_t i, j;
   for (i = 0; i < MAX_SHOTS; i++)
   {
      hd_shot *q = &s->shot[i];
      int32_t x, y;
      if (q->hit)
      {
         q->hit--;
         continue;
      }
      if (!q->life)
         continue;
      q->life--;
      q->age++;
      q->x += q->vx;
      if (q->granule)
      {
         q->vy = hd_min(q->vy + FX_FRAC(18, 100), FX(8)); /* granules fall */
         q->y += q->vy;
      }
      x = FX_INT(q->x);
      y = FX_INT(q->y);
      if (solid(x >> 4, y >> 4) || y < 0)
      {
         q->hit = SHOT_HIT_FRAMES;
         q->life = 0;
         burst(s, x, y, 4, 0xffffe0d0u, FX(1), 2);
         continue;
      }
      for (j = 0; j < MAX_ENEMIES; j++)
      {
         hd_enemy *e = &s->e[j];
         int32_t ex = FX_INT(e->x), ey = FX_INT(e->y);
         /* a shot's middle within the enemy's hitbox, with some grace (a shot is drawn about 16 pixels tall) */
         if (e->alive != 1 || x < ex - 4 || x > ex + enemy_w(e) + 4 || y < ey - SHOT_GRACE || y > ey + enemy_h(e) + SHOT_GRACE)
            continue;
         if (e->kind == EK_BOSS && !s->boss)
            continue; /* not before it wakes up */
         q->hit = SHOT_HIT_FRAMES;
         q->life = 0;
         e->flash = 6;
         if (!q->granule && hd_weapon.super_on && q->owner >= 0 && q->owner < MAX_PLAYERS)
            s->p[q->owner].charge = hd_min(s->p[q->owner].charge + 1, hd_weapon.super_charge);
         if (e->kind != EK_WALKER)
         {
            kind_hit(s, e, q->damage);
            break;
         }
         e->hp -= hd_max(1, q->damage);
         if (e->hp <= 0)
         {
            e->alive = 2;
            e->squash = 30;
            burst(s, ex + EW / 2, ey + EH / 2, 10, 0xffb070f0u, FX(2), 1);
            hd_play(s, SFX_STOMP, screen_x(s, ex));
         }
         else
         {
            e->x += q->vx > 0 ? FX(2) : FX(-2); /* pushed back a little */
            hd_play(s, SFX_HIT, screen_x(s, ex));
         }
         break;
      }
   }
}

/* The enemies' shots fly (falling when they have gravity), burst on walls and hurt the players they touch. */
static void bolts_step(hd_state *s)
{
   int32_t i, j;
   for (i = 0; i < MAX_BOLTS; i++)
   {
      hd_bolt *b = &s->bolt[i];
      int32_t x, y, r;
      if (!b->life)
         continue;
      b->life--;
      b->age++;
      b->vy = hd_min(b->vy + b->gravity, FX(8));
      b->x += b->vx;
      b->y += b->vy;
      x = FX_INT(b->x);
      y = FX_INT(b->y);
      r = b->big ? 10 : 6;
      if (solid(x >> 4, y >> 4) || y > MAP_H * TILE + 32)
      {
         b->life = 0;
         burst(s, x, y, 6, 0xffc0e060u, FX(1), 1);
         continue;
      }
      for (j = 0; j < MAX_PLAYERS && s->phase == PH_PLAY; j++)
      {
         hd_player *p = &s->p[j];
         if (!p->active || p->respawn || p->ko || p->super_t || p->dash_t || p->hurt)
            continue;
         if (overlap(FX_INT(p->x), FX_INT(p->y), PW, PH, x - r, y - r, 2 * r, 2 * r))
         {
            hurt(s, p, x);
            b->life = 0;
            burst(s, x, y, 8, 0xffc0e060u, FX(2), 1);
            break;
         }
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
   if (hd_fx.zoom_auto && s->arena && !s->boss_beaten)
      s->zoom += (256 - s->zoom + 7) / 8; /* a boss's arena is seen whole, at 1x: no zoom moving its edges */
   else if (hd_fx.zoom_auto)
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
   if (s->arena && !s->boss_beaten && s->boss > 0 && s->boss <= MAX_ENEMIES)
   {
      /* a boss's fight stays on its arena: across, and high enough that its floor and the whole boss show (a
         camera that followed the players' jumps up cut the boss's feet off) */
      const hd_enemy *b = &s->e[s->boss - 1];
      tx = s->arena;
      ty = b->home_y + hd_kinds[EK_BOSS].h + 40 - vh;
   }
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

/* Back to the title after the last level or a game over, the music and sounds going on. */
static void over_to_title(hd_state *s)
{
   uint32_t rng = s->rng;
   hd_channel ch[MAX_CHANNELS];
   int32_t row = s->music_row, tick = s->music_tick, sfx = s->sfx_next, mpos = s->music_pos;
   memcpy(ch, s->ch, sizeof ch);
   hd_reset(s);
   s->rng = rng;
   memcpy(s->ch, ch, sizeof ch);
   s->music_row = row;
   s->music_tick = tick;
   s->music_pos = mpos;
   s->sfx_next = sfx;
}

void hd_step(hd_state *s, const hd_input in[MAX_PLAYERS])
{
   int32_t i;
   uint32_t any = 0;
   hd_stage_select(s->stage); /* the level the state plays (a save state may bring another) */
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
         uint32_t who = 0;
         for (i = 0; i < MAX_PLAYERS; i++)
            if ((s->p[i].pad & ~s->p[i].prev) & (PAD_START | PAD_JUMP))
               who |= 1u << i;
         if (hd_screens[SCREEN_INTRO].px)
         {
            /* the level's card first */
            s->phase = PH_INTRO;
            s->phase_t = 0;
            s->skip_hold = 0;
            s->intro_join = who;
         }
         else
         {
            reset_level(s);
            s->phase = PH_PLAY;
            for (i = 0; i < MAX_PLAYERS; i++)
               if (who & (1u << i))
                  join(s, i);
         }
      }
      particles_step(s);
      return;
   }
   if (s->phase == PH_INTRO)
   {
      /* the intro: its seconds, or until someone holds jump long enough; pressing start or jump joins too */
      int held = 0;
      for (i = 0; i < MAX_PLAYERS; i++)
      {
         if ((s->p[i].pad & ~s->p[i].prev) & (PAD_START | PAD_JUMP))
            s->intro_join |= 1u << i;
         if (s->p[i].pad & PAD_JUMP)
            held = 1;
      }
      s->skip_hold = held ? s->skip_hold + 1 : 0;
      if (++s->phase_t >= hd_intro_frames || s->skip_hold >= SKIP_HOLD_FRAMES)
      {
         uint32_t who = s->intro_join;
         int32_t playing = 0;
         for (i = 0; i < MAX_PLAYERS; i++)
            playing |= s->p[i].active;
         if (!playing)
            reset_level(s); /* from the title; after a level the next one is already set up */
         s->phase = PH_PLAY;
         for (i = 0; i < MAX_PLAYERS; i++)
            if ((who & (1u << i)) && !s->p[i].active)
               join(s, i);
      }
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
      else if (s->p[i].active && !s->p[i].cont && (pressed & PAD_START) && s->phase == PH_PLAY)
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

   if (s->phase == PH_OVER)
   {
      /* game over: a moment, then the title */
      if (++s->phase_t >= OVER_FRAMES)
      {
         over_to_title(s);
         return;
      }
   }
   else if (s->phase == PH_CLEAR)
   {
      s->phase_t++;
      if (s->phase_t % 20 == 0 && s->phase_t < 200)
         burst(s, FX_INT(s->cam_x) + 80 + rng_range(&s->rng, HD_W - 160), 60 + rng_range(&s->rng, 120), 16,
               hd_player_color[rng_range(&s->rng, hd_players)], FX(3), 3);
      if (s->phase_t >= CLEAR_FRAMES && s->stage + 1 < hd_stage_count)
      {
         next_stage(s);
         return;
      }
      if (s->phase_t >= CLEAR_FRAMES)
      {
         over_to_title(s);
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
   shots_step(s);
   bolts_step(s);
   if (s->phase == PH_PLAY)
   {
      fights(s);
      dialogs(s);
   }
   camera(s);
   if (s->boss > 0 && s->boss <= MAX_ENEMIES && s->e[s->boss - 1].alive == 1 && s->arena)
      boss_on_screen(s, &s->e[s->boss - 1]); /* again after the camera moved */
   particles_step(s);
}
