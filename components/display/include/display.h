/*
 * display.h - ST7789 Display Driver (from PELLETINO)
 *
 * 240x280 panel driven in landscape (280x240), SPI interface with DMA
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Display dimensions. The panel is 240x280 portrait; we run it rotated (MADCTL)
// so the full NES 256x240 frame fits at native pixels with 12 px bars each side.
#define DISPLAY_WIDTH   280
#define DISPLAY_HEIGHT  240

#define GAME_WIDTH      256
#define GAME_HEIGHT     240
#define GAME_X_OFFSET   ((DISPLAY_WIDTH - GAME_WIDTH) / 2)
#define STRIP_ROWS      16

// GPIO Pin definitions (FIESTA26)
#define PIN_LCD_MOSI    GPIO_NUM_2
#define PIN_LCD_SCLK    GPIO_NUM_1
#define PIN_LCD_CS      GPIO_NUM_5
#define PIN_LCD_DC      GPIO_NUM_3
#define PIN_LCD_RST     GPIO_NUM_4
#define PIN_LCD_BL      GPIO_NUM_6

// SPI configuration
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_SPI_CLOCK   80000000  // 80 MHz (max for ST7789)

/**
 * Initialize the display driver
 */
void display_init(void);

/**
 * Write pixel data to display
 * @param data Pointer to RGB565 pixel data
 * @param len Number of pixels (not bytes)
 */
void display_write(const uint16_t *data, uint32_t len);

/**
 * Write pre-byte-swapped pixel data (faster, no copy needed)
 * @param data Pointer to pre-swapped RGB565 pixel data
 * @param len Number of pixels (not bytes)
 */
void display_write_preswapped(const uint16_t *data, uint32_t len);

/**
 * Push an 8-bit palette-indexed frame: GAME_HEIGHT rows of GAME_WIDTH pixels
 * starting at fb, consecutive rows pitch bytes apart. pal is 256 RGB565 entries
 * already byte-swapped for the panel. Converted in STRIP_ROWS strips into one DMA
 * buffer while the other strip is in flight; returns after the last strip is queued.
 */
void display_push_indexed(const uint8_t *fb, int pitch, const uint16_t *pal);

/**
 * Wait for pending DMA transfer to complete
 */
void display_wait_done(void);

/**
 * Set the drawing window
 * @param x X coordinate (0-239)
 * @param y Y coordinate (0-279)
 * @param w Width
 * @param h Height
 */
void display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/**
 * Fill screen with a solid color
 * @param color RGB565 color
 */
void display_fill(uint16_t color);

/**
 * Set backlight brightness
 * @param brightness 0-255
 */
void display_set_backlight(uint8_t brightness);

// Brightness levels for adaptive power management
#define DISPLAY_BRIGHTNESS_ACTIVE  153  // 60% for active gameplay
#define DISPLAY_BRIGHTNESS_IDLE    76   // 30% for idle/attract mode

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_H
