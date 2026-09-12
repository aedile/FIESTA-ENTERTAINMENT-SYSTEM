/*
 * NESTOR - NES emulator for the Waveshare ESP32-C6-LCD-1.69
 *
 * M4: nofrendo core running the first mapper-0 ROM from the roms partition,
 * PRG/CHR executed straight out of memory-mapped flash. Video, pad and APU audio
 * through PELLETINO's ES8311/I2S path; the I2S DMA queue paces emulation.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "audio_hal.h"
#include "ble_pad.h"
#include "ui.h"
#include "nes/nes.h"
#include "palettes.h"

static const char *TAG = "NESTOR";

#define PIN_BAT_EN   GPIO_NUM_15   /* the medal's battery rail: hold it up or we brown out */
#define PIN_BTN_BOOT GPIO_NUM_9    /* active low */

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

static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: free %lu, largest block %u, min ever %lu", when, esp_get_free_heap_size(),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), esp_get_minimum_free_heap_size());
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

void app_main(void)
{
    gpio_config_t bat = { .pin_bit_mask = 1ULL << PIN_BAT_EN, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bat);
    gpio_set_level(PIN_BAT_EN, 1);
    gpio_config_t btn = { .pin_bit_mask = 1ULL << PIN_BTN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = 1 };
    gpio_config(&btn);

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
    ble_pad_scan_any(true);
    roms_init();
    log_heap("after display+BLE");

    /* M3: first mapper-0 ROM in the partition (or build with -DROM_PICK=n to test another) */
    int pick = -1;
#ifdef ROM_PICK
    pick = ROM_PICK;
#endif
    for (int i = 0; i < nroms && pick < 0; i++)
        if (((roms_base[roms[i].off + 6] >> 4) | (roms_base[roms[i].off + 7] & 0xF0)) == 0) pick = i;
    if (pick < 0) { ESP_LOGE(TAG, "no mapper-0 ROM in partition"); return; }

    nes_t *nes = nes_init(SYS_NES_NTSC, AUDIO_SAMPLE_RATE, false, NULL);
    assert(nes);
    int rc = nes_insertcart(rom_loadmem((uint8 *)roms_base + roms[pick].off, roms[pick].size));
    if (rc < 0) { ESP_LOGE(TAG, "nes_insertcart failed: %d", rc); return; }
    nes->strip_func = push_strip;
    nes_setvidbuf(ui_fb);
    build_palette(4);   /* palettes.h index 4 = PVM, retro-go default */
    ESP_LOGI(TAG, "running %s", roms[pick].name);
    log_heap("with cart loaded");

    int frames = 0, skipped = 0;
    bool draw = true;
    int64_t t_report = esp_timer_get_time(), emu_us = 0, wait_us = 0;
    for (;;) {
        int64_t f0 = esp_timer_get_time();
        input_update(0, nes_buttons(ble_pad_buttons()));
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
        if (f1 - t_report >= 1000000) {
            ESP_LOGI(TAG, "%d fps (%d skipped) | per frame: emu %lld us, push %lld us, audio wait %lld us | underruns %lu | heap %lu",
                     frames, skipped, emu_us / frames, push_us / frames, wait_us / frames, audio_get_underrun_count(),
                     esp_get_free_heap_size());
            frames = skipped = 0; emu_us = push_us = wait_us = 0; t_report = f1;
        }
    }
}
