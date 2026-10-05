# Doom companion firmware

This is a separate ESP-IDF Doom app for the **Waveshare ESP32-C6-Touch-AMOLED-2.16**. Moss remains the factory app. Choosing **Games → Doom** in Moss saves the pet and reboots into Doom; a short press of the physical **PWR** button in Doom selects Moss and reboots back. Doom uses the original shareware episode and runs entirely on the device with touch and hardware button controls. It needs no Bluetooth connection, network, or host computer to play.

## Source and licenses

The engine and ESP32-C6 port started from [aedile/DIABLITO at commit `8602f28e31e9c35c7b6198f5235543cea9a53588`](https://github.com/aedile/DIABLITO/tree/8602f28e31e9c35c7b6198f5235543cea9a53588), which in turn credits id Software's Doom, Chocolate Doom (Simon Howard and contributors), Graham Sanderson's [RP2040 Doom](https://github.com/kilograham/rp2040-doom), and rsheldiii's [rp2040-doom-LCD](https://github.com/rsheldiii/rp2040-doom-LCD). This adaptation adds the 480 × 480 CO5300 panel, CST9220 touch, AXP2101 power control, Moss handoff, and this repository's flash map. Source history and per-file notices remain the authority for individual files.

| Material | License or distribution terms | Local notice |
| --- | --- | --- |
| Doom-derived engine | GNU GPL version 2 or later in its source headers | [`COPYING`](COPYING) and source headers |
| DIABLITO ESP-IDF port | Described by DIABLITO as GPLv2; check individual files before redistributing a combined image | [DIABLITO license notes](https://github.com/aedile/DIABLITO/tree/8602f28e31e9c35c7b6198f5235543cea9a53588#credits-and-license) |
| New RP2040 Doom code, including WHD decoding | BSD 3-Clause in source headers | Source headers; [RP2040 Doom](https://github.com/kilograham/rp2040-doom) |
| SH8601 display driver code | Apache 2.0 | SPDX headers and [`SH8601-LICENSE`](components/display/src/SH8601-LICENSE) |
| `emu8950.c` OPL emulator | MIT | Source header |
| ADPCM-XQ code by David Bryant | BSD-style 3-clause terms | [`license.txt`](components/doom/src/adpcm-xq/license.txt) |
| font8x8 by Daniel Hepper / Marcel Sondaar | Public domain, as credited by DIABLITO | [DIABLITO credits](https://github.com/aedile/DIABLITO/tree/8602f28e31e9c35c7b6198f5235543cea9a53588#credits-and-license) |
| [`doom1.whd`](build-artifacts/doom1.whd) game data | id Software **shareware** terms, separate from the engine's software licenses | [DIABLITO's game-data and license notes](https://github.com/aedile/DIABLITO/tree/8602f28e31e9c35c7b6198f5235543cea9a53588#credits-and-license) |

The top-level repository's MIT license does **not** replace the notices and conditions for the Doom companion. Preserve the source headers and license texts when redistributing it. There is a license compatibility question to resolve before distributing a combined binary: [GNU](https://www.gnu.org/licenses/license-compatibility.en.html) and [Apache](https://www.apache.org/licenses/GPL-compatibility.html) explain that Apache-2.0 is compatible with GPLv3, but not GPLv2. Doom source headers allow a later GPL version; DIABLITO's GPLv2 statement for its ESP-IDF port does not clearly say whether its port-only files do. Confirm that permission or use a compatible arrangement for the Apache-licensed files before a public binary release.

The WHD is a converted copy of the shareware `doom1.wad`; it is not GPL game data. Do not commit or distribute commercial WADs with this project. The bundled WHD is byte-for-byte the one in the cited DIABLITO commit (SHA-256 `1b3d0335b283e6132679291bf8ba026a16ef5442c3b97c00657a84e69d8100b4`). DIABLITO documents generation with RP2040 Doom's `whd_gen` and `-no-super-tiny`; the upstream repo and its submodules are needed to regenerate it. Confirm the shareware terms for any public repackaging of the converted data.

## Controls

The bottom 120 pixels of the screen have eight touch targets:

| Top row | LEFT | FWD | FIRE | MENU |
| --- | --- | --- | --- | --- |
| Bottom row | RIGHT | BACK | USE | MAP |

Hold a target to keep its action pressed. LEFT/RIGHT turn in a level and navigate horizontally in menus; FWD/BACK move and navigate vertically; FIRE confirms; USE backs out; MENU opens Doom's menu; MAP toggles the automap. Swipe horizontally across the game picture to change weapon. The picture occupies the top 360 pixels. The touch controller supplies one contact at a time.

The board's **KEY** button (GPIO 10) fires or confirms, and **BOOT** (GPIO 9) uses or backs out. During play, hold **BOOT** while touching FWD/BACK to run, or while touching LEFT/RIGHT to strafe and run; release BOOT to use doors and switches. Press **KEY + BOOT** together to open or close Doom's menu; release both before the next action. A short **PWR** press returns to Moss; Doom's Save Game menu is available before leaving. Doom's Quit action also returns to Moss. The PMIC handles a long PWR hold in hardware.

## Build and install

Build the Moss factory firmware first, so its generated partition image matches the checked-in layout. The Doom app uses ESP-IDF 5.3.4 in Espressif's Docker image:

```sh
./scripts/build.sh
docker run --rm -v "$PWD/firmware/doom":/project -w /project \
  espressif/idf:v5.3.4 idf.py -B build_docker build
python3 scripts/flash-doom.py --check-only \
  --app firmware/doom/build_docker/diablito.bin \
  --whd firmware/doom/build-artifacts/doom1.whd
python3 scripts/flash-doom.py /dev/cu.usbmodem101 \
  --app firmware/doom/build_docker/diablito.bin \
  --whd firmware/doom/build-artifacts/doom1.whd
./scripts/flash.sh /dev/cu.usbmodem101
```

Use the device's actual serial port in the install and Moss flash commands. The Doom installer validates the ESP32-C6 image, WHD, and partition table; reads the device's existing layout; makes and verifies a full 16 MiB backup in `backups/`; checks the installed Moss image fits the new 6 MiB reservation; clears the OTA boot selection; then writes and verifies WHD, Doom app, and partition table in that order. Flashing the rebuilt Moss image afterward gives its Games menu the new Doom address. This order keeps the factory app bootable throughout the transition. The Doom installer leaves the factory app, save slots, and both NVS partitions unwritten. It supports the original Moss layout and both Doom layouts used by this repository, so it can install or update Doom without clearing saves. Do **not** use `idf.py flash` for the companion: that command targets the factory address in a normal ESP-IDF build and can overwrite Moss.

| Flash region | Address | Size | Role |
| --- | ---: | ---: | --- |
| `factory` | `0x010000` | 6 MiB | Moss app |
| `doom` (`ota_0`) | `0x610000` | 4 MiB | Doom app image |
| `wad` | `0xA10000` | 4 MiB | Memory-mapped WHD |
| `saves` | `0xE10000` | 128 KiB | Doom save slots |
| `otadata` | `0xE30000` | 8 KiB | Selected boot app |
| `nvs`, `pet_nvs` | `0xFE0000`, `0xFF0000` | 64 KiB each | Shared settings and pet data |

The installer does not flash a new Moss image. To launch Doom, boot a Moss build with the Doom menu entry, then choose **Games → Doom**. Moss checks the installed app and WHD before switching boot partitions. On return, Moss reloads its saved pet state. If Doom cannot read its WHD or initialize shared NVS, it selects Moss and reboots.

## Board and memory caveats

- This pinout is for the 480 × 480 CO5300 AMOLED model, not DIABLITO's original 240 × 280 ST7789 LCD board. The CST9220 touch controller is on I2C address `0x5A` (SDA 8, SCL 7; reset 11), and the AXP2101 PMIC controls the display and audio rails. The KEY and BOOT buttons are GPIO 10 and 9.
- Doom uses the board's default physical orientation, regardless of the screen rotation selected in Moss.
- The ESP32-C6 has no PSRAM. The WHD is read from flash, and Doom's compact pointers can address only the first 256 KiB of SRAM. Startup checks that `.bss` still fits this window. The panel driver streams display strips instead of allocating a full 480 × 480 framebuffer.
- The 16 MiB flash holds both app slots, game data, saves, settings, and boot metadata. The 6 MiB Moss reservation leaves room above the current roughly 2.47 MB Moss image; the Doom app may use up to its 4 MiB slot. The included shareware WHD occupies about 2.07 MB of its separate 4 MiB data partition. Other WAD conversions are not validated on this board, even if they fit in flash.
- Audio uses the board's ES8311 codec. Display, touch, and audio load the same single 160 MHz core and internal memory; DIABLITO's measured frame rate on its smaller LCD is not a measured frame rate for this AMOLED adaptation.
- Doom's save slots live in the dedicated `saves` partition. The installer preserves that partition on repeat installs. Do not erase the entire flash if you need existing pet, settings, or Doom save data.
