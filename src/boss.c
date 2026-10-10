/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Format 3's enemies past the built-in walker: the manifest's "enemies"
 * (spore and spitter, for every level) and a level's "boss" (kept with its
 * level, stage.c). The rules are in game.c, the drawing in draw.c.
 *
 *   "enemies": {
 *     "spore":   {"hitbox": [w, h], "health": 2, "speed": 70, "bob": 10, "range": 220},
 *     "spitter": {"hitbox": [w, h], "health": 3, "rate": 110, "shot_speed": 350, "range": 300}
 *   }
 *
 *   and also, each optional:
 *     "roller":   {"hitbox": [w, h], "health": 3, "speed": 300, "range": 360},
 *     "hopper":   {"hitbox": [w, h], "health": 2, "speed": 220, "jump": 650, "rate": 70, "range": 300},
 *     "puffer":   {"hitbox": [w, h], "health": 4, "rate": 150, "shot_speed": 260, "spores": 3, "range": 300},
 *     "splitter": {"hitbox": [w, h], "health": 4, "speed": 60, "range": 400}
 *
 * hitbox in the game's pixels (4 to 400); health the hits it takes (1 to
 * 999); speed, shot_speed and jump in hundredths of a pixel a frame; bob in
 * pixels; range in pixels (how near a player must be); rate in frames;
 * spores 1 to 9. A splitter's halves are two thirds of its hitbox and take
 * half its health.
 *
 *   "boss": {"name": "FAGO REX", "hitbox": [w, h], "health": 60, "speed": 250,
 *            "attacks": ["jump", "spit", "brood", "charge"], "rest": 80, "spit": 3,
 *            "shot_speed": 450, "brood": 5,
 *            "minion": {"hitbox": [w, h], "health": 1, "speed": 160},
 *            "sprites": {"idle": ..., "windup": ..., "attack": ..., "hurt": ..., "down": ...,
 *                        "minion": ..., "shot": ...},
 *            "music": {"file": "boss.wav", "volume": 180}}
 *
 * The attacks are done in turn with `rest` frames between them (two
 * thirds of it once the boss has lost half its health, when it also moves a
 * third faster and lets out half as many minions more): "jump" leaps at
 * the nearest player, "charge" runs at them, "spit" throws a fan of `spit`
 * shots, "advance" walks at the players (a spider's creep) and back to
 * where it stood, "brood" lets out `brood` minions, small ones that come
 * flying from everywhere (thrown by the boss, dropping from above, leaping
 * in from both sides of the screen), then run at the players and hop. The
 * level's goal opens when the boss is beaten.
 */
#include <stdio.h>
#include <string.h>
#include "sprite.h"

hd_enemy_kind hd_kinds[EK_COUNT];
hd_boss_config hd_boss;
hd_anim hd_boss_anim[BOSS_ANIMS];

static const char *const boss_anim_names[BOSS_ANIMS] = { "idle", "windup", "attack", "hurt", "down", "minion", "shot" };
static const char *const attack_names[BA_COUNT] = { "jump", "charge", "spit", "brood", "advance" };

static int32_t num(const json *obj, const char *key, int32_t lo, int32_t hi, int32_t fallback, int *bad)
{
   const json *j = obj ? hd_json_get(obj, key) : NULL;
   if (!j)
      return fallback;
   if (j->type != JSON_INT || j->num < lo || j->num > hi)
   {
      *bad = 1;
      return fallback;
   }
   return (int32_t)j->num;
}

/* "hitbox": [w, h] into k (kept when absent); 0 when it is not one. */
static int hitbox(const json *obj, hd_enemy_kind *k)
{
   const json *hb = obj ? hd_json_get(obj, "hitbox") : NULL;
   if (!hb)
      return 1;
   if (hb->type != JSON_ARRAY || hb->count != 2 || hd_json_at(hb, 0)->type != JSON_INT || hd_json_at(hb, 1)->type != JSON_INT ||
       hd_json_at(hb, 0)->num < 4 || hd_json_at(hb, 0)->num > 400 || hd_json_at(hb, 1)->num < 4 || hd_json_at(hb, 1)->num > 400)
      return 0;
   k->w = (int32_t)hd_json_at(hb, 0)->num;
   k->h = (int32_t)hd_json_at(hb, 1)->num;
   return 1;
}

void hd_kinds_default(void)
{
   hd_enemy_kind *k;
   memset(hd_kinds, 0, sizeof hd_kinds);
   k = &hd_kinds[EK_SPORE];
   k->w = EW;
   k->h = EH;
   k->health = hd_weapon.enemy_health;
   k->speed = FX_FRAC(70, 100);
   k->bob = 10;
   k->range = 220;
   k = &hd_kinds[EK_SPITTER];
   k->w = EW;
   k->h = EH;
   k->health = hd_weapon.enemy_health + 1;
   k->rate = 110;
   k->shot_speed = FX_FRAC(350, 100);
   k->range = 300;
   k = &hd_kinds[EK_ROLLER];
   k->w = EW;
   k->h = EH;
   k->health = hd_weapon.enemy_health;
   k->speed = FX_FRAC(300, 100);
   k->range = 360;
   k = &hd_kinds[EK_HOPPER];
   k->w = EW;
   k->h = EH;
   k->health = hd_weapon.enemy_health;
   k->speed = FX_FRAC(220, 100);
   k->jump = FX_FRAC(650, 100);
   k->rate = 70;
   k->range = 300;
   k = &hd_kinds[EK_PUFFER];
   k->w = EW;
   k->h = EH;
   k->health = hd_weapon.enemy_health + 1;
   k->rate = 150;
   k->shot_speed = FX_FRAC(260, 100);
   k->count = 3;
   k->range = 300;
   k = &hd_kinds[EK_SPLITTER];
   k->w = EW;
   k->h = EH;
   k->health = hd_weapon.enemy_health + 1;
   k->speed = FX_FRAC(60, 100);
   k->range = 400;
}

/* One of the optional kinds ("roller", "hopper", "puffer", "splitter"): what it has of the values. */
static const char *more_kind(const json *enemies, const char *name, hd_enemy_kind *k, int *bad)
{
   static char msg[120];
   const json *j = hd_json_get(enemies, name);
   if (!j)
      return NULL;
   if (j->type != JSON_OBJECT || !hitbox(j, k))
   {
      snprintf(msg, sizeof msg, "the %s must be an object with a hitbox of [width, height]: 4 to 400 pixels each", name);
      return msg;
   }
   k->health = num(j, "health", 1, 999, k->health, bad);
   k->speed = FX_FRAC(num(j, "speed", 0, 2000, (int32_t)((int64_t)k->speed * 100 >> 16), bad), 100);
   k->jump = FX_FRAC(num(j, "jump", 0, 2000, (int32_t)((int64_t)k->jump * 100 >> 16), bad), 100);
   k->rate = num(j, "rate", 30, 1200, k->rate ? k->rate : 60, bad);
   k->shot_speed = FX_FRAC(num(j, "shot_speed", 50, 2000, k->shot_speed ? (int32_t)((int64_t)k->shot_speed * 100 >> 16) : 300, bad), 100);
   k->count = num(j, "spores", 1, 9, k->count ? k->count : 3, bad);
   k->range = num(j, "range", 0, 4000, k->range, bad);
   return NULL;
}

const char *hd_enemies_load(const json *enemies)
{
   const json *sp, *pt;
   int bad = 0;
   hd_enemy_kind *k;
   hd_kinds_default();
   if (!enemies)
      return NULL;
   if (enemies->type != JSON_OBJECT)
      return "manifest.json's enemies must be an object";
   sp = hd_json_get(enemies, "spore");
   pt = hd_json_get(enemies, "spitter");
   if ((sp && sp->type != JSON_OBJECT) || (pt && pt->type != JSON_OBJECT))
      return "the enemies' spore and spitter must be objects";
   k = &hd_kinds[EK_SPORE];
   if (!hitbox(sp, k))
      return "the spore's hitbox must be [width, height]: 4 to 400 pixels each";
   k->health = num(sp, "health", 1, 999, k->health, &bad);
   k->speed = FX_FRAC(num(sp, "speed", 0, 2000, 70, &bad), 100);
   k->bob = num(sp, "bob", 0, 200, k->bob, &bad);
   k->range = num(sp, "range", 0, 4000, k->range, &bad);
   k = &hd_kinds[EK_SPITTER];
   if (!hitbox(pt, k))
      return "the spitter's hitbox must be [width, height]: 4 to 400 pixels each";
   k->health = num(pt, "health", 1, 999, k->health, &bad);
   k->rate = num(pt, "rate", 30, 1200, k->rate, &bad);
   k->shot_speed = FX_FRAC(num(pt, "shot_speed", 50, 2000, 350, &bad), 100);
   k->range = num(pt, "range", 0, 4000, k->range, &bad);
   {
      static const char *const names[4] = { "roller", "hopper", "puffer", "splitter" };
      int32_t i;
      for (i = 0; i < 4; i++)
      {
         const char *err = more_kind(enemies, names[i], &hd_kinds[EK_ROLLER + i], &bad);
         if (err)
            return err;
      }
   }
   if (bad)
      return "the enemies have a value out of range (health 1-999, speed and jump 0-2000, bob 0-200, range 0-4000, rate 30-1200, shot_speed 50-2000, spores 1-9)";
   return NULL;
}

/* The level's boss forms, owned here (hd_boss, hd_kinds[EK_BOSS] and hd_boss_anim show the one in play). */
hd_boss_form hd_boss_forms[2];
static int32_t form_now;

void hd_boss_form_use(int32_t f)
{
   f = f && hd_boss_forms[1].cfg.on ? 1 : 0;
   hd_boss = hd_boss_forms[f].cfg;
   hd_kinds[EK_BOSS] = hd_boss_forms[f].kind;
   memcpy(hd_boss_anim, hd_boss_forms[f].anim, sizeof hd_boss_anim);
   if (f)
   {
      /* the evolved form keeps the first one's minions and shot when it has none of its own */
      if (!hd_boss_anim[BOSS_MINION].frames)
         hd_boss_anim[BOSS_MINION] = hd_boss_forms[0].anim[BOSS_MINION];
      if (!hd_boss_anim[BOSS_SHOT].frames)
         hd_boss_anim[BOSS_SHOT] = hd_boss_forms[0].anim[BOSS_SHOT];
   }
   form_now = f;
}

int32_t hd_boss_form_now(void)
{
   return form_now;
}

void hd_boss_free(void)
{
   int32_t a, f;
   for (f = 0; f < 2; f++)
      for (a = 0; a < BOSS_ANIMS; a++)
         hd_anim_free(&hd_boss_forms[f].anim[a]);
   memset(hd_boss_forms, 0, sizeof hd_boss_forms);
   memset(hd_boss_anim, 0, sizeof hd_boss_anim);
   memset(&hd_boss, 0, sizeof hd_boss);
   memset(&hd_kinds[EK_BOSS], 0, sizeof hd_kinds[EK_BOSS]);
   memset(&hd_kinds[EK_MINION], 0, sizeof hd_kinds[EK_MINION]);
   form_now = 0;
}

/* One form of a boss (the level's, or its "evolve") into hd_boss_forms[f]; NULL or an error. */
static const char *load_form(const hd_zip *zip, const json *boss, int32_t f, const hd_boss_form *before)
{
   static char msg[200];
   const json *att, *it, *name, *sprites;
   hd_boss_form *form = &hd_boss_forms[f];
   hd_boss_config *cfg = &form->cfg;
   hd_enemy_kind *k = &form->kind;
   int bad = 0;
   int32_t a;
   if (boss->type != JSON_OBJECT)
      return f ? "a boss's evolve must be an object" : "a level's boss must be an object";
   cfg->on = 1;
   /* an evolved form starts from the one before it: what it does not say stays */
   if (before)
   {
      *cfg = before->cfg;
      *k = before->kind;
   }
   else
   {
      k->w = 3 * EW;
      k->h = 4 * EH;
   }
   if (!hitbox(boss, k))
      return "the boss's hitbox must be [width, height]: 4 to 400 pixels each";
   k->health = num(boss, "health", 1, 999, before ? k->health : 40, &bad);
   k->speed = FX_FRAC(num(boss, "speed", 10, 2000, before ? (int32_t)((int64_t)k->speed * 100 >> 16) : 250, &bad), 100);
   k->shot_speed = FX_FRAC(num(boss, "shot_speed", 50, 2000, before ? (int32_t)((int64_t)k->shot_speed * 100 >> 16) : 450, &bad), 100);
   cfg->rest = num(boss, "rest", 20, 600, before ? cfg->rest : 80, &bad);
   cfg->spit = num(boss, "spit", 1, 9, before ? cfg->spit : 3, &bad);
   cfg->brood = num(boss, "brood", 1, 12, before ? cfg->brood : 5, &bad);
   if (bad)
      return "the boss has a value out of range (health 1-999, speed 10-2000, shot_speed 50-2000, rest 20-600, spit 1-9, brood 1-12)";
   if ((name = hd_json_get(boss, "name")))
   {
      if (name->type != JSON_STRING)
         return "the boss's name must be a text";
      snprintf(cfg->name, sizeof cfg->name, "%s", name->str);
   }
   att = hd_json_get(boss, "attacks");
   if (att)
   {
      if (att->type != JSON_ARRAY || att->count < 1 || att->count > BOSS_ATTACKS_MAX)
         return "the boss's attacks must be a list of 1 to 8 of \"jump\", \"charge\", \"spit\", \"brood\", \"advance\"";
      cfg->attacks = 0;
      for (it = att->child; it; it = it->next)
      {
         for (a = 0; a < BA_COUNT; a++)
            if (it->type == JSON_STRING && !strcmp(it->str, attack_names[a]))
               break;
         if (a == BA_COUNT)
            return "the boss's attacks must be a list of 1 to 8 of \"jump\", \"charge\", \"spit\", \"brood\", \"advance\"";
         cfg->attack[cfg->attacks++] = a;
      }
   }
   else if (!before)
   {
      static const int32_t all[] = { BA_JUMP, BA_SPIT, BA_BROOD, BA_CHARGE };
      for (a = 0; a < 4; a++)
         cfg->attack[cfg->attacks++] = all[a];
   }
   sprites = hd_json_get(boss, "sprites");
   if (sprites && sprites->type != JSON_OBJECT)
      return "the boss's sprites must be an object";
   for (a = 0; sprites && a < BOSS_ANIMS; a++)
   {
      const json *def = hd_json_get(sprites, boss_anim_names[a]);
      const char *err;
      if (!def)
         continue;
      err = hd_anim_load(zip, def, &form->anim[a], f ? "evolved boss" : "boss", boss_anim_names[a]);
      if (err)
      {
         snprintf(msg, sizeof msg, "%s", err);
         return msg;
      }
   }
   return NULL;
}

const char *hd_boss_load(const hd_zip *zip, const json *boss)
{
   const json *minion, *evolve;
   hd_enemy_kind *m = &hd_kinds[EK_MINION];
   const char *err;
   int bad = 0;
   hd_boss_free();
   if (!boss)
      return NULL;
   err = load_form(zip, boss, 0, NULL);
   evolve = boss->type == JSON_OBJECT ? hd_json_get(boss, "evolve") : NULL;
   if (!err && evolve)
      err = load_form(zip, evolve, 1, &hd_boss_forms[0]);
   if (!err && evolve && hd_json_get(evolve, "evolve"))
      err = "an evolved boss cannot evolve again";
   if (!err)
   {
      minion = hd_json_get(boss, "minion");
      if (minion && minion->type != JSON_OBJECT)
         err = "the boss's minion must be an object";
      m->w = hd_max(4, EW * 2 / 3);
      m->h = hd_max(4, EH * 2 / 3);
      if (!err && !hitbox(minion, m))
         err = "the minion's hitbox must be [width, height]: 4 to 400 pixels each";
      m->health = num(minion, "health", 1, 99, 1, &bad);
      m->speed = FX_FRAC(num(minion, "speed", 10, 2000, 160, &bad), 100);
      if (!err && bad)
         err = "the minion has a value out of range (health 1-99, speed 10-2000)";
   }
   if (err)
   {
      hd_boss_free();
      return err;
   }
   hd_boss_form_use(0);
   return NULL;
}
