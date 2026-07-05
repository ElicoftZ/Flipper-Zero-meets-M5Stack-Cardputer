# Beta Firmware — M5Stack Cardputer-ADV

Beta build for the **M5Stack Cardputer-ADV** (ESP32-S3FN8, no PSRAM).

**File:** `Flipper-cardputer_adv-merged.bin` — single merged image (bootloader +
partition table + app), flash at offset **`0x0`**.

Built: 2026-07-05

## Changes in this build
- **Bluetooth crash guard** — enabling BT now checks free RAM first and refuses
  gracefully ("Not enough RAM") instead of OOM-rebooting the firmware. Best-effort
  connect to the Flipper Mobile app (RAM ceiling unchanged — no PSRAM).
- **WiFi soft-reset warning** — the WiFi app opens with a "This app can trigger a
  soft reset" Back/OK dialog before touching the radios.
- **NRF24 jammer fix** — CE pin corrected from GPIO43 (T-Embed) to GPIO4 on this
  board, so the jammer can key TX.
- **New GPIO app** — manual GPIO control on the Grove/Qwiic port (G2/GPIO2,
  G1/GPIO1). Pin table is editable in `components/furi_hal/furi_hal_resources.c`
  for other PCBs. (USB-UART bridge deferred to a later build.)

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
