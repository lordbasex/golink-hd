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
 * hitbox in the game's pixels (4 to 400); health the hits it takes (1 to
 * 999); speed and shot_speed in hundredths of a pixel a frame; bob in
 * pixels; range in pixels (how near a player must be); rate in frames.
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
   if (bad)
      return "the enemies have a value out of range (health 1-999, speed 0-2000, bob 0-200, range 0-4000, rate 30-1200, shot_speed 50-2000)";
   return NULL;
}

void hd_boss_free(void)
{
   int32_t a;
   for (a = 0; a < BOSS_ANIMS; a++)
      hd_anim_free(&hd_boss_anim[a]);
   memset(&hd_boss, 0, sizeof hd_boss);
   memset(&hd_kinds[EK_BOSS], 0, sizeof hd_kinds[EK_BOSS]);
   memset(&hd_kinds[EK_MINION], 0, sizeof hd_kinds[EK_MINION]);
}

const char *hd_boss_load(const hd_zip *zip, const json *boss)
{
   static char msg[200];
   const json *att, *it, *name, *minion, *sprites;
   hd_enemy_kind *k = &hd_kinds[EK_BOSS], *m = &hd_kinds[EK_MINION];
   int bad = 0;
   int32_t a;
   hd_boss_free();
   if (!boss)
      return NULL;
   if (boss->type != JSON_OBJECT)
      return "a level's boss must be an object";
   hd_boss.on = 1;
   k->w = 3 * EW;
   k->h = 4 * EH;
   if (!hitbox(boss, k))
      return "the boss's hitbox must be [width, height]: 4 to 400 pixels each";
   k->health = num(boss, "health", 1, 999, 40, &bad);
   k->speed = FX_FRAC(num(boss, "speed", 10, 2000, 250, &bad), 100);
   k->shot_speed = FX_FRAC(num(boss, "shot_speed", 50, 2000, 450, &bad), 100);
   hd_boss.rest = num(boss, "rest", 20, 600, 80, &bad);
   hd_boss.spit = num(boss, "spit", 1, 9, 3, &bad);
   hd_boss.brood = num(boss, "brood", 1, 12, 5, &bad);
   if (bad)
      return "the boss has a value out of range (health 1-999, speed 10-2000, shot_speed 50-2000, rest 20-600, spit 1-9, brood 1-12)";
   if ((name = hd_json_get(boss, "name")))
   {
      if (name->type != JSON_STRING)
         return "the boss's name must be a text";
      snprintf(hd_boss.name, sizeof hd_boss.name, "%s", name->str);
   }
   att = hd_json_get(boss, "attacks");
   if (att)
   {
      if (att->type != JSON_ARRAY || att->count < 1 || att->count > BOSS_ATTACKS_MAX)
         return "the boss's attacks must be a list of 1 to 8 of \"jump\", \"charge\", \"spit\", \"brood\", \"advance\"";
      for (it = att->child; it; it = it->next)
      {
         for (a = 0; a < BA_COUNT; a++)
            if (it->type == JSON_STRING && !strcmp(it->str, attack_names[a]))
               break;
         if (a == BA_COUNT)
            return "the boss's attacks must be a list of 1 to 8 of \"jump\", \"charge\", \"spit\", \"brood\", \"advance\"";
         hd_boss.attack[hd_boss.attacks++] = a;
      }
   }
   else
   {
      static const int32_t all[] = { BA_JUMP, BA_SPIT, BA_BROOD, BA_CHARGE };
      for (a = 0; a < 4; a++)
         hd_boss.attack[hd_boss.attacks++] = all[a];
   }
   minion = hd_json_get(boss, "minion");
   if (minion && minion->type != JSON_OBJECT)
      return "the boss's minion must be an object";
   m->w = hd_max(4, EW * 2 / 3);
   m->h = hd_max(4, EH * 2 / 3);
   if (!hitbox(minion, m))
      return "the minion's hitbox must be [width, height]: 4 to 400 pixels each";
   m->health = num(minion, "health", 1, 99, 1, &bad);
   m->speed = FX_FRAC(num(minion, "speed", 10, 2000, 160, &bad), 100);
   if (bad)
      return "the minion has a value out of range (health 1-99, speed 10-2000)";
   sprites = hd_json_get(boss, "sprites");
   if (sprites && sprites->type != JSON_OBJECT)
      return "the boss's sprites must be an object";
   for (a = 0; sprites && a < BOSS_ANIMS; a++)
   {
      const json *def = hd_json_get(sprites, boss_anim_names[a]);
      const char *err;
      if (!def)
         continue;
      err = hd_anim_load(zip, def, &hd_boss_anim[a], "boss", boss_anim_names[a]);
      if (err)
      {
         snprintf(msg, sizeof msg, "%s", err);
         hd_boss_free();
         return msg;
      }
   }
   return NULL;
}
