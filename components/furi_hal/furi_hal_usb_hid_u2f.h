#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* U2F / FIDO CTAPHID transport (USB HID, FIDO usage page 0xF1D0, 64-byte raw
 * IN/OUT reports). Mirrors the stock Flipper firmware API so U2F/FIDO2 apps
 * (e.g. the stock U2F app, ZeroFIDO) build unchanged. Implemented on TinyUSB in
 * furi_hal_usb_hid_tinyusb.c; select it via furi_hal_usb_set_config(&usb_hid_u2f,
 * NULL). */
typedef enum {
    HidU2fDisconnected,
    HidU2fConnected,
    HidU2fRequest,
} HidU2fEvent;

typedef void (*HidU2fCallback)(HidU2fEvent ev, void* context);

/** Register the event callback (connect/disconnect/incoming request). */
void furi_hal_hid_u2f_set_callback(HidU2fCallback callback, void* context);

/** True while the U2F HID device is enumerated by a host. */
bool furi_hal_hid_u2f_is_connected(void);

/** Copy the last received 64-byte CTAPHID packet into `data`; returns its
 * length (0 if none pending). */
uint32_t furi_hal_hid_u2f_get_request(uint8_t* data);

/** Send a 64-byte CTAPHID response packet to the host. */
void furi_hal_hid_u2f_send_response(uint8_t* data, uint8_t len);

#ifdef __cplusplus
}
#endif
