/**
 * @file furi_hal_big_fap.h
 * "Big FAP Mode" — a transient, reboot-surviving flag that lets the no-PSRAM
 * Cardputer-ADV run heavy external apps ("big FAPs") by rebooting into a clean,
 * max-heap state with Bluetooth and WiFi kept off.
 *
 * The flag lives in RTC memory and is gated on the reset reason: only a
 * controlled esp_restart() (ESP_RST_SW) preserves the mode, so a physical reset
 * button or power-cycle automatically returns to normal.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True if the firmware booted into Big FAP Mode. Resolved once (gated on the
 *  reset reason) and cached for the rest of this boot. */
bool furi_hal_big_fap_is_active(void);

/** Enter Big FAP Mode: set the flag and soft-reset into the clean state.
 *  Does not return. */
void furi_hal_big_fap_enter(void);

/** Leave Big FAP Mode: clear the flag and soft-reset back to normal.
 *  Does not return. */
void furi_hal_big_fap_exit(void);

#ifdef __cplusplus
}
#endif
