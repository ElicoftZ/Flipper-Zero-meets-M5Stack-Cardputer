#include "furi_hal_module_pins.h"
#include BOARD_INCLUDE
#include <toolbox/saved_struct.h>
#include <esp_log.h>
#include <string.h>

static const char* TAG = "FuriHalModulePins";

static ModulePinsSettings s_module_pins = {
    .ir_tx = BOARD_PIN_IR_TX,
    .ir_rx = BOARD_PIN_IR_RX,
    .spi_sck = BOARD_PIN_SD_SCLK,
    .spi_miso = BOARD_PIN_SD_MISO,
    .spi_mosi = BOARD_PIN_SD_MOSI,
    .cc1101_csn = BOARD_PIN_CC1101_CSN,
    .cc1101_gdo0 = BOARD_PIN_CC1101_GDO0,
    .nrf24_csn = BOARD_PIN_NRF24_CSN,
    .nrf24_ce = BOARD_PIN_NRF24_CE,
};

static void furi_hal_module_pins_apply(void) {
    gpio_ir_tx.pin = s_module_pins.ir_tx;
    gpio_ir_rx.pin = s_module_pins.ir_rx;
    gpio_ext_pb3.pin = s_module_pins.spi_sck;
    gpio_ext_pa6.pin = s_module_pins.spi_miso;
    gpio_ext_pa7.pin = s_module_pins.spi_mosi;
    gpio_ext_pa4.pin = s_module_pins.cc1101_csn;
    gpio_cc1101_g0.pin = s_module_pins.cc1101_gdo0;
    gpio_nrf24_cs.pin = s_module_pins.nrf24_csn;
    gpio_nrf24_ce.pin = s_module_pins.nrf24_ce;
    ESP_LOGI(TAG, "Pins applied: IR_TX=%d, IR_RX=%d, SPI_SCK=%d, SPI_MISO=%d, SPI_MOSI=%d, CC1101_CSN=%d, CC1101_GDO0=%d, NRF24_CSN=%d, NRF24_CE=%d",
             s_module_pins.ir_tx, s_module_pins.ir_rx, s_module_pins.spi_sck, s_module_pins.spi_miso, s_module_pins.spi_mosi,
             s_module_pins.cc1101_csn, s_module_pins.cc1101_gdo0, s_module_pins.nrf24_csn, s_module_pins.nrf24_ce);
}

void furi_hal_module_pins_load(void) {
    const bool loaded = saved_struct_load(
        MODULE_PINS_SETTINGS_PATH,
        &s_module_pins,
        sizeof(s_module_pins),
        MODULE_PINS_SETTINGS_MAGIC,
        MODULE_PINS_SETTINGS_VER
    );
    if (loaded) {
        ESP_LOGI(TAG, "Settings loaded from %s", MODULE_PINS_SETTINGS_PATH);
    } else {
        ESP_LOGI(TAG, "No settings found, using defaults");
    }
    furi_hal_module_pins_apply();
}

void furi_hal_module_pins_save(const ModulePinsSettings* settings) {
    if (settings) {
        memcpy(&s_module_pins, settings, sizeof(s_module_pins));
    }
    saved_struct_save(
        MODULE_PINS_SETTINGS_PATH,
        &s_module_pins,
        sizeof(s_module_pins),
        MODULE_PINS_SETTINGS_MAGIC,
        MODULE_PINS_SETTINGS_VER
    );
    ESP_LOGI(TAG, "Settings saved to %s", MODULE_PINS_SETTINGS_PATH);
    furi_hal_module_pins_apply();
}

void furi_hal_module_pins_reset(void) {
    s_module_pins.ir_tx = BOARD_PIN_IR_TX;
    s_module_pins.ir_rx = BOARD_PIN_IR_RX;
    s_module_pins.spi_sck = BOARD_PIN_SD_SCLK;
    s_module_pins.spi_miso = BOARD_PIN_SD_MISO;
    s_module_pins.spi_mosi = BOARD_PIN_SD_MOSI;
    s_module_pins.cc1101_csn = BOARD_PIN_CC1101_CSN;
    s_module_pins.cc1101_gdo0 = BOARD_PIN_CC1101_GDO0;
    s_module_pins.nrf24_csn = BOARD_PIN_NRF24_CSN;
    s_module_pins.nrf24_ce = BOARD_PIN_NRF24_CE;
    furi_hal_module_pins_save(NULL);
}

const ModulePinsSettings* furi_hal_module_pins_get(void) {
    return &s_module_pins;
}
