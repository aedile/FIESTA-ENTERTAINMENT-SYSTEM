#include "input.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"
#include "driver/usb_serial_jtag.h"
#include "nvs.h"
#include "ui.h"
#include "music.h"
#include "audio_hal.h"
#include "roms.h"

static const char *TAG = "INPUT";
#define NVS_NS "nestor"

bool muted, quiet, portrait = true;
#define DAC_FULL 0xBF   /* ES8311: 0 dB */
#define DAC_QUIET 0x8F  /* -24 dB */
int demo_lock = -1;
bool demo_skip[64];
bool serial_demo;
static uint32_t serial_medal;
static int64_t serial_last = -10000000;

/* games left out of the demo cycle unless toggled back in with B on the wheel (matched by short name) */
static const char *const demo_skip_default[] = { "DuckTales", "Double Dragon", "Mega Man", "Final Fantasy", "Super Dodge Ball" };

/* ---- bench keys typed into the serial monitor: w/a/s/d d-pad, j A, k B, q Start, e Select,
 * m Menu; x demo now, n / l the PWR / BOOT buttons ---- */
bool serial_active(void) { return esp_timer_get_time() - serial_last < 5000000; }

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

uint32_t pad_now(void)
{
    uint32_t b = ble_pad_buttons(), raw = ble_pad_raw();
    static uint32_t last_b, last_raw;
    if (b != last_b || raw != last_raw) {
        ESP_LOGI(TAG, "PAD raw=%04lx %s%s%s%s%s%s%s%s%s", raw,
                 b & PAD_UP ? "UP " : "", b & PAD_DOWN ? "DOWN " : "", b & PAD_LEFT ? "LEFT " : "",
                 b & PAD_RIGHT ? "RIGHT " : "", b & PAD_A ? "A " : "", b & PAD_B ? "B " : "",
                 b & PAD_START ? "START " : "", b & PAD_SELECT ? "SELECT " : "", b & PAD_MENU ? "MENU " : "");
        last_b = b; last_raw = raw;
    }
    return b | serial_pad();
}

static uint32_t medal_pending;
static bool scan_any;   /* listening for any pad in pairing mode (else only the saved one) */

void pad_forget(void) { ble_pad_forget(); ble_pad_scan_any(true); scan_any = true; }

/* a saved pad gets 5 s after boot to come back before we listen for any pad; once one is
 * connected only it may reconnect */
static void pairing_policy(void)
{
    static int64_t grace = -1;
    if (grace < 0) grace = esp_timer_get_time() + (ble_pad_has_saved() ? 5000000 : 0);
    bool connected = ble_pad_state() == PAD_CONNECTED;
    if (!scan_any && !connected && esp_timer_get_time() > grace) { scan_any = true; ble_pad_scan_any(true); }
    if (scan_any && connected) { scan_any = false; ble_pad_scan_any(false); }
}

/* power off once a plausible reading (2.9-3.4 V: a mis-scaled ADC never switches the medal
 * off) sits under 3 % for a minute */
static void battery_watch(void)
{
    static int64_t low_since;
    int pct = medal_battery_percent(), mv = medal_battery_mv();
    if (pct < 3 && mv > 2900 && mv < 3400) {
        if (!low_since) low_since = esp_timer_get_time();
        if (esp_timer_get_time() - low_since > 60000000) { ESP_LOGW(TAG, "battery %d mV: powering off", mv); medal_power_off(); }
    } else low_since = 0;
}

/* the medal buttons' always-on jobs: PWR hold powers off (inside medal_poll), both buttons toggle
 * the sound, BOOT 10 s forgets the controller. Short presses are kept for the screen that wants them. */
static void medal_global(void)
{
    pairing_policy();
    battery_watch();
    uint32_t ev = medal_poll() | serial_medal;
    serial_medal = 0;
    if (ev & BTN_BOTH) { volume_cycle(); toast_volume(); }
    if (ev & BTN_BOOT_HOLD10) { pad_forget(); toast("Controller forgotten", "pad against the medal to pair"); }
    medal_pending |= ev & (BTN_BOOT_SHORT | BTN_PWR_SHORT);
}

uint32_t pad_edges(void)
{
    static uint32_t prev;
    static int64_t repeat_at;
    medal_global();
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

uint32_t medal_events(void)
{
    medal_global();
    uint32_t ev = medal_pending;
    medal_pending = 0;
    return ev;
}

bool any_button(void)
{
    uint32_t e = pad_edges();
    return e || (medal_events() & (BTN_BOOT_SHORT | BTN_PWR_SHORT));
}

/* ---- settings ---- */
static void nvs_key_for(char out[16], char type, const char *rom)
{
    snprintf(out, 16, "%c%08lx", type, (unsigned long)esp_rom_crc32_le(0, (const uint8_t *)rom, strlen(rom)));
}

void settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    char lock[48] = {0}; size_t n = sizeof lock;
    if (nvs_get_str(h, "demo_lock", lock, &n) == ESP_OK)
        for (int i = 0; i < roms_count(); i++) if (strcmp(rom_get(i)->name, lock) == 0) demo_lock = i;
    for (int i = 0; i < roms_count() && i < 64; i++) {
        char k[16]; uint8_t v = 0;
        nvs_key_for(k, 'd', rom_get(i)->name);
        if (nvs_get_u8(h, k, &v) == ESP_OK) demo_skip[i] = v;
        else {
            char sn[29]; rom_short_name(i, sn, sizeof sn);
            for (size_t d = 0; d < sizeof demo_skip_default / sizeof *demo_skip_default; d++)
                if (strcmp(sn, demo_skip_default[d]) == 0) demo_skip[i] = true;
        }
    }
    uint8_t m = 0;
    if (nvs_get_u8(h, "vol", &m) == ESP_OK) { quiet = m == 1; muted = m == 2; }
    audio_set_mute(muted);
    audio_set_volume(quiet ? DAC_QUIET : DAC_FULL);
    if (nvs_get_u8(h, "portrait", &m) == ESP_OK) portrait = m;
    nvs_close(h);
    ESP_LOGI(TAG, "demo lock: %s, %s, %s", demo_lock >= 0 ? rom_get(demo_lock)->name : "none", volume_name(), portrait ? "portrait" : "landscape");
}

static void nvs_put_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, key, v); nvs_commit(h); nvs_close(h); }
}

const char *volume_name(void) { return muted ? "Muted" : quiet ? "Quiet" : "Full volume"; }

void volume_cycle(void)
{
    int mode = (muted ? 2 : quiet ? 1 : 0) + 1;
    if (mode > 2) mode = 0;
    quiet = mode == 1; muted = mode == 2;
    audio_set_mute(muted);
    audio_set_volume(quiet ? DAC_QUIET : DAC_FULL);
    nvs_put_u8("vol", mode);
    ESP_LOGI(TAG, "%s", volume_name());
}
void set_portrait(bool p) { portrait = p; ui_set_portrait_games(p); nvs_put_u8("portrait", p); ESP_LOGI(TAG, "%s", p ? "portrait" : "landscape"); }

void demo_set_lock(int idx)
{
    demo_lock = idx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (idx >= 0) nvs_set_str(h, "demo_lock", rom_get(idx)->name); else nvs_erase_key(h, "demo_lock");
    nvs_commit(h); nvs_close(h);
}

void demo_set_skip(int idx, bool skip)
{
    demo_skip[idx] = skip;
    char k[16]; nvs_key_for(k, 'd', rom_get(idx)->name);
    nvs_put_u8(k, skip);
}

int demo_next(int i)
{
    int n = roms_count();
    for (int c = (i + 1) % n, tries = 0; tries < n; c = (c + 1) % n, tries++) if (!demo_skip[c]) return c;
    return (i + 1) % n;
}

/* ---- overlays ---- */
void toast(const char *line1, const char *line2)
{
    int w = 8 * (int)(strlen(line1) > strlen(line2) ? strlen(line1) : strlen(line2)) + 32;
    if (w > ui_w) w = ui_w;
    int x = (ui_w - w) / 2, y = ui_h / 2 - 24;
    ui_fill(x, y, w, 48, UI_BLACK);
    ui_frame(x, y, w, 48, UI_WHITE);
    ui_text_center(y + 12, line1, UI_YELLOW);
    ui_text_center(y + 28, line2, UI_WHITE);
    ui_present();
    for (int i = 0; i < 60; i++) music_tick();
}

void toast_volume(void) { toast(volume_name(), "both buttons, or SELECT"); }

void toggle_lock(int idx)
{
    char name[29]; rom_short_name(idx, name, sizeof name);
    if (demo_lock == idx) { demo_set_lock(-1); toast("Demo unlocked", "cycling all games"); }
    else { demo_set_lock(idx); toast("Demo locked on", name); }
}
