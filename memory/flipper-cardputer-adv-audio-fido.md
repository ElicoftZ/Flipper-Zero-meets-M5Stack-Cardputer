---
name: flipper-cardputer-adv-audio-fido
description: Cardputer-ADV mic is the ES8311 codec ADC (not PDM); Recorder app + ZeroFIDO/U2F-HID internalized
metadata: 
  node_type: memory
  type: project
  originSessionId: fa388d06-0ff9-46aa-814c-219c49a565d4
---

2026-07-06 work on the Cardputer-ADV port (see [[flipper-cardputer-adv-fap-apps-quirks]], [[flipper-duplicate-app-trees]]).

**Mic hardware — IMPORTANT correction:** the ADV has **no PDM/SPM1423 mic**. Its ES8311
codec *replaces* the standard Cardputer's NS4168+SPM1423; the mic is a MEMS mic on the
**ES8311 ADC**, read over I2S. The board header's `BOARD_PIN_MIC_DATA=46` /
`BOARD_PIN_MIC_CLK=43` are **inherited-and-misleading** (copied from the standard
Cardputer). Real capture path = the ES8311 on the shared I2S bus: BCLK=41, LRCK=43,
DAC-out DOUT=42, **ADC-in DIN=46**, no MCLK.

**Recorder app** (main menu, `MENUEXTERNAL`, `applications_user/recorder/`): records mono
16-bit PCM WAV to `/ext/recordings/rec_<ts>.wav`. Uses a new `furi_hal_mic.{c,h}` that
shares the speaker's I2S_NUM_0 as **full-duplex** (RX added to the TX-only channel in
`furi_hal_speaker.c`, `i2s_new_channel(&cfg,&tx,&rx)`, RX init made **non-fatal** so a
full-duplex hiccup can't brick boot). Mic reuses the speaker mutex for audio exclusivity;
records at the shared **44.1 kHz** clock. Mic **gain is tuned in `furi_hal_speaker.c`
`es8311_adc_seq[]`** — reg `0x16` = analog PGA (0..7), reg `0x17` = ADC digital volume
(0xBF≈0 dB .. 0xFF≈+32 dB). First hardware test was "so quiet" at 0x16=0x06/0x17=0xC8 →
bumped to **0x16=0x07, 0x17=0xFF** (awaiting confirm; if it clips, drop 0x17; if still
quiet, add SW gain in `furi_hal_mic_read`).

**ZeroFIDO** (FIDO2/CTAP2 passkey over USB CTAPHID) internalized from
github.com/MinorGlitch/zerofido into `applications_user/zerofido/` (USB profile,
`ZF_USB_ONLY`, 67 C files, `entry_point=zerofido_main`). The port already had the emulated
`furi_hal_crypto_enclave_*` it needs. Required a **U2F-HID transport** on TinyUSB:
`components/furi_hal/furi_hal_usb_hid_u2f.h` + backend in `furi_hal_usb_hid_tinyusb.c`
(FIDO usage page 0xF1D0, 64-byte IN/OUT, runtime keyboard↔U2F swap via
`tinyusb_driver_uninstall`+reinstall, `usb_hid_u2f` hooked in `furi_hal_usb.c`). **Key
gotcha:** multi-packet CTAPHID messages (PIN protocol ECDH keys) need a **RX FIFO** in the
transport — a single-slot buffer drops all but the last continuation frame, so PIN-protected
passkeys fail. Fixed with a 32-deep packet FIFO in `furi_hal_usb_hid_tinyusb.c`.

**Dolphin state moved to SD:** `dolphin_state.c` `DOLPHIN_STATE_PATH` now `EXT_PATH` (was
`INT_PATH`) to avoid internal-flash wear — the mood/XP system (Gemini's work) is RTC-cached
during use and flushed to SD. The `dolphin_init_state` SD-presence load gate is now
consistent with the SD save target.

**XP system tuning (2026-07-06, user spec):** all in `dolphin_deed.c` weights/limits tables
(kept in enum order — `_Static_assert` guards the counts). SubGHz=5, WiFi=2, BT=1 XP; daily
limits 20 except **Bluetooth=35**. **NRF24** = new deed `DolphinDeedNrf24Send` mapped to
**`DolphinAppSubGhz`** so it draws the SAME 20/day bucket ("shared with CC1101"), 5 XP;
call site in `applications/main/nrf24/scenes/scene_jam.c` on_enter. **IR 2 vs 4 XP**: new
deed `DolphinDeedIrSendExt` (4), chosen at the 3 IrSend call sites via
`furi_hal_infrared_get_tx_output() != FuriHalInfraredTxPinInternal` (external M5Unit IR
module) — added getter returning `active_tx_pin`. **Flush timeout = 25s** (`FLUSH_TIMEOUT_TICKS`
in `dolphin.c`, re-arms on each deed/level-up). WiFi/BLE deed call sites were already added by
Gemini (wlan_*, ble_spam_hal.c/ble_tracker_hal.c).

**Uptime through light sleep:** manual `esp_light_sleep_start()` freezes the FreeRTOS tick,
so `furi_get_tick()`-based uptime under-counts idle sleep. Added
`furi_hal_power_get_uptime_sec()` = `esp_timer_get_time()/1e6` (esp_timer IS advanced across
light sleep) and pointed `desktop_view_debug.c` + cli `uptime` at it.

**App-aware light sleep (2026-07-06):** the 2-min idle → `furi_hal_power_light_sleep()` in
`notification.c` (`notification_sleep_timer_callback`) now only sleeps when
**`loader_is_locked()` is false** — i.e. at the desktop/dolphin/main-menu. While an app/tool
is running (loader locked) it SKIPS sleep (CPU halt breaks BLE/USB/recording/timing); the
backlight still dims to the ~89% floor and a keypress restores it. Required adding `loader` to
`components/notification/CMakeLists.txt` REQUIRES + `#include <loader/loader.h>` (no dep cycle;
loader doesn't require notification).
