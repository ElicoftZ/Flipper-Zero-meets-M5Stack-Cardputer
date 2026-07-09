#include "gpio_app_i.h"

#include <furi.h>
#include <furi_hal.h>

static bool gpio_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    GpioApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool gpio_app_back_event_callback(void* context) {
    furi_assert(context);
    GpioApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void gpio_app_tick_event_callback(void* context) {
    furi_assert(context);
    GpioApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

GpioApp* gpio_app_alloc(void) {
    GpioApp* app = malloc(sizeof(GpioApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->gpio_items = gpio_items_alloc();

    /* Custom Pin scene defaults (struct is malloc'd, not zeroed). Index 2 in the
     * scene's pin list is GPIO2 (Grove G2) — a safe external default. */
    app->custom_pin_index = 2;
    app->custom_mode_idx = 0;
    app->custom_out_high = false;
    app->custom_applied_index = 0xFF; /* none */
    app->custom_read_item = NULL;

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&gpio_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, gpio_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, gpio_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, gpio_app_tick_event_callback, 100);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        GpioAppViewVarItemList,
        variable_item_list_get_view(app->var_item_list));
    app->gpio_test = gpio_test_alloc(app->gpio_items);
    view_dispatcher_add_view(
        app->view_dispatcher, GpioAppViewGpioTest, gpio_test_get_view(app->gpio_test));

    scene_manager_next_scene(app->scene_manager, GpioSceneStart);

    return app;
}

void gpio_app_free(GpioApp* app) {
    furi_assert(app);

    // Views
    view_dispatcher_remove_view(app->view_dispatcher, GpioAppViewVarItemList);
    view_dispatcher_remove_view(app->view_dispatcher, GpioAppViewGpioTest);
    variable_item_list_free(app->var_item_list);
    gpio_test_free(app->gpio_test);

    // View dispatcher
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    // Close records
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    gpio_items_free(app->gpio_items);
    free(app);
}

int32_t gpio_app(void* p) {
    UNUSED(p);
    GpioApp* gpio_app = gpio_app_alloc();

    view_dispatcher_run(gpio_app->view_dispatcher);

    gpio_app_free(gpio_app);

    return 0;
}
