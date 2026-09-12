/*
 * NESTOR - NES emulator for the Waveshare ESP32-C6-LCD-1.69, worn as a fiesta medal.
 *
 * Boot -> controller screen -> box-art picker -> game (MENU: resume / save / load /
 * picker / controller). No pad within 30 s, or a pad idle for 3 minutes -> demo mode:
 * the games' own attract modes, DEMO_SECONDS each, in a loop.
 *
 * Without a controller the medal's two buttons work everywhere:
 *   PWR  short: next game            long (2 s): power off
 *   BOOT short: lock/unlock the demo to the current game (kept in NVS)
 *        hold 3 s: mute / unmute        hold 10 s: forget the saved controller
 * PRG/CHR ROM run straight out of memory-mapped flash; battery RAM and save
 * states live in the 'saves' NVS partition.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "driver/usb_serial_jtag.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "audio_hal.h"
#include "ble_pad.h"
#include "ui.h"
#include "saves.h"
#include "medal.h"
#include "music.h"
#include "core.h"
#include "nes/nes.h"
#include "palettes.h"

static const char *TAG = "NESTOR";

#define SRAM_SIZE       0x2000
#define DEMO_AFTER_US   30000000LL   /* no controller for this long -> demo mode */
#define IDLE_AFTER_US   180000000LL  /* pad connected but untouched this long -> demo mode */
#ifndef DEMO_SECONDS
#define DEMO_SECONDS    120          /* per ROM in demo mode (override: idf.py -DDEMO_SECONDS=30) */
#endif
#define BACKLIGHT_PLAY  153          /* 60 %, as PELLETINO */
#define BACKLIGHT_DEMO  76           /* 30 % */
#define MUSIC_TRACK     7            /* DuckTales NSF (joshw rip of the release): 7 = The Moon */

/* ---- roms partition: image written by tools/pack_roms.py ---- */
typedef struct __attribute__((packed)) {
    char name[48];
    uint32_t off, size;
    uint32_t art_off;
    uint16_t art_w, art_h;
} rom_entry_t;
static const uint8_t *roms_base;
static const rom_entry_t *roms;
static int nroms;

static void roms_init(void)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "roms");
    assert(p);
    esp_partition_mmap_handle_t h;
    const void *ptr;
    ESP_ERROR_CHECK(esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &ptr, &h));
    roms_base = ptr;
    if (memcmp(roms_base, "NESR", 4) == 0) {
        memcpy(&nroms, roms_base + 4, 4);
        roms = (const rom_entry_t *)(roms_base + 8);
    }
    ESP_LOGI(TAG, "roms partition at %p: %d ROMs", ptr, nroms);
    for (int i = 0; i < nroms; i++)
        ESP_LOGI(TAG, "  [%d] %-40s %6lu bytes mapper %d art %ux%u", i, roms[i].name, roms[i].size,
                 (roms_base[roms[i].off + 6] >> 4) | (roms_base[roms[i].off + 7] & 0xF0), roms[i].art_w, roms[i].art_h);
}

/* "Super Mario Bros. (Japan, USA)" -> "Super Mario Bros." */
static void short_name(const char *in, char *out, size_t n)
{
    const char *p = strstr(in, " (");
    size_t len = p ? (size_t)(p - in) : strlen(in);
    if (len >= n) len = n - 1;
    memcpy(out, in, len);
    out[len] = 0;
}

static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: free %lu, largest block %u, min ever %lu", when, esp_get_free_heap_size(),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), esp_get_minimum_free_heap_size());
}

/* ---- demo settings in NVS: the locked game, and games excluded from the cycle ---- */
#define NVS_NS "nestor"
static int demo_lock = -1;        /* index of the game the demo is locked to, -1 = cycle */
static bool demo_skip[64];
static bool muted;
/* games left out of the demo cycle unless toggled back in with B in the picker (matched by short name) */
static const char *const demo_skip_default[] = { "DuckTales", "Double Dragon", "Mega Man", "Final Fantasy" };

static void nvs_key_for(char out[16], char type, const char *rom)
{
    snprintf(out, 16, "%c%08lx", type, (unsigned long)esp_rom_crc32_le(0, (const uint8_t *)rom, strlen(rom)));
}

static void demo_settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    char lock[48] = {0}; size_t n = sizeof lock;
    if (nvs_get_str(h, "demo_lock", lock, &n) == ESP_OK)
        for (int i = 0; i < nroms; i++) if (strcmp(roms[i].name, lock) == 0) demo_lock = i;
    for (int i = 0; i < nroms && i < 64; i++) {
        char k[16]; uint8_t v = 0;
        nvs_key_for(k, 'd', roms[i].name);
        if (nvs_get_u8(h, k, &v) == ESP_OK) {
            demo_skip[i] = v;
        } else {
            char sn[29]; short_name(roms[i].name, sn, sizeof sn);
            for (size_t d = 0; d < sizeof demo_skip_default / sizeof *demo_skip_default; d++)
                if (strcmp(sn, demo_skip_default[d]) == 0) demo_skip[i] = true;
        }
    }
    uint8_t m = 0;
    muted = nvs_get_u8(h, "mute", &m) == ESP_OK && m;
    audio_set_mute(muted);
    nvs_close(h);
    ESP_LOGI(TAG, "demo lock: %s", demo_lock >= 0 ? roms[demo_lock].name : "none");
}

static void demo_set_lock(int idx)
{
    demo_lock = idx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (idx >= 0) nvs_set_str(h, "demo_lock", roms[idx].name); else nvs_erase_key(h, "demo_lock");
    nvs_commit(h); nvs_close(h);
}

static void demo_set_skip(int idx, bool skip)
{
    demo_skip[idx] = skip;
    char k[16]; nvs_key_for(k, 'd', roms[idx].name);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, k, skip);   /* explicit 0 so a default exclusion can be turned back on */
    nvs_commit(h); nvs_close(h);
}

static void set_mute(bool m)
{
    muted = m;
    audio_set_mute(m);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "mute", m); nvs_commit(h); nvs_close(h); }
    ESP_LOGI(TAG, "%s", m ? "muted" : "sound on");
}

static int demo_next(int i)
{
    for (int n = (i + 1) % nroms, tries = 0; tries < nroms; n = (n + 1) % nroms, tries++)
        if (!demo_skip[n]) return n;
    return (i + 1) % nroms;   /* everything excluded: cycle anyway */
}

/* ---- input: the BLE pad, plus keys typed into the serial monitor for bench testing
 * (w/a/s/d = d-pad, j = A, k = B, q = start, e = select, m = menu; x = demo now,
 *  n / l = the medal's PWR / BOOT short press) ---- */
static int64_t serial_last = -10000000;
static bool serial_demo;
static uint32_t serial_medal;
static bool serial_active(void) { return esp_timer_get_time() - serial_last < 5000000; }

static uint32_t serial_pad(void)
{
    static const char keys[] = "wsadjkqem";
    static const uint32_t bits[] = { PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_A, PAD_B, PAD_START, PAD_SELECT, PAD_MENU };
    static int64_t held_until[9];
    int64_t now = esp_timer_get_time();
    uint8_t c;
    while (usb_serial_jtag_read_bytes(&c, 1, 0) == 1) {
        const char *k = memchr(keys, c, sizeof keys - 1);
        if (k) { held_until[k - keys] = now + 120000; serial_last = now; }
        if (c == 'x') { serial_demo = true; serial_last = now; }
        if (c == 'n') { serial_medal |= BTN_PWR_SHORT; serial_last = now; }
        if (c == 'l') { serial_medal |= BTN_BOOT_SHORT; serial_last = now; }
    }
    uint32_t m = 0;
    for (int i = 0; i < 9; i++) if (held_until[i] > now) m |= bits[i];
    return m;
}

static uint32_t pad_now(void)
{
    uint32_t b = ble_pad_buttons(), raw = ble_pad_raw();
    static uint32_t last_b = 0, last_raw = 0;
    if (b != last_b || raw != last_raw) {
        ESP_LOGI(TAG, "PAD raw=%04lx %s%s%s%s%s%s%s%s%s", raw,
                 b & PAD_UP ? "UP " : "", b & PAD_DOWN ? "DOWN " : "", b & PAD_LEFT ? "LEFT " : "",
                 b & PAD_RIGHT ? "RIGHT " : "", b & PAD_A ? "A " : "", b & PAD_B ? "B " : "",
                 b & PAD_START ? "START " : "", b & PAD_SELECT ? "SELECT " : "", b & PAD_MENU ? "MENU " : "");
        last_b = b; last_raw = raw;
    }
    return b | serial_pad();
}

/* newly pressed bits, with key repeat on the d-pad for lists */
static uint32_t pad_edges(void)
{
    static uint32_t prev;
    static int64_t repeat_at;
    int64_t now = esp_timer_get_time();
    uint32_t cur = pad_now(), e = cur & ~prev;
    uint32_t dpad = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
    if (cur & dpad) {
        if (e & dpad) repeat_at = now + 400000;
        else if (now > repeat_at) { e |= cur & dpad; repeat_at = now + 120000; }
    }
    prev = cur;
    return e;
}

/* medal buttons: real ones plus the serial stand-ins. BOOT 10 s forgets the pad everywhere;
 * BOOT 3 s toggles mute everywhere (the toast is drawn by the caller through toast_mute()). */
static uint32_t medal_events(void)
{
    uint32_t ev = medal_poll() | serial_medal;
    serial_medal = 0;
    if (ev & BTN_BOOT_HOLD10) { ble_pad_forget(); ble_pad_scan_any(true); }
    if (ev & BTN_BOOT_HOLD3) set_mute(!muted);
    return ev;
}

/* ---- overlays ---- */
static void toast(const char *line1, const char *line2)
{
    int w = 8 * (int)(strlen(line1) > strlen(line2) ? strlen(line1) : strlen(line2)) + 32;
    int x = (256 - w) / 2;
    ui_fill(x, 96, w, 48, UI_BLACK);
    ui_frame(x, 96, w, 48, UI_WHITE);
    ui_text_center(108, line1, UI_YELLOW);
    ui_text_center(124, line2, UI_WHITE);
    ui_present();
    for (int i = 0; i < 60; i++) music_tick();   /* ~1 s, music keeps playing if any */
}

static void toast_mute(void) { toast(muted ? "Muted" : "Sound on", "hold BOOT 3 s to toggle"); }

static void toggle_lock(int idx)
{
    char name[29]; short_name(roms[idx].name, name, sizeof name);
    if (demo_lock == idx) { demo_set_lock(-1); toast("Demo unlocked", "cycling all games"); }
    else { demo_set_lock(idx); toast("Demo locked on", name); }
}

/* ---- controller screen. Returns false if nothing connected for DEMO_AFTER_US (or PWR pressed). ---- */
static bool controller_screen(bool boot)
{
    int64_t deadline = esp_timer_get_time() + ((boot && ble_pad_has_saved()) ? 5000000 : 0);
    int64_t demo_at = esp_timer_get_time() + DEMO_AFTER_US;
    bool any = false;
    int sel = 0;
    ble_pad_state_t shown = -1;
    int64_t last_draw = 0;
    ui_palette_cube();
    music_start(MUSIC_TRACK);
    pad_edges();
    for (;;) {
        ble_pad_state_t st = ble_pad_state();
        bool connected = st == PAD_CONNECTED;
        if (!any && !connected && esp_timer_get_time() > deadline) { any = true; ble_pad_scan_any(true); }
        if (connected && any) { any = false; ble_pad_scan_any(false); }   /* reconnects go to this pad only */
        uint32_t mev = medal_events();
        if (mev & BTN_BOOT_HOLD10) { any = true; shown = -1; }
        if (mev & BTN_BOOT_HOLD3) { toast_mute(); shown = -1; }
        if (mev & BTN_PWR_SHORT) return false;

        uint32_t e = pad_edges();
        if (connected || serial_active()) {
            if (e & PAD_UP) sel = 0;
            if (e & PAD_DOWN) sel = 1;
            if ((e & PAD_A) && sel == 1) { ble_pad_forget(); any = true; ble_pad_scan_any(true); sel = 0; }
            if (((e & PAD_A) && sel == 0) || (e & (PAD_B | PAD_MENU))) return true;
        }
        if (boot && connected) return true;
        if (!connected && !serial_active() && esp_timer_get_time() > demo_at) return false;

        if (st != shown || esp_timer_get_time() - last_draw > 250000) {
            shown = st; last_draw = esp_timer_get_time();
            ui_clear(UI_BLACK);
            ui_text_center(16, "CONTROLLER", UI_YELLOW);
            const char *s = "Idle"; uint8_t c = UI_GREY;
            if (st == PAD_SCANNING) { s = "Scanning..."; c = UI_WHITE; }
            if (st == PAD_CONNECTING) { s = "Connecting..."; c = UI_YELLOW; }
            if (connected) { s = "Connected"; c = UI_GREEN; }
            ui_text(24, 52, "Status:", UI_GREY); ui_text(96, 52, s, c);
            ui_text(24, 68, "Found:", UI_GREY);  ui_text(96, 68, ble_pad_name()[0] ? ble_pad_name() : "-", UI_WHITE);
            ui_text(24, 84, "Saved:", UI_GREY);  ui_text(96, 84, ble_pad_has_saved() ? "yes" : "no", UI_WHITE);
            if (!connected) {
                ui_text_center(120, "Put the controller in", UI_WHITE);
                ui_text_center(132, "pairing mode", UI_WHITE);
                char d[32]; snprintf(d, sizeof d, "demo mode in %d s", (int)((demo_at - esp_timer_get_time()) / 1000000));
                ui_text_center(160, d, UI_GREY);
            } else {
                ui_text(40, 120, sel == 0 ? ">" : " ", UI_YELLOW); ui_text(56, 120, "Back", sel == 0 ? UI_YELLOW : UI_WHITE);
                ui_text(40, 136, sel == 1 ? ">" : " ", UI_YELLOW); ui_text(56, 136, "Forget this controller", sel == 1 ? UI_YELLOW : UI_WHITE);
            }
            ui_text_center(200, "PWR: demo now   hold: power off", UI_GREY);
            ui_text_center(216, "BOOT 3s: mute   10s: forget pad", UI_GREY);
            ui_present();
        }
        music_tick();
    }
}

/* ---- box-art picker: the selected cover big in the middle, neighbours half size ---- */
static void draw_cover(int idx, int cx, int cy, int num, int den)
{
    const rom_entry_t *r = &roms[idx];
    int w = r->art_off ? r->art_w : 96, h = r->art_off ? r->art_h : 134;
    int ow = w * num / den, oh = h * num / den, x = cx - ow / 2, y = cy - oh / 2;
    if (r->art_off) {
        ui_bitmap(x, y, roms_base + r->art_off, w, h, num, den);
    } else {
        ui_fill(x, y, ow, oh, UI_GREY);
        if (num == den) { char n[13]; short_name(r->name, n, sizeof n); ui_text_center(cy - 4, n, UI_WHITE); }
    }
}

static int picker(int sel)
{
    bool has_save[64];
    for (int i = 0; i < nroms && i < 64; i++) has_save[i] = saves_has_sram(roms[i].name);
    bool dirty = true;
    int64_t last_input = esp_timer_get_time();
    ui_palette_cube();
    display_set_backlight(BACKLIGHT_PLAY);
    music_start(MUSIC_TRACK);
    pad_edges();
    for (;;) {
        uint32_t e = pad_edges(), mev = medal_events();
        if (e || mev) last_input = esp_timer_get_time();
        if ((e & PAD_LEFT) && sel > 0) { sel--; dirty = true; }
        if (((e & PAD_RIGHT) || (mev & BTN_PWR_SHORT)) && sel < nroms - 1) { sel++; dirty = true; }
        if (e & PAD_A) return sel;
        if (e & PAD_B) { demo_set_skip(sel, !demo_skip[sel]); dirty = true; }
        if ((mev & BTN_BOOT_SHORT) && nroms) { toggle_lock(sel); dirty = true; }
        if (mev & BTN_BOOT_HOLD3) { toast_mute(); dirty = true; }
        if (serial_demo) { serial_demo = false; return -1; }
        if (esp_timer_get_time() - last_input > IDLE_AFTER_US) return -1;
        if (e & PAD_MENU) { if (!controller_screen(false)) return -1; ui_palette_cube(); dirty = true; }
        if (ble_pad_state() != PAD_CONNECTED && !serial_active()) { if (!controller_screen(false)) return -1; ui_palette_cube(); dirty = true; }
        if (dirty) {
            dirty = false;
            ui_clear(UI_BLACK);
            ui_text(8, 6, "NESTOR", UI_YELLOW);
            char bat[8]; snprintf(bat, sizeof bat, "%d%%", medal_battery_percent());
            ui_text(256 - 8 - 8 * strlen(bat), 6, bat, UI_GREY);
            if (nroms == 0) { ui_text_center(100, "No ROMs in partition", UI_RED); ui_present(); continue; }
            if (sel > 0) draw_cover(sel - 1, 40, 96, 1, 2);
            if (sel + 1 < nroms) draw_cover(sel + 1, 216, 96, 1, 2);
            draw_cover(sel, 128, 92, 1, 1);
            ui_frame(128 - 50, 92 - 69, 100, 138, sel == demo_lock ? UI_YELLOW : UI_WHITE);
            char name[29]; short_name(roms[sel].name, name, sizeof name);
            ui_text_center(168, name, UI_WHITE);
            char tags[40] = "";
            if (has_save[sel]) strcat(tags, "* saved  ");
            if (demo_skip[sel]) strcat(tags, "no demo  ");
            if (sel == demo_lock) strcat(tags, "demo locked  ");
            if (muted) strcat(tags, "muted");
            ui_text_center(184, tags, has_save[sel] ? UI_GREEN : UI_GREY);
            char pos[24]; snprintf(pos, sizeof pos, "%d/%d", sel + 1, nroms);
            ui_text_center(200, pos, UI_GREY);
            ui_text_center(228, "A play  B demo on/off  MENU pad", UI_GREY);
            ui_present();
        }
        music_tick();
    }
}

/* ---- in-game menu ---- */
enum { MENU_RESUME, MENU_SAVE, MENU_LOAD, MENU_RESET, MENU_MUTE, MENU_PICKER, MENU_CONTROLLER, MENU_COUNT };
static int game_menu(const char *rom)
{
    const char *items[MENU_COUNT] = { "Resume", "Save state", "Load state", "Reset game", muted ? "Unmute" : "Mute",
                                      "Return to picker", "Controller" };
    bool have_state = saves_has_state(rom);
    int sel = 0;
    bool dirty = true;
    pad_edges();
    for (;;) {
        uint32_t e = pad_edges();
        if ((e & PAD_UP) && sel > 0) { sel--; dirty = true; }
        if ((e & PAD_DOWN) && sel < MENU_COUNT - 1) { sel++; dirty = true; }
        if (e & (PAD_B | PAD_MENU)) return MENU_RESUME;
        if ((e & PAD_A) && !(sel == MENU_LOAD && !have_state)) return sel;
        if (dirty) {
            dirty = false;
            ui_fill(56, 44, 144, 150, UI_BLACK);
            ui_frame(56, 44, 144, 150, UI_WHITE);
            ui_text(72, 56, "MENU", UI_YELLOW);
            for (int i = 0; i < MENU_COUNT; i++) {
                uint8_t c = i == sel ? UI_YELLOW : UI_WHITE;
                if (i == MENU_LOAD && !have_state) c = UI_GREY;
                ui_text(72, 78 + i * 14, i == sel ? ">" : " ", UI_YELLOW);
                ui_text(88, 78 + i * 14, items[i], c);
            }
            ui_present();
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

/* ---- frame output: each 16-line strip is converted and queued for DMA as soon as
 * the PPU has rendered it, so the transfer overlaps emulation of the next strip ---- */
static int64_t push_us;

static void push_strip(uint8 *bmp, int y0, int rows)
{
    int64_t a = esp_timer_get_time();
    display_push_strip(bmp + y0 * FB_PITCH + FB_XOFF, FB_PITCH, y0, ui_pal);
    push_us += esp_timer_get_time() - a;   /* conversion + any wait for the previous strip */
}

static void build_palette(int which)
{
    const uint8_t *p = nes_palettes[which];
    for (int i = 0; i < 64; i++) {
        uint16_t c = ui_rgb(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
        /* the PPU sets bit 6/7 on pixels for priority/transparency: same colour there */
        ui_pal[i] = ui_pal[i + 64] = ui_pal[i + 128] = c;
    }
}

static int nes_buttons(uint32_t b)
{
    int n = 0;
    if (b & PAD_A) n |= NES_PAD_A;
    if (b & PAD_B) n |= NES_PAD_B;
    if (b & PAD_SELECT) n |= NES_PAD_SELECT;
    if (b & PAD_START) n |= NES_PAD_START;
    if (b & PAD_UP) n |= NES_PAD_UP;
    if (b & PAD_DOWN) n |= NES_PAD_DOWN;
    if (b & PAD_LEFT) n |= NES_PAD_LEFT;
    if (b & PAD_RIGHT) n |= NES_PAD_RIGHT;
    return n;
}

/* battery RAM: written to NVS when its CRC has changed and then stayed put for a second */
static uint32_t sram_saved_crc, sram_last_crc;
static void sram_flush(nes_t *nes, const char *rom, bool force)
{
    if (!nes->cart->battery) return;
    uint32_t crc = esp_rom_crc32_le(0, nes->cart->prg_ram, SRAM_SIZE);
    if (crc != sram_saved_crc && (force || crc == sram_last_crc)) {
        if (saves_store_sram(rom, nes->cart->prg_ram, SRAM_SIZE)) sram_saved_crc = crc;
        ESP_LOGI(TAG, "battery RAM saved");
    }
    sram_last_crc = crc;
}

typedef enum { GAME_PICKER, GAME_IDLE, GAME_DEMO_NEXT, GAME_DEMO_EXIT } game_result_t;

static game_result_t run_game_inner(int idx, bool demo);

/* demo: no input, DEMO_SECONDS limit unless locked, saves untouched */
static game_result_t run_game(int idx, bool demo)
{
    game_result_t r = run_game_inner(idx, demo);
    core_unload();   /* hand the core back to the menu music */
    return r;
}

static game_result_t run_game_inner(int idx, bool demo)
{
    const char *rom = roms[idx].name;
    music_stop();
    ui_palette_cube();
    if (demo) {
        char name[29]; short_name(rom, name, sizeof name);
        ui_clear(UI_BLACK);
        draw_cover(idx, 128, 84, 1, 1);
        ui_text_center(164, name, UI_YELLOW);
        ui_text_center(184, demo_lock == idx ? "demo (locked)" : "demo", UI_GREY);
        ui_text_center(216, "press any pad button to play", UI_GREY);
        ui_present();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    nes_t *nes = nes_getptr();
    if (!core_load(roms_base + roms[idx].off, roms[idx].size)) {
        ui_clear(UI_BLACK); ui_text_center(112, "Unsupported ROM", UI_RED); ui_present();
        vTaskDelay(pdMS_TO_TICKS(1500));
        return demo ? GAME_DEMO_NEXT : GAME_PICKER;
    }
    nes->strip_func = push_strip;
    nes_setvidbuf(ui_fb);
    build_palette(4);   /* palettes.h index 4 = PVM, retro-go default */
    if (nes->cart->battery && !demo) {
        if (saves_load_sram(rom, nes->cart->prg_ram, SRAM_SIZE)) ESP_LOGI(TAG, "battery RAM loaded");
        sram_saved_crc = sram_last_crc = esp_rom_crc32_le(0, nes->cart->prg_ram, SRAM_SIZE);
    }
    display_set_backlight(demo ? BACKLIGHT_DEMO : BACKLIGHT_PLAY);
    ESP_LOGI(TAG, "running %s%s", rom, demo ? " (demo)" : "");
    log_heap("with cart loaded");

    int frames = 0, skipped = 0;
    bool draw = true;
    int64_t t_report = esp_timer_get_time(), emu_us = 0, wait_us = 0;
    uint32_t prev = 0, underruns0 = audio_get_underrun_count();
    nes_prof_cpu = nes_prof_ppu = nes_prof_apu = 0; display_wait_us = 0; push_us = 0;
    int64_t t_start = esp_timer_get_time(), last_input = t_start;
    pad_edges();
    for (;;) {
        int64_t f0 = esp_timer_get_time();
        uint32_t b = 0, mev = medal_events();
        if (mev & BTN_BOOT_SHORT) { toggle_lock(idx); build_palette(4); underruns0 = audio_get_underrun_count(); }
        if (mev & BTN_BOOT_HOLD3) { toast_mute(); build_palette(4); underruns0 = audio_get_underrun_count(); }
        if (demo) {
            if (pad_edges()) { ESP_LOGI(TAG, "demo: button pressed, back to the picker"); return GAME_DEMO_EXIT; }
            if (mev & BTN_PWR_SHORT) return GAME_DEMO_NEXT;
            if (demo_lock != idx && f0 - t_start > (int64_t)DEMO_SECONDS * 1000000) return GAME_DEMO_NEXT;
        } else {
            b = pad_now();
            if (b != prev) last_input = f0;
            if (f0 - last_input > IDLE_AFTER_US) { sram_flush(nes, rom, true); return GAME_IDLE; }
            if (mev & BTN_PWR_SHORT) { sram_flush(nes, rom, true); return GAME_PICKER; }
        }
        if (!demo && (b & PAD_MENU) && !(prev & PAD_MENU)) {
            sram_flush(nes, rom, true);
            int a = game_menu(rom);
            if (a == MENU_SAVE) saves_save_state(rom);
            if (a == MENU_LOAD) saves_load_state(rom);
            if (a == MENU_RESET) nes_reset(true);
            if (a == MENU_MUTE) set_mute(!muted);
            if (a == MENU_CONTROLLER) { controller_screen(false); build_palette(4); }   /* music_start is a no-op: core busy */
            if (a == MENU_PICKER) return GAME_PICKER;
            prev = pad_now();
            last_input = esp_timer_get_time();
            underruns0 = audio_get_underrun_count();   /* menus don't feed the DAC; don't count that */
            continue;
        }
        prev = b;
        input_update(0, nes_buttons(b));
        int64_t p0 = push_us;
        nes_emulate(draw);
        int64_t f1 = esp_timer_get_time();
        emu_us += (f1 - f0) - (push_us - p0);
        frames++;
        if (!draw) skipped++;
        /* The APU rendered one frame of samples; queueing them blocks (yielding to BLE and
         * idle) while the I2S DMA queue is full, which locks us to the DAC clock. */
        audio_submit(nes->apu->buffer, nes->apu->samples_per_frame);
        wait_us += esp_timer_get_time() - f1;
        /* Frameskip: when the DAC has eaten into the queue (under 3 of 5 descriptors left)
         * the next frame only emulates (no PPU drawing, no push) so the queue refills.
         * Audio never gaps; the picture drops frames only as fast as the game is slow. */
        draw = audio_queued_samples() >= 3 * AUDIO_DMA_FRAME_NUM;
        if (frames % 60 == 0 && !demo) sram_flush(nes, rom, false);
        if (f1 - t_report >= 5000000) {
            /* profile: cycle counters at 160 MHz -> us; push = strip conversion CPU, dma wait = blocked on SPI */
            uint32_t mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
            ESP_LOGI(TAG, "%d fps (%d skipped) | us/frame: cpu %lu ppu %lu apu %lu push %lld dmawait %lu audiowait %lld | total %lld | underruns %lu | heap %lu",
                     frames / 5, skipped / 5, nes_prof_cpu / mhz / frames, nes_prof_ppu / mhz / frames, nes_prof_apu / mhz / frames,
                     (push_us - display_wait_us) / frames, display_wait_us / frames, wait_us / frames,
                     (f1 - t_report) / frames, audio_get_underrun_count() - underruns0, esp_get_free_heap_size());
            frames = skipped = 0; emu_us = push_us = wait_us = 0; t_report = f1;
            nes_prof_cpu = nes_prof_ppu = nes_prof_apu = 0; display_wait_us = 0;
            underruns0 = audio_get_underrun_count();
        }
    }
}

/* every game (or the locked one) until someone presses a pad button */
static void demo_loop(void)
{
    serial_demo = false;
    if (!nroms) { vTaskDelay(pdMS_TO_TICKS(1000)); return; }
    int i = demo_lock >= 0 ? demo_lock : demo_next(nroms - 1);
    for (;;) {
        if (run_game(i, true) == GAME_DEMO_EXIT) break;
        i = demo_next(i);
        if (demo_lock >= 0) demo_set_lock(i);   /* PWR "next" while locked moves the lock along */
    }
    display_set_backlight(BACKLIGHT_PLAY);
}

void app_main(void)
{
    medal_init();
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&usb);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    display_init();
    ui_init();
    audio_init();
    ble_pad_init();
    roms_init();
    saves_init();
    demo_settings_load();
    log_heap("after display+audio+BLE");

    bool have_pad = controller_screen(true);
    int sel = 0;
    for (;;) {
        if (!have_pad || sel < 0) { demo_loop(); have_pad = true; sel = 0; }
        sel = picker(sel);
        if (sel < 0) continue;
        game_result_t r = run_game(sel, false);
        if (r == GAME_IDLE) sel = -1;
    }
}
