# F.E.S. — Fiesta Entertainment System

An NES emulator that lives inside a wearable **Fiesta San Antonio 2027**
medal. The medal is a Waveshare ESP32-C6-LCD-1.69: one RISC-V core at 160 MHz,
512 KB of RAM, a 240x280 LCD, a speaker, a battery and two buttons. On its
own it runs a cinematic splash and cycles through the games' attract modes
with fireworks, papel picado and dancing mariachis; pair a Bluetooth gamepad
and it's a console.

*Codename NESTOR. The project directory and CMake target keep that name.*

## What it does

* Runs NES games straight out of flash (mappers 0, 1, 2 and 9: Super Mario
  Bros., Zelda, Metroid, Contra, Castlevania, Punch-Out!!, Ninja Gaiden,
  Excitebike, DuckTales, Tetris and friends) at a locked 60 fps with sound.
* Pairs with a BLE HID gamepad (Xbox Wireless Controller; see "Which
  controllers work"), remembers it, reconnects on boot.
* Box-art carousel picker, in-game menu with save states and battery saves.
* **Demo mode** when nobody is playing: an attract card, then every game's own
  attract sequence in turn. Lock it to one game with a button, or leave a game
  out of the rotation.
* Works without a controller: the two onboard buttons skip, lock, mute and
  power off.
* A splash worth watching: FIESTA races past, hard cuts to box art with
  ENTERTAINMENT scrolling through, then fireworks over the Tower of the
  Americas, with the DuckTales Moon theme playing through the emulated APU.

## Hardware

Waveshare ESP32-C6-LCD-1.69: ST7789V2 240x280 panel on SPI (80 MHz, DMA),
ES8311 codec on I2S, QMI8658 IMU (unused), BOOT and PWR buttons, battery
rail enable on GPIO 15, battery sense on ADC1 channel 0. No PSRAM, no SD card.

## Building

The build runs in Espressif's Docker image; nothing is installed on the host
except `esptool` (Homebrew) for flashing.

```sh
./build.sh                 # idf.py build in espressif/idf:v5.3.4 -> build_docker/
./flash.sh [port]          # bootloader, partition table, app and ROM image
tools/monitor.py [secs]    # reset and print the serial log
```

Before the first build, put game content in `ROMS/` (ignored by git):

```sh
cp your/*.zip ROMS/        # .zip with a .nes inside, or bare .nes files
tools/fetch_art.py         # box art -> ROMS/art/<name>.png
tools/fetch_music.py       # menu music -> ROMS/music/menu.nsf
```

The build packs every ROM and its cover into a flash image for the `roms`
partition and flashes it with the app. Nothing is hard-coded: add a zip,
rebuild, and it appears in the picker. Games without art get a plain card;
without the NSF the menus are silent.

Bench testing without a controller: keys typed into the serial monitor act as
a pad (w/a/s/d, j = A, k = B, q = Start, e = Select, m = Menu; n and l stand
in for the PWR and BOOT buttons, x jumps to demo mode). `tools/drive.py`
scripts them.

## Which controllers work

The ESP32-C6 has **Bluetooth Low Energy only**, so the pad must speak HID over
BLE. Most gamepads do not; they use Bluetooth Classic, which this board cannot
hear at all, in any mode, whatever the pad's mode switch says.

| Works (BLE HID) | Does not work (Bluetooth Classic) |
|---|---|
| Xbox Wireless Controller, 2016 onward (tested) | 8BitDo Micro, Zero 2, SN30 Pro and the other small 8BitDo pads: their S, D and K modes are all Classic; the BLE they advertise is only a configuration channel |
| Stadia controller after Google's Bluetooth update (untested) | PS4 DualShock 4, PS5 DualSense |
| | Switch Pro Controller, Joy-Con |

Before buying a pad for this, check that it lists Bluetooth LE HID support
explicitly. The firmware will connect to a close-by device on the pairing
screen and reject it if it has no HID service, so a wrong pad fails fast
rather than half-works.

## Controls

Gamepad, Nintendo positions (on an Xbox pad, B is NES A and A is NES B):

| Screen | Controls |
|---|---|
| Picker | D-pad left/right, A play, B toggle demo rotation, Select mute, Start portrait/landscape, Y or shoulder = controller screen |
| Game | Y or a shoulder button opens the menu: Resume, Save state, Load state, Reset, Mute, Return to picker, Controller |
| Controller screen | Back, Forget this controller |

Medal buttons, no controller needed:

| Button | Short press | Hold |
|---|---|---|
| PWR | next game (demo), move right (picker), leave game | 2 s: power off |
| BOOT | lock/unlock the demo to the current game | 3 s: mute, 10 s: forget the controller |

## The flow

Splash (any button skips) → controller screen → picker → game. If no pad
connects within 30 s, or a connected pad is idle for 3 minutes, demo mode
starts: an 18 s attract card, then each game for 2 minutes with a title card
between. Any pad button returns to the picker. Settings that persist in NVS:
the paired controller, demo lock, per-game demo exclusion, mute, orientation.

Games on the default no-demo list (title screens that never demo) are set in
`main/main.c`; B in the picker overrides that per device.

## How it fits in 512 KB

* PRG and CHR ROM are executed in place from the memory-mapped flash
  partition; only PRG RAM, CHR RAM, nametables and the 8-bit frame live in
  SRAM. About 205 KB of heap stays free in a game with BLE up.
* The PPU renders into a palette-indexed frame; every 16 scanlines a strip is
  converted to RGB565 and queued for SPI DMA, so the transfer overlaps
  emulation of the next strip.
* The APU's 334 samples per frame go into a 5-descriptor I2S DMA queue; the
  blocking write paces emulation to the DAC clock. When the queue runs low
  the next frame skips drawing, so audio never gaps.
* Menus and the splash play the NSF through the same core with the PPU idle,
  pushing their frames in strips under the music at 30 fps.

Per-frame numbers are logged every 5 s in a game (CPU, PPU, APU, push,
DMA wait, audio wait, underruns, heap).

## Layout

```
main/            app: flow, picker, menus, demo, splash, festive drawing, saves, music
components/
  display/       ST7789 driver, DMA strip push (from PELLETINO)
  audio_hal/     ES8311 + I2S DMA (from PELLETINO)
  ble_pad/       NimBLE scanning, HID report parsing, pad mapping, NVS pairing
  esp_hid/       ESP-IDF's HID host, vendored with NimBLE fixes
  nofrendo/      the NES core (GPL-2.0), trimmed to the mappers used
tools/           ROM/art packer, art and music fetchers, serial monitor and driver
partitions.csv   nvs, phy, app 1.3 MB, roms 2.3 MB, saves (NVS) 320 KB
```

## Licensing

Code written for this project is MIT. The NES core is GPL-2.0, so a
distributed firmware image is GPL-2.0 as a whole (source is here). ROMs, box
art and music are not included and belong to their owners. Details, including
the list of modifications to third-party code, are in `LICENSING.md`.

## Credits

nofrendo by Matthew Conte, as maintained in retro-go by ducalex. ESP-IDF by
Espressif. font8x8 by Daniel Hepper. Box art via libretro-thumbnails. NSF via
the joshw NSF archive. PELLETINO (the author's Pac-Man medal) for the display
and audio drivers. Built with Claude Code.
