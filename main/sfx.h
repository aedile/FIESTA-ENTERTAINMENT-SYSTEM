/* sfx.h - the menus' sound effects, synthesized (ported from PELLETINO's chiptune/sfx.c). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef enum {
    SFX_SHING,   /* a blade drawn: the flash in the opening */
    SFX_CLICK,   /* one step of the wheel */
    SFX_COIN,    /* a game chosen */
    SFX_LOW,     /* the battery is running out */
} sfx_t;
void sfx_play(sfx_t which);
void sfx_tone(int hz);                              /* a held tone until told otherwise; 0 stops it */
void sfx_mix(int16_t *buf, int samples, int rate);  /* add whatever is sounding into buf, clipped */
bool sfx_active(void);
