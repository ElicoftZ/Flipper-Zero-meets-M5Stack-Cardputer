---
name: flipper-cardputer-merge
description: What the Flipper project in Downloads is and how the Cardputer merge was assembled
metadata: 
  node_type: memory
  type: project
  originSessionId: 31fb26fd-5105-4003-ba49-daa1b28392ed
---

`C:\Users\Eli\Downloads\Flipper` is a **graft** of M5Stack Cardputer + Cardputer-ADV support onto the latest `Sor3nt/Flipper-Zero-ESP32-Port` base (an ESP-IDF Flipper-Zero port). The Cardputer support came from `0xhalloween/Flipper-Zero-ESP32-ADV`, which had fallen ~5 weeks behind Sor3nt and was a broken half-merge. Done 2026-06-29.

Cardputer pieces grafted in: `components/furi_hal/boards/board_m5stack_cardputer{,_adv}.h`, `targets/m5stack_cardputer{,_adv}/target_input.c`, `sdkconfig.defaults.m5stack_cardputer{,_adv}`, plus board entries in `winbuild.py`/`build.sh` and exclusions in `fam_config.py`.

Bugs fixed during the merge (all verified by building): per-board `SDKCONFIG_DEFAULTS` was never applied on the Windows path (would build Cardputer with PSRAM-on esp32s3 config → boot crash); 16 MB multiboot partition table on 8 MB flash; TinyUSB CDC disabled but base requires it; `furi_hal_display.c` LCD bus double-claimed GPIO39 (added `BOARD_PIN_LCD_SPI_MISO` guard); `furi_hal_sd.c` hardcoded SPI2 so Cardputer SD-on-SPI3 never initialised (added `BOARD_SD_SPI_HOST`/`SD_SEPARATE_BUS` path); ADV input driver used removed `furi_hal_i2c_bus.h` + `InputTypeText` (made self-contained / aliased).

All 5 boards build clean: m5stack_cardputer, m5stack_cardputer_adv, lilygo_t_embed_cc1101, esp32s3_generic, waveshare_c6_1.9. See [[flipper-esp-idf-build-env]] for how to build.

**On-hardware fixes for Cardputer-ADV (verified on the user's device via serial boot-log capture, 2026-06-30):** The ADV has LCD on SPI2 (pins 35/36/37) and SD+CC1101+NRF24 sharing ONE bus on pins 14/39/40 — that shared bus must be SPI3.
- Blank screen: `BOARD_CC1101_SPI_SHARED 0` (don't let SubGHz seize SPI2/the LCD's host) + added `#define BOARD_SUBGHZ_SPI_HOST SPI3_HOST` (furi_hal_spi.c subghz bus now honors it) so SubGHz shares SPI3 with the SD via CS-mux (CC1101 CS=13, SD CS=12).
- Keyboard (TCA8418 I2C): ADV `target_input.c` made self-contained (installs its own legacy I2C on KB_I2C_PORT=I2C_NUM_0). Verified `CFG readback=0x07`.
- **SD/apps/databases were broken because `furi_hal_sd.c` did NOT `#include "boards/board.h"`** → `BOARD_SD_SPI_HOST` invisible → SD fell to the `#else` (SPI2, SD_SEPARATE_BUS=0) → skipped by the pin-conflict check. Adding that include made the SD mount on SPI3 (shares with SubGHz). Verified: `/ext` lists 20 dirs, `/ext/apps` lists the 16 .fap files.

The user's SD (Sor3nt 1.1.5 layout, folders at card ROOT → firmware sees them as `/ext`) holds 16 **Xtensa** FAPs (e_machine 0x5e, target 32, API 1.0) — compatible with this firmware; they load via the Apps browser (`loader_applications.c` scans `EXT_PATH("apps")`). Debugging note: the ADV's USB-Serial-JTAG console (COM port) re-enumerates at app boot, so serial capture needs reset-into-app + reconnect (see scratchpad capture_serial.py). Outstanding user requests: graft NFC/NRF24/SubGHz from 0xhalloween with "module not found" messaging; strip firmware to Cardputer family only; deep HAL cleanup.
