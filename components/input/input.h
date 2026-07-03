/**
 * @file input.h
 * Input: main API
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_INPUT_EVENTS            "input_events"
#define INPUT_SEQUENCE_SOURCE_HARDWARE (0u)
#define INPUT_SEQUENCE_SOURCE_SOFTWARE (1u)
/* Touch panels (Waveshare). A 2-axis source: it natively distinguishes
 * horizontal from vertical swipes, so the encoder Up/Down<->Left/Right
 * remapping in view_dispatcher (meant for the 1-axis T-Embed rotary) must NOT
 * be applied to it. ViewPort orientation remapping still applies (see
 * view_port.c), so touch is treated like HARDWARE everywhere except that one
 * encoder-specific remap. */
#define INPUT_SEQUENCE_SOURCE_TOUCH    (2u)
/* Full physical keyboard (Cardputer-ADV TCA8418).  Like TOUCH this is a true
 * 2-axis source — Left/Right keys exist physically — so the 1-axis encoder
 * Up↔Left/Right remap in view_dispatcher must NOT be applied.  ViewPort
 * orientation remapping still applies normally. */
#define INPUT_SEQUENCE_SOURCE_KEYBOARD (3u)

/** Input Keys */
typedef enum {
    InputKeyUp,
    InputKeyDown,
    InputKeyRight,
    InputKeyLeft,
    InputKeyOk,
    InputKeyBack,
    InputKeyMAX, /**< Special value */
} InputKey;

/** Input Types
 * Some of them are physical events and some logical
 */
typedef enum {
    InputTypePress, /**< Press event, emitted after debounce */
    InputTypeRelease, /**< Release event, emitted after debounce */
    InputTypeShort, /**< Short event, emitted after InputTypeRelease done within INPUT_LONG_PRESS interval */
    InputTypeLong, /**< Long event, emitted after INPUT_LONG_PRESS_COUNTS interval, asynchronous to InputTypeRelease  */
    InputTypeRepeat, /**< Repeat event, emitted with INPUT_LONG_PRESS_COUNTS period after InputTypeLong event */
    InputTypeMAX, /**< Special value for exceptional; equals the count of RPC-mapped types (rpc_gui.c asserts InputTypeMAX==5) */
    InputTypeText, /**< Printable character typed on a physical keyboard (Cardputer-ADV); ASCII carried in InputEvent.key. Placed AFTER InputTypeMAX to preserve the protobuf contract; not sent over RPC. Consumers MUST check type==InputTypeText before reading key as a character. */
} InputType;

/** Input Event, dispatches with FuriPubSub */
typedef struct {
    union {
        uint32_t sequence;
        struct {
            uint8_t sequence_source   : 2;
            uint32_t sequence_counter : 30;
        };
    };
    InputKey key;
    InputType type;
} InputEvent;

/** Get human readable input key name
 * @param key - InputKey
 * @return string
 */
const char* input_get_key_name(InputKey key);

/** Get human readable input type name
 * @param type - InputType
 * @return string
 */
const char* input_get_type_name(InputType type);

#ifdef __cplusplus
}
#endif
