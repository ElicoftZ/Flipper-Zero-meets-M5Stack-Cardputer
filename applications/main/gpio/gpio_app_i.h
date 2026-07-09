#pragma once

#include "gpio_app.h"
#include "gpio_items.h"
#include "scenes/gpio_scene.h"
#include "gpio_custom_event.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <notification/notification_messages.h>
#include <gui/modules/variable_item_list.h>
#include "views/gpio_test.h"
#include <assets_icons.h>

/* USB-UART bridge + 5V/OTG were removed for this ESP32 port (STM32-only USB CDC
 * / LPUART, no OTG boost). Only manual GPIO control remains — see application.fam
 * `sources` which excludes the usb_uart files. */
struct GpioApp {
    Gui* gui;
    NotificationApp* notifications;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;

    VariableItemList* var_item_list;
    GpioTest* gpio_test;
    GPIOItems* gpio_items;

    // Custom Pin scene state (struct is malloc'd — init these in gpio_app_alloc)
    uint8_t custom_pin_index;     // index into the scene's gpio_custom_pins[]
    uint8_t custom_mode_idx;      // CustomMode (see gpio_scene_custom.c)
    bool custom_out_high;         // desired output level
    uint8_t custom_applied_index; // pin currently configured, or 0xFF = none
    VariableItem* custom_read_item; // live input-read display item
};

typedef enum {
    GpioAppViewVarItemList,
    GpioAppViewGpioTest,
} GpioAppView;
