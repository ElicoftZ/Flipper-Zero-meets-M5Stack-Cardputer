#pragma once
#include <stdint.h>
#include <furi_hal_resources.h>

#define MODULE_PINS_SETTINGS_PATH "/int/module_pins.settings"
#define MODULE_PINS_SETTINGS_MAGIC 0x50 // Fits in uint8_t
#define MODULE_PINS_SETTINGS_VER 1

typedef struct {
    uint8_t ir_tx;
    uint8_t ir_rx;
    uint8_t spi_sck;
    uint8_t spi_miso;
    uint8_t spi_mosi;
    uint8_t cc1101_csn;
    uint8_t cc1101_gdo0;
    uint8_t nrf24_csn;
    uint8_t nrf24_ce;
} ModulePinsSettings;

void furi_hal_module_pins_load(void);
void furi_hal_module_pins_save(const ModulePinsSettings* settings);
void furi_hal_module_pins_reset(void);
const ModulePinsSettings* furi_hal_module_pins_get(void);
