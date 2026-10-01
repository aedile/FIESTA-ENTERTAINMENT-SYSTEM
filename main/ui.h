/* ui.h - the one 8-bit framebuffer, in two layouts.
 *
 *   UI_LAYOUT_NES   272x240, the core's frame (8 px overdraw either side of the 256 px line);
 *                   shown as 240x240 (portrait, 8 px cropped each side) or 256x240 (landscape)
 *   UI_LAYOUT_MENU  240x280, the whole portrait panel, for the attract screens and the wheel
 *
 * Colours are indices into a 6x6x5 RGB cube (0..179, see ui_palette_cube), named UI entries
 * above it, and 199..254 for whatever is drawing (the wheel loads a screenshot's colours there).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define FB_PITCH  272     /* NES layout pitch */
#define FB_LINES  240
#define FB_XOFF   8

#define CUBE(r, g, b) ((uint8_t)((r) * 30 + (g) * 5 + (b)))

#define UI_BLACK  192
#define UI_WHITE  193
#define UI_GREY   194
#define UI_YELLOW 195
#define UI_GREEN  196
#define UI_RED    197
#define UI_BLUE   198
#define UI_SNAP_BASE    199   /* a screenshot's own colours live here... */
#define UI_SNAP_COLOURS 56    /* ...this many of them */

typedef enum { UI_LAYOUT_NES, UI_LAYOUT_MENU } ui_layout_t;

extern uint8_t *ui_fb;
extern uint16_t ui_pal[256];   /* RGB565, byte-swapped for the panel */
extern int ui_w, ui_h, ui_pitch, ui_xoff;   /* the current layout */
extern uint8_t ui_white;       /* present everything this far toward white: 0 as drawn, 255 blank white */
extern bool ui_crt;            /* present every other row a quarter darker (the menus' CRT look) */

void ui_init(void);
void ui_layout(ui_layout_t l);             /* also sets the panel's push rectangle and orientation */
void ui_set_portrait_games(bool p);        /* the games' orientation setting (NES layout only) */
void ui_palette_cube(void);                /* entries 0..179 = the cube */
void ui_colour(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
uint16_t ui_rgb(uint8_t r, uint8_t g, uint8_t b);
void ui_clear(uint8_t colour);
void ui_px(int x, int y, uint8_t colour);
void ui_fill(int x, int y, int w, int h, uint8_t colour);
void ui_frame(int x, int y, int w, int h, uint8_t colour);
void ui_text(int x, int y, const char *s, uint8_t colour);
void ui_text_center(int y, const char *s, uint8_t colour);
void ui_text_scaled(int x, int y, const char *s, uint8_t colour, int scale);
void ui_text_centred_scaled(int cx, int y, const char *s, uint8_t colour, int scale);
void ui_bitmap(int x, int y, const uint8_t *px, int w, int h, int num, int den);   /* nearest-neighbour scale, clipped */
void ui_bitmap_shaded(int x, int y, const uint8_t *px, int w, int h, int num, int den, int shade);   /* shade 0..3 darkens cube colours */
void ui_present(void);                     /* push the frame and wait */
int ui_crop(void);                         /* NES layout: columns hidden each side (0 landscape, 8 portrait) */
void ui_line_push(int scanline);           /* music_tick_hook(): push 16 rows per 16 core scanlines */
void ui_line_flush(void);                  /* ...and whatever rows the scanlines did not cover */

/* scene primitives (all procedural) */
void ui_stars(int frame);                  /* twinkling starfield over the top 150 rows */
void ui_grid(int frame, int horizon);      /* scrolling perspective grid below the horizon */
void ui_scanlines(void);                   /* CRT line dimming, in place, cube colours only */
void ui_console(int frame, int cx, int base_y);   /* a toaster-style console with a cartridge in, lights blinking */
/* a packed screenshot (pack_roms.py) as the backdrop, dimmed, its palette loaded at UI_SNAP_BASE */
void ui_snap(const uint8_t *snap);
