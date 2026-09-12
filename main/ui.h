/* ui.h - text screens drawn into the 8-bit framebuffer that the emulator also uses. */
#pragma once
#include <stdint.h>

#define FB_PITCH  272     /* nofrendo layout: 8 px overdraw either side of the 256 px line */
#define FB_LINES  240
#define FB_XOFF   8

/* palette indices reserved for the UI (nofrendo leaves 192..199 for a GUI palette) */
#define UI_BLACK  192
#define UI_WHITE  193
#define UI_GREY   194
#define UI_YELLOW 195
#define UI_GREEN  196
#define UI_RED    197
#define UI_BLUE   198

extern uint8_t *ui_fb;
extern uint16_t ui_pal[256];   /* RGB565, byte-swapped for the panel */

void ui_init(void);            /* allocates the framebuffer, sets the UI palette entries */
void ui_clear(uint8_t colour);
void ui_fill(int x, int y, int w, int h, uint8_t colour);
void ui_text(int x, int y, const char *s, uint8_t colour);
void ui_text_center(int y, const char *s, uint8_t colour);
void ui_text_scaled(int x, int y, const char *s, uint8_t colour, int scale);   /* 8*scale px glyphs, clipped */
void ui_present(void);         /* push the framebuffer to the panel and wait */
int ui_crop(void);             /* pixels hidden on each side of the 256-wide frame: 0 landscape, 8 portrait */
void ui_line_push(int scanline);   /* for music_tick_hook(): pushes the frame in 16-row strips as the core runs scanlines */
void ui_palette_cube(void);    /* entries 0..179 = the 6x6x5 RGB cube the box art is quantised to */
/* blit an 8-bit cube-indexed bitmap (from flash) scaled by num/den, nearest neighbour, clipped */
void ui_bitmap(int x, int y, const uint8_t *px, int w, int h, int num, int den);
void ui_frame(int x, int y, int w, int h, uint8_t colour);   /* 1 px rectangle outline */
uint16_t ui_rgb(uint8_t r, uint8_t g, uint8_t b);
