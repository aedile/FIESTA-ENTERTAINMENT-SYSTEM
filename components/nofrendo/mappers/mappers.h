/* Modified for NESTOR / F.E.S. (September 2026): mapper table reduced to the mappers this game set uses plus the NSF player. Original: nofrendo as carried in retro-go, GPL-2.0, see COPYING. */
#pragma once

#include "nes/mmc.h"

/* Only the mappers the ROM set needs: NROM, MMC1, UxROM, MMC2, plus the NSF player for menu music. */
extern mapintf_t map0_intf;
extern mapintf_t map1_intf;
extern mapintf_t map2_intf;
extern mapintf_t map9_intf;
extern mapintf_t map31_intf;

static const mapintf_t *mappers[] =
{
    &map0_intf,
    &map1_intf,
    &map2_intf,
    &map9_intf,
    &map31_intf,
    NULL
};
