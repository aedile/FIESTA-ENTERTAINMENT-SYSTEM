/*
 * wheel.c - the game wheel, after FIESTACADE's menu.
 *
 * Five places on a curve down the right of the panel: the chosen game's cover large in the
 * middle, its neighbours smaller and dimmer above and below, the pair beyond smaller still and
 * mostly off the edge. A step turns the wheel one place, quickly at first and settling. Behind
 * it, the chosen game's screenshot, dimmed, and the whole panel scanlined on the way out. The
 * last entry is Credits. Covers are scaled on the way in; the far ones are shaded darker.
 */
#include "wheel.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_timer.h"
#include "ui.h"
#include "input.h"
#include "music.h"
#include "sfx.h"
#include "roms.h"
#include "medal.h"

#define HEADER_H 18
#define FOOTER_H 32
#define CENTRE_Y 140
#define STEP 256
#define MIDDLE (2 * STEP)
#define HOLD_MS 2000
#define IDLE_US 45000000LL
#define LAUNCH_TICKS 52
#define LAUNCH_WHITE 18

/* where a cover rests: centre y, shift right of the centre line, the box it is fitted to */
static const struct { int16_t cy, dx, bw, bh; } PLACE[5] = {
    { -30, 72, 30, 44 }, { 44, 60, 50, 72 }, { 140, 0, 104, 150 }, { 236, 60, 50, 72 }, { 310, 72, 30, 44 },
};

static int n_entries, sel, turn, frame, launch = -1;
static bool showcase;

void wheel_init(int games) { n_entries = games + 1; if (sel >= n_entries) sel = 0; turn = 0; launch = -1; }
void wheel_select(int game) { if (game >= 0 && game < n_entries) { sel = game; turn = 0; } }
int wheel_selected(void) { return sel; }
static int wrap(int i) { return n_entries > 0 ? ((i % n_entries) + n_entries) % n_entries : 0; }
static bool is_credits(int i) { return i == n_entries - 1; }

static void nav(int delta)
{
    if (n_entries <= 0) return;
    sel = wrap(sel + delta);
    turn += delta * STEP;
    if (!showcase) sfx_play(SFX_CLICK);
    if (turn > 2 * STEP) turn = 2 * STEP;
    if (turn < -2 * STEP) turn = -2 * STEP;
}

static void backdrop(int index)
{
    const uint8_t *snap = is_credits(index) ? NULL : rom_snap(index);
    if (snap) { ui_snap(snap); return; }
    ui_clear(UI_BLACK);
    ui_stars(frame);
    ui_grid(frame, 150);
}

/* one entry, `at` places down the wheel (in STEPs; MIDDLE is the chosen one); grow is a
 * percentage added to its box for the hold and the launch */
static void draw_entry(int index, int at, int grow)
{
    if (at < 0 || at > 4 * STEP) return;
    int i = at / STEP, f = at % STEP;
    if (i == 4) { i = 3; f = STEP; }
    int cy = PLACE[i].cy + (PLACE[i + 1].cy - PLACE[i].cy) * f / STEP;
    int cx = ui_w / 2 + PLACE[i].dx + (PLACE[i + 1].dx - PLACE[i].dx) * f / STEP;
    int bw = PLACE[i].bw + (PLACE[i + 1].bw - PLACE[i].bw) * f / STEP;
    int bh = PLACE[i].bh + (PLACE[i + 1].bh - PLACE[i].bh) * f / STEP;
    bw += bw * grow / 100; bh += bh * grow / 100;
    int far = (abs(at - MIDDLE) + STEP / 2) / STEP;
    if (far > 2) far = 2;
    int shade = far == 0 ? 0 : far == 1 ? 2 : 3;

    if (is_credits(index)) {
        static const uint8_t tone[3] = { UI_WHITE, UI_GREY, CUBE(1,1,1) };
        int scale = far == 0 ? 2 : 1;
        ui_text_centred_scaled(cx, cy - 4 * scale, "CREDITS", tone[far], scale);
        return;
    }
    int aw, ah;
    const uint8_t *art = rom_art(index, &aw, &ah);
    int h = bh, w = aw * bh / ah;
    if (w > bw) { w = bw; h = ah * bw / aw; }
    if (!art) {
        ui_fill(cx - w / 2, cy - h / 2, w, h, far == 0 ? CUBE(2,2,3) : CUBE(1,1,1));
        if (far == 0) { char name[14]; rom_short_name(index, name, sizeof name); ui_text_centred_scaled(cx, cy - 4, name, UI_WHITE, 1); }
        return;
    }
    /* clip to between header and footer by drawing into the frame and letting them paint over */
    ui_bitmap_shaded(cx - w / 2, cy - h / 2, art, aw, ah, h, ah, shade);
}

static void wheel_draw(int spread, int grow)
{
    int lo = -4, hi = 4;
    while (lo <= hi) {
        int k = abs(lo) >= abs(hi) ? lo++ : hi--;
        int at = MIDDLE + k * STEP + turn + (k > 0 ? spread : k < 0 ? -spread : 0);
        draw_entry(wrap(sel + k), at, k == 0 ? grow : 0);
    }
}

static void pips(void)
{
    if (n_entries < 2) return;
    int pitch = 150 / (n_entries - 1);
    if (pitch > 10) pitch = 10;
    if (pitch < 3) return;
    int y = CENTRE_Y - pitch * (n_entries - 1) / 2;
    for (int i = 0; i < n_entries; i++, y += pitch) {
        if (i == sel) ui_fill(ui_w - 4, y - 3, 3, 7, UI_YELLOW);
        else ui_fill(ui_w - 3, y - 1, 2, 2, CUBE(2,2,3));
    }
}

static void pointers(void)
{
    static const int8_t nudge[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };
    int in = nudge[(frame / 6) % 8];
    for (int i = 0; i < 6; i++) {
        ui_fill(3 + in + i, CENTRE_Y - 6 + i, 1, 12 - 2 * i, UI_YELLOW);
        ui_fill(ui_w - 10 - in - i, CENTRE_Y - 6 + i, 1, 12 - 2 * i, UI_YELLOW);
    }
}

static void header(void)
{
    ui_fill(0, 0, ui_w, HEADER_H, UI_BLACK);
    const char *name = muted ? "MUTED" : quiet ? "QUIET" : "F.E.S.";
    int pct = medal_battery_percent();
    char charge[8]; snprintf(charge, sizeof charge, "%d", pct);
    const int bw = 26, bh = 11, gap = 12;
    int name_w = 8 * (int)strlen(name), charge_w = 8 * (int)strlen(charge);
    int x = (ui_w - (name_w + gap + charge_w + 5 + bw + 2)) / 2;
    ui_text(x, 5, name, muted ? UI_RED : quiet ? UI_YELLOW : CUBE(3,3,4));
    x += name_w + gap;
    ui_text(x, 5, charge, UI_GREY);
    x += charge_w + 5;
    uint8_t c = pct <= 5 ? UI_RED : pct <= 15 ? UI_YELLOW : UI_GREEN;
    ui_frame(x, 4, bw, bh, CUBE(3,3,4));
    ui_fill(x + bw, 7, 2, bh - 6, CUBE(3,3,4));
    int fill = (bw - 4) * pct / 100;
    if (fill > 0) ui_fill(x + 2, 6, fill, bh - 4, c);
}

static void footer(int held_ms, bool pad)
{
    int top = ui_h - FOOTER_H;
    ui_fill(0, top, ui_w, FOOTER_H, UI_BLACK);
    char name[29];
    if (is_credits(sel)) strcpy(name, "Credits"); else rom_short_name(sel, name, sizeof name);
    if (launch >= 0) { ui_text_centred_scaled(ui_w / 2, top + 12, "LOADING", UI_GREEN, 1); return; }
    const char *tag = !is_credits(sel) && demo_skip[sel] ? "no demo" : !is_credits(sel) && demo_lock == sel ? "demo locked" : NULL;
    ui_text_centred_scaled(ui_w / 2, top + 7, name, UI_WHITE, 1);
    if (showcase) { if ((frame / 24) & 1) ui_text_centred_scaled(ui_w / 2, top + 19, "PRESS A BUTTON", UI_YELLOW, 1); return; }
    ui_text_centred_scaled(ui_w / 2, top + 19, tag ? tag : pad ? "A TO PLAY" : "HOLD MIDDLE TO PLAY", tag ? UI_GREY : UI_YELLOW, 1);
    if (held_ms > 0) {
        int w = held_ms * ui_w / HOLD_MS;
        ui_fill(0, top, ui_w, 3, CUBE(1,1,1));
        ui_fill(0, top, w > ui_w ? ui_w : w, 3, UI_YELLOW);
    }
}

static void render(int held_ms, bool pad)
{
    frame++;
    if (frame & 1) {   /* present at 30: the odd ticks only carry the music and settle the turn */
        music_tick_hook(NULL);
        turn = turn * 5 / 8;
        if (abs(turn) < 6) turn = 0;
        return;
    }
    int behind = sel;
    if (turn >= STEP / 2) behind = wrap(sel - 1);
    if (turn <= -STEP / 2) behind = wrap(sel + 1);
    backdrop(behind);
    int spread = 0, grow = 0;
    if (launch >= 0) {
        int t = launch < LAUNCH_TICKS ? launch : LAUNCH_TICKS;
        spread = t * t * 3 * STEP / (LAUNCH_TICKS * LAUNCH_TICKS);
        grow = t * t * 70 / (LAUNCH_TICKS * LAUNCH_TICKS);
        int w = t - (LAUNCH_TICKS - LAUNCH_WHITE);
        ui_white = w <= 0 ? 0 : (uint8_t)(255 * w / LAUNCH_WHITE);
        if (launch < LAUNCH_TICKS) launch++;
    } else if (held_ms > 0) {
        if (held_ms > HOLD_MS) held_ms = HOLD_MS;
        grow = held_ms * 9 / HOLD_MS + ((frame & 2) ? 1 : 0);
    }
    wheel_draw(spread, grow);
    if (launch < 0) pips();
    if (turn == 0 && launch < 0 && !showcase) pointers();
    header();
    footer(held_ms, pad);
    music_tick_hook(ui_line_push);
    turn = turn * 5 / 8;
    if (abs(turn) < 6) turn = 0;
}

static void begin_launch(void) { launch = 0; turn = 0; sfx_tone(0); sfx_play(SFX_COIN); }

wheel_result_t wheel_run(int *game)
{
    showcase = false; launch = -1; ui_white = 0;
    ui_crt = true;
    int64_t last_input = esp_timer_get_time();
    ble_pad_scan_rate(true);
    pad_edges(); medal_events();
    for (;;) {
        uint32_t e = pad_edges(), mev = medal_events();
        int held = medal_boot_held_ms();
        if (e || mev || held) last_input = esp_timer_get_time();
        bool pad = ble_pad_state() == PAD_CONNECTED || serial_active();
        if (launch < 0) {
            if (e & (PAD_UP | PAD_LEFT)) nav(-1);
            if (e & (PAD_DOWN | PAD_RIGHT)) nav(+1);
            if (mev & BTN_PWR_SHORT) nav(+1);
            if (mev & BTN_BOOT_SHORT) nav(-1);
            if ((e & PAD_B) && !is_credits(sel)) { demo_set_skip(sel, !demo_skip[sel]); }
            if (e & PAD_SELECT) { volume_cycle(); toast_volume(); }
            if (e & PAD_START) { set_portrait(!portrait); toast(portrait ? "Portrait" : "Landscape", "games only; START to switch"); }
            if (e & PAD_MENU) { sfx_tone(0); ui_crt = false; return WHEEL_CONTROLLER; }
            if (serial_demo) { sfx_tone(0); ui_crt = false; return WHEEL_IDLE; }
            if (held >= HOLD_MS) { medal_boot_consume(); held = 0; begin_launch(); }
            else if (e & PAD_A) begin_launch();
            else sfx_tone(held > 0 ? 330 + held * 660 / HOLD_MS : 0);
            if (esp_timer_get_time() - last_input > IDLE_US) { sfx_tone(0); ui_crt = false; return WHEEL_IDLE; }
        }
        render(launch >= 0 ? 0 : held, pad);
        if (launch >= LAUNCH_TICKS) {
            ui_white = 0; ui_crt = false; launch = -1;
            *game = sel;
            return is_credits(sel) ? WHEEL_CREDITS : WHEEL_PLAY;
        }
    }
}

bool wheel_showcase(int seconds_per_game)
{
    showcase = true; launch = -1; ui_white = 0; ui_crt = true;
    int step = seconds_per_game * 60;
    any_button();
    for (int i = 0; i < n_entries; i++) {
        if (i) nav(+1);
        for (int t = 0; t < step; t++) {
            if (any_button()) { ui_crt = false; showcase = false; return true; }
            render(0, false);
        }
    }
    ui_crt = false; showcase = false;
    return false;
}
