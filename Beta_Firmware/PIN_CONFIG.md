# M5Stack Cardputer-ADV — Pin Config (for custom PCB design)

**MCU:** ESP32-S3FN8 (dual-core LX7, 8 MB in-package flash, **NO PSRAM**)
**Authoritative source:** `components/furi_hal/boards/board_m5stack_cardputer_adv.h`

This is how the beta firmware expects peripherals to be wired. If your custom PCB
routes a peripheral to a **different GPIO**, change the matching macro in the board
header (see "Remapping for a custom PCB" at the bottom) and rebuild.

---

## Pin map by function

### LCD — ST7789V2 240×135 (SPI2, its own bus)
| Signal | GPIO |
|--------|------|
| MOSI | 35 |
| SCLK | 36 |
| CS | 37 |
| DC | 34 |
| RST | 33 |
| BL (backlight) | 38 |
| MISO | n/a |

### Shared SPI3 bus — SD + CC1101 + NRF24 (CS-muxed)
Bus lines (common to all three):
| Signal | GPIO |
|--------|------|
| SCK | 40 |
| MISO | 39 |
| MOSI | 14 |

Per-device chip selects (this is the mux — each device gets its own CS):
| Device | CS | Extra |
|--------|----|-------|
| SD card | 12 | — |
| CC1101 (SubGHz) | 13 | GDO0 = 5 |
| NRF24L01 | 6 | CE = 4 |

### Keyboard — TCA8418 (I2C_NUM_0 @ 0x34)
| Signal | GPIO |
|--------|------|
| SDA | 8 |
| SCL | 9 |
| INT | 11 |

### Audio — ES8311 codec (I2S + I2C_NUM_0 @ 0x18)
| Signal | GPIO |
|--------|------|
| I2S BCLK | 41 |
| I2S LRCK/WCLK | 43 |
| I2S DOUT | 42 |
| I2S MCLK | 0 |

### Microphone (PDM)
| Signal | GPIO |
|--------|------|
| MIC DATA | 46 |
| MIC CLK | 43 *(shared with I2S LRCK)* |

### IMU — BMI270 (I2C_NUM_0 @ 0x68)
Shares the keyboard/codec I2C bus (SDA=8, SCL=9).

### IR
| Signal | GPIO |
|--------|------|
| TX | 44 |
| RX | 1 ⚠ *(same pin as Grove/NFC SCL — see conflicts)* |

### Grove / Qwiic port (I2C_NUM_1) — the external expansion header
| Signal | GPIO |
|--------|------|
| SDA | 2 |
| SCL | 1 |

NFC (WS1850S/PN532) rides this same Grove I2C bus (SCL=1, SDA=2, addr 0x28).

**This Grove port is the only free external I/O on the stock board, and it is
what the firmware's GPIO app drives** (labels "G2"=GPIO2, "G1"=GPIO1).

### RGB LED — WS2812
| Signal | GPIO |
|--------|------|
| DATA | 21 (1 LED) |

### Misc
| Signal | GPIO |
|--------|------|
| BOOT button | 0 *(also I2S MCLK)* |
| Battery sense | ADC on 10 |

### Not present on this board
Encoder/rotary · extra button · touch controller · power-enable pin ·
RFID/RDM6300 · BQ27220/BQ25896 fuel gauge (battery is read via ADC on GPIO10).

---

## Quick reference — by GPIO number
| GPIO | Used for |
|------|----------|
| 0 | BOOT button / I2S MCLK — **strapping pin** |
| 1 | IR RX **and** Grove/NFC SCL ⚠ conflict |
| 2 | Grove/NFC SDA (GPIO app "G2") |
| 4 | NRF24 CE |
| 5 | CC1101 GDO0 |
| 6 | NRF24 CSN |
| 8 | I2C0 SDA (keyboard/codec/IMU) |
| 9 | I2C0 SCL (keyboard/codec/IMU) |
| 10 | Battery ADC |
| 11 | Keyboard INT |
| 12 | SD CS |
| 13 | CC1101 CSN |
| 14 | SPI3 MOSI (SD/CC1101/NRF24) |
| 21 | WS2812 RGB LED |
| 33 | LCD RST |
| 34 | LCD DC |
| 35 | LCD MOSI (SPI2) |
| 36 | LCD SCLK (SPI2) |
| 37 | LCD CS (SPI2) |
| 38 | LCD backlight |
| 39 | SPI3 MISO (SD/CC1101/NRF24) |
| 40 | SPI3 SCK (SD/CC1101/NRF24) |
| 41 | I2S BCLK |
| 42 | I2S DOUT |
| 43 | I2S LRCK / MIC CLK |
| 44 | IR TX |
| 46 | MIC DATA — **strapping pin** |

---

## Reserved / do-not-use on ESP32-S3 (independent of this board)
- **GPIO26–32** — in-package SPI flash (ESP32-S3FN8). **Never route these.**
- **GPIO19 / GPIO20** — USB D− / D+ (USB-Serial-JTAG, used for console + flashing).
- **Strapping pins** — GPIO0, GPIO3, GPIO45, GPIO46. Usable but must idle at the
  correct boot level; avoid for anything that drives them at reset.

## Genuinely free MCU pins (not assigned by firmware)
GPIO **7, 15, 16, 17, 18, 47, 48** are unused by the firmware and safe for custom
peripherals. GPIO3 and GPIO45 are also unused but are strapping pins (use with care).

> ⚠ **Physical availability:** these "free" pins exist on the ESP32-S3 die, but
> whether they are broken out to a connector depends on the Cardputer-ADV's board
> layout. Confirm against the M5Stack Cardputer-ADV connector/schematic before
> committing your PCB. The one guaranteed-exposed external interface is the Grove
> HY2.0-4P port (GND, 5V, GPIO2, GPIO1).

---

## Flagged conflicts (verify against hardware)
- **GPIO1 = IR RX *and* Grove/NFC SCL.** Same physical pin is assigned to both in
  the board header. Fine if you never use IR-RX and the Grove I2C at the same time,
  but a custom PCB adding an I2C device on Grove should be aware of it.
- GPIO0 (BOOT + I2S MCLK) and GPIO43 (I2S LRCK + MIC CLK) are intentional reuse of
  a strapping pin / shared clock line.

---

## Remapping for a custom PCB
Two firmware places define pins — edit these, then rebuild:

1. **All peripheral pins** →
   `components/furi_hal/boards/board_m5stack_cardputer_adv.h`
   Change the `BOARD_PIN_*` macros (e.g. `BOARD_PIN_NRF24_CE`, `BOARD_PIN_CC1101_CSN`,
   `BOARD_PIN_QWIIC_SDA`, …) to your PCB's GPIO numbers.

2. **The GPIO app's user-controllable pin list** →
   `components/furi_hal/furi_hal_resources.c`, the `gpio_pins[]` table.
   Add a `static const GpioPin` + a `gpio_pins[]` row per pin your board exposes
   (`.name` is the on-screen label). Don't list pins wired to internal peripherals.

Build: `C:\Espressif\flipper_build.bat cardputer_adv` → `build_cardputer_adv/furi_esp32.bin`.
