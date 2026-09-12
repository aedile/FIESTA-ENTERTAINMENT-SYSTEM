/*
 * NESTOR - NES emulator for the Waveshare ESP32-C6-LCD-1.69
 *
 * M1: bring the display up through PELLETINO's driver (rotated to landscape) and push a test pattern
 * through the real frame path: 8-bit indexed framebuffer -> RGB565 LUT -> DMA strips.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "font8x8.h"

static const char *TAG = "NESTOR";

/* nofrendo's frame layout: 8 px of overdraw either side of the 256 px line */
#define FB_PITCH  272
#define FB_LINES  240
#define FB_XOFF   8         /* skip the left overdraw; the full 256 px line is shown */

#define PIN_BAT_EN GPIO_NUM_15   /* the medal's battery rail: hold it up or we brown out */

static uint8_t *fb;
static uint16_t pal[256];

static uint16_t rgb565_swapped(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (c >> 8) | (c << 8);
}

static void draw_text(int x, int y, const char *s, uint8_t colour)
{
    for (; *s; s++, x += 8) {
        if (*s < 32 || *s > 126) continue;
        const uint8_t *g = font8x8[*s - 32];
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                if (g[r] & (0x80 >> c)) fb[(y + r) * FB_PITCH + FB_XOFF + x + c] = colour;
    }
}

static void draw_test_pattern(int frame)
{
    /* 8 colour bars across, a 0..255 gradient below, a moving marker so motion is visible */
    for (int y = 0; y < 120; y++)
        for (int x = 0; x < 256; x++)
            fb[y * FB_PITCH + FB_XOFF + x] = 1 + x / 32;
    for (int y = 120; y < 200; y++)
        for (int x = 0; x < 256; x++)
            fb[y * FB_PITCH + FB_XOFF + x] = 16 + x * 239 / 255;
    for (int y = 200; y < 240; y++)
        memset(&fb[y * FB_PITCH + FB_XOFF], 0, 256);
    int mx = (frame * 2) % 248;
    for (int y = 204; y < 212; y++)
        memset(&fb[y * FB_PITCH + FB_XOFF + mx], 9, 8);
    /* 1 px white frame so cropping errors show */
    for (int x = 0; x < 256; x++) fb[FB_XOFF + x] = fb[239 * FB_PITCH + FB_XOFF + x] = 9;
    for (int y = 0; y < 240; y++) fb[y * FB_PITCH + FB_XOFF] = fb[y * FB_PITCH + FB_XOFF + 255] = 9;
    char line[40];
    snprintf(line, sizeof line, "NESTOR M1  frame %d", frame);
    draw_text(8, 216, line, 9);
    draw_text(8, 228, "256x240 landscape, 8bpp->565 DMA", 10);
}

void app_main(void)
{
    gpio_config_t bat = { .pin_bit_mask = 1ULL << PIN_BAT_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bat);
    gpio_set_level(PIN_BAT_EN, 1);

    ESP_LOGI(TAG, "free heap at boot: %lu", esp_get_free_heap_size());
    display_init();

    fb = calloc(FB_PITCH * FB_LINES, 1);
    assert(fb);

    /* test palette: 0 black, 1..8 bars, 9 white, 10 yellow, 16..255 grey ramp */
    static const uint8_t bars[8][3] = {{255,255,255},{255,255,0},{0,255,255},{0,255,0},
                                       {255,0,255},{255,0,0},{0,0,255},{64,64,64}};
    for (int i = 0; i < 8; i++) pal[1 + i] = rgb565_swapped(bars[i][0], bars[i][1], bars[i][2]);
    pal[9] = rgb565_swapped(255, 255, 255);
    pal[10] = rgb565_swapped(255, 220, 0);
    for (int i = 16; i < 256; i++) { uint8_t v = (i - 16) * 255 / 239; pal[i] = rgb565_swapped(v, v, v); }

    ESP_LOGI(TAG, "free heap after display+fb: %lu", esp_get_free_heap_size());

    int frame = 0;
    int64_t t0 = esp_timer_get_time(), push_us = 0;
    for (;;) {
        draw_test_pattern(frame);
        int64_t a = esp_timer_get_time();
        display_push_indexed(fb + 0, FB_PITCH, pal);
        display_wait_done();
        push_us += esp_timer_get_time() - a;
        frame++;
        int64_t now = esp_timer_get_time();
        if (now - t0 >= 1000000) {
            ESP_LOGI(TAG, "%d fps, push %lld us/frame (SPI %d MHz)", frame, push_us / frame, LCD_SPI_CLOCK / 1000000);
            frame = 0; push_us = 0; t0 = now;
        }
        vTaskDelay(1);  /* let idle run; ~60 Hz cap is not the point here */
    }
}
