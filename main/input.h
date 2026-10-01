/* input.h - the pad, the medal's two buttons, the serial bench keys, and the saved settings. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ble_pad.h"
#include "medal.h"

uint32_t pad_now(void);        /* PAD_* currently down (pad or serial keys) */
uint32_t pad_edges(void);      /* newly pressed PAD_* bits, d-pad repeat; also runs the medal's always-on jobs */
uint32_t medal_events(void);   /* BTN_BOOT_SHORT / BTN_PWR_SHORT collected since the last call */
bool any_button(void);         /* a pad edge or a medal short press: "press a button" */
bool serial_active(void);      /* bench keys typed in the last 5 s: screens treat them as a pad */
extern bool serial_demo;       /* bench key 'x': jump to demo mode */
void pad_forget(void);         /* drop the saved controller and listen for any */

/* settings kept in NVS */
extern bool muted, portrait;
extern int demo_lock;          /* game the demo is locked to, -1 = cycle */
extern bool demo_skip[64];     /* games left out of the demo cycle */
void settings_load(void);      /* after roms_init() */
void set_mute(bool m);
void set_portrait(bool p);
void demo_set_lock(int idx);
void demo_set_skip(int idx, bool skip);
int  demo_next(int i);         /* next game in the cycle after i */

void toast(const char *line1, const char *line2);   /* a boxed message for a second, music keeps going */
void toast_mute(void);
void toggle_lock(int idx);
