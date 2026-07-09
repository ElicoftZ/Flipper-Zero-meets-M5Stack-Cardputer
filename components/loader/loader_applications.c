#include "loader.h"
#include "loader_applications.h"
#include <dialogs/dialogs.h>
#include <flipper_application/flipper_application.h>
#include <assets_icons.h>
#include <gui/gui.h>
#include <gui/view_holder.h>
#include <gui/modules/loading.h>
#include <gui/modules/submenu.h>
#include <toolbox/path.h>
#include <esp_rom_sys.h>
#include <applications.h>

#define TAG "LoaderApplications"
#define SELECT_EVENT (1u << 0)
#define EXIT_EVENT   (1u << 1)

struct LoaderApplications {
    FuriThread* thread;
    void (*closed_cb)(void*);
    void* context;
};

static void loader_applications_trace(const char* step) {
    esp_rom_printf(
        "\r\n[LA] %s free=%u\r\n",
        step,
        (unsigned)furi_thread_get_stack_space(furi_thread_get_current_id()));
}

static int32_t loader_applications_thread(void* p);

LoaderApplications* loader_applications_alloc(void (*closed_cb)(void*), void* context) {
    LoaderApplications* loader_applications = malloc(sizeof(LoaderApplications));
    loader_applications->thread =
        furi_thread_alloc_ex(TAG, 4096, loader_applications_thread, (void*)loader_applications);
    loader_applications->closed_cb = closed_cb;
    loader_applications->context = context;
    furi_thread_start(loader_applications->thread);
    return loader_applications;
}

void loader_applications_free(LoaderApplications* loader_applications) {
    furi_assert(loader_applications);
    furi_thread_join(loader_applications->thread);
    furi_thread_free(loader_applications->thread);
    free(loader_applications);
}

typedef struct {
    FuriString* file_path;
    DialogsApp* dialogs;
    Storage* storage;
    Loader* loader;

    Gui* gui;
    ViewHolder* view_holder;
    Loading* loading;
    uint32_t selected_index;
    FuriThreadId thread_id;
} LoaderApplicationsApp;

static LoaderApplicationsApp* loader_applications_app_alloc(void) {
    LoaderApplicationsApp* app = malloc(sizeof(LoaderApplicationsApp)); //-V799
    app->file_path = furi_string_alloc_set(EXT_PATH("apps"));
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->loader = furi_record_open(RECORD_LOADER);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_holder = view_holder_alloc();
    app->loading = loading_alloc();

    view_holder_attach_to_gui(app->view_holder, app->gui);

    return app;
} //-V773

static void loader_applications_app_free(LoaderApplicationsApp* app) {
    furi_assert(app);

    view_holder_free(app->view_holder);
    loading_free(app->loading);
    furi_record_close(RECORD_GUI);

    furi_record_close(RECORD_LOADER);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_string_free(app->file_path);
    free(app);
}

static bool loader_applications_item_callback(
    FuriString* path,
    void* context,
    uint8_t** icon_ptr,
    FuriString* item_name) {
    LoaderApplicationsApp* loader_applications_app = context;
    furi_assert(loader_applications_app);
    if(furi_string_end_with(path, ".fap")) {
        return flipper_application_load_name_and_icon(
            path, loader_applications_app->storage, icon_ptr, item_name);
    } else {
        path_extract_filename(path, item_name, false);
        memcpy(*icon_ptr, icon_get_frame_data(&I_js_script_10px, 0), FAP_MANIFEST_MAX_ICON_SIZE);
        return true;
    }
}

static bool loader_applications_select_app(LoaderApplicationsApp* loader_applications_app) {
    loader_applications_trace("select_begin");
    const DialogsFileBrowserOptions browser_options = {
        .extension = ".fap|.js",
        .skip_assets = true,
        .icon = &I_unknown_10px,
        .hide_ext = true,
        .item_loader_callback = loader_applications_item_callback,
        .item_loader_context = loader_applications_app,
        .base_path = EXT_PATH("apps"),
    };

    bool result = dialog_file_browser_show(
        loader_applications_app->dialogs,
        loader_applications_app->file_path,
        loader_applications_app->file_path,
        &browser_options);
    loader_applications_trace(result ? "select_done_true" : "select_done_false");
    return result;
}

// TODO: aufrufen, sobald JS-Apps im Browser auswaehlbar sind (Hook fehlt noch) -> bewusst ungenutzt
__attribute__((unused)) static void
    loader_applications_show_js_not_supported(LoaderApplicationsApp* app) {
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "JS not supported", 64, 3, AlignCenter, AlignTop);
    dialog_message_set_text(
        message, "JS apps are visible,\nbut cannot run yet.", 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "Back", NULL, NULL);
    dialog_message_show(app->dialogs, message);
    dialog_message_free(message);
}

#define APPLICATION_STOP_EVENT 1

static void loader_pubsub_callback(const void* message, void* context) {
    const LoaderEvent* event = message;
    const FuriThreadId thread_id = (FuriThreadId)context;

    if(event->type == LoaderEventTypeNoMoreAppsInQueue) {
        furi_thread_flags_set(thread_id, APPLICATION_STOP_EVENT);
    }
}

static void
    loader_applications_start_app(LoaderApplicationsApp* app, const char* name, const char* args) {
    loader_applications_trace("start_app_begin");
    // load app
    FuriThreadId thread_id = furi_thread_get_current_id();
    FuriPubSubSubscription* subscription =
        furi_pubsub_subscribe(loader_get_pubsub(app->loader), loader_pubsub_callback, thread_id);

    LoaderStatus status = loader_start_with_gui_error(app->loader, name, args);
    loader_applications_trace("start_app_after_loader");

    if(status == LoaderStatusOk) {
        furi_thread_flags_wait(APPLICATION_STOP_EVENT, FuriFlagWaitAny, FuriWaitForever);
    }

    furi_pubsub_unsubscribe(loader_get_pubsub(app->loader), subscription);
    furi_thread_flags_clear(APPLICATION_STOP_EVENT);
}

static void loader_applications_back_callback(void* context) {
    LoaderApplicationsApp* app = context;
    furi_thread_flags_set(app->thread_id, EXIT_EVENT);
}

static void loader_applications_submenu_callback(void* context, uint32_t index) {
    LoaderApplicationsApp* app = context;
    app->selected_index = index;
    furi_thread_flags_set(app->thread_id, SELECT_EVENT);
}

static int32_t loader_applications_thread(void* p) {
    LoaderApplications* loader_applications = p;
    loader_applications_trace("thread_start");
    LoaderApplicationsApp* app = loader_applications_app_alloc();
    app->thread_id = furi_thread_get_current_id();
    loader_applications_trace("app_alloc");

    Submenu* submenu = submenu_alloc();
    submenu_set_header(submenu, "Applications");
    for(size_t i = 0; i < FLIPPER_INTERNAL_EXTERNAL_APPS_COUNT; i++) {
        submenu_add_item(
            submenu,
            FLIPPER_INTERNAL_EXTERNAL_APPS[i].name,
            i,
            loader_applications_submenu_callback,
            app);
    }

    view_holder_set_back_callback(app->view_holder, loader_applications_back_callback, app);
    view_holder_set_view(app->view_holder, submenu_get_view(submenu));

    while(true) {
        uint32_t flags = furi_thread_flags_wait(SELECT_EVENT | EXIT_EVENT, FuriFlagWaitAny, FuriWaitForever);
        if(flags & EXIT_EVENT) {
            break;
        }
        if(flags & SELECT_EVENT) {
            // Temporarily hide the menu so the launched app can use the screen
            view_holder_set_view(app->view_holder, NULL);
            
            // Start the selected app
            const char* app_name = FLIPPER_INTERNAL_EXTERNAL_APPS[app->selected_index].path;
            loader_applications_start_app(app, app_name, NULL);
            
            // Restore the submenu after the app finishes
            view_holder_set_view(app->view_holder, submenu_get_view(submenu));
        }
    }

    view_holder_set_view(app->view_holder, NULL);
    submenu_free(submenu);
    loader_applications_app_free(app);
    loader_applications_trace("app_free");

    if(loader_applications->closed_cb) {
        loader_applications->closed_cb(loader_applications->context);
    }

    loader_applications_trace("thread_end");
    return 0;
}
