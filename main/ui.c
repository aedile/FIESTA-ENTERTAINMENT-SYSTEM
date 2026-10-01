#include "ui.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "display.h"
#include "font8x8.h"

uint8_t *ui_fb;
uint16_t ui_pal[256];
int ui_w = 256, ui_h = FB_LINES, ui_pitch = FB_PITCH, ui_xoff = FB_XOFF;
uint8_t ui_white;
bool ui_crt;
static uint8_t pal_rgb[256][3];
static uint16_t pal_dim[256];
static ui_layout_t layout;
static bool portrait_games = true;

uint16_t ui_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (c >> 8) | (c << 8);
}

void ui_colour(uint8_t i, uint8_t r, uint8_t g, uint8_t b)
{
    pal_rgb[i][0] = r; pal_rgb[i][1] = g; pal_rgb[i][2] = b;
    ui_pal[i] = ui_rgb(r, g, b);
    pal_dim[i] = ui_rgb(r * 3 / 4, g * 3 / 4, b * 3 / 4);
}

void ui_palette_cube(void)
{
    for (int i = 0; i < 180; i++) ui_colour(i, (i / 30) * 51, ((i / 5) % 6) * 51, (i % 5) * 63);
}

void ui_init(void)
{
    ui_fb = calloc(240 * 280 > FB_PITCH * FB_LINES ? 240 * 280 : FB_PITCH * FB_LINES, 1);
    assert(ui_fb);
    ui_colour(UI_BLACK, 0, 0, 0);
    ui_colour(UI_WHITE, 255, 255, 255);
    ui_colour(UI_GREY, 128, 128, 128);
    ui_colour(UI_YELLOW, 255, 220, 0);
    ui_colour(UI_GREEN, 40, 220, 40);
    ui_colour(UI_RED, 240, 40, 40);
    ui_colour(UI_BLUE, 40, 80, 220);
}

void ui_set_portrait_games(bool p) { portrait_games = p; if (layout == UI_LAYOUT_NES) ui_layout(UI_LAYOUT_NES); }

void ui_layout(ui_layout_t l)
{
    layout = l;
    if (l == UI_LAYOUT_MENU) {
        ui_w = 240; ui_h = 280; ui_pitch = 240; ui_xoff = 0;
        display_set_orientation(true);
        display_set_rect(0, 0, 240, 280);
    } else {
        ui_w = 256; ui_h = FB_LINES; ui_pitch = FB_PITCH; ui_xoff = FB_XOFF;
        display_set_orientation(portrait_games);
    }
    memset(ui_fb, UI_BLACK, 240 * 280 > FB_PITCH * FB_LINES ? 240 * 280 : FB_PITCH * FB_LINES);
}

void ui_clear(uint8_t colour) { memset(ui_fb, colour, (size_t)ui_pitch * ui_h); }

void ui_px(int x, int y, uint8_t colour)
{
    if (x >= 0 && x < ui_w && y >= 0 && y < ui_h) ui_fb[y * ui_pitch + ui_xoff + x] = colour;
}

void ui_fill(int x, int y, int w, int h, uint8_t colour)
{
    int x0 = x < 0 ? 0 : x, x1 = x + w > ui_w ? ui_w : x + w;
    if (x1 <= x0) return;
    for (int r = y < 0 ? 0 : y; r < y + h && r < ui_h; r++)
        memset(ui_fb + r * ui_pitch + ui_xoff + x0, colour, x1 - x0);
}

void ui_frame(int x, int y, int w, int h, uint8_t colour)
{
    ui_fill(x, y, w, 1, colour); ui_fill(x, y + h - 1, w, 1, colour);
    ui_fill(x, y, 1, h, colour); ui_fill(x + w - 1, y, 1, h, colour);
}

void ui_text_scaled(int x, int y, const char *s, uint8_t colour, int scale)
{
    if (scale < 1) return;
    for (; *s; s++, x += 8 * scale) {
        if (*s < 32 || *s > 126) continue;
        const uint8_t *g = font8x8[*s - 32];
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                if (g[r] & (0x80 >> c)) {
                    if (scale == 1) ui_px(x + c, y + r, colour);
                    else ui_fill(x + c * scale, y + r * scale, scale, scale, colour);
                }
    }
}

void ui_text(int x, int y, const char *s, uint8_t colour) { ui_text_scaled(x, y, s, colour, 1); }
void ui_text_center(int y, const char *s, uint8_t colour) { ui_text_scaled((ui_w - 8 * (int)strlen(s)) / 2, y, s, colour, 1); }
void ui_text_centred_scaled(int cx, int y, const char *s, uint8_t colour, int scale) { ui_text_scaled(cx - 4 * scale * (int)strlen(s), y, s, colour, scale); }

static uint8_t shade_cube(uint8_t v, int shade)
{
    if (v >= 180 || !shade) return v;
    int r = v / 30, g = (v / 5) % 6, b = v % 5;
    r = r * (4 - shade) / 4; g = g * (4 - shade) / 4; b = b * (4 - shade) / 4;
    return CUBE(r, g, b);
}

void ui_bitmap_shaded(int x, int y, const uint8_t *px, int w, int h, int num, int den, int shade)
{
    int ow = w * num / den, oh = h * num / den;
    int ox0 = x < 0 ? -x : 0, ox1 = x + ow > ui_w ? ui_w - x : ow;
    if (ox1 <= ox0 || ow <= 0 || oh <= 0) return;
    static uint16_t xmap[512];
    if (ox1 > 512) ox1 = 512;
    for (int ox = ox0; ox < ox1; ox++) xmap[ox] = ox * den / num;
    for (int oy = 0; oy < oh; oy++) {
        int sy = y + oy;
        if (sy < 0 || sy >= ui_h) continue;
        const uint8_t *row = px + (oy * den / num) * w;
        uint8_t *dst = ui_fb + sy * ui_pitch + ui_xoff + x;
        if (shade) for (int ox = ox0; ox < ox1; ox++) dst[ox] = shade_cube(row[xmap[ox]], shade);
        else for (int ox = ox0; ox < ox1; ox++) dst[ox] = row[xmap[ox]];
    }
}

void ui_bitmap(int x, int y, const uint8_t *px, int w, int h, int num, int den) { ui_bitmap_shaded(x, y, px, w, h, num, den, 0); }

/* ---- presenting: the palette chosen per row for the flash and the CRT look ---- */
static const uint16_t *row_palette(int y)
{
    static uint16_t flash[256];
    static uint8_t flash_for = 0;
    if (ui_white) {
        if (flash_for != ui_white) {
            flash_for = ui_white;
            for (int i = 0; i < 256; i++) {
                const uint8_t *c = pal_rgb[i];
                flash[i] = ui_rgb(c[0] + (255 - c[0]) * ui_white / 255, c[1] + (255 - c[1]) * ui_white / 255, c[2] + (255 - c[2]) * ui_white / 255);
            }
        }
        return flash;
    }
    return (ui_crt && (y & 1)) ? pal_dim : ui_pal;
}

static void push_rows(int y0, int n)
{
    const uint16_t *even = row_palette(0), *odd = (ui_crt && !ui_white) ? pal_dim : NULL;
    display_push_rows2(ui_fb + y0 * ui_pitch + ui_xoff + ui_crop(), ui_pitch, y0, n, even, odd);
}

static int pushed_rows;

void ui_present(void)
{
    for (int y = 0; y < ui_h; y += 16) push_rows(y, ui_h - y < 16 ? ui_h - y : 16);
    display_wait_done();
    pushed_rows = ui_h;
}

int ui_crop(void) { return layout == UI_LAYOUT_MENU ? 0 : (256 - display_game_width()) / 2; }

void ui_line_push(int scanline)
{
    if (scanline == 0) pushed_rows = 0;
    if ((scanline & 15) == 15 && scanline - 15 < ui_h) {
        int y0 = scanline - 15, n = ui_h - y0 < 16 ? ui_h - y0 : 16;
        push_rows(y0, n);
        pushed_rows = y0 + n;
    }
}

void ui_line_flush(void)
{
    while (pushed_rows < ui_h) { int n = ui_h - pushed_rows < 16 ? ui_h - pushed_rows : 16; push_rows(pushed_rows, n); pushed_rows += n; }
}

/* ---- scene ---- */
void ui_stars(int frame)
{
    static const uint8_t tier[3] = { CUBE(1,1,2), CUBE(3,3,4), UI_WHITE };
    for (int i = 0; i < 100; i++) {
        uint32_t h = (uint32_t)i * 2654435761u;
        int x = (h >> 8) % ui_w, y = (h >> 20) % 150;
        int t = (h >> 4) % 10, tw = ((((frame + (int)(h & 63)) >> 3) * 5) + i) % 7;
        if (t < 6) { if (tw > 1) ui_px(x, y, tier[0]); }
        else if (t < 9) ui_px(x, y, tw > 3 ? tier[1] : tier[0]);
        else {
            ui_px(x, y, tier[2]);
            if (tw > 4) { ui_px(x - 1, y, tier[1]); ui_px(x + 1, y, tier[1]); ui_px(x, y - 1, tier[1]); ui_px(x, y + 1, tier[1]); }
        }
    }
}

/* Horizontal rules crowd toward the horizon and scroll outward; verticals fan from a
 * vanishing point. A rule at depth z lands at horizon + below/z. */
void ui_grid(int frame, int horizon)
{
    if (horizon < 1 || horizon >= ui_h) return;
    int below = ui_h - horizon;
    uint8_t near = CUBE(4,0,5), far = CUBE(1,0,2);
    for (int k = 1; k <= 18; k++) {
        int z16 = k * 16 + (frame % 16);
        int y = horizon + (below * 16) / z16;
        if (y >= ui_h || y <= horizon) continue;
        ui_fill(0, y, ui_w, 1, (y - horizon) > below / 3 ? near : far);
    }
    for (int i = -7; i <= 7; i++) {
        int xb = ui_w / 2 + i * 42;
        for (int y = horizon; y < ui_h; y++)
            ui_px(ui_w / 2 + (xb - ui_w / 2) * (y - horizon) / below, y, (y - horizon) > below / 3 ? near : far);
    }
}

void ui_scanlines(void)
{
    for (int y = 1; y < ui_h; y += 2) {
        uint8_t *row = ui_fb + y * ui_pitch + ui_xoff;
        for (int x = 0; x < ui_w; x++) {
            uint8_t v = row[x];
            if (!v || v >= 180) continue;
            row[x] = CUBE((v / 30) * 2 / 3, ((v / 5) % 6) * 2 / 3, (v % 5) * 2 / 3);
        }
    }
}

/* a grey console with a dark front-loading door, a red power light and a cartridge standing in it;
 * no Nintendo marking, just the shape everyone knows */
void ui_console(int frame, int cx, int base_y)
{
    uint8_t body = CUBE(4,4,4), body_lt = CUBE(5,5,5), body_dk = CUBE(2,2,2), door = CUBE(1,1,1), cart = CUBE(3,3,3);
    int w = 120, h = 34, x = cx - w / 2, y = base_y - h;
    ui_fill(x, y, w, h, body);
    ui_fill(x, y, w, 3, body_lt);
    ui_fill(x, y + h - 4, w, 4, body_dk);
    ui_fill(x + 6, y + 8, w - 12, 14, door);            /* the flap */
    ui_fill(x + 6, y + 8, w - 12, 1, body_dk);
    ui_fill(x + w / 2 - 22, y + 3, 44, 4, cart);         /* cartridge spine showing above the door */
    ui_fill(x + w / 2 - 22, y + 2, 44, 1, body_lt);
    ui_fill(x + 10, y + 26, 10, 4, body_dk);             /* power */
    ui_fill(x + 24, y + 26, 10, 4, body_dk);             /* reset */
    ui_fill(x + 12, y + 24, 3, 2, (frame >> 4) & 1 ? CUBE(5,0,0) : CUBE(3,0,0));   /* the red LED */
    for (int i = 0; i < 18; i++) ui_fill(x + 44 + i * 4, y + 26, 2, 4, body_dk);   /* vents */
    /* two controller leads into the front */
    ui_fill(x + w - 30, y + 26, 6, 4, door); ui_fill(x + w - 18, y + 26, 6, 4, door);
    ui_fill(x + w - 28, y + 30, 2, 14 + ((frame >> 3) & 1), door);
    ui_fill(x + w - 16, y + 30, 2, 12, door);
}

/* snap blob: 56 x (r,g,b) | u32 row_off[280] | RLE rows (see artconv.rle_rows) */
void ui_snap(const uint8_t *snap)
{
    static const uint8_t *loaded;
    if (!snap) { ui_clear(UI_BLACK); return; }
    if (loaded != snap) {
        for (int k = 0; k < UI_SNAP_COLOURS; k++) ui_colour(UI_SNAP_BASE + k, snap[k * 3], snap[k * 3 + 1], snap[k * 3 + 2]);
        loaded = snap;
    }
    const uint8_t *offs = snap + UI_SNAP_COLOURS * 3, *data = offs + 280 * 4;
    int rows = ui_h < 280 ? ui_h : 280;
    for (int y = 0; y < rows; y++) {
        uint32_t off; memcpy(&off, offs + y * 4, 4);
        const uint8_t *p = data + off;
        uint8_t *dst = ui_fb + y * ui_pitch + ui_xoff, *end = dst + (ui_w < 240 ? ui_w : 240);
        while (dst < end) {
            uint8_t c = *p++;
            if (c < 128) { int n = c + 1; if (dst + n > end) n = end - dst; for (int i = 0; i < n; i++) dst[i] = UI_SNAP_BASE + p[i]; p += c + 1; dst += n; }
            else { int n = c - 126; if (dst + n > end) n = end - dst; memset(dst, UI_SNAP_BASE + *p++, n); dst += n; }
        }
    }
}
