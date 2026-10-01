/*
 * NESTOR - NES emulator for the Waveshare ESP32-C6-LCD-1.69, worn as a fiesta medal.
 *
 * Boot -> title -> the wheel -> a game (MENU: resume / save / load / controller / wheel).
 * Left alone 45 s on the wheel, or 3 minutes in a game, the medal puts on its show: title,
 * how to play, the wheel turning by itself, the credits, the fireworks, then the games' own
 * attract modes DEMO_SECONDS each. Any button brings the wheel back.
 *
 * The medal's two buttons work without a controller: PWR / BOOT step the wheel, BOOT held
 * picks, both together cycle the volume (full, quiet, muted), PWR held 2 s powers off, BOOT held
 * 10 s forgets the controller. In a game BOOT locks the demo to that game, BOOT held 5 s (or PWR)
 * leaves it.
 * PRG/CHR ROM run straight out of memory-mapped flash; battery RAM and save states live in
 * the 'saves' NVS partition.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include "driver/usb_serial_jtag.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "audio_hal.h"
#include "ui.h"
#include "saves.h"
#include "music.h"
#include "splash.h"
#include "festive.h"
#include "core.h"
#include "roms.h"
#include "input.h"
#include "attract.h"
#include "wheel.h"
#include "sfx.h"
#include "nes/nes.h"
#include "palettes.h"

static const char *TAG = "NESTOR";

#define SRAM_SIZE       0x2000
#define IDLE_AFTER_US   180000000LL  /* a game untouched this long -> the show */
#ifndef DEMO_SECONDS
#define DEMO_SECONDS    120          /* per game in the show (override: idf.py -DDEMO_SECONDS=30) */
#endif
#define BACKLIGHT_PLAY  153          /* 60 %, as PELLETINO */
#define BACKLIGHT_DEMO  76           /* 30 % */
#define MUSIC_TRACK     7            /* DuckTales NSF (joshw rip of the release): 7 = The Moon */
#define SHOWCASE_SECONDS 4

static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: free %lu, largest block %u, min ever %lu", when, esp_get_free_heap_size(),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), esp_get_minimum_free_heap_size());
}

/* the menus: portrait panel, cube palette, music if there is any */
static void menu_mode(void)
{
    ui_layout(UI_LAYOUT_MENU);
    ui_palette_cube();
    music_start(MUSIC_TRACK);
}

/* splash.c's hooks */
bool splash_skip_requested(void) { return any_button(); }
const uint8_t *splash_cover(const char *sn, int *w, int *h)
{
    int i = rom_find_short(sn);
    return rom_art(i, w, h);
}

/* ---- controller screen ---- */
static void controller_screen(void)
{
    int sel = 0;
    int64_t last = esp_timer_get_time();
    menu_mode();
    pad_edges();
    for (int frame = 0; ; frame++) {
        ble_pad_state_t st = ble_pad_state();
        bool connected = st == PAD_CONNECTED;
        uint32_t e = pad_edges(), mev = medal_events();
        if (e || mev) last = esp_timer_get_time();
        if (mev & (BTN_PWR_SHORT | BTN_BOOT_SHORT)) return;
        if (e & PAD_UP) sel = 0;
        if (e & PAD_DOWN) sel = 1;
        if ((e & PAD_A) && sel == 1) { pad_forget(); sel = 0; toast("Controller forgotten", "pair one now"); }
        if (((e & PAD_A) && sel == 0) || (e & (PAD_B | PAD_MENU))) return;
        if (esp_timer_get_time() - last > 60000000) return;

        ui_clear(UI_BLACK);
        festive_confetti(frame);
        festive_papel_picado(frame);
        ui_text_centred_scaled(ui_w / 2, 44, "CONTROLLER", UI_YELLOW, 2);
        const char *s = "Idle"; uint8_t c = UI_GREY;
        if (st == PAD_SCANNING) { s = "Scanning..."; c = UI_WHITE; }
        if (st == PAD_CONNECTING) { s = "Connecting..."; c = UI_YELLOW; }
        if (connected) { s = "Connected"; c = UI_GREEN; }
        ui_text(32, 76, "Status:", UI_GREY); ui_text(104, 76, s, c);
        ui_text(32, 88, "Found:", UI_GREY);  ui_text(104, 88, ble_pad_name()[0] ? ble_pad_name() : "-", UI_WHITE);
        ui_text(32, 100, "Saved:", UI_GREY); ui_text(104, 100, ble_pad_has_saved() ? "yes" : "no", UI_WHITE);
        festive_dancers(frame, 186);
        if (!connected) {
            ui_text_center(200, "Pairing mode on the pad,", UI_WHITE);
            ui_text_center(212, "hold it against the medal", UI_WHITE);
            ui_text_center(232, "Xbox pads (Bluetooth LE)", UI_GREY);
        } else {
            ui_text(56, 200, sel == 0 ? ">" : " ", UI_YELLOW); ui_text(72, 200, "Back", sel == 0 ? UI_YELLOW : UI_WHITE);
            ui_text(56, 214, sel == 1 ? ">" : " ", UI_YELLOW); ui_text(72, 214, "Forget this controller", sel == 1 ? UI_YELLOW : UI_WHITE);
        }
        ui_text_center(256, "B BACK", UI_GREY);
        music_tick_hook((frame & 1) ? NULL : ui_line_push);   /* 30 fps under the music */
    }
}

/* ---- in-game menu ---- */
enum { MENU_RESUME, MENU_SAVE, MENU_LOAD, MENU_RESET, MENU_MUTE, MENU_WHEEL, MENU_CONTROLLER, MENU_COUNT };
static int game_menu(const char *rom)
{
    const char *items[MENU_COUNT] = { "Resume", "Save state", "Load state", "Reset game", muted ? "Sound: muted" : quiet ? "Sound: quiet" : "Sound: full",
                                      "Back to the wheel", "Controller" };
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
    display_push_strip(bmp + y0 * FB_PITCH + FB_XOFF + ui_crop(), FB_PITCH, y0, ui_pal);
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

/* the show's card before each game: its screenshot behind its cover; silent */
static bool demo_card(int idx)
{
    char name[29]; rom_short_name(idx, name, sizeof name);
    int aw, ah;
    const uint8_t *art = rom_art(idx, &aw, &ah), *snap = rom_snap(idx);
    ui_layout(UI_LAYOUT_MENU); ui_palette_cube();
    any_button();
    for (int t = 0; t < 120; t++) {
        if (snap) ui_snap(snap); else { ui_clear(UI_BLACK); ui_stars(t); }
        int h = 150, w = aw * h / ah;
        if (art) ui_bitmap(ui_w / 2 - w / 2, 36, art, aw, ah, h, ah); else ui_fill(ui_w / 2 - w / 2, 36, w, h, UI_GREY);
        ui_fill(0, 198, ui_w, 82, UI_BLACK);
        ui_text_center(206, name, UI_WHITE);
        ui_text_center(220, demo_lock == idx ? "DEMO (LOCKED)" : "DEMO", UI_GREY);
        ui_text_center(248, "PRESS A BUTTON TO PLAY", (t & 32) ? UI_YELLOW : UI_GREY);
        music_tick_hook((t & 1) ? NULL : ui_line_push);
        if (any_button()) return true;
    }
    return false;
}

typedef enum { GAME_WHEEL, GAME_IDLE, GAME_DEMO_NEXT, GAME_DEMO_EXIT } game_result_t;

static game_result_t run_game_inner(int idx, bool demo);

/* demo: no input, DEMO_SECONDS limit unless locked, saves untouched */
static game_result_t run_game(int idx, bool demo)
{
    music_stop();
    if (demo && demo_card(idx)) return GAME_DEMO_EXIT;
    ui_layout(UI_LAYOUT_NES);
    game_result_t r = run_game_inner(idx, demo);
    core_unload();   /* hand the core back to the menu music */
    return r;
}

static game_result_t run_game_inner(int idx, bool demo)
{
    const char *rom = rom_get(idx)->name;
    nes_t *nes = nes_getptr();
    if (!core_load(rom_data(idx), rom_get(idx)->size)) {
        ui_clear(UI_BLACK); ui_text_center(112, "Unsupported ROM", UI_RED); ui_present();
        vTaskDelay(pdMS_TO_TICKS(1500));
        return demo ? GAME_DEMO_NEXT : GAME_WHEEL;
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
        if (demo && (mev & BTN_BOOT_HOLD5)) return GAME_DEMO_EXIT;
        if (mev & BTN_BOOT_SHORT) { toggle_lock(idx); build_palette(4); underruns0 = audio_get_underrun_count(); }
        if (demo) {
            if (pad_edges()) { ESP_LOGI(TAG, "demo: button pressed, back to the wheel"); return GAME_DEMO_EXIT; }
            if (mev & BTN_PWR_SHORT) return GAME_DEMO_NEXT;
            if (demo_lock != idx && f0 - t_start > (int64_t)DEMO_SECONDS * 1000000) return GAME_DEMO_NEXT;
        } else {
            b = pad_now();
            if (b != prev) last_input = f0;
            if (f0 - last_input > IDLE_AFTER_US) { sram_flush(nes, rom, true); return GAME_IDLE; }
            if (mev & (BTN_PWR_SHORT | BTN_BOOT_HOLD5)) { sram_flush(nes, rom, true); return GAME_WHEEL; }
        }
        if (!demo && (b & PAD_MENU) && !(prev & PAD_MENU)) {
            sram_flush(nes, rom, true);
            int a = game_menu(rom);
            if (a == MENU_SAVE) saves_save_state(rom);
            if (a == MENU_LOAD) saves_load_state(rom);
            if (a == MENU_RESET) nes_reset(true);
            if (a == MENU_MUTE) volume_cycle();
            if (a == MENU_CONTROLLER) { controller_screen(); ui_layout(UI_LAYOUT_NES); build_palette(4); }   /* music_start is a no-op: core busy */
            if (a == MENU_WHEEL) return GAME_WHEEL;
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

/* ---- the show: attract scenes, then every game (or the locked one) until a button ---- */
static void show(void)
{
    serial_demo = false;
    display_set_backlight(BACKLIGHT_DEMO);
    ble_pad_scan_rate(false);   /* unattended: the radio listens 3 % of the time */
    for (;;) {
        menu_mode();
        if (attract_title() || attract_howto() || wheel_showcase(SHOWCASE_SECONDS) || attract_credits()) break;
        ui_layout(UI_LAYOUT_NES);
        if (splash_run()) break;
        if (!roms_count()) continue;
        int first = demo_next(roms_count() - 1), i = demo_lock >= 0 ? demo_lock : first;
        bool out = false;
        do {
            out = run_game(i, true) == GAME_DEMO_EXIT;
            i = demo_next(i);
            if (demo_lock >= 0) demo_set_lock(i);   /* PWR "next" while locked moves the lock along */
        } while (!out && (demo_lock >= 0 || i != first));
        if (out) break;
    }
    display_set_backlight(BACKLIGHT_PLAY);
    ble_pad_scan_rate(true);
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
    settings_load();
    ui_set_portrait_games(portrait);
    log_heap("after display+audio+BLE");
    ESP_LOGI(TAG, "reset reason %d (1 power-on, 3 software, 6 task wdt, 7 int wdt, 8 deep sleep, 9 brownout)", esp_reset_reason());

    display_set_backlight(BACKLIGHT_PLAY);
    menu_mode();
    attract_title();
    wheel_init(roms_count());
    for (;;) {
        menu_mode();
        int game = 0;
        switch (wheel_run(&game)) {
        case WHEEL_PLAY:
            if (run_game(game, false) == GAME_IDLE) show();
            break;
        case WHEEL_CREDITS: attract_credits(); break;
        case WHEEL_CONTROLLER: controller_screen(); break;
        case WHEEL_IDLE: show(); break;
        }
    }
}
