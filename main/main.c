/*
 * NESTOR - NES emulator for the Waveshare ESP32-C6-LCD-1.69
 *
 * M2: BLE gamepad pairing. Controller sync screen, button states on serial,
 * saved controller reconnects on boot.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "ble_pad.h"
#include "ui.h"

static const char *TAG = "NESTOR";

#define PIN_BAT_EN   GPIO_NUM_15   /* the medal's battery rail: hold it up or we brown out */
#define PIN_BTN_BOOT GPIO_NUM_9    /* active low */

static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: free %lu, largest block %u, min ever %lu", when, esp_get_free_heap_size(),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), esp_get_minimum_free_heap_size());
}

static void draw_sync_screen(void)
{
    ui_clear(UI_BLACK);
    ui_text_center(24, "CONTROLLER", UI_YELLOW);
    const char *st = "";
    uint8_t c = UI_GREY;
    switch (ble_pad_state()) {
    case PAD_IDLE:       st = "Idle"; break;
    case PAD_SCANNING:   st = "Scanning..."; c = UI_WHITE; break;
    case PAD_CONNECTING: st = "Connecting..."; c = UI_YELLOW; break;
    case PAD_CONNECTED:  st = "Connected"; c = UI_GREEN; break;
    }
    ui_text(16, 64, "Status:", UI_GREY);
    ui_text(88, 64, st, c);
    ui_text(16, 80, "Found:", UI_GREY);
    ui_text(88, 80, ble_pad_name()[0] ? ble_pad_name() : "-", UI_WHITE);
    ui_text(16, 96, "Saved:", UI_GREY);
    ui_text(88, 96, ble_pad_has_saved() ? "yes" : "no", UI_WHITE);
    char adv[32];
    snprintf(adv, sizeof adv, "%lu adverts seen", ble_pad_adv_seen());
    ui_text(16, 112, adv, UI_GREY);
    if (ble_pad_state() != PAD_CONNECTED) {
        ui_text_center(144, "Put the controller in", UI_WHITE);
        ui_text_center(156, "pairing mode", UI_WHITE);
    } else {
        ui_text_center(144, "Press buttons - see serial", UI_WHITE);
    }
    ui_text_center(208, "BOOT button: forget saved", UI_GREY);
    ui_present();
}

void app_main(void)
{
    gpio_config_t bat = { .pin_bit_mask = 1ULL << PIN_BAT_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bat);
    gpio_set_level(PIN_BAT_EN, 1);
    gpio_config_t btn = { .pin_bit_mask = 1ULL << PIN_BTN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = 1 };
    gpio_config(&btn);

    log_heap("at boot");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    display_init();
    ui_init();
    log_heap("after display+fb");
    ble_pad_init();
    vTaskDelay(pdMS_TO_TICKS(500));
    log_heap("after BLE up");

    /* Give a saved controller a few seconds to come back before opening pairing to anyone. */
    int64_t deadline = esp_timer_get_time() + (ble_pad_has_saved() ? 5000000 : 0);
    bool any = false, boot_was_down = false;
    uint32_t last_buttons = 0, last_raw = 0;
    for (;;) {
        if (!any && ble_pad_state() != PAD_CONNECTED && esp_timer_get_time() > deadline) {
            any = true;
            ble_pad_scan_any(true);
        }
        bool boot_down = gpio_get_level(PIN_BTN_BOOT) == 0;
        if (boot_down && !boot_was_down) { ble_pad_forget(); any = true; ble_pad_scan_any(true); }
        boot_was_down = boot_down;

        uint32_t b = ble_pad_buttons(), raw = ble_pad_raw();
        if (b != last_buttons || raw != last_raw) {
            ESP_LOGI(TAG, "PAD raw=%04lx  %s%s%s%s%s%s%s%s%s", raw,
                     b & PAD_UP ? "UP " : "", b & PAD_DOWN ? "DOWN " : "", b & PAD_LEFT ? "LEFT " : "",
                     b & PAD_RIGHT ? "RIGHT " : "", b & PAD_A ? "A " : "", b & PAD_B ? "B " : "",
                     b & PAD_START ? "START " : "", b & PAD_SELECT ? "SELECT " : "", b & PAD_MENU ? "MENU " : "");
            last_buttons = b; last_raw = raw;
        }
        static ble_pad_state_t shown = -1;
        static int64_t last_draw;
        if (ble_pad_state() != shown || esp_timer_get_time() - last_draw > 500000) {
            if (ble_pad_state() != shown && ble_pad_state() == PAD_CONNECTED) log_heap("connected");
            shown = ble_pad_state();
            last_draw = esp_timer_get_time();
            draw_sync_screen();
            static uint32_t last_adv; static int tick;
            if (++tick % 10 == 0 && ble_pad_adv_seen() != last_adv) {
                last_adv = ble_pad_adv_seen();
                ESP_LOGI(TAG, "scanner: %lu adverts seen, state %d", last_adv, ble_pad_state());
            }
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
