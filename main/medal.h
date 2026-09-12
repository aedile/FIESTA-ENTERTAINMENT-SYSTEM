/* medal.h - the wearable's own controls: BOOT/PWR buttons, battery rail, battery gauge, backlight. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define BTN_BOOT_SHORT 0x01
#define BTN_BOOT_HOLD3 0x02  /* held 3-10 s, reported on release (mute) */
#define BTN_BOOT_HOLD10 0x04 /* held 10 s, reported while still down (forget controller) */
#define BTN_PWR_SHORT  0x08
#define BTN_PWR_LONG   0x10  /* 2 s: medal_poll() powers off by itself */

void medal_init(void);          /* holds the battery rail up, configures the buttons and ADC */
uint32_t medal_poll(void);      /* BTN_* events since the last call */
int medal_battery_percent(void);/* 0..100, cached, refreshed every few seconds */
void medal_power_off(void);     /* cuts the battery rail; on USB just blanks the screen and halts */
