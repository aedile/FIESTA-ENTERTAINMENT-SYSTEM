# F.E.S. — Fiesta Entertainment System

**A Nintendo Entertainment System inside a wearable Fiesta San Antonio 2027 medal. Twenty games at a locked 60 fps with sound, a Bluetooth gamepad, and a show of its own when nobody is playing. One $20 ESP32-C6 board: a single 160 MHz RISC-V core, 512 KB of RAM, no PSRAM, no SD card.**

F.E.S. is a port of the [nofrendo](https://github.com/ducalex/retro-go) NES core to ESP-IDF on the [Waveshare ESP32-C6-LCD-1.69](https://www.waveshare.com/esp32-c6-lcd-1.69.htm). Games run straight out of flash. Pair a BLE controller and it is a console; leave it alone and it plays a cinematic splash, then cycles through every game's own attract mode with fireworks over the Tower of the Americas, papel picado and dancing mariachis in between.

> **Licensing in one line:** the code written here is MIT, the NES core is
> GPL-2.0, so a firmware image you hand to someone is GPL-2.0 as a whole (the
> source is this repository). Read [LICENSING.md](LICENSING.md) before you distribute.
>
> **No ROMs, no box art and no music are included.** Those belong to their
> owners; supplying them is your part.

| | |
|---|---|
| Emulation speed | **60 fps locked** to the audio DAC on every game tested |
| Frames drawn | 46 to 60 of every 60, depending on the game (audio never gaps; see [How it fits](#how-it-fits-in-512-kb)) |
| Per-frame cost, Super Mario Bros. in play | 6502 7.0 ms, PPU 5.3 ms, APU 0.2–1.5 ms, display conversion 2.7 ms |
| Free heap in a game, BLE + audio running | 205 KB of 512 KB |
| Firmware image | 0.83 MB (nofrendo + ESP-IDF + NimBLE) |
| Game data | 4.0 MB for 20 games with covers, executed in place from a 6 MB flash partition |
| Mappers | 0 NROM, 1 MMC1, 2 UxROM, 3 CNROM, 4 MMC3, 9 MMC2, plus the NSF player for menu music |
| Input | BLE HID gamepad (Xbox Wireless Controller tested); the two onboard buttons do everything else |
| Audio | emulated 2A03 APU, 20.05 kHz mono, ES8311 codec over I2S DMA |
| Display | ST7789V2 over SPI at 80 MHz with DMA, portrait 240×240 or landscape 256×240, switchable |

Every number in this README was measured on the device over serial.

From the same board as [PELLETINO](https://github.com/aedile/PELLETINO) (tilt
Pac-Man), [FIESTACADE](https://github.com/aedile/FIESTACADE) (twenty-six arcade
games) and [DIABLITO](https://github.com/aedile/DIABLITO) (shareware Doom).
*Codename NESTOR: the CMake project and some log tags keep that name.*

---

## Contents

- [Why this is hard on a C6](#why-this-is-hard-on-a-c6)
- [How it fits in 512 KB](#how-it-fits-in-512-kb)
- [Getting it running](#getting-it-running)
- [Which controllers work](#which-controllers-work)
- [Controls](#controls)
- [The flow: splash, picker, demo mode](#the-flow-splash-picker-demo-mode)
- [Adding games, covers and music](#adding-games-covers-and-music)
- [Repository layout](#repository-layout)
- [Status and known gaps](#status-and-known-gaps)
- [Credits and license](#credits-and-license)

---

## Why this is hard on a C6

The usual ESP32 NES builds run on a dual-core ESP32 or S3 with PSRAM: one core
emulates, the other pushes pixels, and the ROM is copied into megabytes of RAM.
The C6 has none of that.

- **One core.** Emulation, the display, audio, the Bluetooth stack and the UI all
  share a single 160 MHz RISC-V core. A frame is 16.7 ms; the 6502 and PPU alone
  take about 12 of them.
- **512 KB of RAM, total.** NimBLE takes about 90 KB of it. There is no room to
  copy a 512 KB cartridge anywhere.
- **A slow pipe to the screen.** A full frame is 123 KB over SPI: 13 ms at
  80 MHz, most of a frame on its own.
- **BLE only.** The C6 has no Bluetooth Classic radio, which rules out most
  gamepads on the market. See [Which controllers work](#which-controllers-work).

## How it fits in 512 KB

- **Cartridges execute in place.** PRG and CHR ROM are read directly from a
  memory-mapped flash partition through the cache. Only PRG RAM, CHR RAM, the
  nametables and one 8-bit palette-indexed frame live in SRAM. The C6 cannot map
  a whole 6 MB partition, so the firmware reads the image's table first and maps
  only the bytes in use.
- **The display overlaps emulation.** The core calls back every 16 scanlines; that
  strip is converted to RGB565 and queued for SPI DMA while the next 16 lines are
  being emulated. Without the overlap the 13 ms push serialised behind a 13 ms
  frame and Super Mario Bros. ran at 38 fps. With it, the push costs 2.7 ms of CPU.
- **Audio paces everything.** The APU's 334 samples per frame go into a small I2S
  DMA queue and the blocking write locks emulation to the DAC clock. When the
  queue runs low, the next frame emulates without drawing. Sound never gaps; the
  picture drops frames only as fast as the game is slow.
- **Hot paths in IRAM.** The 6502 core, `ppu_renderline` and `apu_process` run from
  RAM rather than the flash cache, for 6 KB of heap.
- **Menus reuse the core.** The menu music is the DuckTales Moon theme as an NSF,
  played through the same emulated APU with the PPU idle. The splash and menus
  push their frames in strips under the music at 30 fps using the same callback.
- **The radio sleeps when nobody is there.** On the pairing screen the scanner
  listens 60% of the time; in demo mode, 3%.

In a game the firmware logs fps, skipped frames, audio underruns and free heap
every five seconds, with the per-subsystem breakdown.

## Getting it running

### 1. Prerequisites

- A [Waveshare ESP32-C6-LCD-1.69](https://www.waveshare.com/esp32-c6-lcd-1.69.htm) and a USB-C cable.
- Docker. The build runs in Espressif's `espressif/idf:v5.3.4` image; no local ESP-IDF is needed.
- `esptool` on the host for flashing (`brew install esptool` on a Mac). Docker on macOS cannot reach USB.
- Python 3 for the tools. The packer and fetchers use only the standard library; the serial monitor and key driver need `pyserial` (the Python that ships inside Homebrew's esptool already has it).

### 2. Clone

```sh
git clone https://github.com/aedile/FIESTA-ENTERTAINMENT-SYSTEM.git
cd FIESTA-ENTERTAINMENT-SYSTEM
```

### 3. Supply games

`ROMS/` is ignored by git and ships empty. Put your own cartridge dumps in it,
as `.nes` files or zips containing one, then fetch covers and the menu music:

```sh
mkdir -p ROMS && cp /path/to/your/*.zip ROMS/
tools/fetch_art.py          # box art    -> ROMS/art/<name>.png
tools/fetch_music.py        # menu music -> ROMS/music/menu.nsf  (needs bsdtar)
```

Both fetches are optional: a game without art gets a plain card, and without the
NSF the menus are silent.

### 4. Build

```sh
./build.sh
```

The build packs every ROM and cover into one image for the `roms` partition and
prints the inventory with each game's mapper. A game whose mapper is not in the
table above will show "Unsupported ROM" on the device.

### 5. Flash

```sh
./flash.sh                  # or ./flash.sh /dev/cu.usbmodemXXXX
```

This writes the bootloader, partition table, firmware and the ROM image. After a
flash the board resets twice; that is the USB serial port closing, not a crash.

### 6. First boot

The splash plays (any button skips it), then the controller screen. Put a
supported gamepad in pairing mode and hold it against the medal. With no
controller, demo mode starts after 30 seconds.

### Troubleshooting

- **"Unsupported ROM".** The cartridge's mapper is not built in. The build log
  lists each game's mapper; adding one from retro-go's nofrendo is a file copy
  and a line in `components/nofrendo/mappers/mappers.h`.
- **A pad never appears on the controller screen.** It is almost certainly a
  Bluetooth Classic pad. See the next section.
- **Watching it work.** `tools/drive.py 20` resets the board and prints 20 seconds
  of serial log. Keys typed into it act as a pad (w/a/s/d, `j` A, `k` B, `q` Start,
  `e` Select, `m` Menu; `n` and `l` stand in for the PWR and BOOT buttons, `x`
  jumps to demo mode), so the whole UI can be driven without a controller.

## Which controllers work

The ESP32-C6 has **Bluetooth Low Energy only**, so the pad must speak HID over
BLE. Most gamepads do not; they use Bluetooth Classic, which this board cannot
hear at all, in any mode, whatever the pad's mode switch says.

| Works (BLE HID) | Does not work (Bluetooth Classic) |
|---|---|
| Xbox Wireless Controller, 2016 onward (**tested**) | 8BitDo Micro, Zero 2 and the other small 8BitDo pads (**tested**: every mode is Classic; the BLE they advertise is only a configuration channel) |
| Reported working on BLE-only ESP32s in [Bluepad32 #154](https://github.com/ricardoquesada/bluepad32/issues/154), untested here: Terios T3, BSP-D11, ShanWan Q36/Q37 **with an X/D/V switch** in the D position | ShanWan Q36 **with an X/S/P switch** (**tested**: emits no BLE at all) |
| Per 8BitDo, untested here: Ultimate 2C, Ultimate 2, Ultimate 3-mode and Pro 3 in D-input mode | PS4 DualShock 4, PS5 DualSense, Switch Pro Controller, Joy-Con |

Before buying a pad for this, scan it with a BLE scanner app such as nRF Connect
while it is in pairing mode. If it does not show up there, it will not show up
here. The firmware will connect to any named device held against the medal on the
pairing screen and reject it cleanly if it has no HID service, so a wrong pad
fails fast instead of half working.

## Controls

Gamepad, Nintendo positions (on an Xbox pad, B is NES A and A is NES B):

| Screen | Controls |
|---|---|
| Picker | D-pad left/right, A play, B toggle the game in or out of the demo rotation, Select mute, Start portrait/landscape, Y or a shoulder button for the controller screen |
| Game | Y or a shoulder button opens the menu: Resume, Save state, Load state, Reset game, Mute, Return to picker, Controller |
| Controller screen | Back, Forget this controller |

Medal buttons, on every screen, no controller needed:

| Button | Short press | Hold |
|---|---|---|
| PWR | next game (demo), move right (picker), leave the game | 2 s: power off |
| BOOT | lock or unlock the demo to the current game | 3 s: mute; 10 s: forget the controller |

## The flow: splash, picker, demo mode

Splash → controller screen → box-art picker → game.

- **Splash.** A cold open races FIESTA across the screen, then hard-cuts through
  box art from the ROM partition while ENTERTAINMENT SYSTEM and FIESTA 2027 scroll
  through a letterbox band. It lands on fireworks over the Tower of the Americas.
  About 34 seconds, skippable.
- **Demo mode** starts when no pad connects within 30 s, or a connected pad is
  idle for 3 minutes. It shows an 18 s attract card, then each game's own attract
  mode for 2 minutes, in a loop. Any pad button returns to the picker. Battery
  saves are neither loaded nor written in demo mode.
- **Lock** the demo to one game with BOOT; the choice survives a reboot, so a
  medal can be "the Contra one" all day.
- Games whose title screens never demo are left out of the rotation by a default
  list at the top of `main/main.c`; B in the picker overrides it per device.

Kept in NVS: the paired controller, demo lock, per-game demo exclusion, mute and
orientation. Battery RAM and save states live in their own NVS partition.

## Adding games, covers and music

Drop a zip in `ROMS/`, run `tools/fetch_art.py`, rebuild and flash. Nothing is
hard-coded; the picker shows whatever the partition holds. Covers come from the
libretro-thumbnails project, whose files use the same No-Intro names as the ROMs
(the fetcher tries looser region and revision tags when the exact name is
missing). To use your own art, put a PNG at `ROMS/art/<rom name>.png`. The build
quantises covers to a 180-colour cube with ordered dithering, in pure Python.

The menu music is whatever NSF sits at `ROMS/music/menu.nsf`; the track number is
`MUSIC_TRACK` in `main/main.c`.

## Repository layout

```
main/            app: flow, picker, menus, demo mode, splash, festive drawing, saves, music
components/
  display/       ST7789 driver, DMA strip push, runtime orientation (from PELLETINO)
  audio_hal/     ES8311 + I2S DMA, queue-depth accounting (from PELLETINO)
  ble_pad/       NimBLE scanning, HID report-descriptor parsing, pad mapping, pairing in NVS
  esp_hid/       ESP-IDF's HID host, vendored with NimBLE fixes (see LICENSING.md)
  nofrendo/      the NES core (GPL-2.0), trimmed to the mappers used
tools/           ROM/art packer, art and music fetchers, serial monitor and key driver
partitions.csv   nvs, phy, app 1.3 MB, roms 6 MB, saves (NVS) 512 KB, on a 16 MB part
```

## Status and known gaps

- **Frame time is saturated.** The 6502 plus the PPU take 12 to 13 ms of a
  16.7 ms frame on heavy games, so 3 to 14 frames per 60 go undrawn there.
  Getting to zero means work inside the core.
- **Very few gamepads speak BLE HID.** Only the Xbox Wireless Controller has
  been verified here. Reports of other working pads are welcome.
- **Three fixes to ESP-IDF's NimBLE HID host live in a vendored copy**
  (`components/esp_hid`): it hung forever on a GATT read error, never paired
  before discovery, and dropped every input report from pads with no Protocol
  Mode characteristic. They have not been sent upstream yet.
- **The battery gauge is uncalibrated.** The ×3 divider and the 3.3–4.2 V linear
  map are marked in `main/medal.c`. Auto power-off only trusts readings inside a
  plausible window, so a wrong scale can never switch the medal off.
- **Battery life** was 7 to 9 hours on the author's medal before the scanner was
  duty-cycled; it has not been re-measured since.
- **Six mappers.** Anything else needs its mapper file added.
- **No hero video or photos yet** in this repository.

## Credits and license

- **Matthew Conte** for nofrendo, and **ducalex** for the
  [retro-go](https://github.com/ducalex/retro-go) fork this port started from.
- **Espressif** for ESP-IDF and NimBLE.
- **Daniel Hepper / Marcel Sondaar** for font8x8 (public domain).
- **libretro-thumbnails** for box art and the **joshw NSF archive** for the music
  rip, both fetched at build time for your own build and not redistributed here.
- **nevsie** for the hands-on BLE gamepad list in Bluepad32 #154.
- [PELLETINO](https://github.com/aedile/PELLETINO), for the display and audio drivers.
- Built with Claude Code.

Code written for this project is MIT (`LICENSE`). The NES core is GPL-2.0 and the
vendored HID host is Apache-2.0, each with its modifications listed in
[LICENSING.md](LICENSING.md). No game ROMs, box art or music are distributed here.
Fiesta San Antonio and Nintendo are not affiliated with this project.
