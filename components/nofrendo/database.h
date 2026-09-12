// ###################################################################################
// #
// # Mesen Game Database
// #
// # Automatically generated database based on Nestopia's DB and NesCartDB
// #
// # Generated on 2020-05-02 using:
// #     -NewRisingSun's NES 2.0 header database (2020-04-26)
// #     -NesCartDB (dated 2017-08-21)
// #     -Nestopia UE's latest DB (dated 2015-10-22)
// #
// # Fields: CRC, System, Board, PCB, Chip, Mapper, PrgRomSize, ChrRomSize, ChrRamSize, WorkRamSize, SaveRamSize, Battery, Mirroring, Controller Type, Bus Conflicts, SubMapper, VsSystemType, PpuModel
// #
// ###################################################################################
// Source: https://raw.githubusercontent.com/SourMesen/Mesen/master/GUI.NET/Dependencies/MesenDB.txt


// Retro-Go changes:
// Things irrelevant to us have been removed (to save on flash):
//  - Systems: VSSystem, Playchoice, VT*, Dendy
//  - Fields: Controller Type, Bus Conflicts, VsSystemType, PpuModel
//  - Combined work ram and save ram

// #include "nes/nes.h"

typedef struct __attribute__((packed))
{
    uint32_t crc;
    uint32_t system:2;      // 0 - 3
    uint32_t mapper:8;      // 0 - 255
    uint32_t submap:3;      // 0 - 7
    uint32_t mirror:3;      // 0 - 4
    uint32_t battery:1;     // 0 - 1
    uint32_t prg_rom:10;    // 0 - 512
    uint32_t prg_ram:4;     // 0 - 8
    int16_t  chr_rom:10;    // -1 - 256
    int16_t  chr_ram:6;     // -1 - 16
} db_game_t;

// This will integer round to the nearest 8 which is what we use internally
#define BANK(x) ((x) > 0 ? ((x) + 7) / 8 : x)

// This wrapper might seem unecessary but it is to reorder values to better pack them
#define ENTRY(crc, sys, map, d, e, f, g, h, batt, mirr, sub) {crc, sys, map, sub, mirr, batt, BANK(d), BANK(g > h ? g : h), BANK(e), BANK(f)}

const db_game_t games_database[] =
{
    {0} /* stub: no CRC lookup on this port (CRC32 is compiled out) */
};
