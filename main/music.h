/* music.h - menu music: an NSF played by the emulator core with the PPU idle. */
#pragma once
#include <stdbool.h>
void music_start(int track);   /* loads the embedded NSF (no-op if none was built in) */
void music_tick(void);         /* one frame of music; blocks ~16 ms on the audio queue, or sleeps 16 ms when silent */
void music_tick_hook(void (*line)(int scanline));   /* same, calling line() after each of the 262 scanlines (0..261) */
void music_stop(void);
bool music_playing(void);
