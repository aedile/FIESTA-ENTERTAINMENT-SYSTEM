/*
 * attract.c - the attract sequence, after PELLETINO's.
 *
 * Title: the wordmark crosses a starfield and leaves; the screen flashes white with the
 * sound of a blade being drawn; the title screen is there as the flash clears, a perspective
 * grid, a console, the wordmark, with CRT line dimming over all of it. Then how to play, a row
 * at a time. Then the credits roll. Frames tick at 60 under the music and present at 30.
 */
#include "attract.h"
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "input.h"
#include "music.h"
#include "sfx.h"
#include "roms.h"

#define CX (ui_w / 2)
#define WORD "F.E.S."
#define WORD_SCALE 4
#define WORD_W (6 * 8 * WORD_SCALE)
#define FLY_STEP 3                 /* px per 60 Hz tick: the speed PELLETINO's 6 px at 30 fps had */
#define P1_END ((ui_w + 40 + WORD_W) / FLY_STEP + 12)
#define FLASH_FULL 6
#define FLASH_FADE 28
#define TITLE_END (P1_END + 60 * 12)
#define HORIZON 150

bool attract_any_button(void) { return any_button(); }

static void present(int t) { music_tick_hook((t & 1) ? NULL : ui_line_push); }

static void speed_lines(int t)
{
    for (int i = 0; i < 12; i++) {
        uint32_t h = (uint32_t)(i + 1) * 2246822519u;
        int y = (h >> 9) % ui_h, len = 30 + (h >> 3) % 70;
        int x = ((int)((h >> 17) % ui_w) - t * 13) % ui_w;
        if (x < 0) x += ui_w;
        ui_fill(x, y, len, 1, CUBE(1,1,2));
    }
}

bool attract_title(void)
{
    char games[24];
    snprintf(games, sizeof games, "%d GAMES", roms_count());
    any_button();
    for (int t = 0; t < TITLE_END; t++) {
        if (any_button()) { ui_white = 0; return true; }
        ui_clear(UI_BLACK);
        ui_stars(t);
        if (t < P1_END) {
            speed_lines(t);
            ui_text_scaled(ui_w + 40 - t * FLY_STEP, 110, WORD, UI_WHITE, WORD_SCALE);
        } else {
            int k = t - P1_END;
            if (k == 0) sfx_play(SFX_SHING);
            ui_white = k < FLASH_FULL ? 255 : k < FLASH_FULL + FLASH_FADE ? (uint8_t)(255 * (FLASH_FULL + FLASH_FADE - k) / FLASH_FADE) : 0;
            ui_grid(t, HORIZON);
            ui_console(t, CX, HORIZON + 48);
            ui_text_scaled(CX - WORD_W / 2, 30, WORD, UI_WHITE, WORD_SCALE);
            ui_text_center(68, "FIESTA ENTERTAINMENT SYSTEM", CUBE(5,4,0));
            ui_text_center(84, "SAN ANTONIO 2027", CUBE(5,1,3));
            ui_text_center(104, games, CUBE(0,4,4));
            if (k > FLASH_FULL + FLASH_FADE && ((k / 24) & 1)) ui_text_center(250, "PRESS A BUTTON", UI_WHITE);
        }
        present(t);
    }
    return false;
}

/* ---- how to play: three short tables that write themselves out a row at a time ---- */
static const struct { const char *what, *does; } rows[] = {
    { "ON THE WHEEL",    NULL },
    { "TOP BUTTON",      "GAME ABOVE" },
    { "MIDDLE BUTTON",   "GAME BELOW" },
    { "A",               "PLAY" },
    { "HOLD MIDDLE",     "PLAY, NO PAD" },
    { "",                NULL },
    { "IN A GAME",       NULL },
    { "Y OR SHOULDER",   "MENU" },
    { "MIDDLE BUTTON",   "LOCK DEMO" },
    { "HOLD MIDDLE 5S",  "TO THE WHEEL" },
    { "",                NULL },
    { "ANYWHERE",        NULL },
    { "BOTH BUTTONS",    "VOLUME" },
    { "HOLD TOP",        "POWER OFF" },
    { "PAD AGAINST IT",  "TO PAIR" },
};
#define ROWS ((int)(sizeof rows / sizeof rows[0]))
#define ROW_EVERY 28
#define HOLD_AFTER 300
#define GUTTER 128
#define TOP 62
#define ROW_H 12

bool attract_howto(void)
{
    any_button();
    int total = ROWS * ROW_EVERY + HOLD_AFTER;
    for (int t = 0; t < total; t++) {
        if (any_button()) return true;
        ui_clear(UI_BLACK);
        ui_stars(t);
        ui_text_centred_scaled(CX, 30, "HOW TO PLAY", UI_WHITE, 2);
        int shown = t / ROW_EVERY + 1;
        if (shown > ROWS) shown = ROWS;
        for (int i = 0; i < shown; i++) {
            int y = TOP + i * ROW_H;
            if (!rows[i].does) { ui_text_center(y, rows[i].what, CUBE(0,4,4)); continue; }
            bool fresh = i == shown - 1 && t < ROWS * ROW_EVERY;
            ui_text(GUTTER - 8 * (int)strlen(rows[i].what), y, rows[i].what, fresh ? UI_WHITE : CUBE(5,4,0));
            ui_text(GUTTER + 8, y, rows[i].does, fresh ? UI_WHITE : CUBE(4,4,4));
        }
        if (t % ROW_EVERY == 0 && t / ROW_EVERY < ROWS && rows[t / ROW_EVERY].does) sfx_play(SFX_CLICK);
        if ((t / 24) & 1) ui_text_center(260, "PRESS A BUTTON", UI_WHITE);
        present(t);
    }
    return false;
}

/* ---- credits: '#' heading, '~' dim, otherwise plain; the games come from the partition ---- */
static const char *const before_games[] = {
    "#F.E.S.", "Fiesta Entertainment", "System", "", "",
    "#MADE BY", "Jesse Castro", "~for Fiesta San Antonio", "~2027", "", "",
    "#THE GAMES", "~on this medal", "",
};
static const char *const after_games[] = {
    "", "~Every game belongs to its", "~maker. No ROMs ship with", "~this project.", "", "",
    "#EMULATION", "", "nofrendo", "~Matthew Conte", "", "retro-go fork", "~ducalex", "", "",
    "#SOFTWARE", "", "ESP-IDF, NimBLE", "~Espressif", "", "Font", "~font8x8 - Daniel Hepper", "",
    "Menus and effects", "~after PELLETINO", "", "",
    "#MUSIC", "", "DuckTales - The Moon", "~Hiroshige Tonomura", "~Capcom 1989", "", "",
    "#ART", "", "Box art, screenshots", "~libretro-thumbnails", "", "",
    "#THANK YOU", "for playing", "", "~github.com/aedile",
};
#define COUNT(a) ((int)(sizeof(a) / sizeof *(a)))
#define LINE_H 12
#define HEAD_H 30
#define MARGIN 16

static int total_lines(void) { return COUNT(before_games) + roms_count() + COUNT(after_games); }

static const char *line_at(int i, char *buf, size_t n)
{
    if (i < COUNT(before_games)) return before_games[i];
    i -= COUNT(before_games);
    if (i < roms_count()) { rom_short_name(i, buf, n); return buf; }
    return after_games[i - roms_count()];
}

static void draw_line(const char *s, int y)
{
    if (!*s) return;
    if (s[0] == '#') ui_text_centred_scaled(CX, y + 4, s + 1, CUBE(5,4,0), 2);
    else if (s[0] == '~') ui_text_center(y, s + 1, CUBE(3,3,4));
    else ui_text_center(y, s, UI_WHITE);
}

bool attract_credits(void)
{
    char buf[29];
    int n = total_lines(), height = 0;
    for (int i = 0; i < n; i++) height += line_at(i, buf, sizeof buf)[0] == '#' ? HEAD_H : LINE_H;
    any_button();
    for (int t = 0; ; t++) {
        if (any_button()) return true;
        int top = ui_h - t / 2;             /* half a pixel a tick: 30 px a second, rising */
        if (top + height < 0) return false;
        ui_clear(UI_BLACK);
        ui_stars(t);
        int y = top;
        for (int i = 0; i < n; i++) {
            const char *s = line_at(i, buf, sizeof buf);
            int h = s[0] == '#' ? HEAD_H : LINE_H;
            if (y > -h && y < ui_h) draw_line(s, y);
            y += h;
        }
        present(t);
    }
}
