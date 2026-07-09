#include "furi_hal_usb.h"
#include "furi_hal_usb_hid.h"
#include "furi_hal_usb_hid_backend.h"
#include "furi_hal_usb_hid_u2f.h"

#include <stddef.h>

/* No-op HID backend for SoCs without USB-OTG (e.g. ESP32-C6).
 * Tracks only the connection-state callback so BLE HID can coexist
 * while the BadUSB USB interface remains unsupported. */

static HidStateCallback s_user_cb = NULL;
static void* s_user_ctx = NULL;
static bool s_connected = false;

bool furi_hal_usb_hid_backend_start(const FuriHalUsbHidConfig* cfg) {
    (void)cfg;
    return false;
}

void furi_hal_usb_hid_backend_stop(void) {
    s_connected = false;
    if(s_user_cb) s_user_cb(false, s_user_ctx);
}

bool furi_hal_hid_is_connected(void) {
    return s_connected;
}

uint8_t furi_hal_hid_get_led_state(void) {
    return 0;
}

void furi_hal_hid_set_state_callback(HidStateCallback cb, void* ctx) {
    s_user_cb = cb;
    s_user_ctx = ctx;
    if(cb) cb(s_connected, ctx);
}

bool furi_hal_hid_kb_press(uint16_t button) {
    (void)button;
    return false;
}

bool furi_hal_hid_kb_release(uint16_t button) {
    (void)button;
    return false;
}

bool furi_hal_hid_kb_release_all(void) {
    return false;
}

bool furi_hal_hid_mouse_move(int8_t dx, int8_t dy) {
    (void)dx;
    (void)dy;
    return false;
}

bool furi_hal_hid_mouse_press(uint8_t button) {
    (void)button;
    return false;
}

bool furi_hal_hid_mouse_release(uint8_t button) {
    (void)button;
    return false;
}

bool furi_hal_hid_mouse_scroll(int8_t delta) {
    (void)delta;
    return false;
}

bool furi_hal_hid_consumer_key_press(uint16_t button) {
    (void)button;
    return false;
}

bool furi_hal_hid_consumer_key_release(uint16_t button) {
    (void)button;
    return false;
}

bool furi_hal_hid_consumer_key_release_all(void) {
    return false;
}

/* U2F/CTAPHID is unsupported without USB-OTG; no-op everything. */
bool furi_hal_usb_hid_u2f_backend_start(void) {
    return false;
}

void furi_hal_usb_hid_u2f_backend_stop(void) {
    s_connected = false;
}

void furi_hal_hid_u2f_set_callback(HidU2fCallback callback, void* context) {
    (void)callback;
    (void)context;
}

bool furi_hal_hid_u2f_is_connected(void) {
    return false;
}

uint32_t furi_hal_hid_u2f_get_request(uint8_t* data) {
    (void)data;
    return 0;
}

void furi_hal_hid_u2f_send_response(uint8_t* data, uint8_t len) {
    (void)data;
    (void)len;
}
