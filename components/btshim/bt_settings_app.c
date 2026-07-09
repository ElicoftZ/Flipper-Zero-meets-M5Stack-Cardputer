#include "btshim.h"

#include <furi.h>
#include <gui.h>
#include <view_dispatcher.h>
#include <dialog_ex.h>
#include <popup.h>
#include <variable_item_list.h>
#include <esp_heap_caps.h>

#define TAG "BtSettings"

typedef enum {
    BtSettingsViewList,
    BtSettingsViewConfirm,
    BtSettingsViewPopup,
} BtSettingsView;

typedef enum {
    BtSettingsItemToggle,
    BtSettingsItemUnpair,
} BtSettingsItem;

typedef struct {
    Gui* gui;
    Bt* bt;
    BtSettings settings;
    ViewDispatcher* view_dispatcher;
    VariableItemList* variable_item_list;
    DialogEx* confirm_dialog;
    Popup* result_popup;
} BtSettingsApp;

static const char* const bt_setting_text[] = {
    "OFF",
    "ON",
};

static uint32_t bt_settings_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t bt_settings_return_to_list(void* context) {
    UNUSED(context);
    return BtSettingsViewList;
}

static void bt_settings_show_result(BtSettingsApp* app, const char* header, const char* text) {
    popup_reset(app->result_popup);
    popup_set_header(app->result_popup, header, 14, 15, AlignLeft, AlignTop);
    popup_set_text(app->result_popup, text, 64, 32, AlignCenter, AlignCenter);
    popup_set_timeout(app->result_popup, 1200);
    popup_set_context(app->result_popup, app);
    popup_enable_timeout(app->result_popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, BtSettingsViewPopup);
}

static void bt_settings_result_popup_callback(void* context) {
    BtSettingsApp* app = context;
    furi_assert(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, BtSettingsViewList);
}

static void bt_settings_toggle_callback(VariableItem* item) {
    BtSettingsApp* app = variable_item_get_context(item);
    furi_assert(app);

    const uint8_t index = variable_item_get_current_value_index(item);

    /* TESTING BRANCH (ble-testing): the Phase-5 gate that refused to enable BT
     * on this no-PSRAM board is lifted so the hardened BLE path (BTU stack bump,
     * btshim/ble_serial thread-safety fixes) can be exercised on hardware.
     * The RAM ceiling is unchanged — enabling BT here brings up the controller +
     * Bluedroid (~64 KB); expect it to be tight and possibly OOM under an active
     * RPC connection. Do NOT merge this toggle change to master as-is. */
    app->settings.enabled = (index == 1);
    variable_item_set_current_value_text(item, bt_setting_text[index]);

    /* Refuse to enable when the heap is too low to bring up the BLE stack
     * (~64 KB). The service layer guards this too, but checking here keeps the
     * toggle from persisting an ON that the service will silently reject, and
     * gives the user immediate feedback. */
    if(app->settings.enabled) {
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if(free_internal < BT_MIN_FREE_INTERNAL || largest_internal < BT_MIN_LARGEST_INTERNAL) {
            app->settings.enabled = false;
            variable_item_set_current_value_index(item, 0);
            variable_item_set_current_value_text(item, bt_setting_text[0]);
            bt_settings_show_result(
                app, "Bluetooth", "Not enough RAM.\nClose apps or reboot,\nthen retry.");
            FURI_LOG_W(
                TAG,
                "BT enable refused: %u B free, %u B largest",
                (unsigned)free_internal,
                (unsigned)largest_internal);
            return;
        }
    }

    bt_set_settings(app->bt, &app->settings);
    if(app->settings.enabled) {
        bt_settings_show_result(
            app, "Enabling BLE", "Memory is tight on\nthis board — may be\nunstable (test build)");
    }
    FURI_LOG_I(TAG, "Bluetooth toggled: enabled=%d", app->settings.enabled);
}

static void bt_settings_confirm_result_callback(DialogExResult result, void* context) {
    BtSettingsApp* app = context;
    furi_assert(app);

    if(result == DialogExResultRight) {
        bt_forget_bonded_devices(app->bt);
        bt_settings_show_result(app, "Done", "Pairings removed");
    } else {
        view_dispatcher_switch_to_view(app->view_dispatcher, BtSettingsViewList);
    }
}

static void bt_settings_show_unpair_confirm(BtSettingsApp* app) {
    dialog_ex_reset(app->confirm_dialog);
    dialog_ex_set_context(app->confirm_dialog, app);
    dialog_ex_set_result_callback(app->confirm_dialog, bt_settings_confirm_result_callback);
    dialog_ex_set_header(
        app->confirm_dialog, "Unpair All Devices?", 64, 0, AlignCenter, AlignTop);
    dialog_ex_set_text(
        app->confirm_dialog, "All previous pairings\nwill be lost!", 64, 18, AlignCenter, AlignTop);
    dialog_ex_set_left_button_text(app->confirm_dialog, "Cancel");
    dialog_ex_set_right_button_text(app->confirm_dialog, "Unpair");
    view_dispatcher_switch_to_view(app->view_dispatcher, BtSettingsViewConfirm);
}

static void bt_settings_enter_callback(void* context, uint32_t index) {
    BtSettingsApp* app = context;
    furi_assert(app);

    if(index == BtSettingsItemUnpair) {
        bt_settings_show_unpair_confirm(app);
    }
}

static void bt_settings_populate_list(BtSettingsApp* app) {
    VariableItem* item = NULL;

    variable_item_list_reset(app->variable_item_list);

    item = variable_item_list_add(
        app->variable_item_list, "Bluetooth", 2, bt_settings_toggle_callback, app);
    variable_item_set_current_value_index(item, app->settings.enabled ? 1 : 0);
    variable_item_set_current_value_text(
        item, bt_setting_text[app->settings.enabled ? 1 : 0]);

    variable_item_list_add(app->variable_item_list, "Unpair All Devices", 1, NULL, NULL);
    variable_item_list_set_enter_callback(
        app->variable_item_list, bt_settings_enter_callback, app);
}

int32_t bt_settings_app(void* p) {
    UNUSED(p);

    BtSettingsApp* app = calloc(1, sizeof(BtSettingsApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->bt = furi_record_open(RECORD_BT);
    bt_get_settings(app->bt, &app->settings);

    app->view_dispatcher = view_dispatcher_alloc();
    app->variable_item_list = variable_item_list_alloc();
    app->confirm_dialog = dialog_ex_alloc();
    app->result_popup = popup_alloc();

    popup_set_callback(app->result_popup, bt_settings_result_popup_callback);
    popup_set_context(app->result_popup, app);

    View* list_view = variable_item_list_get_view(app->variable_item_list);
    view_set_previous_callback(list_view, bt_settings_exit);
    View* confirm_view = dialog_ex_get_view(app->confirm_dialog);
    view_set_previous_callback(confirm_view, bt_settings_return_to_list);
    View* popup_view = popup_get_view(app->result_popup);
    view_set_previous_callback(popup_view, bt_settings_return_to_list);

    view_dispatcher_add_view(app->view_dispatcher, BtSettingsViewList, list_view);
    view_dispatcher_add_view(app->view_dispatcher, BtSettingsViewConfirm, confirm_view);
    view_dispatcher_add_view(app->view_dispatcher, BtSettingsViewPopup, popup_view);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    bt_settings_populate_list(app);
    FURI_LOG_I(TAG, "Opening Bluetooth settings (enabled=%d)", app->settings.enabled);

    view_dispatcher_switch_to_view(app->view_dispatcher, BtSettingsViewList);
    view_dispatcher_run(app->view_dispatcher);

    view_dispatcher_remove_view(app->view_dispatcher, BtSettingsViewPopup);
    view_dispatcher_remove_view(app->view_dispatcher, BtSettingsViewConfirm);
    view_dispatcher_remove_view(app->view_dispatcher, BtSettingsViewList);
    popup_free(app->result_popup);
    dialog_ex_free(app->confirm_dialog);
    variable_item_list_free(app->variable_item_list);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_GUI);
    free(app);

    return 0;
}
