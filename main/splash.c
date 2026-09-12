#include "splash.h"
#include <string.h>
#include <stdlib.h>
#include "esp_timer.h"
#include "display.h"
#include "ui.h"
#include "music.h"
#include "festive.h"

#define SPLASH_FRAMES (60 * 20)
#define GROUND 204

/* visible window in portrait is columns 8..247 of the 256-wide frame */
#define L 8
#define R 248
#define CX 128

static uint32_t rnd_state = 12345;
static uint32_t rnd(void) { rnd_state = rnd_state * 1664525u + 1013904223u; return rnd_state >> 8; }
static int rndn(int n) { return rnd() % n; }

#define px festive_px

/* ---- fireworks ---- */
typedef struct { int16_t x, y, vx, vy; uint8_t life, colour; } spark_t;   /* positions in 1/16 px */
#define SPARKS 160
static spark_t sparks[SPARKS];
typedef struct { int16_t x, y, vy; uint8_t colour; bool live; } rocket_t;
static rocket_t rockets[3];

static void burst(int x, int y, uint8_t colour)
{
    int n = 0;
    for (int i = 0; i < SPARKS && n < 64; i++) {
        if (sparks[i].life) continue;
        int a = rndn(64), sp = 12 + rndn(30);
        /* crude sin/cos from a 16-entry table */
        static const int8_t tbl[16] = { 0, 12, 23, 30, 32, 30, 23, 12, 0, -12, -23, -30, -32, -30, -23, -12 };
        sparks[i] = (spark_t){ x * 16, y * 16, tbl[(a + 4) & 15] * sp / 32, tbl[a & 15] * sp / 32, 55 + rndn(35), colour };
        n++;
    }
}

static void fireworks(int frame)
{
    if (frame > 20 && rndn(30) == 0)
        for (int i = 0; i < 3; i++) if (!rockets[i].live) {
            rockets[i] = (rocket_t){ (L + 30 + rndn(R - L - 60)) * 16, GROUND * 16, -(58 + rndn(16)), fiesta_colours[rndn(6)], true };
            break;
        }
    for (int i = 0; i < 3; i++) {
        rocket_t *r = &rockets[i];
        if (!r->live) continue;
        r->y += r->vy; r->vy += 1;
        px(r->x / 16, r->y / 16, UI_WHITE); px(r->x / 16, r->y / 16 + 1, UI_GREY);
        if (r->vy >= -4) { r->live = false; burst(r->x / 16, r->y / 16, r->colour); }
    }
    for (int i = 0; i < SPARKS; i++) {
        spark_t *s = &sparks[i];
        if (!s->life) continue;
        s->x += s->vx; s->y += s->vy; s->vy += 1; s->life--;
        s->vx -= s->vx / 24; s->vy -= s->vy / 24;   /* air drag: bursts stay rounder */
        uint8_t c = s->colour;
        if (s->life < 10) c = UI_GREY;
        else if (s->life < 22) c = CUBE((s->colour / 30) / 2, ((s->colour / 5) % 6) / 2, (s->colour % 5) / 2);
        else if (s->life > 70 || (s->life & 4)) c = s->colour;
        int sx = s->x / 16, sy = s->y / 16;
        px(sx, sy, c); if (s->life > 22) { px(sx + 1, sy, c); px(sx, sy + 1, c); px(sx + 1, sy + 1, c); }
        if (s->y / 16 >= GROUND) s->life = 0;
    }
}

/* ---- scene ---- */
static void skyline(void)
{
    static const uint8_t bld[][3] = { {8,30,26},{38,18,40},{56,24,34},{78,14,48},{170,20,44},{190,26,30},{216,16,38},{232,16,24} };
    uint8_t dark = CUBE(0,0,1), win = CUBE(5,5,2);
    for (size_t i = 0; i < sizeof bld / sizeof *bld; i++) {
        ui_fill(bld[i][0], GROUND - bld[i][2], bld[i][1], bld[i][2], dark);
        for (int y = GROUND - bld[i][2] + 3; y < GROUND - 2; y += 5)
            for (int x = bld[i][0] + 2; x < bld[i][0] + bld[i][1] - 2; x += 5)
                if (((x * 7 + y * 13) / 5) % 3) px(x, y, win);
    }
    /* Tower of the Americas: the tan concrete shaft, flared at the base, a tall tophouse
     * (restaurant + observation levels with lit windows under a wide crown), and the spire */
    uint8_t tan = CUBE(4,3,2), tan_dk = CUBE(3,2,1), tan_lt = CUBE(5,4,3), glass = CUBE(0,0,2);
    ui_fill(CX - 7, 92, 14, GROUND - 92, tan);
    ui_fill(CX - 7, 92, 3, GROUND - 92, tan_lt);          /* lit side */
    ui_fill(CX + 4, 92, 3, GROUND - 92, tan_dk);          /* shaded side */
    for (int y = GROUND - 24; y < GROUND; y++) {          /* base flare */
        int w = (y - (GROUND - 24)) / 3;
        ui_fill(CX - 7 - w, y, 14 + 2 * w, 1, tan);
        px(CX - 7 - w, y, tan_lt); px(CX + 6 + w, y, tan_dk);
    }
    ui_fill(CX - 26, 56, 52, 5, tan_lt);                  /* crown */
    ui_fill(CX - 24, 61, 48, 3, tan);
    ui_fill(CX - 22, 64, 44, 8, glass);                   /* restaurant level */
    for (int x = CX - 20; x < CX + 20; x += 4) ui_fill(x, 66, 2, 4, win);
    ui_fill(CX - 22, 72, 44, 3, tan);
    ui_fill(CX - 20, 75, 40, 7, glass);                   /* observation level */
    for (int x = CX - 18; x < CX + 18; x += 4) ui_fill(x, 77, 2, 3, win);
    ui_fill(CX - 20, 82, 40, 3, tan);
    for (int y = 85; y < 92; y++) ui_fill(CX - 17 + (y - 85) * 3 / 2, y, 34 - (y - 85) * 3, 1, tan_dk);   /* underside taper */
    ui_fill(CX - 1, 30, 2, 26, tan_lt);                   /* spire */
    ui_fill(CX - 2, 44, 4, 2, tan);
    ui_fill(L, GROUND, R - L, FB_LINES - GROUND, CUBE(0,0,1));
}

/* aircraft beacon on the spire tip, drawn last so nothing covers it */
static void beacon(int frame) { if ((frame / 30) & 1) { px(CX - 1, 29, CUBE(5,0,0)); px(CX, 29, CUBE(5,0,0)); } }

/* a proper night sky: ~100 stars in three brightness tiers, each twinkling on its own
 * phase, the brightest flaring into 4-point sparkles, and a shooting star now and then */
static void stars(int frame)
{
    static const uint8_t tier[3] = { CUBE(1,1,2), CUBE(3,3,4), UI_WHITE };
    for (int i = 0; i < 100; i++) {
        uint32_t h = (uint32_t)i * 2654435761u;
        int x = L + (h >> 8) % (R - L), y = (h >> 20) % 150;
        int t = (h >> 4) % 10;                      /* 0-5 dim, 6-8 mid, 9 bright */
        int phase = (frame + (h & 63)) >> 3;
        int tw = ((phase * 5 + i) % 7);             /* 0..6 twinkle level */
        if (t < 6) { if (tw > 1) px(x, y, tier[0]); }
        else if (t < 9) { px(x, y, tw > 4 ? tier[2] : tier[1]); }
        else {
            px(x, y, UI_WHITE);
            if (tw == 6) { px(x - 1, y, tier[1]); px(x + 1, y, tier[1]); px(x, y - 1, tier[1]); px(x, y + 1, tier[1]); }
            if (tw == 5) { px(x - 1, y, tier[0]); px(x + 1, y, tier[0]); }
        }
    }
    /* shooting star: one every ~6 s, a 40-frame streak with a fading tail */
    int cycle = frame % 360;
    if (cycle < 40) {
        uint32_t h = (uint32_t)(frame / 360 + 7) * 2246822519u;
        int x0 = L + 20 + (h >> 8) % 140, y0 = 10 + (h >> 20) % 50;
        int x = x0 + cycle * 3, y = y0 + cycle;
        for (int k = 0; k < 10; k++) {
            uint8_t c = k < 2 ? UI_WHITE : k < 5 ? tier[1] : tier[0];
            px(x - k * 3, y - k, c);
        }
    }
}

/* letters drop in one by one with a bounce */
static void title(int frame)
{
    static const int8_t bounce[24] = { 0, -14, -22, -26, -24, -18, -10, -2, 3, 6, 7, 6, 3, 0, -3, -4, -3, 0, 2, 2, 1, 0, 0, 0 };
    const char *word = "FIESTA";
    int x0 = CX - 3 * 24;
    for (int i = 0; i < 6; i++) {
        int t = frame - 60 - i * 9;
        if (t < 0) continue;
        int y = 108 + (t < 24 ? bounce[t] - (t == 0 ? 60 : 0) : 0);
        char ch[2] = { word[i], 0 };
        ui_text_scaled(x0 + i * 24 + 2, y + 2, ch, CUBE(2,0,1), 3);   /* shadow */
        ui_text_scaled(x0 + i * 24, y, ch, i & 1 ? CUBE(5,5,0) : CUBE(5,1,3), 3);
    }
    if (frame > 150) {
        int t = frame - 150;
        uint8_t c = t < 15 ? UI_GREY : UI_WHITE;
        ui_frame(CX - 84, 100, 168, 52, c);
        ui_text_center(138, "ENTERTAINMENT SYSTEM", c);
    }
    if (frame > 210) {
        int t = frame - 210;
        uint8_t c = t < 20 ? CUBE(2,2,2) : t < 40 ? CUBE(4,4,4) : UI_WHITE;
        ui_text_center(166, "SAN ANTONIO", c);
        ui_text_center(182, "FIESTA 2027", t < 40 ? c : CUBE(5,5,0));
    }
}

extern bool splash_skip_requested(void);   /* main.c: any pad or medal button */

/* ---- the cold open: FIESTA races past, then three hard cuts to box art, one word each ---- */
static void speed_lines(int frame, int dir)
{
    for (int i = 0; i < 28; i++) {
        uint32_t h = (uint32_t)(i + 3) * 2654435761u;
        int y = (h >> 8) % FB_LINES, len = 30 + (h >> 20) % 60;
        int x = ((h >> 4) % 400 + frame * (10 + i % 5) * dir) % 400 - 80;
        uint8_t c = i % 3 == 0 ? UI_WHITE : i % 3 == 1 ? CUBE(3,3,4) : CUBE(1,1,2);
        ui_fill(x, y, len, 1, c);
    }
}

/* one cut: frames 0..len-1. Cover slides in from `from` (0 right, 1 left, 2 bottom) with a zoom punch,
 * the word slams into a letterbox band. Returns false when nothing was drawn (no cover). */
static void cut(int t, int len, const char *game, const char *word, int from, uint8_t word_colour)
{
    int w, h;
    const uint8_t *art = splash_cover(game, &w, &h);
    ui_clear(UI_BLACK);
    if (t < 2) { ui_fill(L, 0, R - L, FB_LINES, UI_WHITE); return; }   /* the flash */
    speed_lines(t, from == 1 ? 1 : -1);
    /* slide: 6 frames from off-screen, then a zoom from 3/2 to 7/4 over the rest */
    int slide = t < 8 ? (8 - t) * 40 : 0;
    int num = 6 + (t > 8 ? (t - 8) / 6 : 0), den = 4;   /* 1.5x -> up to ~2x */
    if (num > 8) num = 8;
    int cw = w * num / den, ch = h * num / den;
    int x = CX - cw / 2 + (from == 0 ? slide : from == 1 ? -slide : 0);
    int y = 100 - ch / 2 + (from == 2 ? slide : 0);
    if (art) ui_bitmap(x, y, art, w, h, num, den);
    else ui_fill(x, y, cw, ch, UI_GREY);
    /* letterbox band with the word, punching in from the side */
    ui_fill(L, 168, R - L, 44, UI_BLACK);
    ui_fill(L, 168, R - L, 2, word_colour); ui_fill(L, 210, R - L, 2, word_colour);
    int ww = 8 * 3 * (int)strlen(word);
    int wx = CX - ww / 2, punch = t < 6 ? (6 - t) * 30 * (from == 1 ? -1 : 1) : 0;
    ui_text_scaled(wx + punch + 2, 180 + 2, word, CUBE(1,0,1), 3);
    ui_text_scaled(wx + punch, 180, word, word_colour, 3);
}

static bool cold_open(void)
{
    /* FIESTA races right-to-left, 4x letters, over speed lines */
    for (int t = 0; t < 80; t++) {
        ui_clear(UI_BLACK);
        speed_lines(t, -1);
        int x = R + 40 - t * 7;   /* 192 px wide word: fully across in ~70 frames */
        ui_text_scaled(x + 3, 106 + 3, "FIESTA", CUBE(2,0,1), 4);
        ui_text_scaled(x, 106, "FIESTA", t & 4 ? CUBE(5,5,0) : CUBE(5,1,3), 4);
        music_tick_hook((t & 1) ? NULL : ui_line_push);
        if (splash_skip_requested()) return false;
    }
    static const struct { const char *game, *word; int from; uint8_t colour; } cuts[3] = {
        { "Super Mario Bros.", "ENTER", 0, CUBE(5,0,0) },
        { "Legend of Zelda, The", "TAINMENT", 1, CUBE(1,5,1) },
        { "Metroid", "SYSTEM", 2, CUBE(5,3,0) },
    };
    for (int c = 0; c < 3; c++)
        for (int t = 0; t < 42; t++) {
            cut(t, 42, cuts[c].game, cuts[c].word, cuts[c].from, cuts[c].colour);
            music_tick_hook((t & 1) ? NULL : ui_line_push);
            if (splash_skip_requested()) return false;
        }
    /* flash into the scene */
    ui_fill(L, 0, R - L, FB_LINES, UI_WHITE);
    music_tick_hook(ui_line_push);
    return true;
}

void splash_run(void)
{
    ui_palette_cube();
    memset(sparks, 0, sizeof sparks);
    memset(rockets, 0, sizeof rockets);
    rnd_state = (uint32_t)esp_timer_get_time();
    if (!cold_open()) { display_wait_done(); return; }
    for (int frame = 30; frame < SPLASH_FRAMES; frame++) {
        ui_clear(UI_BLACK);
        stars(frame);
        skyline();
        fireworks(frame);
        festive_papel_picado(frame);
        title(frame);
        beacon(frame);
        if (frame > SPLASH_FRAMES - 90 && (frame & 16)) ui_text_center(224, "press any button", UI_GREY);
        /* simulate at 60 Hz, present at 30: a full-frame push every music frame overran the audio budget */
        music_tick_hook((frame & 1) ? NULL : ui_line_push);
        if (splash_skip_requested()) break;
    }
    display_wait_done();
}
