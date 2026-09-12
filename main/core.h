/* core.h - the one nofrendo instance, shared by games and the menu music. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool core_load(const uint8_t *data, size_t size);   /* shuts down whatever runs, inits, inserts; false = unsupported */
void core_unload(void);
bool core_loaded(void);
