/*
 * sfx.c - sound effects made rather than recorded. From PELLETINO (0BSD).
 *
 * An effect is a handful of ringing partials, each a two-pole resonator with its own decay,
 * under a burst of noise: a drawn blade (many high partials, close pairs beating, long decays)
 * or a ratchet click (one low partial, gone in milliseconds). It may instead be a few square-wave
 * notes (a coin, a warning). And one held triangle tone goes wherever it is told: the hold to
 * pick a game, rising as it fills. Everything runs inside sfx_mix() on the caller's audio task.
 */
#include "sfx.h"
#include <math.h>
#include <string.h>

#define MAX_PARTIALS 6
#define MAX_NOTES 4
typedef struct { float hz; int level; float seconds; } partial_def_t;
typedef struct { uint16_t hz, ms; } note_def_t;
typedef struct {
    partial_def_t partial[MAX_PARTIALS];
    int noise_level; float noise_seconds;
    note_def_t note[MAX_NOTES]; int note_level; float note_seconds;
} sfx_def_t;

static const sfx_def_t defs[] = {
    [SFX_SHING] = { { { 2637, 3000, 0.42f }, { 2671, 3000, 0.42f }, { 4186, 2200, 0.34f }, { 5274, 1700, 0.27f },
                      { 6645, 1300, 0.20f }, { 8372, 900, 0.13f } }, 4200, 0.045f },
    [SFX_CLICK] = { { { 1850, 5200, 0.006f }, { 3300, 2200, 0.003f } }, 3800, 0.0015f },
    [SFX_COIN]  = { .note = { { 988, 75 }, { 1319, 480 } }, .note_level = 5200, .note_seconds = 0.16f },
    [SFX_LOW]   = { .note = { { 659, 150 }, { 0, 60 }, { 440, 320 } }, .note_level = 5200, .note_seconds = 0.30f },
};

typedef struct { int32_t y1, y2, k; uint32_t env, decay; } ring_t;
static ring_t ring[MAX_PARTIALS];
static int rings;
static uint32_t noise_env, noise_decay;
static int noise_level, noise_last;
static uint32_t seed = 0x2545F491u;
static volatile int wanted = -1;
static const note_def_t *notes;
static int note_at, note_left, note_level;
static uint32_t note_phase, note_step, note_env, note_decay;
#define TONE_LEVEL 2600
static volatile int tone_hz;
static int tone_now;
static uint32_t tone_phase;

void sfx_play(sfx_t which) { if ((unsigned)which < sizeof defs / sizeof defs[0]) wanted = (int)which; }
void sfx_tone(int hz) { tone_hz = hz > 0 ? hz : 0; }

static uint32_t decay_for(float seconds, int rate) { return (uint32_t)(65536.0f * expf(-1.0f / (seconds * (float)rate))); }

static void start(const sfx_def_t *d, int rate)
{
    rings = 0;
    for (int i = 0; i < MAX_PARTIALS; i++) {
        const partial_def_t *p = &d->partial[i];
        if (p->level <= 0 || p->hz * 2.2f > (float)rate) continue;
        float w = 6.2831853f * p->hz / (float)rate;
        ring_t *r = &ring[rings++];
        r->k = (int32_t)(2.0f * cosf(w) * 16384.0f); r->y1 = 0; r->y2 = (int32_t)(-(float)p->level * sinf(w));
        r->env = 1u << 24; r->decay = decay_for(p->seconds, rate);
    }
    noise_level = d->noise_level; noise_env = d->noise_level ? 1u << 24 : 0; noise_decay = decay_for(d->noise_seconds, rate);
    notes = d->note_level ? d->note : NULL; note_at = -1; note_left = 0; note_level = d->note_level;
    note_decay = d->note_level ? decay_for(d->note_seconds, rate) : 0;
}

static void next_note(int rate)
{
    if (++note_at >= MAX_NOTES || !notes[note_at].ms) { notes = NULL; return; }
    note_left = (int)((long)notes[note_at].ms * rate / 1000);
    note_step = (uint32_t)(((uint64_t)notes[note_at].hz << 32) / (uint32_t)rate);
    note_env = 1u << 24; note_phase = 0;
}

bool sfx_active(void) { return wanted >= 0 || rings > 0 || noise_env > 0 || notes || tone_hz || tone_now; }

void sfx_mix(int16_t *buf, int samples, int rate)
{
    if (wanted >= 0) { start(&defs[wanted], rate); wanted = -1; }
    if (!rings && !noise_env && !notes && !tone_hz && !tone_now) return;
    uint32_t tone_step = (uint32_t)(((uint64_t)tone_hz << 32) / (uint32_t)rate);
    for (int i = 0; i < samples; i++) {
        int32_t out = 0;
        if (notes) {
            if (note_left <= 0) next_note(rate);
            if (notes) {
                note_left--;
                if (note_step) {
                    int32_t level = (int32_t)(((int64_t)note_level * (note_env >> 8)) >> 16);
                    out += (note_phase & 0x80000000u) ? -level : level;
                    note_phase += note_step;
                    note_env = (uint32_t)(((uint64_t)note_env * note_decay) >> 16);
                }
            }
        }
        if (tone_hz || tone_now) {
            if (tone_hz && tone_now < TONE_LEVEL) tone_now += 8;
            if (!tone_hz) tone_now = tone_now > 8 ? tone_now - 8 : 0;
            uint32_t ph = tone_phase >> 16;
            int32_t tri = (int32_t)(ph < 32768 ? ph : 65535 - ph) - 16384;
            out += tri * tone_now / 16384;
            tone_phase += tone_step;
        }
        for (int r = 0; r < rings; r++) {
            ring_t *g = &ring[r];
            int32_t y = (int32_t)(((int64_t)g->k * g->y1) >> 14) - g->y2;
            g->y2 = g->y1; g->y1 = y;
            out += (int32_t)(((int64_t)y * (g->env >> 8)) >> 16);
            g->env = (uint32_t)(((uint64_t)g->env * g->decay) >> 16);
        }
        if (noise_env) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            int n = (int)(seed & 0xFFFF) - 32768;
            int hiss = (n - noise_last) / 2; noise_last = n;
            out += (int32_t)(((int64_t)hiss * noise_level >> 15) * (int32_t)(noise_env >> 8) >> 16);
            noise_env = (uint32_t)(((uint64_t)noise_env * noise_decay) >> 16);
            if (noise_env < 1u << 10) noise_env = 0;
        }
        int32_t v = buf[i] + out;
        buf[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    int kept = 0;
    for (int r = 0; r < rings; r++) if (ring[r].env >= 1u << 12) ring[kept++] = ring[r];
    rings = kept;
}
