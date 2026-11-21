#!/usr/bin/env python3
"""Host regression: compile actual model, layouts, handlers and event callback.
Usage: python3 tests/host/test_text_input.py [path/to/text_input.c]
Rendering, model lifecycle and hardware are outside this harness.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / 'components/gui/modules/text_input.c'
text = source.read_text()
# Keep production definitions and functions verbatim; remove SDK-dependent drawing/lifecycle.
model = text[text.index('struct TextInput {'):text.index('static void text_input_view_draw_callback(')]
handlers = text[text.index('static void text_input_handle_'):text.index('void text_input_timer_callback(')]
shim = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define UNUSED(x) (void)(x)
#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))
#define CLAMP(x, upper, lower) (((x) > (upper)) ? (upper) : (((x) < (lower)) ? (lower) : (x)))
#define furi_assert(x) assert(x)
#define furi_check(x) assert(x)
typedef void View;
typedef void FuriTimer;
typedef void FuriString;
typedef struct TextInput TextInput;
typedef void (*TextInputCallback)(void*);
typedef bool (*TextInputValidatorCallback)(const char*, FuriString*, void*);
typedef enum {InputKeyUp, InputKeyDown, InputKeyRight, InputKeyLeft, InputKeyOk, InputKeyBack} InputKey;
typedef enum {InputTypePress, InputTypeRelease, InputTypeShort, InputTypeLong, InputTypeRepeat, InputTypeText} InputType;
typedef struct {InputKey key; InputType type;} InputEvent;
static void* view_get_model(View* view) { return view; }
static void view_commit_model(View* view, bool update) { (void)view; (void)update; }
static unsigned furi_kernel_get_tick_frequency(void) { return 1000; }
static unsigned timer_starts;
static void furi_timer_start(FuriTimer* timer, unsigned ticks) {
    (void)timer; assert(ticks == 4000); timer_starts++;
}
'''
cases = r'''
static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while(0)
static TextInputModel fresh(char* buf, size_t cap) {
    TextInputModel m = {0}; m.text_buffer = buf; m.text_buffer_size = cap;
    m.cursor_pos = strlen(buf); m.minimum_length = 1; return m;
}
static bool send(TextInputModel* m, InputKey key, InputType type) {
    TextInput ti = {.view = m}; InputEvent event = {.key = key, .type = type};
    return text_input_view_input_callback(&event, &ti);
}
static unsigned saves;
static void save(void* ctx) { assert(ctx == &saves); saves++; }
static bool reject(const char* s, FuriString* msg, void* ctx) {
    (void)s; (void)msg; (void)ctx; return false;
}
int main(void) {
    char buf[32] = "abc";
    const InputKey keys[] = {InputKeyUp, InputKeyDown, InputKeyLeft, InputKeyRight};
    const InputType types[] = {InputTypeShort, InputTypeLong, InputTypeRepeat};
    // Exhaust every valid key position in both keyboards and all navigation event types.
    for(unsigned kb = 0; kb < 2; kb++) for(unsigned t = 0; t < 3; t++)
    for(unsigned row = 0; row < 3; row++) {
        TextInputModel layout = fresh(buf, sizeof(buf)); layout.selected_keyboard = kb;
        for(unsigned col = 0; col < get_row_size(&layout, row); col++)
        for(unsigned k = 0; k < 4; k++) {
            TextInputModel m = layout; m.selected_row = row; m.selected_column = col;
            unsigned er = row, ec = col;
            if(keys[k] == InputKeyUp && row > 0) er--;
            if(keys[k] == InputKeyDown && row < 2) er++;
            if(keys[k] == InputKeyUp || keys[k] == InputKeyDown) {
                unsigned last = get_row_size(&layout, er) - 1;
                if(ec > last) ec = last;
            } else if(keys[k] == InputKeyLeft) {
                if(col) ec--; else { er = (row + 2) % 3; ec = get_row_size(&layout, er) - 1; }
            } else {
                if(col + 1 < get_row_size(&layout, row)) ec++;
                else { er = (row + 1) % 3; ec = 0; }
            }
            CHECK(send(&m, keys[k], types[t]));
            CHECK(m.selected_row == er && m.selected_column == ec);
            CHECK(!m.cursor_select && m.cursor_pos == 3 && strcmp(buf, "abc") == 0);
        }
    }
    // Cursor mode has no current public entry point; exercise it directly.
    for(unsigned t = 0; t < 3; t++) for(unsigned pos = 0; pos <= 3; pos++)
    for(unsigned k = 0; k < 4; k++) {
        TextInputModel m = fresh(buf, sizeof(buf)); m.cursor_select = true;
        m.cursor_pos = pos; m.clear_default_text = true;
        CHECK(send(&m, keys[k], types[t]));
        if(keys[k] == InputKeyLeft || keys[k] == InputKeyRight) {
            unsigned expected = keys[k] == InputKeyLeft ? (pos ? pos - 1 : 0) : (pos < 3 ? pos + 1 : 3);
            CHECK(m.cursor_pos == expected && m.cursor_select && !m.clear_default_text);
        } else CHECK(m.cursor_pos == pos && !m.cursor_select && m.clear_default_text);
        CHECK(m.selected_row == 0 && m.selected_column == 0 && strcmp(buf, "abc") == 0);
    }
    TextInputModel m = fresh(buf, sizeof(buf));
    CHECK(!send(&m, InputKeyRight, InputTypePress));
    CHECK(!send(&m, InputKeyRight, InputTypeRelease));
    CHECK(!send(&m, InputKeyBack, InputTypeShort));
    CHECK(m.selected_column == 0 && m.cursor_pos == 3);
    m.validator_message_visible = true;
    CHECK(send(&m, InputKeyRight, InputTypeShort));
    CHECK(!m.validator_message_visible && m.selected_column == 0);
    // Physical ASCII inserts at cursor; repeat OK does not duplicate a glyph.
    m.cursor_pos = 1;
    CHECK(send(&m, (InputKey)'X', InputTypeText)); CHECK(strcmp(buf, "aXbc") == 0);
    CHECK(send(&m, InputKeyOk, InputTypeRepeat)); CHECK(strcmp(buf, "aXbc") == 0);
    CHECK(send(&m, InputKeyBack, InputTypeLong)); CHECK(strcmp(buf, "abc") == 0);
    CHECK(send(&m, InputKeyBack, InputTypeRepeat)); CHECK(strcmp(buf, "bc") == 0);
    m.clear_default_text = true;
    CHECK(send(&m, (InputKey)'!', InputTypeText)); CHECK(strcmp(buf, "!") == 0 && m.cursor_pos == 1);
    strcpy(buf, "abc"); m = fresh(buf, sizeof(buf)); m.cursor_pos = 1;
    CHECK(send(&m, InputKeyOk, InputTypeShort)); CHECK(strcmp(buf, "aqbc") == 0);
    CHECK(send(&m, InputKeyOk, InputTypeLong)); CHECK(strcmp(buf, "aqQbc") == 0);
    m.selected_row = 1; m.selected_column = 9;
    CHECK(send(&m, InputKeyOk, InputTypeShort)); CHECK(strcmp(buf, "aqbc") == 0);
    m.selected_row = 2; m.selected_column = 0;
    CHECK(send(&m, InputKeyOk, InputTypeShort)); CHECK(m.selected_keyboard == 1);
    CHECK(send(&m, InputKeyOk, InputTypeShort)); CHECK(m.selected_keyboard == 0);
    m.selected_column = 12; switch_keyboard(&m); CHECK(m.selected_column == 9);
    m.selected_column = 6; m.callback = save; m.callback_context = &saves;
    m.minimum_length = 31; CHECK(send(&m, InputKeyOk, InputTypeShort)); CHECK(saves == 0);
    m.minimum_length = 1; m.validator_callback = reject;
    CHECK(send(&m, InputKeyOk, InputTypeShort)); CHECK(m.validator_message_visible && timer_starts == 1 && saves == 0);
    m.validator_message_visible = false; m.validator_callback = NULL;
    CHECK(send(&m, InputKeyOk, InputTypeShort)); CHECK(saves == 1);
    char full[4] = "abc"; m = fresh(full, sizeof(full));
    CHECK(send(&m, (InputKey)'X', InputTypeText)); CHECK(strcmp(full, "abc") == 0);
    CHECK(send(&m, InputKeyOk, InputTypeShort)); CHECK(strcmp(full, "abc") == 0);
    char empty[4] = ""; m = fresh(empty, sizeof(empty)); m.cursor_select = true;
    CHECK(send(&m, InputKeyLeft, InputTypeShort)); CHECK(m.cursor_pos == 0);
    CHECK(send(&m, InputKeyRight, InputTypeShort)); CHECK(m.cursor_pos == 0);
    CHECK(send(&m, InputKeyOk, InputTypeShort)); CHECK(m.clear_default_text);
    printf("PASS: %u assertions (real production handlers and input callback)\n", checks);
}
'''
with tempfile.TemporaryDirectory(prefix='text-input-test-') as tmp:
    cfile = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    cfile.write_text(shim + model + handlers + cases)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-g', '-O1',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(cfile), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
