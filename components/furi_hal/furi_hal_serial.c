#include "furi_hal_serial.h"
#include "furi_hal_serial_control.h"

struct FuriHalSerialHandle {
    FuriHalSerialId id;
    uint32_t baud;
};

static FuriHalSerialHandle s_serial_handles[FuriHalSerialIdMax] = {
    [FuriHalSerialIdUsart] = { .id = FuriHalSerialIdUsart, .baud = 0 },
    [FuriHalSerialIdLpuart] = { .id = FuriHalSerialIdLpuart, .baud = 0 },
};

FuriHalSerialHandle* furi_hal_serial_control_acquire(FuriHalSerialId id) {
    if (id < FuriHalSerialIdMax) {
        return &s_serial_handles[id];
    }
    return NULL;
}

void furi_hal_serial_control_release(FuriHalSerialHandle* handle) {
    (void)handle;
}

void furi_hal_serial_init(FuriHalSerialHandle* handle, uint32_t baud) {
    if (handle) {
        handle->baud = baud;
    }
}

void furi_hal_serial_deinit(FuriHalSerialHandle* handle) {
    if (handle) {
        handle->baud = 0;
    }
}

void furi_hal_serial_async_rx_start(FuriHalSerialHandle* handle, FuriHalSerialAsyncRxCallback callback, void* context, bool report_errors) {
    (void)handle;
    (void)callback;
    (void)context;
    (void)report_errors;
}

void furi_hal_serial_async_rx_stop(FuriHalSerialHandle* handle) {
    (void)handle;
}

void furi_hal_serial_tx(FuriHalSerialHandle* handle, const uint8_t* buffer, size_t size) {
    (void)handle;
    (void)buffer;
    (void)size;
}

void furi_hal_serial_tx_wait_complete(FuriHalSerialHandle* handle) {
    (void)handle;
}

bool furi_hal_serial_async_rx_available(FuriHalSerialHandle* handle) {
    (void)handle;
    return false;
}

uint8_t furi_hal_serial_async_rx(FuriHalSerialHandle* handle) {
    (void)handle;
    return 0;
}
