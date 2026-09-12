#include "saves.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "nes/nes.h"

static const char *TAG = "SAVES";
#define PART "saves"
#define NS   "nestor"
#define STATE_MAX (40 * 1024)   /* SNSS: 2K RAM + 4K nametables + OAM + 8K CHR RAM + 8K SRAM + headers */

void saves_init(void)
{
    esp_err_t r = nvs_flash_init_partition(PART);
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase_partition(PART);
        r = nvs_flash_init_partition(PART);
    }
    ESP_ERROR_CHECK(r);
}

/* NVS keys are 15 chars max: one type letter + crc32 of the ROM name */
static void key(char out[16], char type, const char *rom)
{
    snprintf(out, 16, "%c%08lx", type, (unsigned long)esp_rom_crc32_le(0, (const uint8_t *)rom, strlen(rom)));
}

static bool blob_size(char type, const char *rom, size_t *len)
{
    char k[16]; key(k, type, rom);
    nvs_handle_t h;
    if (nvs_open_from_partition(PART, NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_blob(h, k, NULL, len) == ESP_OK;
    nvs_close(h);
    return ok;
}

static bool blob_load(char type, const char *rom, void *buf, size_t len)
{
    char k[16]; key(k, type, rom);
    nvs_handle_t h;
    if (nvs_open_from_partition(PART, NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t got = len;
    bool ok = nvs_get_blob(h, k, buf, &got) == ESP_OK && got == len;
    nvs_close(h);
    return ok;
}

static bool blob_store(char type, const char *rom, const void *buf, size_t len)
{
    char k[16]; key(k, type, rom);
    nvs_handle_t h;
    if (nvs_open_from_partition(PART, NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t r = nvs_set_blob(h, k, buf, len);
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    if (r != ESP_OK) ESP_LOGE(TAG, "store %s: %s", k, esp_err_to_name(r));
    return r == ESP_OK;
}

bool saves_has_sram(const char *rom)  { size_t n; return blob_size('s', rom, &n); }
bool saves_has_state(const char *rom) { size_t n; return blob_size('t', rom, &n); }
bool saves_load_sram(const char *rom, uint8_t *buf, size_t len)        { return blob_load('s', rom, buf, len); }
bool saves_store_sram(const char *rom, const uint8_t *buf, size_t len) { return blob_store('s', rom, buf, len); }

bool saves_save_state(const char *rom)
{
    uint8_t *buf = malloc(STATE_MAX);
    if (!buf) return false;
    FILE *f = fmemopen(buf, STATE_MAX, "wb");
    bool ok = f && state_save(f) == 0;
    long len = 0;
    if (f) { fseek(f, 0, SEEK_END); len = ftell(f); }   /* state_save seeks back to patch the header */
    if (f) fclose(f);
    if (ok) ok = blob_store('t', rom, buf, len);
    ESP_LOGI(TAG, "state save %s: %ld bytes", ok ? "ok" : "FAILED", len);
    free(buf);
    return ok;
}

bool saves_load_state(const char *rom)
{
    size_t len;
    if (!blob_size('t', rom, &len)) return false;
    uint8_t *buf = malloc(len);
    if (!buf) return false;
    bool ok = blob_load('t', rom, buf, len);
    if (ok) {
        FILE *f = fmemopen(buf, len, "rb");
        ok = f && state_load(f) == 0;
        if (f) fclose(f);
    }
    ESP_LOGI(TAG, "state load %s: %u bytes", ok ? "ok" : "FAILED", (unsigned)len);
    free(buf);
    return ok;
}
