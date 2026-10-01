/* splash.h - boot splash: fireworks over the Tower of the Americas, with the menu music. */
#pragma once
#include <stdbool.h>
bool splash_run(void);   /* ~20 s in the NES layout; true if a button cut it short */
#include <stdint.h>
/* provided by main.c: box art of the game whose short name matches, NULL if absent */
const uint8_t *splash_cover(const char *short_name, int *w, int *h);
