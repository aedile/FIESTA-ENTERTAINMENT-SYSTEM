# Licensing

F.E.S. is a hobby project: an NES emulator firmware for a wearable Fiesta
medal. This file says exactly what is in the repository, who owns it, and what
you may do with it. Nothing here is legal advice.

## The code written for this project: MIT

Everything under `main/`, `components/display`, `components/audio_hal`,
`components/ble_pad` and `tools/`, plus the build files, is
Copyright (c) 2026 Jesse Castro and released under the MIT license (`LICENSE`).
Use it, change it, ship it; keep the notice.

The display and audio drivers began life in the author's own PELLETINO
project (also MIT) and were adapted here.

## The NES core: nofrendo, GPL-2.0

`components/nofrendo` is Matthew Conte's nofrendo as carried in
[retro-go](https://github.com/ducalex/retro-go) by ducalex. Its `COPYING` is
the GNU General Public License version 2; individual source headers cite the
GNU Library General Public License version 2. Both are copyleft. Treat the
component as GPL-2.0.

It was trimmed and modified for this port. Per the GPL, the modified files
carry a notice at the top; in summary (September 2026):

| File | Change |
|---|---|
| `mappers/mappers.h` | mapper table reduced to NROM, MMC1, UxROM, CNROM, MMC3, MMC2 and the NSF player |
| `nes/nes.h`, `nes/nes.c` | `strip_func` (per-16-scanline callback) and `line_func` (per-scanline callback) hooks; RISC-V cycle-count profiling of CPU/PPU/APU |
| `nes/state.c`, `nes/state.h` | `state_save`/`state_load` take an open `FILE *` instead of a filename |
| `nes/utils.h` | non-retro-go build: `printf` logging, ESP-IDF `IRAM_ATTR`, CRC database lookup compiled out |
| `nes/ppu.c`, `nes/apu.c` | hot functions placed in IRAM |
| `mappers/map031.c` | `nsf_play_song()` to pick a track, play-call counter, sync write moved after the play call |
| `database.h` | replaced by an empty table (the CRC lookup is compiled out) |

Only the six mappers the game set needs are included, unmodified; the other
50-odd mapper files in retro-go's copy were not copied.

### What GPL-2.0 means for the firmware image

The compiled firmware links the MIT code and the GPL-2.0 core into one
program. If you **distribute a firmware binary** (or a medal flashed with
one), the GPL applies to the whole: you must make the complete corresponding
source available under the GPL, which this repository does, and you may not
add restrictions beyond the GPL's. The MIT parts remain MIT on their own; the
GPL does not stop you selling or giving away the medal, it only requires the
source offer. Using the firmware yourself carries no obligation.

## ESP-IDF and the modified HID host: Apache-2.0

The firmware is built on Espressif's ESP-IDF v5.3.4 (Apache-2.0). One IDF
component, `esp_hid`, is vendored into `components/esp_hid` because its NimBLE
HID host needed fixes. Only `src/nimble_hidh.c` differs from the original, and
it says so at the top; the changes (September 2026) are: wake the waiter on
a GATT read error, pair before service discovery and retry a read that
failed for insufficient encryption, handle repeat pairing, default the
protocol mode to Report, set the connected flag, reject a device with no HID
report map inside the opening task, and log discovered services. Apache-2.0 requires modified
files to be marked and the notices kept; both are done.

## Font: public domain

`main/font8x8.h` is the 8x8 bitmap font by Daniel Hepper, derived from Marcel
Sondaar's public-domain VGA font (https://github.com/dhepper/font8x8).

## Things that are NOT in this repository

The repository ships **no game content**. `ROMS/` is ignored by git.

* **Game ROMs** are copyrighted by their publishers (Nintendo, Capcom, Konami,
  Rare, Tecmo and others). Supply your own; the packer takes whatever is in
  `ROMS/`. This project does not distribute them and does not tell you where
  to get them.
* **Box art** is fetched on demand by `tools/fetch_art.py` from the
  libretro-thumbnails project for your own build. The artwork belongs to the
  publishers.
* **Menu music** is the game's own sound engine and music data in NSF form,
  fetched on demand by `tools/fetch_music.py` from a public NSF archive for
  your own build. Copyright Capcom.

Flashing these onto a device you own for personal use is, as far as the
author knows, the ordinary emulator situation; redistributing a flashed medal
with the content on it is a different question and is on you.

## Trademarks

Fiesta San Antonio is the festival of the Fiesta San Antonio Commission.
Nintendo Entertainment System is a trademark of Nintendo. Neither is
affiliated with this project. "Fiesta Entertainment System" is a pun on a
souvenir medal, not a product.

## Hardware

Built for the Waveshare ESP32-C6-LCD-1.69 board. Nothing here is affiliated
with Waveshare or Espressif.
