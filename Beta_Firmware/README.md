# Beta Firmware — M5Stack Cardputer-ADV

Beta build for the **M5Stack Cardputer-ADV** (ESP32-S3FN8, no PSRAM).

**File:** `Flipper-cardputer_adv-merged.bin` — single merged image (bootloader +
partition table + app), flash at offset **`0x0`**.

Built: 2026-07-06

## New in this build (2026-07-06)
- **Recorder app (NEW)** — main-menu app. Records the built-in microphone (ES8311
  codec ADC, captured over full-duplex I2S) to a mono 16-bit PCM **WAV** on the SD
  card at `/ext/recordings/rec_<timestamp>.wav`. OK to start/stop, with a live
  level meter, elapsed timer and KB counter.
- **ZeroFIDO (NEW)** — FIDO2 / CTAP2 **passkey authenticator over USB** (CTAPHID),
  in the Applications menu. Credentials are stored on the device with on-screen
  approval; supports ClientPIN. Runs on a new U2F-HID transport built into the
  firmware (native ESP32-S3 USB HID, FIDO usage page 0xF1D0).
- **Status LED** — the WS2812 shows activity: orange on an action (scan / spam /
  copy / read), blue while connecting Bluetooth, red/green for battery; off during
  light sleep. Enable it from the lock menu ("Status LED").
- **Dolphin XP / mood on SD** — level, XP and mood are cached in RTC RAM during use
  and flushed to the **SD card** ~25 s after activity stops (avoids internal-flash
  wear); mood also updates for elapsed off-time on boot.
- **XP rewards per module** (each capped per day):
  - CC1101 / Sub-GHz — 5 XP (20/day, **shared** with NRF24)
  - NRF24 — 5 XP (20/day, shared with Sub-GHz)
  - Infrared — 2 XP built-in emitter, **4 XP** when an external M5Unit IR module is
    detected (20/day)
  - WiFi — 2 XP (20/day)
  - Bluetooth — 1 XP (35/day)
- **Uptime counts through light sleep** — the on-device uptime (debug view) and CLI
  `uptime` now use a clock that keeps running while the ESP32 is idle-asleep, so
  they reflect true "time on" instead of freezing during sleep.
- **Idle backlight floor raised to ~89%** (the panel reads as black below ~85%).

## Earlier in this beta line
- **Bluetooth crash guard** — enabling BT now checks free RAM first and refuses
  gracefully ("Not enough RAM") instead of OOM-rebooting the firmware. Best-effort
  connect to the Flipper Mobile app (RAM ceiling unchanged — no PSRAM).
- **WiFi soft-reset warning** — the WiFi app opens with a "This app can trigger a
  soft reset" Back/OK dialog before touching the radios.
- **NRF24 jammer fix** — CE pin corrected from GPIO43 (T-Embed) to GPIO4 on this
  board, so the jammer can key TX.
- **New GPIO app** — manual GPIO control:
  - *Manual Control* — quick presets on the Grove/Qwiic port (G2/GPIO2, G1/GPIO1);
    preset table editable in `components/furi_hal/furi_hal_resources.c`.
  - *Custom Pin (any GPIO)* — pick any valid GPIO at runtime (excludes flash
    GPIO26–32 and nonexistent 22–25), choose mode (output push-pull / input
    pull-up / pull-down / float), drive high/low, and live-read the level. Lets
    you use a custom PCB's pins without recompiling.
  - (USB-UART bridge deferred to a later build.)
- **Readable idle backlight** — the panel can't display below ~85% backlight, so
  the idle/dim floor is clamped to ~86% (idle no longer goes black).
- **Idle light sleep (app-aware)** — after 2 min with no input the device enters
  ESP32 light sleep (screen off, CPU halted, low power) **only when idling at the
  desktop / dolphin / menu**. If an app or tool is running (e.g. Bluetooth, a scan,
  recording), it does **not** sleep — halting the CPU would disrupt it; instead the
  backlight just dims to the readable ~89% floor and the app keeps running, with any
  keypress restoring full brightness. Wakes on any key or the power button; a timer
  fallback force-wakes it. Also inhibited while USB is connected.

## Flash instructions
Put the ESP32-S3 in the right mode and flash at offset `0x0`.

**esptool (CLI):**
```
esptool --chip esp32s3 -p <PORT> -b 460800 write_flash 0x0 Flipper-cardputer_adv-merged.bin
```

**Web flasher (no install, from Chrome/Edge):**
[esptool-js](https://espressif.github.io/esptool-js/) — connect, set address `0x0`,
select this file, Program.

> Beta build. Flashed and boot-confirmed on the author's unit; features beyond BT
> best-effort are hardware-dependent.
