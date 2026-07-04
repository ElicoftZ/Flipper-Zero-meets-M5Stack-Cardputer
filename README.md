# Flipper Zero meets M5Stack Cardputer

A port of the [Flipper Zero](https://flipperzero.one/) firmware to the **M5Stack Cardputer** and **M5Stack Cardputer ADV**. This project brings the Flipper Zero UI, services, and application framework to affordable ESP32-S3 hardware.

## Discord

Join the [Flipper Zero meets ESP32 - Discord](https://discord.gg/5DnAqFXaBC) for support, releases, and announcements.

## Supported Hardware

![M5Stack Cardputer](pic1.png)

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

> ⚠️ M5Stack Cardputer is not available, Coming soon


![M5Stack Cardputer Side](pic2.png)

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

## Features of This Fork

This repository is optimized specifically for the **M5Stack Cardputer** and **Cardputer ADV** with the following enhancements:

* **Fixed Arrow Keys Navigation**: Solved the issue where Left and Right buttons behaved as Up and Down (which was caused by a rotary-encoder remap block meant for LilyGo T-Embed). Arrow keys now map correctly on both Standard and ADV keyboards.
* **Eli the Digital Pet**: Hardcoded the default name of the Flipper pet to **Eli** (replaces "Byte" and other random names from the eFuse MAC pool).
* **Auto-Merge Build Tool**: The build scripts (`winbuild.py` and `build.sh`) now automatically output a single, merged binary (`Flipper-cardputer_adv-merged.bin`) at `0x0` ready to flash, instead of three separate binaries.
* **Streamlined Cardputer-Only Repo**: Deleted all non-Cardputer targets and configuration files (LilyGo T-Embed, Waveshare C6, etc.) to keep the codebase clean, lightweight, and dedicated.

### What Your Flipper Zero Cardputer Can Do:
* 📡 **Sub-GHz RF Transceiver**: Fully supports transmit, receive, read raw, brute-force dictionaries, and TPMS decoding on the Cardputer ADV (via CC1101 SPI module).
* 🌐 **WiFi Pentesting**: Built-in Evil Portal (with custom HTML templates, live credential logs, and internet bridge), Deauther, WiFi Scanner, Handshake Capture, and Beacon Spammer.
* 📶 **NRF24 Tools**: Jammer (Protocol / Manual / WiFi / Activity scans) and MouseJacker for vulnerable wireless devices.
* 🖥️ **BLE & Bluetooth**: Apple Continuity BLE Spam, Google FastPair / SwiftPair spammers, FindMy Tag emulator, and BLE scanner.
* ⌨️ **BadUSB**: Run Ducky-script payloads over BLE or USB with custom physical keyboard layouts.
* 🪪 **NFC & RFID**: Read, save, emulate, and write NFC cards/tags (Mifare Classic, Ultralight, DESFire, EMV, etc.) using external modules.
* 📺 **Infrared Controller**: Send and learn IR signals. Universal TV/AC/Audio remote databases fully functional.
* 🎮 **Snake & Doom**: Full Doom engine port, allowing you to walk, turn, open doors, and play levels directly on your Cardputer!

---

## How to Flash

### Method 1: Web Flasher (Easiest)

Connect your Cardputer over USB and use the online flasher in a Chrome/Edge browser:
# (Coming soon)
 👉 **[Flash via Browser]**  

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

## How to Install (Step-by-Step)

### Step 1: Flash the Firmware
You can flash the pre-compiled merged binary directly to your Cardputer:
1. Connect your M5Stack Cardputer to your PC using a USB-C cable.
2. Download the latest `Flipper-cardputer_adv-merged.bin` (or build it yourself).
3. Use `esptool.py` to write the image directly to the flash offset `0x0`:
   ```powershell
   esptool.py --chip esp32s3 -b 460800 write_flash 0x0 Flipper-cardputer_adv-merged.bin
   ```
   *(Alternatively, you can use the Espressif GUI Flash Download Tool or any web flasher).*

### Step 2: Prepare the MicroSD Card
The firmware requires specific asset files to run Flipper Zero apps:
1. Format a MicroSD card to **FAT32**.
2. Download the **`sdcard.zip`** starter pack.
3. Extract the ZIP contents directly to the root directory of your MicroSD card.
4. Insert the card into your Cardputer's SD slot.

### Step 3: Boot Up
1. Toggle the power switch on the Cardputer to turn it on.
2. The Flipper home screen will load, showing your digital pet on the desktop!

---

## Credits

* **[Sor3nt](https://github.com/Sor3nt/Flipper-Zero-ESP32-Port)** for the original Flipper Zero port for ESP32.
* **[0xhalloween](https://github.com/0xhalloween/Flipper-Zero-ESP32-ADV)** for adding the M5Stack Cardputer support.
* **Flipper Devices Inc.** for the original firmware and application framework.


