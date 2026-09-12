#include "ui.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "display.h"
#include "font8x8.h"

uint8_t *ui_fb;
uint16_t ui_pal[256];

uint16_t ui_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (c >> 8) | (c << 8);
}

void ui_init(void)
{
    ui_fb = calloc(FB_PITCH * FB_LINES, 1);
    assert(ui_fb);
    ui_pal[UI_BLACK] = ui_rgb(0, 0, 0);
    ui_pal[UI_WHITE] = ui_rgb(255, 255, 255);
    ui_pal[UI_GREY] = ui_rgb(128, 128, 128);
    ui_pal[UI_YELLOW] = ui_rgb(255, 220, 0);
    ui_pal[UI_GREEN] = ui_rgb(40, 220, 40);
    ui_pal[UI_RED] = ui_rgb(240, 40, 40);
    ui_pal[UI_BLUE] = ui_rgb(40, 80, 220);
}

void ui_clear(uint8_t colour) { memset(ui_fb, colour, FB_PITCH * FB_LINES); }

void ui_fill(int x, int y, int w, int h, uint8_t colour)
{
    for (int r = y; r < y + h && r < FB_LINES; r++)
        memset(ui_fb + r * FB_PITCH + FB_XOFF + x, colour, w);
}

void ui_text(int x, int y, const char *s, uint8_t colour)
{
    for (; *s; s++, x += 8) {
        if (*s < 32 || *s > 126 || x > 248 || y > 232) continue;
        const uint8_t *g = font8x8[*s - 32];
        uint8_t *row = ui_fb + y * FB_PITCH + FB_XOFF + x;
        for (int r = 0; r < 8; r++, row += FB_PITCH)
            for (int c = 0; c < 8; c++)
                if (g[r] & (0x80 >> c)) row[c] = colour;
    }
}

void ui_text_center(int y, const char *s, uint8_t colour)
{
    ui_text((256 - 8 * (int)strlen(s)) / 2, y, s, colour);
}

void ui_present(void)
{
    display_push_indexed(ui_fb + FB_XOFF, FB_PITCH, ui_pal);
    display_wait_done();
}

void ui_palette_cube(void)
{
    for (int i = 0; i < 180; i++)
        ui_pal[i] = ui_rgb((i / 30) * 51, ((i / 5) % 6) * 51, (i % 5) * 63);
}

void ui_bitmap(int x, int y, const uint8_t *px, int w, int h, int num, int den)
{
    int ow = w * num / den, oh = h * num / den;
    for (int oy = 0; oy < oh; oy++) {
        int sy = y + oy;
        if (sy < 0 || sy >= FB_LINES) continue;
        const uint8_t *row = px + (oy * den / num) * w;
        uint8_t *dst = ui_fb + sy * FB_PITCH + FB_XOFF;
        for (int ox = 0; ox < ow; ox++) {
            int sx = x + ox;
            if (sx >= 0 && sx < 256) dst[sx] = row[ox * den / num];
        }
    }
}

void ui_frame(int x, int y, int w, int h, uint8_t colour)
{
    ui_fill(x, y, w, 1, colour); ui_fill(x, y + h - 1, w, 1, colour);
    ui_fill(x, y, 1, h, colour); ui_fill(x + w - 1, y, 1, h, colour);
}
