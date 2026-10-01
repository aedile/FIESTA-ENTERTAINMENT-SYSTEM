#include "roms.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "ROMS";
static const uint8_t *base;
static const rom_entry_t *tab;
static int count;

void roms_init(void)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "roms");
    assert(p);
    /* Map only what the image uses: the C6's mappable flash window is a few MB and the app
     * already takes part of it, so mapping a whole big partition fails. Read the table first. */
    uint8_t head[8];
    ESP_ERROR_CHECK(esp_partition_read(p, 0, head, sizeof head));
    size_t used = 0;
    if (memcmp(head, "NESR", 4) == 0) {
        uint32_t n; memcpy(&n, head + 4, 4);
        rom_entry_t *t = malloc(n * sizeof *t);
        ESP_ERROR_CHECK(esp_partition_read(p, 8, t, n * sizeof *t));
        for (uint32_t i = 0; i < n; i++) {
            size_t e = t[i].off + t[i].size;
            if (t[i].art_off) { size_t a = t[i].art_off + (size_t)t[i].art_w * t[i].art_h; if (a > e) e = a; }
            if (t[i].snap_off) { size_t a = t[i].snap_off + 56 * 3 + 280 * 4 + 240 * 280; if (a > e) e = a; }   /* generous: RLE is smaller than raw */
            if (e > used) used = e;
        }
        free(t);
    }
    if (used == 0 || used > p->size) used = p->size;
    esp_partition_mmap_handle_t h;
    const void *ptr;
    ESP_ERROR_CHECK(esp_partition_mmap(p, 0, used, ESP_PARTITION_MMAP_DATA, &ptr, &h));
    base = ptr;
    if (memcmp(base, "NESR", 4) == 0) { memcpy(&count, base + 4, 4); tab = (const rom_entry_t *)(base + 8); }
    ESP_LOGI(TAG, "roms image %u bytes of a %lu byte partition at %p: %d ROMs", (unsigned)used, p->size, ptr, count);
    for (int i = 0; i < count; i++)
        ESP_LOGI(TAG, "  [%d] %-40s %6lu bytes mapper %d art %ux%u snap %s", i, tab[i].name, tab[i].size, rom_mapper(i), tab[i].art_w, tab[i].art_h, tab[i].snap_off ? "yes" : "no");
}

int roms_count(void) { return count; }
const rom_entry_t *rom_get(int i) { return i >= 0 && i < count ? &tab[i] : NULL; }
const uint8_t *rom_data(int i) { return base + tab[i].off; }
const uint8_t *rom_art(int i, int *w, int *h)
{
    if (i < 0 || i >= count || !tab[i].art_off) { *w = 96; *h = 134; return NULL; }
    *w = tab[i].art_w; *h = tab[i].art_h; return base + tab[i].art_off;
}
const uint8_t *rom_snap(int i) { return i >= 0 && i < count && tab[i].snap_off ? base + tab[i].snap_off : NULL; }
int rom_mapper(int i) { const uint8_t *d = rom_data(i); return (d[6] >> 4) | (d[7] & 0xF0); }

void rom_short_name(int i, char *out, size_t n)
{
    const char *in = tab[i].name, *p = strstr(in, " (");
    size_t len = p ? (size_t)(p - in) : strlen(in);
    if (len >= n) len = n - 1;
    memcpy(out, in, len); out[len] = 0;
}

int rom_find_short(const char *sn)
{
    for (int i = 0; i < count; i++) { char b[48]; rom_short_name(i, b, sizeof b); if (strcmp(b, sn) == 0) return i; }
    return -1;
}
