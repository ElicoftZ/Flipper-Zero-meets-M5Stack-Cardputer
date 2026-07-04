#include "ble_spam_app.h"
#include "ble_uuid_db.h"
#include "ble_spam_hal.h"
#include <dialogs/dialogs.h>
#include <furi_hal_big_fap.h>
#include "views/ble_spam_view.h"
#include "views/ble_walk_scan_view.h"
#include "views/ble_walk_detail_view.h"
#include "views/ble_auto_walk_view.h"
#include "views/tracker_list_view.h"
#include "views/tracker_geiger_view.h"
#include "views/race_detector_view.h"
#include <gui/modules/text_input.h>

static bool ble_spam_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    BleSpamApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool ble_spam_back_event_callback(void* context) {
    furi_assert(context);
    BleSpamApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void ble_spam_tick_event_callback(void* context) {
    furi_assert(context);
    BleSpamApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

static BleSpamApp* ble_spam_app_alloc(void) {
    /* On this ESP32 port malloc() returns NULL on OOM (mainline Flipper aborts),
     * so bail cleanly here instead of NULL-dereferencing on the next line when
     * the heap floor is too low to open this (view-heavy) app. */
    BleSpamApp* app = malloc(sizeof(BleSpamApp));
    if(!app) return NULL;

    app->gui = furi_record_open(RECORD_GUI);

    app->scene_manager = scene_manager_alloc(&ble_spam_scene_handlers, app);
    app->view_dispatcher = view_dispatcher_alloc();

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, ble_spam_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, ble_spam_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, ble_spam_tick_event_callback, 250);
    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Submenu
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BleSpamViewSubmenu, submenu_get_view(app->submenu));

    // Running view
    app->view_running = ble_spam_view_alloc();
    view_set_context(app->view_running, app->view_dispatcher);
    view_dispatcher_add_view(
        app->view_dispatcher, BleSpamViewRunning, app->view_running);

    // BLE Walk views
    app->view_walk_scan = ble_walk_scan_view_alloc();
    view_set_context(app->view_walk_scan, app->view_dispatcher);
    view_dispatcher_add_view(
        app->view_dispatcher, BleSpamViewWalkScan, app->view_walk_scan);

    app->view_walk_detail = ble_walk_detail_view_alloc();
    view_set_context(app->view_walk_detail, app->view_dispatcher);
    view_dispatcher_add_view(
        app->view_dispatcher, BleSpamViewWalkDetail, app->view_walk_detail);

    // BLE Auto-Walk view
    app->view_auto_walk = ble_auto_walk_view_alloc();
    view_set_context(app->view_auto_walk, app->view_dispatcher);
    view_dispatcher_add_view(
        app->view_dispatcher, BleSpamViewAutoWalk, app->view_auto_walk);

    // BLE Tracker views
    app->view_tracker_scan = tracker_list_view_alloc();
    view_set_context(app->view_tracker_scan, app->view_dispatcher);
    view_dispatcher_add_view(
        app->view_dispatcher, BleSpamViewTrackerScan, app->view_tracker_scan);

    app->view_tracker_geiger = tracker_geiger_view_alloc();
    view_set_context(app->view_tracker_geiger, app->view_dispatcher);
    view_dispatcher_add_view(
        app->view_dispatcher, BleSpamViewTrackerGeiger, app->view_tracker_geiger);

    app->tracker_geiger_timer = NULL;

    // Text input view for custom pair names
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BleSpamViewTextInput, text_input_get_view(app->text_input));

    // BLE RACE Detector view (CVE-2025-20700)
    app->view_race_detector = race_detector_view_alloc();
    view_set_context(app->view_race_detector, app->view_dispatcher);
    view_dispatcher_add_view(
        app->view_dispatcher, BleSpamViewRaceDetector, app->view_race_detector);
    app->race_probe_abort = false;

    // State
    app->attack_type = BleSpamAttackAppleDevice;
    app->running = false;
    app->packet_count = 0;
    app->delay_ms = 100;
    app->current_index = 0;
    app->current_device[0] = '\0';
    app->custom_pair_name[0] = '\0';

    return app;
}

static void ble_spam_app_free(BleSpamApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, BleSpamViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, BleSpamViewRunning);
    view_dispatcher_remove_view(app->view_dispatcher, BleSpamViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, BleSpamViewWalkScan);
    view_dispatcher_remove_view(app->view_dispatcher, BleSpamViewWalkDetail);
    view_dispatcher_remove_view(app->view_dispatcher, BleSpamViewAutoWalk);
    view_dispatcher_remove_view(app->view_dispatcher, BleSpamViewTrackerScan);
    view_dispatcher_remove_view(app->view_dispatcher, BleSpamViewTrackerGeiger);
    view_dispatcher_remove_view(app->view_dispatcher, BleSpamViewRaceDetector);

    submenu_free(app->submenu);
    ble_spam_view_free(app->view_running);
    ble_walk_scan_view_free(app->view_walk_scan);
    ble_walk_detail_view_free(app->view_walk_detail);
    ble_auto_walk_view_free(app->view_auto_walk);
    tracker_list_view_free(app->view_tracker_scan);
    tracker_geiger_view_free(app->view_tracker_geiger);
    race_detector_view_free(app->view_race_detector);
    text_input_free(app->text_input);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    app->gui = NULL;

    free(app);
}

int32_t ble_spam_app(void* args) {
    UNUSED(args);

    /* Big FAP Mode blocks the radios so heavy apps keep the heap. */
    if(furi_hal_big_fap_is_active()) {
        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
        DialogMessage* message = dialog_message_alloc();
        dialog_message_set_header(message, "Big FAP Mode", 64, 8, AlignCenter, AlignTop);
        dialog_message_set_text(
            message, "Big FAP Mode active -\nonly apps can run.", 64, 34, AlignCenter, AlignCenter);
        dialog_message_set_buttons(message, NULL, NULL, "OK");
        dialog_message_show(dialogs, message);
        dialog_message_free(message);
        furi_record_close(RECORD_DIALOGS);
        return 0;
    }

    ble_uuid_db_init();
    BleSpamApp* app = ble_spam_app_alloc();
    if(!app) {
        FURI_LOG_E("BleSpam", "Out of memory: cannot open BLE Spam");
        ble_uuid_db_deinit();
        return -1;
    }

    /* The BLE controller + Bluedroid need ~72 KB free internal RAM. On this
     * no-PSRAM board the app's own UI + UUID DB leave only ~59 KB, so every mode
     * would fail ble_spam_hal_start(). The mode scenes handle that failure by
     * calling scene_manager_previous_scene() from within on_enter(), which
     * re-enters + re-fires the HAL start ~4x/s and starves the GUI into a
     * ViewPort lockup (the "crash"). Refuse up front with a clear message
     * instead — consistent with the BT/SubGHz/NFC "not available" gating. */
    if(!ble_spam_hal_have_ram()) {
        FURI_LOG_W("BleSpam", "Not enough RAM for BLE radio; refusing launch");
        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
        DialogMessage* message = dialog_message_alloc();
        dialog_message_set_header(message, "BLE Spam Unavailable", 64, 8, AlignCenter, AlignTop);
        dialog_message_set_text(
            message,
            "Not enough free RAM to\nstart the BLE radio on\nthis board (no PSRAM).",
            64,
            34,
            AlignCenter,
            AlignCenter);
        dialog_message_set_buttons(message, NULL, NULL, "OK");
        dialog_message_show(dialogs, message);
        dialog_message_free(message);
        furi_record_close(RECORD_DIALOGS);
        ble_spam_app_free(app);
        ble_uuid_db_deinit();
        return 0;
    }

    scene_manager_next_scene(app->scene_manager, BleSpamSceneMain);
    view_dispatcher_run(app->view_dispatcher);

    // Safety net: tear down the radio if the app exits with it still up
    // (idempotent — scene_main on_enter normally does this first).
    ble_spam_hal_stop();
    ble_spam_app_free(app);
    ble_uuid_db_deinit();
    return 0;
}
