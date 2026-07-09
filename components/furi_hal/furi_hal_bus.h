#pragma once

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FuriHalBusTIM1,
    FuriHalBusTIM2,
    FuriHalBusTIM3,
    FuriHalBusTIM14,
    FuriHalBusTIM15,
    FuriHalBusTIM16,
    FuriHalBusTIM17,
    FuriHalBusMAX,
} FuriHalBus;

void furi_hal_bus_enable(FuriHalBus bus);
void furi_hal_bus_disable(FuriHalBus bus);
bool furi_hal_bus_is_enabled(FuriHalBus bus);

#ifdef __cplusplus
}
#endif
