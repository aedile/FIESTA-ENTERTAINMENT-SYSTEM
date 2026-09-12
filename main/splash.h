/* splash.h - boot splash: fireworks over the Tower of the Americas, with the menu music. */
#pragma once
void splash_run(void);   /* ~10 s, or until skip() says so; music must already be started */
#include <stdint.h>
/* provided by main.c: box art of the game whose short name matches, NULL if absent */
const uint8_t *splash_cover(const char *short_name, int *w, int *h);
