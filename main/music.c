#include "music.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_hal.h"
#include "core.h"
#include "nes/nes.h"

static const char *TAG = "MUSIC";
static bool playing;

#ifdef HAVE_MENU_NSF
extern const uint8_t nsf_start[] asm("_binary_menu_nsf_start");
extern const uint8_t nsf_end[] asm("_binary_menu_nsf_end");
void nsf_play_song(int song);   /* mappers/map031.c */
extern uint32_t nsf_play_calls;
#endif

void music_start(int track)
{
#ifdef HAVE_MENU_NSF
    if (playing) return;
    if (core_loaded()) return;   /* a game is loaded (in-game menu / controller screen from a game) */
    if (!core_load(nsf_start, nsf_end - nsf_start)) { ESP_LOGE(TAG, "NSF load failed"); return; }
    nsf_play_song(track);
    playing = true;
    ESP_LOGI(TAG, "playing track %d", track);
#endif
}

void music_tick(void)
{
    if (!playing) { vTaskDelay(pdMS_TO_TICKS(16)); return; }
    nes_t *nes = nes_getptr();
    int64_t t0 = esp_timer_get_time();
    nes_emulate(false);
    int64_t t1 = esp_timer_get_time();
    audio_submit(nes->apu->buffer, nes->apu->samples_per_frame);
    static int64_t report, emu_us; static int frames; static uint32_t underruns0;
    emu_us += t1 - t0; frames++;
    if (t1 - report > 5000000) {
        static uint32_t calls0;
        if (report) ESP_LOGI(TAG, "%d frames, %lu play calls in %lld ms: %lld us/frame emulation, underruns %lu, heap %lu", frames,
                             nsf_play_calls - calls0, (t1 - report) / 1000, emu_us / frames,
                             audio_get_underrun_count() - underruns0, esp_get_free_heap_size());
        report = t1; frames = 0; emu_us = 0; underruns0 = audio_get_underrun_count(); calls0 = nsf_play_calls;
    }
}

void music_stop(void)
{
    if (!playing) return;
    playing = false;
    core_unload();
}

bool music_playing(void) { return playing; }
