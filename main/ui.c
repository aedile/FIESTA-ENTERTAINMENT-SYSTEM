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

static uint8_t shade_lut[4][256];
static uint8_t shade_cube(uint8_t v, int shade)
{
    static bool built;
    if (!built) {
        built = true;
        for (int sh = 0; sh < 4; sh++)
            for (int i = 0; i < 256; i++) {
                int r = i / 30, g = (i / 5) % 6, b = i % 5;
                shade_lut[sh][i] = (i >= 180 || !sh) ? i : CUBE(r * (4 - sh) / 4, g * (4 - sh) / 4, b * (4 - sh) / 4);
            }
    }
    return shade_lut[shade][v];
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
    uint8_t near = CUBE(4,0,4), far = CUBE(1,0,2);   /* blue runs 0..4 */
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

/* the front-loading console seen from the front and a little above, after the photo: a pale lid
 * with a ribbed patch, the black stripe wrapping over the top and down the front at the right,
 * the door with FIESTA in red, a darker lower band with the red LED, POWER and RESET, the ports in
 * the stripe; a pad in front, cabled to port 1. (cx, base_y) is the middle of the floor line. */
void ui_console(int frame, int cx, int base_y)
{
    const uint8_t lid = CUBE(4,4,3), lid_lt = CUBE(5,5,4), rib = CUBE(3,3,2), band = CUBE(3,3,2),
                  band_dk = CUBE(2,2,2), black = CUBE(1,1,1), ink = CUBE(0,0,1), red = CUBE(4,0,0);
    const int W = 140, TOP = 16, UP = 17, LOW = 15, STRIPE = 104, STRIPE_W = 18;
    int x = cx - 95 + 50, y = base_y - LOW - UP - TOP;
    /* lid: rows narrow toward the back */
    for (int r = 0; r < TOP; r++) {
        int in = (TOP - r) / 2, l = x + in, w = W - 2 * in;
        ui_fill(l, y + r, w, 1, r == TOP - 1 ? lid_lt : lid);
        int s0 = l + STRIPE * w / W, s1 = l + (STRIPE + STRIPE_W) * w / W;
        if (r & 1) ui_fill(l + 70 * w / W, y + r, s0 - (l + 70 * w / W), 1, rib);   /* the ribbed patch */
        ui_fill(s0, y + r, s1 - s0, 1, r < 3 ? CUBE(2,2,2) : black);
    }
    /* front, upper: the door and the logo */
    int fy = y + TOP;
    ui_fill(x, fy, W, UP, lid);
    ui_fill(x + 4, fy + 2, STRIPE - 8, 1, rib);                  /* the door's top edge */
    ui_fill(x + 4, fy + UP - 2, STRIPE - 8, 1, rib);
    ui_text(x + 10, fy + 5, "FIESTA", red);
    /* front, lower band */
    int by = fy + UP;
    ui_fill(x, by, W, LOW, band);
    ui_fill(x, by, W, 1, band_dk);
    ui_fill(x + 6, by + 6, 2, 2, (frame >> 4) & 1 ? CUBE(5,0,0) : CUBE(2,0,0));   /* the LED */
    for (int i = 0; i < 2; i++) {                                                  /* POWER, RESET */
        int bx = x + 12 + i * 22;
        ui_fill(bx, by + 3, 18, 8, band_dk);
        ui_fill(bx + 1, by + 4, 16, 6, lid);
        ui_fill(bx + 3, by + 7, 12, 1, red);
    }
    ui_fill(x, base_y - 1, W, 1, band_dk);
    /* the stripe down the front, the ports in it */
    ui_fill(x + STRIPE, fy, STRIPE_W, UP + LOW, black);
    ui_fill(x + STRIPE + 2, fy + 6, STRIPE_W - 4, 1, CUBE(2,2,2));
    for (int i = 0; i < 2; i++) {
        int px = x + STRIPE + 3 + i * 7;
        ui_fill(px, by + 3, 5, 9, band_dk);
        ui_fill(px + 1, by + 4, 3, 7, ink);
    }
    /* the pad, in front and to the left, and its cable to port 1 */
    int pw = 46, ph = 20, px = cx - 95, py = base_y + 8 - ph;
    int port_x = x + STRIPE + 5, port_y = by + 12;
    ui_fill(px + pw, base_y + 3, port_x - (px + pw) + 1, 1, band_dk);   /* cable: along the floor, up into the port */
    ui_fill(port_x, port_y, 1, base_y + 3 - port_y, band_dk);
    ui_fill(px, py, pw, ph, lid);
    ui_fill(px + 2, py + 3, pw - 4, ph - 5, black);
    ui_fill(px + 6, py + 9, 9, 3, rib);  ui_fill(px + 9, py + 6, 3, 9, rib);      /* d-pad */
    ui_fill(px + 19, py + 9, 4, 2, rib); ui_fill(px + 25, py + 9, 4, 2, rib);     /* select, start */
    ui_fill(px + 32, py + 8, 4, 4, red); ui_fill(px + 38, py + 8, 4, 4, red);     /* B, A */
    ui_fill(px + 32, py + 5, 10, 1, red);                                         /* the logo line */
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
