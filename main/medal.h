/* medal.h - the wearable's own controls: BOOT/PWR buttons, battery rail, battery gauge, backlight. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define BTN_BOOT_SHORT 0x01
#define BTN_BOTH       0x02  /* both buttons pressed together (sound); neither counts on its own then */
#define BTN_BOOT_HOLD5  0x04 /* BOOT held 5 s (a game: back to the wheel) */
#define BTN_BOOT_HOLD10 0x20 /* BOOT held 10 s (forget controller) */
#define BTN_PWR_SHORT  0x08
#define BTN_PWR_LONG   0x10  /* PWR held 2 s: medal_poll() powers off by itself */

void medal_init(void);          /* holds the battery rail up, configures the buttons and ADC */
uint32_t medal_poll(void);      /* BTN_* events since the last call */
int  medal_boot_held_ms(void);  /* how long BOOT has been down, 0 when up (hold-to-pick reads this) */
void medal_boot_consume(void);  /* the current BOOT press has been used: no SHORT on release */
int medal_battery_percent(void);/* 0..100, cached, refreshed every few seconds */
int medal_battery_mv(void);     /* last battery reading in mV (after the divider), 0 if none yet */
void medal_power_off(void);     /* cuts the battery rail; on USB just blanks the screen and halts */
