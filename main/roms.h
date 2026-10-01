/* roms.h - the packed ROM image in the 'roms' partition (tools/pack_roms.py), memory-mapped. */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct __attribute__((packed)) {
    char name[48];
    uint32_t off, size;
    uint32_t art_off;
    uint16_t art_w, art_h;
    uint32_t snap_off;
} rom_entry_t;
void roms_init(void);
int roms_count(void);
const rom_entry_t *rom_get(int i);
const uint8_t *rom_data(int i);                 /* the iNES file, in flash */
const uint8_t *rom_art(int i, int *w, int *h);  /* box art indices (cube palette), NULL if none */
const uint8_t *rom_snap(int i);                 /* packed screenshot for ui_snap(), NULL if none */
int rom_mapper(int i);
void rom_short_name(int i, char *out, size_t n); /* "Super Mario Bros. (Japan, USA)" -> "Super Mario Bros." */
int rom_find_short(const char *short_name);     /* index, or -1 */
