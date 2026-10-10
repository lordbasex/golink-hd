/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Save states: a 48 byte header ("GLHD", the layout version, the state's
 * size, a checksum of the body, the game's SHA-256) and then every 32-bit
 * word of hd_state in little endian, so a save state means the same on every
 * CPU. A save state of another version, another game, another size or with
 * a wrong checksum is refused, never misread.
 */
#include <string.h>
#include "hd.h"
#include "sprite.h"

/* The state rule (hd.h): only 32-bit fields, so no padding anywhere. */
typedef char hd_state_is_words[(sizeof(hd_state) % 4 == 0) ? 1 : -1];

static void put32(uint8_t *p, uint32_t v)
{
   p[0] = (uint8_t)v;
   p[1] = (uint8_t)(v >> 8);
   p[2] = (uint8_t)(v >> 16);
   p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
   return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* FNV-1a over the body. */
static uint32_t checksum(const uint8_t *p, uint32_t n)
{
   uint32_t h = 2166136261u, i;
   for (i = 0; i < n; i++)
      h = (h ^ p[i]) * 16777619u;
   return h;
}

void hd_save(const hd_state *s, uint8_t *out)
{
   const uint32_t n = (uint32_t)(sizeof(hd_state) / 4);
   uint32_t i, w;
   uint8_t *body = out + HD_SAVE_HEADER;
   for (i = 0; i < n; i++)
   {
      memcpy(&w, (const uint8_t *)s + i * 4, 4);
      put32(body + i * 4, w);
   }
   memcpy(out, "GLHD", 4);
   put32(out + 4, HD_STATE_VERSION);
   put32(out + 8, (uint32_t)sizeof(hd_state));
   put32(out + 12, checksum(body, (uint32_t)sizeof(hd_state)));
   memcpy(out + 16, hd_content_id, 32);
}

/*
 * A save state can come from anywhere (a file, the network), so every value
 * used as an index or a divisor is brought back into range after loading.
 */
#define FAR FX(MAP_W * TILE + 4096)
#define SPEED FX(64)
#define COUNT (1 << 30) /* counters that only grow */

static void sanitize(hd_state *s)
{
   int32_t i;
   s->phase = hd_clamp(s->phase, PH_TITLE, PH_CREDITS);
   s->music_row = hd_clamp(s->music_row, 0, 63);
   s->part_next = hd_clamp(s->part_next, 0, MAX_PARTICLES - 1);
   s->sfx_next = hd_clamp(s->sfx_next, MUSIC_CHANNELS, MAX_CHANNELS - 1);
   s->hitstop = hd_clamp(s->hitstop, 0, 60);
   s->shake = hd_clamp(s->shake, 0, 60);
   s->shake_x = hd_clamp(s->shake_x, -8, 8);
   s->shake_y = hd_clamp(s->shake_y, -8, 8);
   s->frame = hd_clamp(s->frame, 0, COUNT);
   s->phase_t = hd_clamp(s->phase_t, 0, COUNT);
   s->music_tick = hd_clamp(s->music_tick, 0, COUNT);
   s->skip_hold = hd_clamp(s->skip_hold, 0, 600);
   s->stage = hd_stage_count ? hd_clamp(s->stage, 0, hd_stage_count - 1) : 0;
   hd_stage_select(s->stage); /* its music, for the clamp below */
   s->music_boss = s->music_boss && hd_pkg_boss_music ? 1 : 0;
   s->music_pos = hd_clamp(s->music_pos, 0, (s->music_boss ? hd_pkg_boss_music_frames : hd_pkg_music_frames) > 0 ? (s->music_boss ? hd_pkg_boss_music_frames : hd_pkg_music_frames) - 1 : 0);
   s->paused = s->paused ? 1 : 0;
   s->zoom = s->zoom ? hd_clamp(s->zoom, 128, 512) : 0;
   s->dlg = hd_clamp(s->dlg, 0, DIALOGS_MAX);
   s->dlg_chars = hd_clamp(s->dlg_chars, 0, 1000);
   hd_show_sanitize(&s->show);
   s->lowpass = hd_clamp(s->lowpass, 0, 256);
   s->echo = hd_clamp(s->echo, 0, ECHO_MAX);
   s->echo_feedback = hd_clamp(s->echo_feedback, 0, 230);
   s->echo_mix = hd_clamp(s->echo_mix, 0, 256);
   s->echo_pos = s->echo ? hd_clamp(s->echo_pos, 0, s->echo - 1) : 0;
   s->lp_l = hd_clamp(s->lp_l, -(1 << 20), 1 << 20);
   s->lp_r = hd_clamp(s->lp_r, -(1 << 20), 1 << 20);
   for (i = 0; i < ECHO_MAX * 2; i++)
      s->echo_buf[i] = hd_clamp(s->echo_buf[i], -131072, 131071);
   if (!s->rng)
      s->rng = 1;
   for (i = 0; i < MAX_CHANNELS; i++)
   {
      hd_channel *c = &s->ch[i];
      if (c->sample < -1 || c->sample >= HD_SAMPLES)
         c->sample = -1;
      c->pos = hd_max(c->pos, 0);
      c->frac &= 0xffff;
      c->step = hd_clamp(c->step, 0, FX(64));
      c->vol = hd_clamp(c->vol, 0, 256);
      c->decay = hd_clamp(c->decay, 0, 256);
      c->pan = hd_clamp(c->pan, -256, 256);
   }
   for (i = 0; i < MAX_PARTICLES; i++)
   {
      hd_particle *q = &s->part[i];
      q->max = hd_clamp(q->max, 0, 1000);
      q->life = hd_clamp(q->life, 0, q->max); /* drawing divides by max only while life > 0 */
   }
   for (i = 0; i < MAX_PARTICLES; i++)
   {
      hd_particle *q = &s->part[i];
      q->x = hd_clamp(q->x, -FAR, FAR);
      q->y = hd_clamp(q->y, -FAR, FAR);
      q->vx = hd_clamp(q->vx, -SPEED, SPEED);
      q->vy = hd_clamp(q->vy, -SPEED, SPEED);
   }
   s->shot_next = hd_clamp(s->shot_next, 0, MAX_SHOTS - 1);
   for (i = 0; i < MAX_SHOTS; i++)
   {
      hd_shot *q = &s->shot[i];
      q->life = hd_clamp(q->life, 0, 4000);
      q->hit = hd_clamp(q->hit, 0, SHOT_HIT_FRAMES);
      q->age = hd_clamp(q->age, 0, COUNT);
      q->owner = hd_clamp(q->owner, 0, MAX_PLAYERS - 1);
      q->x = hd_clamp(q->x, -FAR, FAR);
      q->y = hd_clamp(q->y, -FAR, FAR);
      q->vx = hd_clamp(q->vx, -SPEED, SPEED);
      q->vy = hd_clamp(q->vy, -SPEED, SPEED);
      q->damage = hd_clamp(q->damage, 0, 100);
      q->granule = q->granule ? 1 : 0;
   }
   for (i = 0; i < MAX_ENEMIES; i++)
   {
      hd_enemy *e = &s->e[i];
      e->alive = hd_clamp(e->alive, 0, 2);
      e->hp = hd_clamp(e->hp, 0, 999);
      e->kind = hd_clamp(e->kind, 0, EK_COUNT - 1);
      e->t = hd_clamp(e->t, 0, COUNT);
      e->act = hd_clamp(e->act, -1, BA_COUNT);
      e->act_t = hd_clamp(e->act_t, 0, COUNT);
      e->face = hd_clamp(e->face, -1, 1);
      e->ground = e->ground ? 1 : 0;
      e->seq = hd_clamp(e->seq, 0, COUNT);
      e->home_x = hd_clamp(e->home_x, -MAP_W * TILE, 2 * MAP_W * TILE);
      e->home_y = hd_clamp(e->home_y, -MAP_H * TILE, 2 * MAP_H * TILE);
      e->flash = hd_clamp(e->flash, 0, 60);
      e->squash = hd_clamp(e->squash, 0, 600);
      e->anim = hd_clamp(e->anim, 0, COUNT);
      e->x = hd_clamp(e->x, -FAR, FAR);
      e->y = hd_clamp(e->y, -FAR, FAR);
      e->vx = hd_clamp(e->vx, -SPEED, SPEED);
      e->vy = hd_clamp(e->vy, -SPEED, SPEED);
   }
   s->bolt_next = hd_clamp(s->bolt_next, 0, MAX_BOLTS - 1);
   for (i = 0; i < MAX_BOLTS; i++)
   {
      hd_bolt *b = &s->bolt[i];
      b->life = hd_clamp(b->life, 0, 4000);
      b->age = hd_clamp(b->age, 0, COUNT);
      b->x = hd_clamp(b->x, -FAR, FAR);
      b->y = hd_clamp(b->y, -FAR, FAR);
      b->vx = hd_clamp(b->vx, -SPEED, SPEED);
      b->vy = hd_clamp(b->vy, -SPEED, SPEED);
      b->gravity = hd_clamp(b->gravity, 0, FX(2));
      b->big = b->big ? 1 : 0;
      b->puff = b->puff ? 1 : 0;
   }
   s->boss = hd_clamp(s->boss, 0, MAX_ENEMIES);
   s->boss_max = hd_clamp(s->boss_max, 0, 999);
   s->boss_angry = s->boss_angry ? 1 : 0;
   s->boss_beaten = s->boss_beaten ? 1 : 0;
   s->boss_form = s->boss_form && hd_boss_forms[1].cfg.on ? 1 : 0;
   hd_boss_form_use(s->boss_form);
   s->arena = hd_clamp(s->arena, 0, MAP_W * TILE);
   s->cam_x = hd_clamp(s->cam_x, 0, FX(MAP_W * TILE - HD_W));
   s->cam_y = hd_clamp(s->cam_y, 0, FX(MAP_H * TILE - HD_H));
   for (i = 0; i < MAX_PLAYERS; i++)
   {
      hd_player *p = &s->p[i];
      p->x = hd_clamp(p->x, -FAR, FAR);
      p->y = hd_clamp(p->y, -FAR, FAR);
      p->vx = hd_clamp(p->vx, -SPEED, SPEED);
      p->vy = hd_clamp(p->vy, -SPEED, SPEED);
      p->active = p->active ? 1 : 0;
      p->facing = hd_clamp(p->facing, -1, 1);
      p->anim = hd_clamp(p->anim, 0, COUNT);
      p->landed = hd_clamp(p->landed, 0, COUNT);
      p->still = hd_clamp(p->still, 0, 1 << 20);
      p->coins = hd_clamp(p->coins, 0, COUNT);
      p->coyote = hd_clamp(p->coyote, 0, 60);
      p->buffer = hd_clamp(p->buffer, 0, 60);
      p->drop = hd_clamp(p->drop, 0, 60);
      p->check_x = hd_clamp(p->check_x, 0, MAP_W * TILE);
      p->check_y = hd_clamp(p->check_y, 0, MAP_H * TILE);
      p->respawn = hd_clamp(p->respawn, 0, 600);
      p->hurt = hd_clamp(p->hurt, 0, 600);
      p->shot_wait = hd_clamp(p->shot_wait, 0, 120);
      p->aim = hd_clamp(p->aim, 0, 200);
      p->dash_t = hd_clamp(p->dash_t, 0, 60);
      p->dash_wait = hd_clamp(p->dash_wait, 0, 600);
      p->dash_air = p->dash_air ? 1 : 0;
      p->hp = hd_clamp(p->hp, 0, 99);
      p->ko = hd_clamp(p->ko, 0, 600);
      p->lives = hd_clamp(p->lives, 0, 99);
      p->cont = hd_clamp(p->cont, 0, 60 * 60);
      p->charge = hd_clamp(p->charge, 0, 200);
      p->super_t = hd_clamp(p->super_t, 0, 240);
   }
}

int hd_load(hd_state *s, const uint8_t *in, uint32_t size)
{
   const uint32_t n = (uint32_t)(sizeof(hd_state) / 4);
   const uint8_t *body = in + HD_SAVE_HEADER;
   uint32_t i, w;
   if (size < HD_SAVE_HEADER || memcmp(in, "GLHD", 4) != 0)
      return 0;
   if (memcmp(in + 16, hd_content_id, 32) != 0)
      return 0; /* another game's */
   if (get32(in + 4) != HD_STATE_VERSION || get32(in + 8) != sizeof(hd_state) || size < HD_SAVE_SIZE)
      return 0;
   if (get32(in + 12) != checksum(body, (uint32_t)sizeof(hd_state)))
      return 0;
   for (i = 0; i < n; i++)
   {
      w = get32(body + i * 4);
      memcpy((uint8_t *)s + i * 4, &w, 4);
   }
   sanitize(s);
   return 1;
}
