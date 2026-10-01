/* attract.h - what the medal shows when nobody is playing: title, how to play, the games, credits.
 * Each scene returns true the moment a button is pressed. Music, if any, is the caller's. */
#pragma once
#include <stdbool.h>
bool attract_title(void);
bool attract_howto(void);
bool attract_credits(void);
bool attract_any_button(void);   /* a pad or medal press since the last call (shared edge detector) */
