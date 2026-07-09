/**
 * @file btshim.h
 * BT service public API — ESP32 implementation
 *
 * Mirrors the STM32 bt.h API surface for compatibility.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <furi.h>
#include "../furi_ble/profile_interface.h"
#include "bt_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_BT "bt"

/* Minimum free internal RAM required before bringing up the BLE stack. The
 * controller + Bluedroid reserve ~64 KB the moment the radio starts; on this
 * no-PSRAM board (~200 KB total) starting below this threshold OOM-aborts
 * *inside* ESP-IDF and reboots the firmware. Every enable path checks this and
 * refuses gracefully instead. Tune on hardware. */
#define BT_MIN_FREE_INTERNAL (72 * 1024)
#define BT_MIN_LARGEST_INTERNAL (24 * 1024)

typedef struct Bt Bt;

typedef enum {
    BtStatusUnavailable,
    BtStatusOff,
    BtStatusAdvertising,
    BtStatusConnected,
} BtStatus;

typedef void (*BtStatusChangedCallback)(BtStatus status, void* context);

/** Change BLE Profile
 *
 * @param bt                 Bt instance
 * @param profile_template   Profile template to change to
 * @param params             Profile parameters. Can be NULL
 *
 * @return          profile instance, NULL on failure
 */
FuriHalBleProfileBase* bt_profile_start(
    Bt* bt,
    const FuriHalBleProfileTemplate* profile_template,
    FuriHalBleProfileParams params);

/** Stop current BLE Profile and restore default profile
 *
 * @param bt        Bt instance
 *
 * @return          true on success
 */
bool bt_profile_restore_default(Bt* bt);

/** Disconnect from Central
 *
 * @param bt        Bt instance
 */
void bt_disconnect(Bt* bt);

/** Set callback for Bluetooth status change notification
 *
 * @param bt        Bt instance
 * @param callback  BtStatusChangedCallback instance
 * @param context   pointer to context
 */
void bt_set_status_changed_callback(Bt* bt, BtStatusChangedCallback callback, void* context);

/** Forget bonded devices
 *
 * @param bt        Bt instance
 */
void bt_forget_bonded_devices(Bt* bt);

/** Set keys storage file path
 *
 * @param bt                    Bt instance
 * @param keys_storage_path     Path to file with saved keys
 */
void bt_keys_storage_set_storage_path(Bt* bt, const char* keys_storage_path);

/** Set default keys storage file path
 *
 * @param bt                    Bt instance
 */
void bt_keys_storage_set_default_path(Bt* bt);

/** Get current BT settings (thread-safe via message queue)
 *
 * @param bt        Bt instance
 * @param settings  Output settings
 */
void bt_get_settings(Bt* bt, BtSettings* settings);

/** Check if BT is enabled
 *
 * @param bt        Bt instance
 * @return          true if enabled
 */
bool bt_is_enabled(Bt* bt);

/** Get current BT status (Unavailable/Off/Advertising/Connected). Lightweight
 * direct read intended for status indicators (e.g. the status LED). */
BtStatus bt_get_status(Bt* bt);

/** Set BT settings (thread-safe via message queue)
 *
 * @param bt        Bt instance
 * @param settings  New settings
 */
void bt_set_settings(Bt* bt, const BtSettings* settings);

/** Stop BLE stack completely (blocking). Frees ~60KB internal RAM for WiFi. */
void bt_stop_stack(Bt* bt);

/** Restart BLE stack (blocking). Only starts if BT is enabled in settings. */
void bt_start_stack(Bt* bt);

/** Latch set by the WiFi app after it esp_bt_controller_mem_release()'s the BLE
 * controller to reclaim RAM. Once set, esp_bt_controller_init() would fault
 * (the controller's memory is gone), so every BLE start path MUST check
 * bt_is_mem_released() and refuse gracefully until the next reboot. */
void bt_mark_mem_released(void);
bool bt_is_mem_released(void);

#ifdef __cplusplus
}
#endif
