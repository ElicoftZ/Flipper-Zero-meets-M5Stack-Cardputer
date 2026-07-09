#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/variable_item_list.h>
#include <furi_hal_module_pins.h>
#include <esp_log.h>

#define TAG "ModulePinsApp"

static const uint16_t pin_choices[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    UINT16_MAX
};
#define PIN_CHOICES_COUNT (sizeof(pin_choices)/sizeof(pin_choices[0]))

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    VariableItemList* var_item_list;
    ModulePinsSettings settings;
    VariableItem* items[9];
    char value_texts[9][16];
} ModulePinsApp;

static uint8_t get_choice_index(uint16_t pin) {
    for (uint8_t i = 0; i < PIN_CHOICES_COUNT; i++) {
        if (pin_choices[i] == pin) return i;
    }
    return PIN_CHOICES_COUNT - 1; // Default to None
}

static void update_item_text(VariableItem* item, uint16_t pin, char* text_buf, size_t max_len) {
    if (pin == UINT16_MAX) {
        snprintf(text_buf, max_len, "None");
    } else {
        snprintf(text_buf, max_len, "GPIO%d", pin);
    }
    variable_item_set_current_value_text(item, text_buf);
}

static void pin_changed_callback(VariableItem* item) {
    ModulePinsApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    const uint16_t pin = pin_choices[index];
    
    // Find which item was changed
    int item_index = -1;
    for (int i = 0; i < 9; i++) {
        if (app->items[i] == item) {
            item_index = i;
            break;
        }
    }
    
    if (item_index == -1) return;
    
    switch (item_index) {
        case 0: app->settings.ir_tx = pin; update_item_text(item, pin, app->value_texts[0], 16); break;
        case 1: app->settings.ir_rx = pin; update_item_text(item, pin, app->value_texts[1], 16); break;
        case 2: app->settings.spi_sck = pin; update_item_text(item, pin, app->value_texts[2], 16); break;
        case 3: app->settings.spi_miso = pin; update_item_text(item, pin, app->value_texts[3], 16); break;
        case 4: app->settings.spi_mosi = pin; update_item_text(item, pin, app->value_texts[4], 16); break;
        case 5: app->settings.cc1101_csn = pin; update_item_text(item, pin, app->value_texts[5], 16); break;
        case 6: app->settings.cc1101_gdo0 = pin; update_item_text(item, pin, app->value_texts[6], 16); break;
        case 7: app->settings.nrf24_csn = pin; update_item_text(item, pin, app->value_texts[7], 16); break;
        case 8: app->settings.nrf24_ce = pin; update_item_text(item, pin, app->value_texts[8], 16); break;
    }
}

static void item_enter_callback(void* context, uint32_t index) {
    ModulePinsApp* app = context;
    if (index == 9) { // Reset to defaults
        furi_hal_module_pins_reset();
        app->settings = *furi_hal_module_pins_get();
        // Re-update the UI values
        for (uint8_t i = 0; i < 9; i++) {
            VariableItem* item = app->items[i];
            if (!item) continue;
            uint16_t pin = 0;
            switch (i) {
                case 0: pin = app->settings.ir_tx; break;
                case 1: pin = app->settings.ir_rx; break;
                case 2: pin = app->settings.spi_sck; break;
                case 3: pin = app->settings.spi_miso; break;
                case 4: pin = app->settings.spi_mosi; break;
                case 5: pin = app->settings.cc1101_csn; break;
                case 6: pin = app->settings.cc1101_gdo0; break;
                case 7: pin = app->settings.nrf24_csn; break;
                case 8: pin = app->settings.nrf24_ce; break;
            }
            uint8_t choice_idx = get_choice_index(pin);
            variable_item_set_current_value_index(item, choice_idx);
            update_item_text(item, pin, app->value_texts[i], 16);
        }
    }
}

static uint32_t module_pins_app_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static ModulePinsApp* module_pins_app_alloc(void) {
    ModulePinsApp* app = malloc(sizeof(ModulePinsApp));
    memset(app, 0, sizeof(ModulePinsApp));
    
    app->settings = *furi_hal_module_pins_get();
    
    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    
    app->var_item_list = variable_item_list_alloc();
    variable_item_list_set_enter_callback(app->var_item_list, item_enter_callback, app);
    
    // Add the items
    const char* names[] = {
        "IR TX",
        "IR RX",
        "SPI SCK",
        "SPI MISO",
        "SPI MOSI",
        "CC1101 CSN",
        "CC1101 GDO0",
        "NRF24 CSN",
        "NRF24 CE"
    };
    
    for (uint8_t i = 0; i < 9; i++) {
        uint16_t pin = 0;
        switch (i) {
            case 0: pin = app->settings.ir_tx; break;
            case 1: pin = app->settings.ir_rx; break;
            case 2: pin = app->settings.spi_sck; break;
            case 3: pin = app->settings.spi_miso; break;
            case 4: pin = app->settings.spi_mosi; break;
            case 5: pin = app->settings.cc1101_csn; break;
            case 6: pin = app->settings.cc1101_gdo0; break;
            case 7: pin = app->settings.nrf24_csn; break;
            case 8: pin = app->settings.nrf24_ce; break;
        }
        
        VariableItem* item = variable_item_list_add(
            app->var_item_list,
            names[i],
            PIN_CHOICES_COUNT,
            pin_changed_callback,
            app
        );
        app->items[i] = item;
        uint8_t choice_idx = get_choice_index(pin);
        variable_item_set_current_value_index(item, choice_idx);
        update_item_text(item, pin, app->value_texts[i], 16);
    }
    
    // Add Reset to defaults button
    variable_item_list_add(app->var_item_list, "Reset to defaults", 1, NULL, NULL);
    
    view_set_previous_callback(
        variable_item_list_get_view(app->var_item_list), module_pins_app_exit);
    
    view_dispatcher_add_view(
        app->view_dispatcher,
        0,
        variable_item_list_get_view(app->var_item_list)
    );
    
    view_dispatcher_switch_to_view(app->view_dispatcher, 0);
    
    return app;
}

static void module_pins_app_free(ModulePinsApp* app) {
    furi_assert(app);
    
    furi_hal_module_pins_save(&app->settings);
    
    view_dispatcher_remove_view(app->view_dispatcher, 0);
    variable_item_list_free(app->var_item_list);
    view_dispatcher_free(app->view_dispatcher);
    
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t module_pins_settings_app(void* p) {
    UNUSED(p);
    ModulePinsApp* app = module_pins_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    module_pins_app_free(app);
    return 0;
}
