# Flipper Zero meets M5Stack Cardputer

A port of the [Flipper Zero](https://flipperzero.one/) firmware to the **M5Stack Cardputer** and **M5Stack Cardputer ADV**. This project brings the Flipper Zero UI, services, and application framework to affordable ESP32-S3 hardware.

## Discord

Join the [Flipper Zero meets ESP32 - Discord](https://discord.gg/5DnAqFXaBC) for support, releases, and announcements.

## Supported Hardware

![M5Stack Cardputer](pic1.heic)

| Feature | M5Stack Cardputer (Standard) | M5Stack Cardputer ADV |
|---|---|---|
| **MCU** | ESP32-S3 (8 MB Flash, No PSRAM) | ESP32-S3 (8 MB Flash, No PSRAM) |
| **Display** | ST7789V2 240×135 RGB565 | ST7789V2 240×135 RGB565 |
| **Keyboard** | 56-key matrix via 74HC138 | 56-key matrix via TCA8418 (I2C) |
| **Sub-GHz (CC1101)** | — | Yes (SPI3 shared with SD) |
| **NRF24 (2.4 GHz)** | — | Yes (CE=4, CSN=6) |
| **Infrared (IR)** | TX (GPIO 44) | TX (GPIO 44) + RX (GPIO 1) |
| **Audio** | NS4168 I2S Speaker Amp | ES8311 I2S Codec (I2C) |
| **IMU (Motion)** | — | BMI270 (I2C) |
| **RGB LED** | — | WS2812 (GPIO 21) |
| **SD Card Slot** | Yes (SPI3) | Yes (SPI3) |

![M5Stack Cardputer Side](pic2.heic)

---

## Navigation Key Mapping

The physical keyboard is fully mapped to navigate the Flipper Zero UI:

| Flipper Key | Cardputer Key |
|---|---|
| **Up** | `↑` (Fn + `;` keycap) |
| **Down** | `↓` (Fn + `.` keycap) |
| **Left** | `←` (Fn + `,` keycap) |
| **Right** | `→` (Fn + `/` keycap) |
| **OK** | `Enter` |
| **Back** | `Backspace` or `Esc` (`\`` keycap) |

*Full keyboard typing (printable ASCII characters) is supported in text fields.*

---

## How to Flash

### Method 1: Web Flasher (Easiest)

Connect your Cardputer over USB and use the online flasher in a Chrome/Edge browser:

👉 **[Flash via Browser](https://sor3nt.github.io/interface.html)**

### Method 2: Flash the Merged Binary Directly

After building, a single merged binary is automatically generated in the project root:

```powershell
# Flash the merged binary (using esptool)
esptool.py --chip esp32s3 -b 460800 write_flash 0x0 Flipper-cardputer_adv-merged.bin
```

---

## Setup SD Card

Most applications require database and asset files on an SD card. 
1. Format a MicroSD card to **FAT32**.
2. Download the SD card files from the releases section or use the starter files:
   [sdcard.zip](https://github.com/ElicoftZ/Flipper-Zero-ESP32-Port-meets-M5-Cardputer/releases)
3. Extract the contents directly to the root of the MicroSD card and insert it into your Cardputer.

---

## Building from Source

### Prerequisites

1. Install **[ESP-IDF v5.4.1](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32s3/get-started/)**.
2. Source the ESP-IDF export script:
   * **Linux/macOS**: `source ~/esp/esp-idf/export.sh`
   * **Windows (Standard CMD)**: `C:\Espressif\frameworks\esp-idf-v5.4.1\export.bat`

### Building on Windows (Recommended Wrapper)

We provide a custom script `build_adv.bat` in the root folder that configures your ESP-IDF python environments and triggers the build:

```powershell
# Run the bat script directly
.\build_adv.bat
```

Or you can use `winbuild.py` manually:

```powershell
# Build Cardputer ADV (Default)
python winbuild.py build --board cardputer_adv

# Build Standard Cardputer
python winbuild.py build --board cardputer
```

### Building on Linux / macOS

```bash
# Build Cardputer ADV
./build.sh --board cardputer_adv --build-only

# Build Standard Cardputer
./build.sh --board cardputer --build-only
```

---

## Credits

* **Sor3nt** for the original ESP32 port.
* Flipper Devices Inc. for the original firmware and application framework.
