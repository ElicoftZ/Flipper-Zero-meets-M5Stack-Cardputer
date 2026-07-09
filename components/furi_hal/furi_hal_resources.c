/**
 * @file furi_hal_resources.c
 * Resources HAL — pin definitions driven by board header
 */

#include "furi_hal_resources.h"
#include "boards/board.h"

const InputPin input_pins[] = {};
const size_t input_pins_count = 0;

/* Hardware Button Pins */
const GpioPin gpio_button_boot  = {.port = NULL, .pin = BOARD_PIN_BUTTON_BOOT};
const GpioPin gpio_battery_sense = {.port = NULL, .pin = BOARD_PIN_BATTERY_ADC};

/* LCD Pins */
const GpioPin gpio_lcd_din = {.port = NULL, .pin = BOARD_PIN_LCD_MOSI};
const GpioPin gpio_lcd_clk = {.port = NULL, .pin = BOARD_PIN_LCD_SCLK};
const GpioPin gpio_lcd_dc  = {.port = NULL, .pin = BOARD_PIN_LCD_DC};
const GpioPin gpio_lcd_cs  = {.port = NULL, .pin = BOARD_PIN_LCD_CS};
const GpioPin gpio_lcd_rst = {.port = NULL, .pin = BOARD_PIN_LCD_RST};
const GpioPin gpio_lcd_bl  = {.port = NULL, .pin = BOARD_PIN_LCD_BL};

/* SD Card Pins */
const GpioPin gpio_sdcard_cs   = {.port = NULL, .pin = BOARD_PIN_SD_CS};
const GpioPin gpio_sdcard_miso = {.port = NULL, .pin = BOARD_PIN_SD_MISO};

/* Touch Pins */
const GpioPin gpio_touch_scl = {.port = NULL, .pin = BOARD_PIN_TOUCH_SCL};
const GpioPin gpio_touch_sda = {.port = NULL, .pin = BOARD_PIN_TOUCH_SDA};
const GpioPin gpio_touch_rst = {.port = NULL, .pin = BOARD_PIN_TOUCH_RST};
const GpioPin gpio_touch_int = {.port = NULL, .pin = BOARD_PIN_TOUCH_INT};

/* External / CC1101 pins */
const GpioPin gpio_ext_pc0  = {.port = NULL, .pin = UINT16_MAX};
const GpioPin gpio_ext_pc1  = {.port = NULL, .pin = UINT16_MAX};
const GpioPin gpio_ext_pc3  = {.port = NULL, .pin = UINT16_MAX};
const GpioPin gpio_ext_pb2  = {.port = NULL, .pin = UINT16_MAX};
GpioPin gpio_ext_pb3  = {.port = NULL, .pin = BOARD_PIN_CC1101_SCK};
GpioPin gpio_ext_pa4  = {.port = NULL, .pin = BOARD_PIN_CC1101_CSN};
GpioPin gpio_ext_pa6  = {.port = NULL, .pin = BOARD_PIN_CC1101_MISO};
GpioPin gpio_ext_pa7  = {.port = NULL, .pin = BOARD_PIN_CC1101_MOSI};
GpioPin gpio_cc1101_g0 = {.port = NULL, .pin = BOARD_PIN_CC1101_GDO0};

#ifdef BOARD_PIN_NRF24_CSN
GpioPin gpio_nrf24_cs = {.port = NULL, .pin = BOARD_PIN_NRF24_CSN};
#else
GpioPin gpio_nrf24_cs = {.port = NULL, .pin = UINT16_MAX};
#endif

#ifdef BOARD_PIN_NRF24_CE
GpioPin gpio_nrf24_ce = {.port = NULL, .pin = BOARD_PIN_NRF24_CE};
#else
GpioPin gpio_nrf24_ce = {.port = NULL, .pin = UINT16_MAX};
#endif

GpioPin gpio_ir_tx = {.port = NULL, .pin = BOARD_PIN_IR_TX};
GpioPin gpio_ir_rx = {.port = NULL, .pin = BOARD_PIN_IR_RX};

const GpioPin gpio_ibutton  = {.port = NULL, .pin = UINT16_MAX};
const GpioPin gpio_speaker  = {.port = NULL, .pin = UINT16_MAX};

/* ---- User-controllable GPIO header (the "GPIO -> Manual Control" app) -------
 * On the Cardputer-ADV the only free external I/O is the Grove/Qwiic HY2.0-4P
 * port: SDA=GPIO2, SCL=GPIO1. They default to the Grove I2C bus but can be
 * driven as plain GPIO by the app.
 *
 * >>> DIFFERENT PCB? Edit THIS table. Add a `static const GpioPin` for each pin
 *     your board exposes and a matching gpio_pins[] row (`.name` is the label
 *     shown in the app; set `.debug = true` to hide a pin). Do NOT list pins
 *     wired to internal peripherals (LCD/SD/keyboard/CC1101/speaker/IR) — driving
 *     them will hang or corrupt the device. <<< */
static const GpioPin gpio_ext_grove_sda = {.port = NULL, .pin = BOARD_PIN_QWIIC_SDA};
static const GpioPin gpio_ext_grove_scl = {.port = NULL, .pin = BOARD_PIN_QWIIC_SCL};

const GpioPinRecord gpio_pins[] = {
    {.pin = &gpio_ext_grove_sda, .name = "G2 (SDA)", .debug = false},
    {.pin = &gpio_ext_grove_scl, .name = "G1 (SCL)", .debug = false},
};
const size_t gpio_pins_count = sizeof(gpio_pins) / sizeof(gpio_pins[0]);
