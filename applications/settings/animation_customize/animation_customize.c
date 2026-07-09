#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <power/power_service/power.h>

#define TAG "AnimationCustomize"
#define ANIMATION_DIR EXT_PATH("dolphin")
#define ANIMATION_MANIFEST_FILE ANIMATION_DIR "/manifest.txt"
#define MAX_ANIMATIONS 100

typedef struct {
    char name[64];
    bool selected;
} AnimationItem;

typedef enum {
    AnimationCustomizeViewList,
    AnimationCustomizeViewConfirm,
} AnimationCustomizeView;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    VariableItemList* variable_item_list;
    Widget* widget;
    AnimationItem items[MAX_ANIMATIONS];
    uint32_t total_count;
    AnimationCustomizeView current_view;
} AnimationCustomizeApp;

// Helper: Check if name is in the manifest file
static bool is_animation_in_manifest(Storage* storage, const char* name) {
    bool found = false;
    FlipperFormat* file = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_existing(file, ANIMATION_MANIFEST_FILE)) {
        FuriString* read_string = furi_string_alloc();
        uint32_t version;
        if(flipper_format_read_header(file, read_string, &version) &&
           furi_string_cmp_str(read_string, "Flipper Animation Manifest") == 0) {
            while(flipper_format_read_string(file, "Name", read_string)) {
                if(furi_string_cmp_str(read_string, name) == 0) {
                    found = true;
                    break;
                }
                // Skip other parameters of this animation
                uint32_t u32val;
                flipper_format_read_uint32(file, "Min butthurt", &u32val, 1);
                flipper_format_read_uint32(file, "Max butthurt", &u32val, 1);
                flipper_format_read_uint32(file, "Min level", &u32val, 1);
                flipper_format_read_uint32(file, "Max level", &u32val, 1);
                flipper_format_read_uint32(file, "Weight", &u32val, 1);
            }
        }
        furi_string_free(read_string);
        flipper_format_free(file);
    }
    return found;
}

// Helper: Scan /ext/dolphin for folders
static void scan_animations(AnimationCustomizeApp* app) {
    app->total_count = 0;
    File* dir = storage_file_alloc(app->storage);
    if(storage_dir_open(dir, ANIMATION_DIR)) {
        FileInfo fileinfo;
        char name[256];
        while(storage_dir_read(dir, &fileinfo, name, sizeof(name)) && app->total_count < MAX_ANIMATIONS) {
            if(fileinfo.flags & FSF_DIRECTORY) {
                if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                    continue;
                }
                strncpy(app->items[app->total_count].name, name, 63);
                app->items[app->total_count].name[63] = '\0';
                app->items[app->total_count].selected = is_animation_in_manifest(app->storage, name);
                app->total_count++;
            }
        }
        storage_dir_close(dir);
    }
    storage_file_free(dir);
}

// Callback: Variable item value change
static void item_change_callback(VariableItem* item) {
    AnimationCustomizeApp* app = variable_item_get_context(item);
    uint32_t index = (uint32_t)variable_item_get_current_value_index(item);
    
    // Find the item index in our list
    uint8_t selected_item_idx = variable_item_list_get_selected_item_index(app->variable_item_list);
    app->items[selected_item_idx].selected = (index == 1);
    
    variable_item_set_current_value_text(item, index ? "[X]" : "[ ]");
}

// Callback: Enter button on list item (toggle)
static void item_enter_callback(void* context, uint32_t index) {
    AnimationCustomizeApp* app = context;
    if(index < app->total_count) {
        app->items[index].selected = !app->items[index].selected;
        VariableItem* item = variable_item_list_get(app->variable_item_list, index);
        if(item) {
            variable_item_set_current_value_index(item, app->items[index].selected ? 1 : 0);
            variable_item_set_current_value_text(item, app->items[index].selected ? "[X]" : "[ ]");
        }
    }
}

// Callback: Dialog/Widget button callbacks
static void confirm_button_callback(GuiButtonType button_type, InputType type, void* context) {
    AnimationCustomizeApp* app = context;
    if(type != InputTypeShort) return;

    if(button_type == GuiButtonTypeLeft) {
        // "No" pressed -> Go back to list
        app->current_view = AnimationCustomizeViewList;
        view_dispatcher_switch_to_view(app->view_dispatcher, AnimationCustomizeViewList);
    } else if(button_type == GuiButtonTypeRight) {
        // "Yes" pressed -> Write manifest.txt and trigger reset!
        
        // 1. Write the new manifest file
        FlipperFormat* file = flipper_format_file_alloc(app->storage);
        if(flipper_format_file_open_always(file, ANIMATION_MANIFEST_FILE)) {
            flipper_format_write_header_cstr(file, "Flipper Animation Manifest", 1);
            for(uint32_t i = 0; i < app->total_count; i++) {
                if(app->items[i].selected) {
                    flipper_format_write_string_cstr(file, "Name", app->items[i].name);
                    flipper_format_write_uint32(file, "Min butthurt", (uint32_t[]){0}, 1);
                    flipper_format_write_uint32(file, "Max butthurt", (uint32_t[]){14}, 1);
                    flipper_format_write_uint32(file, "Min level", (uint32_t[]){1}, 1);
                    flipper_format_write_uint32(file, "Max level", (uint32_t[]){3}, 1);
                    flipper_format_write_uint32(file, "Weight", (uint32_t[]){1}, 1);
                }
            }
            flipper_format_free(file);
        }
        
        // 2. Clear widget and show reset warning
        widget_reset(app->widget);
        widget_add_string_multiline_element(
            app->widget,
            120,
            25,
            AlignCenter,
            AlignCenter,
            FontSecondary,
            "Rebooting device...\nRequired to apply animations.");
            
        // 3. Reboot the device
        FURI_LOG_I(TAG, "Rebooting to apply animation updates...");
        furi_delay_ms(1500);
        Power* power = furi_record_open(RECORD_POWER);
        power_reboot(power, PowerBootModeNormal);
        furi_record_close(RECORD_POWER);
    }
}

// Callback: Back navigation handler
static bool back_event_callback(void* context) {
    AnimationCustomizeApp* app = context;

    if(app->current_view == AnimationCustomizeViewList) {
        // Go to confirmation page
        widget_reset(app->widget);
        
        // Build selected list string
        FuriString* str = furi_string_alloc();
        uint32_t selected_count = 0;
        
        furi_string_cat_printf(str, "\e#Selected animations:\n");
        for(uint32_t i = 0; i < app->total_count; i++) {
            if(app->items[i].selected) {
                furi_string_cat_printf(str, "- %s\n", app->items[i].name);
                selected_count++;
            }
        }
        
        furi_string_cat_printf(str, "\nTotal: %ld animations selected.\n", selected_count);
        furi_string_cat_printf(str, "\e#Warning: Device will soft reset\nrequired to apply updates.\nSave & Reboot?\n");
        
        widget_add_text_scroll_element(app->widget, 0, 0, 240, 95, furi_string_get_cstr(str));
        furi_string_free(str);
        
        // Add left and right button
        widget_add_button_element(app->widget, GuiButtonTypeLeft, "No", confirm_button_callback, app);
        widget_add_button_element(app->widget, GuiButtonTypeRight, "Yes", confirm_button_callback, app);
        
        app->current_view = AnimationCustomizeViewConfirm;
        view_dispatcher_switch_to_view(app->view_dispatcher, AnimationCustomizeViewConfirm);
        return true;
    } else if(app->current_view == AnimationCustomizeViewConfirm) {
        // Go back to list
        app->current_view = AnimationCustomizeViewList;
        view_dispatcher_switch_to_view(app->view_dispatcher, AnimationCustomizeViewList);
        return true;
    }
    
    return false;
}

// App entry point
int32_t animation_customize_app(void* p) {
    UNUSED(p);
    AnimationCustomizeApp* app = malloc(sizeof(AnimationCustomizeApp));
    
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->current_view = AnimationCustomizeViewList;
    
    scan_animations(app);
    
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, back_event_callback);
    
    app->variable_item_list = variable_item_list_alloc();
    variable_item_list_set_enter_callback(app->variable_item_list, item_enter_callback, app);
    
    // Add all items to the variable item list view
    for(uint32_t i = 0; i < app->total_count; i++) {
        VariableItem* item = variable_item_list_add(
            app->variable_item_list,
            app->items[i].name,
            2,
            item_change_callback,
            app);
        variable_item_set_current_value_index(item, app->items[i].selected ? 1 : 0);
        variable_item_set_current_value_text(item, app->items[i].selected ? "[X]" : "[ ]");
    }
    
    app->widget = widget_alloc();
    
    view_dispatcher_add_view(
        app->view_dispatcher,
        AnimationCustomizeViewList,
        variable_item_list_get_view(app->variable_item_list));
    view_dispatcher_add_view(
        app->view_dispatcher,
        AnimationCustomizeViewConfirm,
        widget_get_view(app->widget));
        
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, AnimationCustomizeViewList);
    
    view_dispatcher_run(app->view_dispatcher);
    
    view_dispatcher_remove_view(app->view_dispatcher, AnimationCustomizeViewList);
    view_dispatcher_remove_view(app->view_dispatcher, AnimationCustomizeViewConfirm);
    
    variable_item_list_free(app->variable_item_list);
    widget_free(app->widget);
    view_dispatcher_free(app->view_dispatcher);
    
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    
    free(app);
    return 0;
}
