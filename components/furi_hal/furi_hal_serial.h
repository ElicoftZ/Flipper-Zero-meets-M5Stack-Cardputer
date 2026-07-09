#pragma once

#include "furi_hal_serial_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FuriHalSerialAsyncRxCallback)(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context);

void furi_hal_serial_init(FuriHalSerialHandle* handle, uint32_t baud);
void furi_hal_serial_deinit(FuriHalSerialHandle* handle);
void furi_hal_serial_async_rx_start(FuriHalSerialHandle* handle, FuriHalSerialAsyncRxCallback callback, void* context, bool report_errors);
void furi_hal_serial_async_rx_stop(FuriHalSerialHandle* handle);
void furi_hal_serial_tx(FuriHalSerialHandle* handle, const uint8_t* buffer, size_t size);
void furi_hal_serial_tx_wait_complete(FuriHalSerialHandle* handle);
bool furi_hal_serial_async_rx_available(FuriHalSerialHandle* handle);
uint8_t furi_hal_serial_async_rx(FuriHalSerialHandle* handle);

#ifdef __cplusplus
}
#endif
