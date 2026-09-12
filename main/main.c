/*
 * NESTOR - NES emulator for the Waveshare ESP32-C6-LCD-1.69
 *
 * Boot -> controller screen (until a pad connects) -> ROM picker -> game.
 * No pad within 30 s -> demo mode: every ROM in turn, running its own attract
 * mode (DEMO_BOT=1 adds an input bot), until someone presses a pad button.
 * MENU (Y / shoulder) in a game: resume, save/load state, back to picker, controller screen.
 * PRG/CHR ROM are executed straight out of memory-mapped flash; battery RAM and
 * save states live in the 'saves' NVS partition.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_random.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "audio_hal.h"
#include "ble_pad.h"
#include "ui.h"
#include "saves.h"
#include "nes/nes.h"
#include "palettes.h"

static const char *TAG = "NESTOR";

#define PIN_BAT_EN   GPIO_NUM_15   /* the medal's battery rail: hold it up or we brown out */
#define PIN_BTN_BOOT GPIO_NUM_9    /* active low */
#define SRAM_SIZE    0x2000
#define DEMO_AFTER_US   30000000LL   /* no controller for this long -> demo mode */
#ifndef DEMO_SECONDS
#define DEMO_SECONDS    30           /* per ROM in demo mode (override: idf.py -DDEMO_SECONDS=300) */
#endif
#define DEMO_BOT        0            /* 1: demo_bot() feeds input; 0: games run their own attract modes */

/* ---- roms partition: image written by tools/pack_roms.py ---- */
typedef struct __attribute__((packed)) { char name[48]; uint32_t off, size; } rom_entry_t;
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
        ESP_LOGI(TAG, "  [%d] %-44s %6lu bytes mapper %d", i, roms[i].name, roms[i].size,
                 (roms_base[roms[i].off + 6] >> 4) | (roms_base[roms[i].off + 7] & 0xF0));
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

/* ---- input: the BLE pad, plus keys typed into the serial monitor for bench testing
 * (w/a/s/d = d-pad, j = A, k = B, q = start, e = select, m = menu) ---- */
static int64_t serial_last = -10000000;
static bool serial_demo;
static bool serial_active(void) { return esp_timer_get_time() - serial_last < 5000000; }   /* bench driving: screens accept keys as if a pad were connected */

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
        if (c == 'x') serial_demo = true;   /* bench: jump straight into demo mode from the picker */
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

/* newly pressed bits, with key repeat on up/down for lists */
static uint32_t pad_edges(void)
{
    static uint32_t prev;
    static int64_t repeat_at;
    int64_t now = esp_timer_get_time();
    uint32_t cur = pad_now(), e = cur & ~prev;
    if (cur & (PAD_UP | PAD_DOWN)) {
        if (e & (PAD_UP | PAD_DOWN)) repeat_at = now + 400000;
        else if (now > repeat_at) { e |= cur & (PAD_UP | PAD_DOWN); repeat_at = now + 100000; }
    }
    prev = cur;
    return e;
}

static bool boot_button_edge(void)
{
    static bool was;
    bool down = gpio_get_level(PIN_BTN_BOOT) == 0;
    bool e = down && !was;
    was = down;
    return e;
}

/* ---- controller screen. Returns false if nothing connected for DEMO_AFTER_US. ---- */
static bool controller_screen(bool boot)
{
    int64_t deadline = esp_timer_get_time() + ((boot && ble_pad_has_saved()) ? 5000000 : 0);
    int64_t demo_at = esp_timer_get_time() + DEMO_AFTER_US;
    bool any = false;
    int sel = 0;
    ble_pad_state_t shown = -1;
    int64_t last_draw = 0;
    pad_edges();   /* swallow the press that brought us here */
    for (;;) {
        ble_pad_state_t st = ble_pad_state();
        bool connected = st == PAD_CONNECTED;
        if (!any && !connected && esp_timer_get_time() > deadline) { any = true; ble_pad_scan_any(true); }
        if (connected && any) { any = false; ble_pad_scan_any(false); }   /* reconnects go to this pad only */
        if (boot_button_edge()) { ble_pad_forget(); any = true; ble_pad_scan_any(true); }

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
            char adv[32]; snprintf(adv, sizeof adv, "%lu adverts seen", ble_pad_adv_seen());
            ui_text(24, 100, adv, UI_GREY);
            if (!connected) {
                ui_text_center(136, "Put the controller in", UI_WHITE);
                ui_text_center(148, "pairing mode", UI_WHITE);
            } else {
                ui_text(40, 136, sel == 0 ? ">" : " ", UI_YELLOW); ui_text(56, 136, "Back", sel == 0 ? UI_YELLOW : UI_WHITE);
                ui_text(40, 152, sel == 1 ? ">" : " ", UI_YELLOW); ui_text(56, 152, "Forget this controller", sel == 1 ? UI_YELLOW : UI_WHITE);
            }
            if (!connected) {
                char d[32]; snprintf(d, sizeof d, "demo mode in %d s", (int)((demo_at - esp_timer_get_time()) / 1000000));
                ui_text_center(176, d, UI_GREY);
            }
            ui_text_center(216, "BOOT button: forget saved", UI_GREY);
            ui_present();
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

/* ---- ROM picker ---- */
#define PICK_ROWS 12
static int picker(int sel)
{
    bool has_save[64];
    for (int i = 0; i < nroms && i < 64; i++) has_save[i] = saves_has_sram(roms[i].name);
    int top = 0;
    bool dirty = true;
    pad_edges();
    for (;;) {
        uint32_t e = pad_edges();
        if ((e & PAD_UP) && sel > 0) { sel--; dirty = true; }
        if ((e & PAD_DOWN) && sel < nroms - 1) { sel++; dirty = true; }
        if (e & PAD_A) return sel;
        if (serial_demo) { serial_demo = false; return -1; }
        if (e & PAD_MENU) { controller_screen(false); dirty = true; }
        if (ble_pad_state() != PAD_CONNECTED && !serial_active()) { if (!controller_screen(false)) return -1; dirty = true; }
        if (dirty) {
            dirty = false;
            if (sel < top) top = sel;
            if (sel >= top + PICK_ROWS) top = sel - PICK_ROWS + 1;
            ui_clear(UI_BLACK);
            ui_text(16, 8, "NESTOR", UI_YELLOW);
            ui_text(160, 8, "* = battery save", UI_GREY);
            for (int r = 0; r < PICK_ROWS && top + r < nroms; r++) {
                int i = top + r, y = 32 + r * 16;
                char name[29]; short_name(roms[i].name, name, sizeof name);
                ui_text(8, y, i == sel ? ">" : " ", UI_YELLOW);
                ui_text(24, y, name, i == sel ? UI_YELLOW : UI_WHITE);
                if (has_save[i]) ui_text(240, y, "*", UI_GREEN);
            }
            if (nroms == 0) ui_text_center(100, "No ROMs in partition", UI_RED);
            ui_text_center(228, "A: play   MENU: controller", UI_GREY);
            ui_present();
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

/* ---- in-game menu ---- */
enum { MENU_RESUME, MENU_SAVE, MENU_LOAD, MENU_PICKER, MENU_CONTROLLER, MENU_COUNT };
static int game_menu(const char *rom)
{
    static const char *items[MENU_COUNT] = { "Resume", "Save state", "Load state", "Return to picker", "Controller" };
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
            ui_fill(56, 56, 144, 120, UI_BLACK);
            ui_fill(56, 56, 144, 2, UI_WHITE); ui_fill(56, 174, 144, 2, UI_WHITE);
            ui_fill(56, 56, 2, 120, UI_WHITE); ui_fill(198, 56, 2, 120, UI_WHITE);
            ui_text(72, 68, "MENU", UI_YELLOW);
            for (int i = 0; i < MENU_COUNT; i++) {
                uint8_t c = i == sel ? UI_YELLOW : UI_WHITE;
                if (i == MENU_LOAD && !have_state) c = UI_GREY;
                ui_text(72, 92 + i * 14, i == sel ? ">" : " ", UI_YELLOW);
                ui_text(88, 92 + i * 14, items[i], c);
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

/* ---- demo bot: gets past title screens, then wanders with a bias to the right and taps
 * A/B. Not smart, but platformers run and jump, racers steer, menus advance. ---- */
static uint32_t demo_bot(int64_t t_us)
{
    static int64_t next_change, a_until, b_until;
    static uint32_t held;
    uint32_t out = 0;
    int64_t sec = t_us / 1000000;
    if (sec < 20 && (t_us % 5000000) < 150000) out |= PAD_START;   /* tap Start at 0,5,10,15 s */
    if (t_us > next_change) {
        int r = esp_random() % 100;
        held = r < 45 ? PAD_RIGHT : r < 60 ? PAD_LEFT : r < 70 ? PAD_UP : r < 80 ? PAD_DOWN : 0;
        next_change = t_us + 300000 + esp_random() % 900000;
    }
    if (esp_random() % 100 < 2) a_until = t_us + 150000;
    if (esp_random() % 100 < 1) b_until = t_us + 100000;
    out |= held;
    if (t_us < a_until) out |= PAD_A;
    if (t_us < b_until) out |= PAD_B;
    return out;
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

/* demo: bot input, DEMO_SECONDS time limit, saves untouched; returns true if a pad button was pressed */
static bool run_game(int idx, bool demo)
{
    const char *rom = roms[idx].name;
    static nes_t *nes;
    if (demo) {
        char name[29]; short_name(rom, name, sizeof name);
        ui_clear(UI_BLACK);
        ui_text_center(100, name, UI_YELLOW);
        ui_text_center(124, "demo - press any button to play", UI_GREY);
        ui_present();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if (nes) nes_shutdown();
    nes = nes_init(SYS_NES_NTSC, AUDIO_SAMPLE_RATE, false, NULL);
    assert(nes);
    int rc = nes_insertcart(rom_loadmem((uint8 *)roms_base + roms[idx].off, roms[idx].size));
    if (rc < 0) {
        ESP_LOGE(TAG, "nes_insertcart failed: %d", rc);
        ui_clear(UI_BLACK); ui_text_center(112, "Unsupported ROM", UI_RED); ui_present();
        vTaskDelay(pdMS_TO_TICKS(1500));
        return false;
    }
    nes->strip_func = push_strip;
    nes_setvidbuf(ui_fb);
    build_palette(4);   /* palettes.h index 4 = PVM, retro-go default */
    if (nes->cart->battery && !demo) {
        if (saves_load_sram(rom, nes->cart->prg_ram, SRAM_SIZE)) ESP_LOGI(TAG, "battery RAM loaded");
        sram_saved_crc = sram_last_crc = esp_rom_crc32_le(0, nes->cart->prg_ram, SRAM_SIZE);
    }
    ESP_LOGI(TAG, "running %s%s", rom, demo ? " (demo)" : "");
    log_heap("with cart loaded");

    int frames = 0, skipped = 0;
    bool draw = true;
    int64_t t_report = esp_timer_get_time(), emu_us = 0, wait_us = 0;
    uint32_t prev = 0, underruns0 = audio_get_underrun_count();
    nes_prof_cpu = nes_prof_ppu = nes_prof_apu = 0; display_wait_us = 0; push_us = 0;
    int64_t t_start = esp_timer_get_time();
    pad_edges();
    for (;;) {
        int64_t f0 = esp_timer_get_time();
        uint32_t b;
        if (demo) {
            if (pad_edges()) { ESP_LOGI(TAG, "demo: button pressed, back to the picker"); return true; }
            if (f0 - t_start > (int64_t)DEMO_SECONDS * 1000000) return false;
            b = DEMO_BOT ? demo_bot(f0 - t_start) : 0;
        } else {
            b = pad_now();
        }
        if (!demo && (b & PAD_MENU) && !(prev & PAD_MENU)) {
            sram_flush(nes, rom, true);
            int a = game_menu(rom);
            if (a == MENU_SAVE) saves_save_state(rom);
            if (a == MENU_LOAD) saves_load_state(rom);
            if (a == MENU_CONTROLLER) controller_screen(false);
            if (a == MENU_PICKER) return false;
            prev = pad_now();
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

void app_main(void)
{
    gpio_config_t bat = { .pin_bit_mask = 1ULL << PIN_BAT_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bat);
    gpio_set_level(PIN_BAT_EN, 1);
    gpio_config_t btn = { .pin_bit_mask = 1ULL << PIN_BTN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = 1 };
    gpio_config(&btn);
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
    log_heap("after display+audio+BLE");

    bool have_pad = controller_screen(true);
    int sel = 0;
    for (;;) {
        if (!have_pad || sel < 0) {
            /* demo mode: every ROM in turn until someone presses a button */
            bool pressed = false;
            serial_demo = false;
            for (int i = 0; nroms && !pressed; i = (i + 1) % nroms) pressed = run_game(i, true);
            have_pad = true; sel = 0;
        }
        sel = picker(sel);
        if (sel >= 0) run_game(sel, false);
    }
}
