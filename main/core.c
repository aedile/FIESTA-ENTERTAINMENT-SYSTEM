#include "core.h"
#include "esp_log.h"
#include "audio_hal.h"
#include "nes/nes.h"

static const char *TAG = "CORE";
static bool loaded;

bool core_load(const uint8_t *data, size_t size)
{
    core_unload();
    nes_t *nes = nes_init(SYS_NES_NTSC, AUDIO_SAMPLE_RATE, false, NULL);
    assert(nes);
    loaded = true;
    int rc = nes_insertcart(rom_loadmem((uint8 *)data, size));
    if (rc < 0) { ESP_LOGE(TAG, "nes_insertcart failed: %d", rc); return false; }
    return true;
}

void core_unload(void)
{
    if (!loaded) return;
    loaded = false;
    nes_shutdown();
}

bool core_loaded(void) { return loaded; }
