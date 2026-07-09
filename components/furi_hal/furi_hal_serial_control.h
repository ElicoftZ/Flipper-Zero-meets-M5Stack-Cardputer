#pragma once

#include "furi_hal_serial_types.h"

#ifdef __cplusplus
extern "C" {
#endif

FuriHalSerialHandle* furi_hal_serial_control_acquire(FuriHalSerialId id);
void furi_hal_serial_control_release(FuriHalSerialHandle* handle);

#ifdef __cplusplus
}
#endif
