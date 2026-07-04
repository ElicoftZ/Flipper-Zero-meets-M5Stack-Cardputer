#include "wlan_app.h"
#include "wlan_html_inject.h"
#include "wlan_cred_sniff.h"
#include "wlan_hal.h"
#include "wlan_netcut.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>

/* The WiFi app allocates ~40 KB of views/records and then esp_wifi_init needs
 * ~40 KB more. On this no-PSRAM board, if another radio app (e.g. BLE Spam) has
 * leaked/fragmented the heap, opening here would OOM mid-view-alloc and crash
 * (StoreProhibited writing a NULL view model). Refuse gracefully below this. */
#define WLAN_APP_MIN_FREE_INTERNAL (55 * 1024)

static bool wlan_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    WlanApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool wlan_app_back_event_callback(void* context) {
    furi_assert(context);
    WlanApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void wlan_app_tick_event_callback(void* context) {
    furi_assert(context);
    WlanApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

static WlanApp* wlan_app_alloc(void) {
    WlanApp* app = malloc(sizeof(WlanApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->scene_manager = scene_manager_alloc(&wlan_app_scene_handlers, app);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, wlan_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, wlan_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, wlan_app_tick_event_callback, 250);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewSubmenu, submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewWidget, widget_get_view(app->widget));

    app->loading = loading_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewLoading, loading_get_view(app->loading));

    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewPopup, popup_get_view(app->popup));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewTextInput, text_input_get_view(app->text_input));

    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        WlanAppViewVariableItemList,
        variable_item_list_get_view(app->variable_item_list));

    app->view_lan = wlan_lan_view_alloc();
    wlan_lan_view_set_view_dispatcher(app->view_lan, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewLan, app->view_lan);

    app->view_connect = wlan_connect_view_alloc();
    wlan_connect_view_set_view_dispatcher(app->view_connect, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewConnect, app->view_connect);

    app->view_portscan = wlan_portscan_view_alloc();
    view_set_context(app->view_portscan, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewPortscan, app->view_portscan);

    app->view_handshake = wlan_handshake_view_alloc();
    view_set_context(app->view_handshake, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewHandshake, app->view_handshake);

    app->view_handshake_channel = wlan_handshake_channel_view_alloc();
    view_set_context(app->view_handshake_channel, app->view_dispatcher);
    view_dispatcher_add_view(
        app->view_dispatcher, WlanAppViewHandshakeChannel, app->view_handshake_channel);

    app->view_deauther = wlan_deauther_view_alloc();
    view_set_context(app->view_deauther, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewDeauther, app->view_deauther);

    app->sniffer_view_obj = wlan_sniffer_view_alloc();
    app->view_sniffer = wlan_sniffer_view_get_view(app->sniffer_view_obj);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewSniffer, app->view_sniffer);

    app->evil_portal_view_obj = wlan_evil_portal_view_alloc();
    app->view_evil_portal = wlan_evil_portal_view_get_view(app->evil_portal_view_obj);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewEvilPortal, app->view_evil_portal);

    app->evil_portal_captured_view_obj = wlan_evil_portal_captured_view_alloc();
    app->view_evil_portal_captured =
        wlan_evil_portal_captured_view_get_view(app->evil_portal_captured_view_obj);
    view_dispatcher_add_view(
        app->view_dispatcher, WlanAppViewEvilPortalCaptured, app->view_evil_portal_captured);

    app->live_creds_view_obj = wlan_live_creds_view_alloc();
    app->view_live_creds = wlan_live_creds_view_get_view(app->live_creds_view_obj);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewLiveCreds, app->view_live_creds);

    app->view_sd_update = wlan_sd_update_view_alloc();
    view_set_context(app->view_sd_update, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewSdUpdate, app->view_sd_update);


    app->ap_records = malloc(sizeof(WlanApRecord) * WLAN_APP_MAX_APS);
    app->ap_count = 0;
    app->ap_selected_index = 0;

    app->devices = malloc(sizeof(WlanDeviceRecord) * WLAN_APP_MAX_DEVICES);
    app->device_count = 0;
    app->device_selected_index = 0;
    app->lan_menu_device_idx = -1;
    app->lan_popup_active = false;
    app->lan_scan_complete = false;

    memset(app->deauth_clients, 0, sizeof(app->deauth_clients));
    app->deauth_client_count = 0;
    app->deauth_auto = false;

    strcpy(app->evil_portal_ssid, "Free WiFi");
    app->evil_portal_channel = 6;
    app->evil_portal_template_index = 0;
    app->evil_portal_templates.count = 0;
    app->evil_portal_valid_ssid[0] = '\0';
    app->evil_portal_valid_pwd[0] = '\0';

    app->connected = false;
    app->target_selected = false;
    memset(&app->connected_ap, 0, sizeof(app->connected_ap));
    memset(&app->target_ap, 0, sizeof(app->target_ap));
    memset(app->password_input, 0, sizeof(app->password_input));

    app->attack_block_internet = false;
    app->attack_throttle = WlanAppThrottleOff;

    app->update_sd_flow = false;
    app->sd_update = wlan_sd_update_alloc();

    app->text_buf = furi_string_alloc();
    app->netcut = wlan_netcut_alloc();
    /* cred_sniff is ~15 KB (ring[32] of WlanCredEntry) and ONLY the live-creds /
     * evil-portal / MITM features use it. Allocating it here left only ~20 KB
     * free by the time the app brought up esp_wifi_init (~40 KB needed) →
     * ESP_ERR_NO_MEM → scan found no networks. Defer it: the live-creds scene
     * (the single arming point) calls wlan_app_ensure_cred_sniff() on enter. The
     * cred-sniff API is NULL-safe throughout (feed/snapshot/drain/set_armed all
     * no-op on NULL), so nothing crashes before it is ensured. */
    app->cred_sniff = NULL;

    app->mitm_inject_enabled = true;
    app->mitm_store_cred = true;
    // Default-Payload für "custom". Wird raw als JS am mitm-server /code
    // ausgeliefert; in HTML injizieren wir nur einen <script src=...>-Loader.
    strcpy(app->mitm_inject_code, "alert(1234);");
    app->mitm_payloads.count = 0;
    app->mitm_payload_index = 0; // wird in scene_mitm_menu_on_enter auf "custom" gesetzt

    wlan_handshake_settings_load(&app->hs_settings);

    return app;
}

WlanCredSniff* wlan_app_ensure_cred_sniff(WlanApp* app) {
    /* Lazy one-time allocation of the ~15 KB credential sniffer + its wiring
     * into netcut / html-inject. Deferred from app start so esp_wifi_init has
     * enough heap to come up (see wlan_app_alloc). Returns NULL if OOM — callers
     * must handle it (the whole cred-sniff API tolerates a NULL instance). */
    if(!app->cred_sniff) {
        app->cred_sniff = wlan_cred_sniff_alloc();
        if(app->cred_sniff) {
            wlan_netcut_set_cred_sniff(app->netcut, app->cred_sniff);
            wlan_html_inject_set_cred_sniff(app->cred_sniff);
        }
    }
    return app->cred_sniff;
}

static void wlan_app_free(WlanApp* app) {
    // Aktive ARP-Spoofs beenden + Restore-Frames senden (deinstalliert auch den
    // L2-Hook, der den Cred-Sniffer referenziert), dann erst cred_sniff freigeben.
    if(app->netcut) {
        wlan_netcut_free(app->netcut);
        app->netcut = NULL;
    }
    if(app->cred_sniff) {
        wlan_cred_sniff_free(app->cred_sniff);
        app->cred_sniff = NULL;
    }
    if(app->sd_update) {
        wlan_sd_update_free(app->sd_update);
        app->sd_update = NULL;
    }
    wlan_hal_stop();

    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewLoading);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewPopup);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewVariableItemList);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewLan);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewConnect);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewPortscan);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewHandshake);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewHandshakeChannel);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewDeauther);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewSniffer);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewEvilPortal);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewEvilPortalCaptured);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewLiveCreds);
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewSdUpdate);

    submenu_free(app->submenu);
    widget_free(app->widget);
    loading_free(app->loading);
    popup_free(app->popup);
    text_input_free(app->text_input);
    variable_item_list_free(app->variable_item_list);
    wlan_lan_view_free(app->view_lan);
    wlan_connect_view_free(app->view_connect);
    wlan_portscan_view_free(app->view_portscan);
    wlan_handshake_view_free(app->view_handshake);
    wlan_handshake_channel_view_free(app->view_handshake_channel);
    wlan_deauther_view_free(app->view_deauther);
    wlan_sniffer_view_free(app->sniffer_view_obj);
    wlan_evil_portal_view_free(app->evil_portal_view_obj);
    wlan_evil_portal_captured_view_free(app->evil_portal_captured_view_obj);
    wlan_live_creds_view_free(app->live_creds_view_obj);
    wlan_sd_update_view_free(app->view_sd_update);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    free(app->ap_records);
    free(app->devices);
    furi_string_free(app->text_buf);

    furi_record_close(RECORD_GUI);
    app->gui = NULL;
    free(app);
}

/* --- Evil Portal RAM headroom -------------------------------------------------
 * The portal (SoftAP + httpd + DNS) needs ~10-15 KB more internal RAM than the
 * fully-loaded WiFi app leaves free on this no-PSRAM board. These free the
 * feature views NOT used while the portal runs, and restore them on portal exit
 * before the user can navigate back to those features. Keep this list in sync
 * with wlan_app_alloc()/wlan_app_free(). */
void wlan_app_portal_views_free(WlanApp* app) {
    if(!app->view_lan) return; // already freed

    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewLan);
    wlan_lan_view_free(app->view_lan);
    app->view_lan = NULL;
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewConnect);
    wlan_connect_view_free(app->view_connect);
    app->view_connect = NULL;
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewPortscan);
    wlan_portscan_view_free(app->view_portscan);
    app->view_portscan = NULL;
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewHandshake);
    wlan_handshake_view_free(app->view_handshake);
    app->view_handshake = NULL;
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewHandshakeChannel);
    wlan_handshake_channel_view_free(app->view_handshake_channel);
    app->view_handshake_channel = NULL;
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewDeauther);
    wlan_deauther_view_free(app->view_deauther);
    app->view_deauther = NULL;
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewSniffer);
    wlan_sniffer_view_free(app->sniffer_view_obj);
    app->sniffer_view_obj = NULL;
    app->view_sniffer = NULL;
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewLiveCreds);
    wlan_live_creds_view_free(app->live_creds_view_obj);
    app->live_creds_view_obj = NULL;
    app->view_live_creds = NULL;
    view_dispatcher_remove_view(app->view_dispatcher, WlanAppViewSdUpdate);
    wlan_sd_update_view_free(app->view_sd_update);
    app->view_sd_update = NULL;
}

void wlan_app_portal_views_restore(WlanApp* app) {
    if(app->view_lan) return; // already present

    app->view_lan = wlan_lan_view_alloc();
    wlan_lan_view_set_view_dispatcher(app->view_lan, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewLan, app->view_lan);
    app->view_connect = wlan_connect_view_alloc();
    wlan_connect_view_set_view_dispatcher(app->view_connect, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewConnect, app->view_connect);
    app->view_portscan = wlan_portscan_view_alloc();
    view_set_context(app->view_portscan, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewPortscan, app->view_portscan);
    app->view_handshake = wlan_handshake_view_alloc();
    view_set_context(app->view_handshake, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewHandshake, app->view_handshake);
    app->view_handshake_channel = wlan_handshake_channel_view_alloc();
    view_set_context(app->view_handshake_channel, app->view_dispatcher);
    view_dispatcher_add_view(
        app->view_dispatcher, WlanAppViewHandshakeChannel, app->view_handshake_channel);
    app->view_deauther = wlan_deauther_view_alloc();
    view_set_context(app->view_deauther, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewDeauther, app->view_deauther);
    app->sniffer_view_obj = wlan_sniffer_view_alloc();
    app->view_sniffer = wlan_sniffer_view_get_view(app->sniffer_view_obj);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewSniffer, app->view_sniffer);
    app->live_creds_view_obj = wlan_live_creds_view_alloc();
    app->view_live_creds = wlan_live_creds_view_get_view(app->live_creds_view_obj);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewLiveCreds, app->view_live_creds);
    app->view_sd_update = wlan_sd_update_view_alloc();
    view_set_context(app->view_sd_update, app->view_dispatcher);
    view_dispatcher_add_view(app->view_dispatcher, WlanAppViewSdUpdate, app->view_sd_update);
}

/* Flush + unmount the SD so no captured data (pcaps, creds, passwords) or
 * settings write is lost, THEN soft-reset. The reset hands the BLE controller
 * RAM (released for WiFi) back for the next boot so Bluetooth works again. */
static void wlan_app_flush_and_reset(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_sd_unmount(storage);
    furi_record_close(RECORD_STORAGE);
    furi_delay_ms(100); // let the unmount + any logs settle
    esp_restart();
}

int32_t wlan_app(void* args) {
    UNUSED(args);

    /* Fully release Bluetooth FIRST (reclaims ~64 KB incl. the deinit residual)
     * so the free-RAM gate below passes even right after Bluetooth was used —
     * this is the "using WiFi shuts Bluetooth down fully" behaviour. */
    wlan_hal_release_bt();

    /* Bail cleanly if the heap is too depleted to allocate the app safely
     * (e.g. right after a radio app like BLE Spam leaked memory) — otherwise a
     * view allocation returns NULL and we crash on first use. */
    size_t freeh = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if(freeh < WLAN_APP_MIN_FREE_INTERNAL) {
        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
        DialogMessage* msg = dialog_message_alloc();
        dialog_message_set_header(msg, "WiFi Unavailable", 64, 8, AlignCenter, AlignTop);
        dialog_message_set_text(
            msg,
            "Not enough free RAM.\nReboot, or close other\nradio apps first.",
            64,
            34,
            AlignCenter,
            AlignCenter);
        dialog_message_set_buttons(msg, NULL, NULL, "OK");
        dialog_message_show(dialogs, msg);
        dialog_message_free(msg);
        furi_record_close(RECORD_DIALOGS);
        /* We already released the BLE controller RAM at entry, so soft-reset to
         * bring it (and Bluetooth) back for the next boot. */
        wlan_app_flush_and_reset();
        return 0;
    }

    WlanApp* app = wlan_app_alloc();
    scene_manager_next_scene(app->scene_manager, WlanAppSceneMain);
    view_dispatcher_run(app->view_dispatcher);
    wlan_app_free(app);

    /* Leaving WiFi: we mem_release()'d the BLE controller on entry, so Bluetooth
     * cannot re-init this boot. Soft-reset so the next boot has all the radio RAM
     * back and BLE works again — the clean way to alternate WiFi and Bluetooth on
     * this no-PSRAM board (the user asked for this explicitly). */
    FURI_LOG_I("WlanApp", "WiFi closed — flush SD + soft reset to restore BLE RAM");
    wlan_app_flush_and_reset();
    return 0;
}
