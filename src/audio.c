/* Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com> */
/*
 * Sound: effects made in code at start (square waves, sweeps and noise, with
 * integer math so every build makes the same samples), a small music
 * sequencer and a 32 channel mixer at 48 kHz stereo. The channels live in
 * hd_state, so a save state brings the sound back exactly where it was.
 */
#include <string.h>
#include "hd.h"
#include "sprite.h"

#define WAVE_LEN 64

enum { WAVE_SQUARE = SFX_COUNT, WAVE_TRIANGLE };

typedef struct
{
   const int16_t *data;
   int32_t len, loop;
} sample;

static sample samples[HD_SAMPLES];

static int16_t s_jump[HD_RATE * 16 / 100];
static int16_t s_coin[HD_RATE * 30 / 100];
static int16_t s_stomp[HD_RATE * 15 / 100];
static int16_t s_hurt[HD_RATE * 35 / 100];
static int16_t s_join[HD_RATE * 30 / 100];
static int16_t s_check[HD_RATE * 45 / 100];
static int16_t s_clear[HD_RATE * 140 / 100];
static int16_t s_pause[HD_RATE * 8 / 100];
static int16_t w_square[WAVE_LEN], w_triangle[WAVE_LEN];

/* Frequency in millihertz of each MIDI note, from A4 = 440 Hz. */
static int32_t note_mhz[128];

/* The phase step per sample for a frequency, as a fraction of 2^32. */
static uint32_t phase_inc(int32_t mhz)
{
   return (uint32_t)(((int64_t)mhz << 32) / ((int64_t)HD_RATE * 1000));
}

/*
 * A square wave sliding from f0 to f1 (millihertz) over n samples, fading out.
 * With noise set it is a noise burst whose pitch follows the same slide.
 */
static void tone(int16_t *out, int32_t from, int32_t n, int32_t f0, int32_t f1, int32_t amp, int noise)
{
   uint32_t phase = 0, lfsr = 0xACE1u, last = 0;
   int32_t i;
   for (i = 0; i < n; i++)
   {
      int32_t f = f0 + (int32_t)((int64_t)(f1 - f0) * i / n);
      int32_t env = amp * (n - i) / n;
      int32_t v;
      uint32_t before = phase;
      phase += phase_inc(f);
      if (noise)
      {
         if (phase < before) /* one period passed: next noise value */
         {
            lfsr = (lfsr >> 1) ^ (uint32_t)(-(int32_t)(lfsr & 1u) & 0xB400u);
            last = lfsr;
         }
         v = (last & 1) ? env : -env;
      }
      else
         v = phase < 0x80000000u ? env : -env;
      out[from + i] = (int16_t)hd_clamp(out[from + i] + v, -32767, 32767);
   }
}

/* Notes one after another, each len samples long. */
static void arpeggio(int16_t *out, int32_t total, const int32_t *notes, int32_t count, int32_t len, int32_t amp)
{
   int32_t i;
   for (i = 0; i < count && (i + 1) * len <= total; i++)
      tone(out, i * len, i == count - 1 ? total - i * len : len, note_mhz[notes[i]], note_mhz[notes[i]], amp, 0);
}

#define N(a) (int32_t)(sizeof(a) / sizeof((a)[0]))

void hd_audio_build(void)
{
   static const int32_t join[] = { 72, 76, 79, 84 };
   static const int32_t check[] = { 79, 84, 88, 91 };
   static const int32_t clear[] = { 72, 76, 79, 84, 79, 84, 88, 96 };
   int32_t i;

   note_mhz[69] = 440000;
   for (i = 70; i < 128; i++)
      note_mhz[i] = (int32_t)((int64_t)note_mhz[i - 1] * 1059463 / 1000000);
   for (i = 68; i >= 0; i--)
      note_mhz[i] = (int32_t)((int64_t)note_mhz[i + 1] * 1000000 / 1059463);

   memset(s_jump, 0, sizeof s_jump);
   tone(s_jump, 0, N(s_jump), 300000, 760000, 5200, 0);
   memset(s_coin, 0, sizeof s_coin);
   tone(s_coin, 0, HD_RATE / 20, 987767, 987767, 5000, 0);
   tone(s_coin, HD_RATE / 20, N(s_coin) - HD_RATE / 20, 1318510, 1318510, 5000, 0);
   memset(s_stomp, 0, sizeof s_stomp);
   tone(s_stomp, 0, N(s_stomp), 4000000, 800000, 4500, 1);
   tone(s_stomp, 0, N(s_stomp), 180000, 60000, 5000, 0);
   memset(s_hurt, 0, sizeof s_hurt);
   tone(s_hurt, 0, N(s_hurt), 620000, 140000, 5200, 0);
   memset(s_join, 0, sizeof s_join);
   arpeggio(s_join, N(s_join), join, N(join), N(s_join) / N(join), 4200);
   memset(s_check, 0, sizeof s_check);
   arpeggio(s_check, N(s_check), check, N(check), N(s_check) / N(check), 4200);
   memset(s_clear, 0, sizeof s_clear);
   arpeggio(s_clear, N(s_clear), clear, N(clear), HD_RATE / 10, 4600);
   memset(s_pause, 0, sizeof s_pause);
   tone(s_pause, 0, N(s_pause), 1046502, 1046502, 4000, 0);

   for (i = 0; i < WAVE_LEN; i++)
   {
      w_square[i] = (int16_t)(i < WAVE_LEN / 2 ? 4000 : -4000);
      w_triangle[i] = (int16_t)((i < WAVE_LEN / 2 ? i * 4 - WAVE_LEN : 3 * WAVE_LEN - i * 4) * 9000 / WAVE_LEN);
   }

   samples[SFX_JUMP] = (sample){ s_jump, N(s_jump), 0 };
   samples[SFX_COIN] = (sample){ s_coin, N(s_coin), 0 };
   samples[SFX_STOMP] = (sample){ s_stomp, N(s_stomp), 0 };
   samples[SFX_HURT] = (sample){ s_hurt, N(s_hurt), 0 };
   samples[SFX_JOIN] = (sample){ s_join, N(s_join), 0 };
   samples[SFX_CHECK] = (sample){ s_check, N(s_check), 0 };
   samples[SFX_CLEAR] = (sample){ s_clear, N(s_clear), 0 };
   samples[SFX_PAUSE] = (sample){ s_pause, N(s_pause), 0 };
   samples[WAVE_SQUARE] = (sample){ w_square, WAVE_LEN, 1 };
   samples[WAVE_TRIANGLE] = (sample){ w_triangle, WAVE_LEN, 1 };
}

void hd_audio_sample(int32_t sfx, const int16_t *data, int32_t len)
{
   if (sfx < 0 || sfx >= SFX_COUNT || !data || len <= 0)
      return;
   samples[sfx].data = data;
   samples[sfx].len = len;
   samples[sfx].loop = 0;
}

/* Starts a sound effect; screen_x (0..639) places it left or right. */
void hd_play(hd_state *s, int32_t sfx, int32_t screen_x)
{
   hd_channel *c = &s->ch[s->sfx_next];
   s->sfx_next = s->sfx_next + 1 >= MAX_CHANNELS ? MUSIC_CHANNELS : s->sfx_next + 1;
   c->sample = sfx;
   c->pos = 0;
   c->frac = 0;
   c->step = FX_ONE;
   c->vol = 256;
   c->decay = 0;
   c->pan = (hd_clamp(screen_x, 0, HD_W) - HD_W / 2) * 256 / HD_W;
}

/* The tune: 64 rows of melody and bass (MIDI notes, 0 = keep the last note sounding). */
static const uint8_t melody[64] = {
   76, 0, 79, 0, 84, 0, 79, 0, 81, 0, 79, 0, 76, 0, 74, 0,
   72, 0, 74, 0, 76, 0, 79, 0, 76, 0, 74, 0, 72, 0, 0, 0,
   77, 0, 81, 0, 84, 0, 81, 0, 79, 0, 76, 0, 72, 0, 76, 0,
   74, 0, 0, 0, 67, 0, 71, 0, 74, 0, 0, 0, 79, 0, 0, 0,
};
static const uint8_t bass[64] = {
   48, 0, 55, 0, 48, 0, 55, 0, 48, 0, 55, 0, 52, 0, 55, 0,
   45, 0, 52, 0, 45, 0, 52, 0, 45, 0, 52, 0, 48, 0, 52, 0,
   41, 0, 48, 0, 41, 0, 48, 0, 41, 0, 48, 0, 45, 0, 48, 0,
   43, 0, 50, 0, 43, 0, 50, 0, 43, 0, 47, 0, 50, 0, 47, 0,
};

static void note(hd_channel *c, int32_t wave, int32_t midi, int32_t vol, int32_t decay)
{
   c->sample = wave;
   c->step = (int32_t)((int64_t)note_mhz[midi] * WAVE_LEN * FX_ONE / ((int64_t)HD_RATE * 1000));
   c->vol = vol;
   c->decay = decay;
   c->pan = 0;
}

/* One frame of the sequencer: a row every 7 frames (about 128 beats a minute). */
void hd_music_step(hd_state *s)
{
   if (hd_pkg_music)
      return; /* a package's own music plays instead (hd_mix) */
   if (s->music_tick++ % 7)
      return;
   s->music_row = (s->music_row + 1) % 64;
   if (melody[s->music_row])
      note(&s->ch[0], WAVE_SQUARE, melody[s->music_row], 120, 4);
   if (bass[s->music_row])
      note(&s->ch[1], WAVE_TRIANGLE, bass[s->music_row], 200, 3);
}

void hd_mix(hd_state *s, int16_t *out, int music_on)
{
   int32_t acc[HD_SAMPLES_PER_FRAME * 2];
   int32_t i, n;
   memset(acc, 0, sizeof acc);
   for (i = 0; i < MAX_CHANNELS; i++)
   {
      hd_channel *c = &s->ch[i];
      const sample *smp;
      int32_t gl, gr, len;
      if (c->sample < 0)
         continue;
      smp = &samples[c->sample];
      len = smp->len;
      gl = (256 - hd_max(c->pan, 0)) * c->vol >> 8;
      gr = (256 + hd_min(c->pan, 0)) * c->vol >> 8;
      for (n = 0; n < HD_SAMPLES_PER_FRAME; n++)
      {
         int32_t v;
         if (c->pos >= len)
         {
            if (!smp->loop)
            {
               c->sample = -1;
               break;
            }
            c->pos %= len;
         }
         v = smp->data[c->pos];
         if (i >= MUSIC_CHANNELS || music_on)
         {
            acc[2 * n] += v * gl >> 8;
            acc[2 * n + 1] += v * gr >> 8;
         }
         c->frac += c->step;
         c->pos += c->frac >> 16;
         c->frac &= 0xffff;
      }
      if (c->decay)
      {
         c->vol -= c->decay;
         if (c->vol <= 0)
         {
            c->vol = 0;
            c->sample = -1;
         }
      }
   }
   /* a package's music, stereo, over and over */
   if (hd_pkg_music)
   {
      int32_t pos = hd_clamp(s->music_pos, 0, hd_pkg_music_frames - 1);
      for (n = 0; n < HD_SAMPLES_PER_FRAME; n++)
      {
         if (music_on)
         {
            acc[2 * n] += hd_pkg_music[2 * pos] * hd_pkg_music_vol >> 8;
            acc[2 * n + 1] += hd_pkg_music[2 * pos + 1] * hd_pkg_music_vol >> 8;
         }
         if (++pos >= hd_pkg_music_frames)
            pos = hd_clamp(hd_pkg_music_loop, 0, hd_pkg_music_frames - 1);
      }
      s->music_pos = pos;
   }
   /* the mix's effects: a one pole low pass (muffled, underwater) and an echo (caves) */
   if (s->lowpass > 0 && s->lowpass < 256)
      for (n = 0; n < HD_SAMPLES_PER_FRAME; n++)
      {
         s->lp_l += (acc[2 * n] - s->lp_l) * s->lowpass / 256;
         s->lp_r += (acc[2 * n + 1] - s->lp_r) * s->lowpass / 256;
         acc[2 * n] = s->lp_l;
         acc[2 * n + 1] = s->lp_r;
      }
   if (s->echo > 0)
   {
      int32_t delay = hd_min(s->echo, ECHO_MAX);
      for (n = 0; n < HD_SAMPLES_PER_FRAME; n++)
      {
         int32_t *e = &s->echo_buf[2 * s->echo_pos], k;
         for (k = 0; k < 2; k++)
         {
            int32_t dry = acc[2 * n + k], wet = e[k];
            acc[2 * n + k] = dry + wet * s->echo_mix / 256;
            e[k] = hd_clamp(dry + wet * s->echo_feedback / 256, -131072, 131071);
         }
         s->echo_pos = (s->echo_pos + 1) % delay;
      }
   }
   for (n = 0; n < HD_SAMPLES_PER_FRAME * 2; n++)
      out[n] = (int16_t)hd_clamp(acc[n], -32767, 32767);
}

void hd_audio_effects(hd_state *s, int32_t lowpass, int32_t echo_ms, int32_t feedback, int32_t mix)
{
   int32_t delay = hd_clamp(echo_ms, 0, 300) * HD_RATE / 1000;
   s->lowpass = hd_clamp(lowpass, 0, 256);
   if (delay != s->echo)
   {
      memset(s->echo_buf, 0, sizeof s->echo_buf);
      s->echo_pos = 0;
   }
   s->echo = delay;
   s->echo_feedback = hd_clamp(feedback, 0, 230);
   s->echo_mix = hd_clamp(mix, 0, 256);
}
