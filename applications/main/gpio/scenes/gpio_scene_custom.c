#include "../gpio_app_i.h"

#include <furi_hal_gpio.h>
#include <stdio.h>

/* "Custom Pin" — drive/read ANY valid GPIO at runtime, so the app isn't locked to
 * the compile-time gpio_pins[] list. The selectable set below excludes the pins
 * that don't physically exist on the ESP32-S3 (22-25) and the in-package SPI
 * flash (26-32) — driving those hard-crashes the chip. Everything else is
 * allowed; driving pins used by the screen / SD / keyboard / USB will disrupt the
 * device until a reboot (inherent to raw GPIO control — that's the user's call). */
static const uint16_t gpio_custom_pins[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
};
#define GPIO_CUSTOM_PIN_COUNT ((uint8_t)(sizeof(gpio_custom_pins) / sizeof(gpio_custom_pins[0])))
#define GPIO_CUSTOM_NONE      0xFF

typedef enum {
    CustomModeOutPushPull,
    CustomModeInputPullUp,
    CustomModeInputPullDown,
    CustomModeInputFloat,
    CustomModeCount,
} CustomMode;

static const char* const custom_mode_text[CustomModeCount] = {
    "Out P-P",
    "In Pull-Up",
    "In Pull-Dn",
    "In Float",
};

static const char* const custom_level_text[2] = {"Low", "High"};

static GpioPin gpio_custom_pin_at(uint8_t index) {
    GpioPin p = {.port = NULL, .pin = gpio_custom_pins[index]};
    return p;
}

/* Release the currently-configured pin back to Hi-Z (analog), matching how the
 * preset test scene releases its pins on exit. */
static void gpio_custom_release_applied(GpioApp* app) {
    if(app->custom_applied_index == GPIO_CUSTOM_NONE) return;
    GpioPin pin = gpio_custom_pin_at(app->custom_applied_index);
    furi_hal_gpio_init(&pin, GpioModeAnalog, GpioPullNo, GpioSpeedVeryHigh);
    app->custom_applied_index = GPIO_CUSTOM_NONE;
}

/* Configure the currently-selected pin to the chosen mode (and drive it if it is
 * an output). Only one pin is ever actively configured at a time. */
static void gpio_custom_apply_selected(GpioApp* app) {
    if(app->custom_applied_index != GPIO_CUSTOM_NONE &&
       app->custom_applied_index != app->custom_pin_index) {
        gpio_custom_release_applied(app);
    }
    GpioPin pin = gpio_custom_pin_at(app->custom_pin_index);
    switch(app->custom_mode_idx) {
    case CustomModeOutPushPull:
        furi_hal_gpio_init(&pin, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
        furi_hal_gpio_write(&pin, app->custom_out_high);
        break;
    case CustomModeInputPullUp:
        furi_hal_gpio_init(&pin, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
        break;
    case CustomModeInputPullDown:
        furi_hal_gpio_init(&pin, GpioModeInput, GpioPullDown, GpioSpeedVeryHigh);
        break;
    case CustomModeInputFloat:
    default:
        furi_hal_gpio_init(&pin, GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);
        break;
    }
    app->custom_applied_index = app->custom_pin_index;
}

static void gpio_custom_pin_changed(VariableItem* item) {
    GpioApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->custom_pin_index = index;

    char buf[12];
    snprintf(buf, sizeof(buf), "GPIO%u", (unsigned)gpio_custom_pins[index]);
    variable_item_set_current_value_text(item, buf);

    /* Browsing pins must not leave an old pin silently driven — release it. The
     * user re-activates the newly-selected pin by touching "Mode". */
    gpio_custom_release_applied(app);
}

static void gpio_custom_mode_changed(VariableItem* item) {
    GpioApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->custom_mode_idx = index;
    variable_item_set_current_value_text(item, custom_mode_text[index]);
    gpio_custom_apply_selected(app);
}

static void gpio_custom_level_changed(VariableItem* item) {
    GpioApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->custom_out_high = (index == 1);
    variable_item_set_current_value_text(item, custom_level_text[index]);

    /* Only meaningful when the selected pin is the applied output. */
    if(app->custom_applied_index == app->custom_pin_index &&
       app->custom_mode_idx == CustomModeOutPushPull) {
        GpioPin pin = gpio_custom_pin_at(app->custom_pin_index);
        furi_hal_gpio_write(&pin, app->custom_out_high);
    }
}

void gpio_scene_custom_on_enter(void* context) {
    GpioApp* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    app->custom_applied_index = GPIO_CUSTOM_NONE;
    variable_item_list_reset(list);

    item = variable_item_list_add(
        list, "GPIO", GPIO_CUSTOM_PIN_COUNT, gpio_custom_pin_changed, app);
    variable_item_set_current_value_index(item, app->custom_pin_index);
    {
        char buf[12];
        snprintf(buf, sizeof(buf), "GPIO%u", (unsigned)gpio_custom_pins[app->custom_pin_index]);
        variable_item_set_current_value_text(item, buf);
    }

    item = variable_item_list_add(list, "Mode", CustomModeCount, gpio_custom_mode_changed, app);
    variable_item_set_current_value_index(item, app->custom_mode_idx);
    variable_item_set_current_value_text(item, custom_mode_text[app->custom_mode_idx]);

    item = variable_item_list_add(list, "Out Level", 2, gpio_custom_level_changed, app);
    variable_item_set_current_value_index(item, app->custom_out_high ? 1 : 0);
    variable_item_set_current_value_text(item, custom_level_text[app->custom_out_high ? 1 : 0]);

    app->custom_read_item = variable_item_list_add(list, "Read", 1, NULL, NULL);
    variable_item_set_current_value_text(app->custom_read_item, "--");

    view_dispatcher_switch_to_view(app->view_dispatcher, GpioAppViewVarItemList);
}

bool gpio_scene_custom_on_event(void* context, SceneManagerEvent event) {
    GpioApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(app->custom_read_item) {
            const char* txt = "--";
            if(app->custom_applied_index == app->custom_pin_index) {
                GpioPin pin = gpio_custom_pin_at(app->custom_pin_index);
                txt = furi_hal_gpio_read(&pin) ? "HIGH" : "LOW";
            }
            variable_item_set_current_value_text(app->custom_read_item, txt);
        }
        consumed = true;
    }
    return consumed;
}

void gpio_scene_custom_on_exit(void* context) {
    GpioApp* app = context;
    /* Never leave a pin driven when the user backs out. */
    gpio_custom_release_applied(app);
    app->custom_read_item = NULL;
    variable_item_list_reset(app->var_item_list);
}
