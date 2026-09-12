/* festive.h - the Fiesta dressing shared by the splash, controller screen and picker. */
#pragma once
#include <stdint.h>
#define CUBE(r, g, b) ((uint8_t)((r) * 30 + (g) * 5 + (b)))   /* index into ui_palette_cube() */
extern const uint8_t fiesta_colours[6];
void festive_papel_picado(int frame);          /* string of flags across the top, swaying */
void festive_confetti(int frame);              /* slow drifting confetti over the whole frame */
void festive_dancers(int frame, int floor_y);  /* folklorico dancers and mariachis, feet on floor_y */
void festive_px(int x, int y, uint8_t c);      /* clipped to the visible portrait window */
