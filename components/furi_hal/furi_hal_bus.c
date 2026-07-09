#include "furi_hal_bus.h"

static bool s_bus_state[FuriHalBusMAX] = {0};

void furi_hal_bus_enable(FuriHalBus bus) {
    if (bus < FuriHalBusMAX) {
        s_bus_state[bus] = true;
    }
}

void furi_hal_bus_disable(FuriHalBus bus) {
    if (bus < FuriHalBusMAX) {
        s_bus_state[bus] = false;
    }
}

bool furi_hal_bus_is_enabled(FuriHalBus bus) {
    if (bus < FuriHalBusMAX) {
        return s_bus_state[bus];
    }
    return false;
}
