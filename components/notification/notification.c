/**
 * @file notification.c
 * @brief ESP32 notification service with STM32-style backlight handling.
 */

#include <furi.h>
#include <furi_hal_display.h>
#include <furi_hal_light.h>
#include <furi_hal_power.h>
#include <furi_hal_rtc.h>
#include <furi_hal_speaker.h>
#include <btshim.h>
#include <esp_random.h>
#include <input.h>
#include <saved_struct.h>
#include <storage/storage.h>
#include <loader/loader.h>
#include "notification_app.h"
#include "notification_messages.h"

#define TAG "NotificationSrv"

#define NOTIFICATION_EVENT_COMPLETE 0x00000001U

#define NOTIFICATION_SETTINGS_PATH    INT_PATH(".notification.settings")
#define NOTIFICATION_SETTINGS_MAGIC   0x42
#define NOTIFICATION_SETTINGS_VERSION 0x0A

/* Status LED enable flag — its own tiny file so toggling it never invalidates
 * (resets) the main notification settings above. */
#define STATUS_LED_PATH    INT_PATH(".statusled.settings")
#define STATUS_LED_MAGIC   0x53
#define STATUS_LED_VERSION 0x01

typedef enum {
    NotificationLayerMessage,
    InternalLayerMessage,
    SaveSettingsMessage,
} NotificationAppMessageType;

typedef struct {
    const NotificationSequence* sequence;
    NotificationAppMessageType type;
    FuriEventFlag* back_event;
} NotificationAppMessage;

static bool lcd_backlight_is_on = false;

static uint8_t notification_clamp_u8(float value) {
    if(value <= 0.0f) return 0;
    if(value >= 255.0f) return UINT8_MAX;

    return (uint8_t)value;
}

static uint8_t notification_scale_display_brightness(
    NotificationApp* app,
    uint8_t value,
    float display_brightness_setting) {
    return notification_clamp_u8(value * display_brightness_setting * app->current_night_shift);
}

static uint8_t notification_settings_get_display_brightness(NotificationApp* app, uint8_t value) {
    return notification_clamp_u8(value * app->settings.display_brightness);
}

static uint32_t notification_settings_display_off_delay_ticks(NotificationApp* app) {
    return (float)(app->settings.display_off_delay_ms) /
           (1000.0f / furi_kernel_get_tick_frequency());
}

static void notification_apply_internal_display_layer(NotificationApp* app, uint8_t layer_value) {
    furi_assert(app);

    app->display.value[LayerInternal] = layer_value;

    if(app->display.index == LayerInternal) {
        furi_hal_display_set_backlight(app->display.value[LayerInternal]);
    }
}

static void notification_apply_notification_display_layer(NotificationApp* app, uint8_t layer_value) {
    furi_assert(app);

    app->display.index = LayerNotification;
    app->display.value[LayerNotification] = layer_value;

    if(lcd_backlight_is_on) return;

    furi_hal_display_set_backlight(app->display.value[LayerNotification]);
}

static void notification_reset_notification_display_layer(NotificationApp* app) {
    furi_assert(app);

    app->display.value[LayerNotification] = 0;
    app->display.index = LayerInternal;

    furi_hal_display_set_backlight(app->display.value[LayerInternal]);
}

static void notification_reset_notification_layer(
    NotificationApp* app,
    bool reset_display,
    float display_brightness_setting) {
    if(!reset_display) return;

    if(display_brightness_setting != app->settings.display_brightness) {
        furi_hal_display_set_backlight(
            notification_clamp_u8(
                app->settings.display_brightness * 255.0f * app->current_night_shift));
    }

    if(app->settings.display_off_delay_ms > 0) {
        furi_timer_start(app->display_timer, notification_settings_display_off_delay_ticks(app));
    }
}

static void notification_display_timer(void* context) {
    furi_assert(context);
    NotificationApp* app = context;
    notification_message(app, &sequence_display_backlight_off);
}

static void night_shift_demo_timer_callback(void* context) {
    furi_assert(context);
    NotificationApp* app = context;
    notification_message(app, &sequence_display_backlight_force_on);
}

void night_shift_timer_start(NotificationApp* app) {
    if(app->settings.night_shift != 1.0f) {
        if(furi_timer_is_running(app->night_shift_timer)) {
            furi_timer_stop(app->night_shift_timer);
        }
        furi_timer_start(app->night_shift_timer, furi_ms_to_ticks(1000));
    }
}

void night_shift_timer_stop(NotificationApp* app) {
    if(furi_timer_is_running(app->night_shift_timer)) {
        furi_timer_stop(app->night_shift_timer);
    }
}

static void night_shift_timer_callback(void* context) {
    furi_assert(context);
    NotificationApp* app = context;
    DateTime current_date_time;

    furi_hal_rtc_get_datetime(&current_date_time);
    uint32_t time = current_date_time.hour * 60 + current_date_time.minute;

    if((time > app->settings.night_shift_end) && (time < app->settings.night_shift_start)) {
        app->current_night_shift = 1.0f;
    } else {
        app->current_night_shift = app->settings.night_shift;
    }
}

static void notification_process_notification_message(
    NotificationApp* app,
    NotificationAppMessage* message) {
    uint32_t notification_message_index = 0;
    const NotificationMessage* notification_message = (*message->sequence)[notification_message_index];

    bool reset_notifications = true;
    float display_brightness_setting = app->settings.display_brightness;
    float speaker_volume_setting = app->settings.speaker_volume;
    bool force_volume = false;
    bool reset_display = false;

    while(notification_message != NULL) {
        switch(notification_message->type) {
        case NotificationMessageTypeLedDisplayBacklight:
            if(notification_message->data.led.value > 0x00) {
                notification_apply_notification_display_layer(
                    app,
                    notification_scale_display_brightness(
                        app,
                        notification_message->data.led.value,
                        display_brightness_setting));
                reset_display = true;
                lcd_backlight_is_on = true;
            } else {
                reset_display = false;
                notification_reset_notification_display_layer(app);
                lcd_backlight_is_on = false;

                if(furi_timer_is_running(app->display_timer)) {
                    furi_timer_stop(app->display_timer);
                }
            }
            break;
        case NotificationMessageTypeLedDisplayBacklightForceOn:
            lcd_backlight_is_on = false;
            notification_apply_notification_display_layer(
                app,
                notification_scale_display_brightness(
                    app,
                    notification_message->data.led.value,
                    display_brightness_setting));
            reset_display = true;
            lcd_backlight_is_on = true;
            break;
        case NotificationMessageTypeLedDisplayBacklightEnforceOn:
            if(!app->display_led_lock) {
                app->display_led_lock = true;
                notification_apply_internal_display_layer(
                    app,
                    notification_scale_display_brightness(
                        app,
                        notification_message->data.led.value,
                        display_brightness_setting));
                lcd_backlight_is_on = true;
            }
            break;
        case NotificationMessageTypeLedDisplayBacklightEnforceAuto:
            if(app->display_led_lock) {
                app->display_led_lock = false;
                notification_apply_internal_display_layer(
                    app,
                    notification_scale_display_brightness(
                        app,
                        notification_message->data.led.value,
                        display_brightness_setting));
            } else {
                FURI_LOG_E(TAG, "Incorrect BacklightEnforceAuto usage");
            }
            break;
        case NotificationMessageTypeLedRed:
        case NotificationMessageTypeLedGreen:
        case NotificationMessageTypeLedBlue:
        case NotificationMessageTypeLedBlinkStart:
        case NotificationMessageTypeLedBlinkStop:
        case NotificationMessageTypeLedBlinkColor:
            /* WS2812 ring is driven exclusively by the user's ambient color/effect
             * settings. Per-app notification flashes are deliberately ignored here
             * — at full PWM on 8 LEDs they were strobing painfully under high-rate
             * callers (e.g. SubGHz frequency hopper). The visual signal in those
             * apps already comes from on-screen UI; no LED override needed. */
            /* Status LED mode: an app emitting an LED notification == an action in
             * progress (scan / spam / copy / read) → show orange for a short hold. */
            if(app->status_led_on) {
                app->status_action_until = furi_get_tick() + furi_ms_to_ticks(1500);
            }
            break;
        case NotificationMessageTypeSoundOn:
            if(!furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode) || force_volume) {
                float vol = notification_message->data.sound.volume * speaker_volume_setting;
                if(furi_hal_speaker_is_mine() || furi_hal_speaker_acquire(30)) {
                    furi_hal_speaker_start(
                        notification_message->data.sound.frequency, vol);
                }
            }
            break;
        case NotificationMessageTypeSoundOff:
            if(furi_hal_speaker_is_mine()) {
                furi_hal_speaker_stop();
                furi_hal_speaker_release();
            }
            break;
        case NotificationMessageTypeDelay:
            furi_delay_ms(notification_message->data.delay.length);
            break;
        case NotificationMessageTypeDoNotReset:
            reset_notifications = false;
            break;
        case NotificationMessageTypeForceSpeakerVolumeSetting:
            speaker_volume_setting = notification_message->data.forced_settings.speaker_volume;
            force_volume = true;
            break;
        case NotificationMessageTypeForceDisplayBrightnessSetting:
            display_brightness_setting =
                notification_message->data.forced_settings.display_brightness;
            break;
        default:
            break;
        }

        notification_message_index++;
        notification_message = (*message->sequence)[notification_message_index];
    }

    if(reset_notifications) {
        notification_reset_notification_layer(app, reset_display, display_brightness_setting);
    }
}

static void notification_process_internal_message(
    NotificationApp* app,
    NotificationAppMessage* message) {
    uint32_t notification_message_index = 0;
    const NotificationMessage* notification_message = (*message->sequence)[notification_message_index];

    while(notification_message != NULL) {
        switch(notification_message->type) {
        case NotificationMessageTypeLedDisplayBacklight:
            notification_apply_internal_display_layer(
                app,
                notification_settings_get_display_brightness(
                    app, notification_message->data.led.value));
            break;
        default:
            break;
        }

        notification_message_index++;
        notification_message = (*message->sequence)[notification_message_index];
    }
}

/* Idle time before the device drops into ESP32 light sleep (screen off, CPU
 * halted, wakes on any key). Reset on every input event below. */
#define NOTIFICATION_SLEEP_IDLE_MS (2 * 60 * 1000)

static void notification_sleep_timer_callback(void* context) {
    NotificationApp* app = context;
    /* 2 min with no input → light sleep, but ONLY when idling at the desktop /
     * dolphin / main menu (loader not locked). If an app or tool is running,
     * halting the CPU would disrupt it (BLE / USB / recording / timing), so we
     * skip sleep and leave it running — the backlight has already dimmed to the
     * readable ~89% floor on idle, and any keypress restores full brightness.
     * No-op too if USB is connected (handled inside furi_hal_power_light_sleep). */
    Loader* loader = furi_record_open(RECORD_LOADER);
    bool app_running = loader_is_locked(loader);
    furi_record_close(RECORD_LOADER);

    if(!app_running) {
        /* Blocks here until a key/button (or force-wake fallback) wakes it. */
        furi_hal_power_light_sleep();
    }
    if(app->sleep_timer) {
        furi_timer_restart(app->sleep_timer, furi_ms_to_ticks(NOTIFICATION_SLEEP_IDLE_MS));
    }
}

static void input_event_callback(const void* value, void* context) {
    furi_assert(value);
    furi_assert(context);

    NotificationApp* app = context;
    /* Any input resets the idle-sleep countdown. */
    if(app->sleep_timer) {
        furi_timer_restart(app->sleep_timer, furi_ms_to_ticks(NOTIFICATION_SLEEP_IDLE_MS));
    }
    notification_message(app, &sequence_display_backlight_on);
    /* Keep the WS2812 ring alive on user input. If the idle-off timer already
     * darkened it, re-light fully (resets phase + restarts the effect). If it's
     * still lit, only re-arm the idle-off timer — restarting here would reset a
     * running animation back to frame 0 on every encoder tick. */
    if(app->led_idle_off) {
        notification_apply_led_color(app);
    } else if(app->led_off_timer && app->settings.led_off_delay_ms > 0) {
        furi_timer_start(app->led_off_timer, furi_ms_to_ticks(app->settings.led_off_delay_ms));
    }
}

static void notification_message_send(
    NotificationApp* app,
    NotificationAppMessageType type,
    const NotificationSequence* sequence,
    FuriEventFlag* back_event) {
    furi_assert(app);
    furi_assert(sequence);

    NotificationAppMessage message = {
        .sequence = sequence,
        .type = type,
        .back_event = back_event,
    };

    furi_check(furi_message_queue_put(app->queue, &message, FuriWaitForever) == FuriStatusOk);
}

static bool notification_load_settings(NotificationApp* app) {
    NotificationSettings tmp;
    bool ok = saved_struct_load(
        NOTIFICATION_SETTINGS_PATH,
        &tmp,
        sizeof(NotificationSettings),
        NOTIFICATION_SETTINGS_MAGIC,
        NOTIFICATION_SETTINGS_VERSION);
    if(ok) {
        app->settings = tmp;
        FURI_LOG_I(
            TAG,
            "LOAD ok: brightness=%.2f vol=%.2f delay_ms=%u",
            (double)app->settings.display_brightness,
            (double)app->settings.speaker_volume,
            (unsigned)app->settings.display_off_delay_ms);
    } else {
        FURI_LOG_W(TAG, "LOAD failed — using defaults");
    }
    /* Sanity: never let a 0ms delay sneak through and instantly blank the screen. */
    if(app->settings.display_off_delay_ms < 2000) app->settings.display_off_delay_ms = 2000;
    return ok;
}

static bool notification_save_settings(NotificationApp* app) {
    bool ok = saved_struct_save(
        NOTIFICATION_SETTINGS_PATH,
        &app->settings,
        sizeof(NotificationSettings),
        NOTIFICATION_SETTINGS_MAGIC,
        NOTIFICATION_SETTINGS_VERSION);
    if(ok) {
        FURI_LOG_I(
            TAG,
            "SAVE ok: brightness=%.2f vol=%.2f",
            (double)app->settings.display_brightness,
            (double)app->settings.speaker_volume);
    } else {
        FURI_LOG_E(TAG, "SAVE failed");
    }
    return ok;
}

/* ──────────────────────────── LED Effect Engine ──────────────────────────── */

#define LED_EFFECT_TICK_MS 33  /* ~30 FPS */
static uint32_t led_effect_phase = 0;

static inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

/* Unpack settings.led_color into channels scaled by settings.led_brightness.
 * Applies cubic gamma (k -> k^3) because WS2812 LEDs on the T-Embed Plus are
 * unusually bright at low PWM duty — quadratic gamma was perceptually still
 * too bright at low slider values. Cubic mapping: 20% slider drives ~0.8%
 * (channel ~2/255), 30% drives ~2.7% (channel ~6/255), 50% drives 12.5%,
 * 100% stays 100%. Anything below ~15% slider clamps to 0 after uint8 cast. */
static void notification_unpack_color(NotificationApp* app, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint32_t c = app->settings.led_color;
    float k = clamp01(app->settings.led_brightness);
    k = k * k * k;  /* cubic gamma correction */
    *r = (uint8_t)(((c >> 16) & 0xFF) * k);
    *g = (uint8_t)(((c >> 8) & 0xFF) * k);
    *b = (uint8_t)((c & 0xFF) * k);
}

/* HSV→RGB at S=255, used for rainbow. h is 0-255 (one full hue cycle).
 * v is the value (brightness) 0-255. */
static void hsv_to_rgb(uint8_t h, uint8_t v, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint8_t region = h / 43;       /* 0..5 */
    uint8_t remainder = (h - region * 43) * 6;
    uint8_t p = 0;
    uint8_t q = (uint8_t)((v * (uint16_t)(255 - remainder)) / 255);
    uint8_t t = (uint8_t)((v * (uint16_t)remainder) / 255);
    switch(region) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

static void render_breathing(NotificationApp* app) {
    /* 60-tick cycle (~2s at 30fps). Triangle wave 0..255..0. */
    uint32_t p = led_effect_phase % 60;
    uint16_t scale = (p < 30) ? (p * 255 / 30) : ((59 - p) * 255 / 30);
    uint8_t r, g, b;
    notification_unpack_color(app, &r, &g, &b);
    r = (uint8_t)((r * scale) / 255);
    g = (uint8_t)((g * scale) / 255);
    b = (uint8_t)((b * scale) / 255);
    furi_hal_light_set_rgb_all(r, g, b);
}

static void render_chase(NotificationApp* app) {
    uint16_t n = furi_hal_light_pixel_count();
    if(n == 0) return;
    /* Advance one pixel every 4 ticks (~133ms). */
    uint16_t head = (led_effect_phase / 4) % n;
    uint8_t r, g, b;
    notification_unpack_color(app, &r, &g, &b);
    for(uint16_t i = 0; i < n; i++) {
        if(i == head) {
            furi_hal_light_set_pixel(i, r, g, b);
        } else {
            /* Dim trail: 1/8th brightness on the rest. */
            furi_hal_light_set_pixel(i, r >> 3, g >> 3, b >> 3);
        }
    }
    furi_hal_light_refresh();
}

static void render_rainbow(NotificationApp* app) {
    uint16_t n = furi_hal_light_pixel_count();
    if(n == 0) return;
    /* Hue advances 1 unit per 2 ticks (~66ms) → ~17s full rainbow cycle. */
    float k = clamp01(app->settings.led_brightness);
    k = k * k * k; /* match cubic gamma in notification_unpack_color */
    uint8_t v = (uint8_t)(255 * k);
    uint8_t base = (uint8_t)((led_effect_phase / 2) & 0xFF);
    /* Each LED offset around the ring so the rainbow appears to rotate. */
    for(uint16_t i = 0; i < n; i++) {
        uint8_t hue = (uint8_t)(base + (i * 256) / n);
        uint8_t r, g, b;
        hsv_to_rgb(hue, v, &r, &g, &b);
        furi_hal_light_set_pixel(i, r, g, b);
    }
    furi_hal_light_refresh();
}

/* Strobe: sharp on/off flashes. 2 ticks on, 4 ticks off. */
static void render_strobe(NotificationApp* app) {
    bool on = (led_effect_phase % 6) < 2;
    if(on) {
        uint8_t r, g, b;
        notification_unpack_color(app, &r, &g, &b);
        furi_hal_light_set_rgb_all(r, g, b);
    } else {
        furi_hal_light_set_rgb_all(0, 0, 0);
    }
}

/* Cylon (Larson Scanner): single bright pixel sweeps back and forth, with a
 * dim trail on the immediate neighbors for a smear effect. */
static void render_cylon(NotificationApp* app) {
    uint16_t n = furi_hal_light_pixel_count();
    if(n < 2) return;
    /* Triangle wave: head walks 0..n-1..0..n-1 with one-tick-per-step granularity
     * adjusted by /3 so it's not too fast at default speed. */
    uint16_t period = 2 * (n - 1);
    uint16_t step = (uint16_t)((led_effect_phase / 3) % period);
    uint16_t head = (step < n) ? step : (uint16_t)(period - step);
    uint8_t r, g, b;
    notification_unpack_color(app, &r, &g, &b);
    for(uint16_t i = 0; i < n; i++) {
        int diff = (int)i - (int)head;
        if(diff < 0) diff = -diff;
        if(diff == 0) {
            furi_hal_light_set_pixel(i, r, g, b);
        } else if(diff == 1) {
            furi_hal_light_set_pixel(i, r >> 2, g >> 2, b >> 2); /* 25% trail */
        } else {
            furi_hal_light_set_pixel(i, 0, 0, 0);
        }
    }
    furi_hal_light_refresh();
}

/* Sparkle: each tick, every pixel has a small chance of flashing on for one
 * frame; otherwise dark. Creates a randomized twinkle on a dark base. */
static void render_sparkle(NotificationApp* app) {
    uint16_t n = furi_hal_light_pixel_count();
    if(n == 0) return;
    uint8_t r, g, b;
    notification_unpack_color(app, &r, &g, &b);
    uint32_t rnd = esp_random();
    for(uint16_t i = 0; i < n; i++) {
        /* Pull 4 bits of randomness per pixel (16 values), threshold 2 → ~12.5% on. */
        uint8_t roll = (uint8_t)((rnd >> (i * 4)) & 0xF);
        if(roll < 2) {
            furi_hal_light_set_pixel(i, r, g, b);
        } else {
            furi_hal_light_set_pixel(i, 0, 0, 0);
        }
    }
    furi_hal_light_refresh();
}

/* Wipe: paint pixels around the ring one at a time, then erase them one at a
 * time. Two-phase loop. */
static void render_wipe(NotificationApp* app) {
    uint16_t n = furi_hal_light_pixel_count();
    if(n == 0) return;
    uint16_t period = 2 * n;
    uint16_t step = (uint16_t)((led_effect_phase / 4) % period); /* one slot per 4 ticks */
    uint8_t r, g, b;
    notification_unpack_color(app, &r, &g, &b);
    for(uint16_t i = 0; i < n; i++) {
        bool lit;
        if(step < n) {
            /* Fill phase: 0..step are lit */
            lit = (i <= step);
        } else {
            /* Clear phase: 0..(step-n) are dark, rest still lit */
            lit = (i > (uint16_t)(step - n));
        }
        if(lit) {
            furi_hal_light_set_pixel(i, r, g, b);
        } else {
            furi_hal_light_set_pixel(i, 0, 0, 0);
        }
    }
    furi_hal_light_refresh();
}

static void notification_led_effect_tick(void* context) {
    furi_assert(context);
    NotificationApp* app = context;
    led_effect_phase++;
    switch(app->settings.led_effect) {
        case LedEffectBreathing: render_breathing(app); break;
        case LedEffectChase:     render_chase(app);     break;
        case LedEffectRainbow:   render_rainbow(app);   break;
        case LedEffectStrobe:    render_strobe(app);    break;
        case LedEffectCylon:     render_cylon(app);     break;
        case LedEffectSparkle:   render_sparkle(app);   break;
        case LedEffectWipe:      render_wipe(app);      break;
        default: break; /* Solid/Off don't tick */
    }
}

/* Push the current settings to the WS2812 ring. Static effects (Solid/Off)
 * push once and stop the effect timer. Animated effects start the timer.
 * Also (re)starts the idle-off timer. */
void notification_apply_led_color(NotificationApp* app) {
    if(!app) return;
    /* Status LED mode owns the WS2812 — ignore all ambient color/effect writes. */
    if(app->status_led_on) return;
    /* About to re-light the ring — clear the idle-off marker. */
    app->led_idle_off = false;
    /* Always stop the effect timer; we'll restart for animated effects. */
    if(app->led_effect_timer && furi_timer_is_running(app->led_effect_timer)) {
        furi_timer_stop(app->led_effect_timer);
    }

    LedEffect eff = (LedEffect)app->settings.led_effect;
    uint8_t r, g, b;
    switch(eff) {
        case LedEffectOff:
            furi_hal_light_set_rgb_all(0, 0, 0);
            break;
        case LedEffectBreathing:
        case LedEffectChase:
        case LedEffectRainbow:
        case LedEffectStrobe:
        case LedEffectCylon:
        case LedEffectSparkle:
        case LedEffectWipe:
            led_effect_phase = 0;
            notification_led_effect_tick(app);          /* render frame 0 immediately */
            if(app->led_effect_timer) {
                uint32_t tick = app->settings.led_speed_tick_ms;
                if(tick == 0) tick = LED_EFFECT_TICK_MS; /* sane fallback */
                furi_timer_start(app->led_effect_timer, furi_ms_to_ticks(tick));
            }
            break;
        case LedEffectSolid:
        default:
            notification_unpack_color(app, &r, &g, &b);
            furi_hal_light_set_rgb_all(r, g, b);
            break;
    }

    /* Re-arm idle-off timer */
    if(app->led_off_timer) {
        if(app->settings.led_off_delay_ms > 0) {
            furi_timer_start(app->led_off_timer, furi_ms_to_ticks(app->settings.led_off_delay_ms));
        } else if(furi_timer_is_running(app->led_off_timer)) {
            furi_timer_stop(app->led_off_timer);
        }
    }
}

/* Idle-off timer callback: dark-out the WS2812 ring AND stop any running
 * effect. The next user input event re-applies color/effect via
 * notification_apply_led_color. */
static void notification_led_off_timer_cb(void* context) {
    furi_assert(context);
    NotificationApp* app = context;
    if(app->led_effect_timer && furi_timer_is_running(app->led_effect_timer)) {
        furi_timer_stop(app->led_effect_timer);
    }
    furi_hal_light_set_rgb_all(0, 0, 0);
    app->led_idle_off = true;
}

/* ─────────────────────────── Status LED ────────────────────────────────────
 * Drive the WS2812 as a status indicator (lock-menu toggle). Priority:
 *   action(orange) > Bluetooth connecting(blue) > battery(low=red / ok=green).
 * Runs on a ~1s periodic timer while enabled; goes dark during light sleep
 * (furi_hal_power_light_sleep clears it and the CPU is halted so it stays off). */
static void notification_status_led_tick(void* context) {
    NotificationApp* app = context;
    if(!app->status_led_on) return;
    if(!app->status_bt_rec) app->status_bt_rec = furi_record_open(RECORD_BT);

    uint8_t r = 0, g = 0, b = 0;
    if(furi_get_tick() < app->status_action_until) {
        r = 255;
        g = 45; /* orange = action in progress (scan / spam / copy / read) */
    } else if(
        bt_get_status((Bt*)app->status_bt_rec) == BtStatusAdvertising ||
        bt_get_status((Bt*)app->status_bt_rec) == BtStatusConnected) {
        b = 255; /* blue = Bluetooth connecting / connected */
    } else if(furi_hal_power_get_pct() <= 20 && !furi_hal_power_is_charging()) {
        r = 255; /* red = low battery */
    } else {
        g = 255; /* green = battery ok / full */
    }
    furi_hal_light_set_rgb_all(r, g, b);
}

void notification_status_led_set(NotificationApp* app, bool on) {
    if(!app) return;
    app->status_led_on = on;
    uint8_t v = on ? 1 : 0;
    saved_struct_save(STATUS_LED_PATH, &v, sizeof(v), STATUS_LED_MAGIC, STATUS_LED_VERSION);
    if(on) {
        /* Take over the ring: stop the ambient effect/idle-off timers. */
        if(app->led_effect_timer) furi_timer_stop(app->led_effect_timer);
        if(app->led_off_timer) furi_timer_stop(app->led_off_timer);
        if(app->status_led_timer)
            furi_timer_start(app->status_led_timer, furi_ms_to_ticks(1000));
        notification_status_led_tick(app); /* apply immediately */
    } else {
        if(app->status_led_timer) furi_timer_stop(app->status_led_timer);
        notification_apply_led_color(app); /* restore the ambient color/effect */
    }
}

bool notification_status_led_get(NotificationApp* app) {
    return app && app->status_led_on;
}

/* ─────────────────────────── UI color (LCD foreground) ─────────────────────
 * The ST7789 render path in furi_hal_display reads a single uint16_t fg_color
 * for every drawn pixel. Updating it at runtime recolors the entire UI on the
 * next frame — no full redraw needed. The byte order is RGB565 byte-swapped
 * for the S3 SPI peripheral (see board_lilygo_t_embed_cc1101.h:58). */

#define UI_COLOR_TICK_MS 50  /* Spectrum hue increments every 50ms → ~13s rotation */

/* Pack RGB888 (8-bit per channel) into RGB565 with the byte swap the S3 SPI
 * peripheral expects. */
#define UI_RGB(r, g, b)                                                                 \
    ((uint16_t)((((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) & 0xFF) << 8) | \
     (uint16_t)((((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) >> 8)))

/* UI accent color presets. Order matches the labels in notification_settings_app.c.
 * Used by both ui_color_index (Background = fg_color) and ui_fg_color_index
 * (Foreground = bg_color). The last index (UI_COLOR_SPECTRUM_INDEX) is a
 * sentinel — the actual color is driven by the spectrum timer, not the table.
 *
 * Defaults: Background = Orange (idx 1), Foreground = Black (idx 0). Together
 * those preserve the stock Flipper black-on-orange look. */
#define UI_COLOR_SPECTRUM_INDEX 10
const uint16_t ui_color_value[UI_COLOR_SPECTRUM_INDEX] = {
    UI_RGB(0x00, 0x00, 0x00), /* Black */
    UI_RGB(0xFF, 0xA5, 0x00), /* Orange — default for Background */
    UI_RGB(0xFF, 0x00, 0x00), /* Red */
    UI_RGB(0x00, 0xFF, 0x00), /* Green */
    UI_RGB(0x00, 0x00, 0xFF), /* Blue */
    UI_RGB(0x00, 0xFF, 0xFF), /* Cyan */
    UI_RGB(0xFF, 0x1D, 0xCE), /* Magenta — matches LED preset */
    UI_RGB(0xFF, 0xFF, 0x00), /* Yellow */
    UI_RGB(0xFF, 0xFF, 0xFF), /* White */
    UI_RGB(0x8F, 0x00, 0xFF), /* Purple — matches LED preset */
};

/* Inline RGB888→RGB565+byte-swap for the spectrum tick (avoids macro re-eval). */
static inline uint16_t ui_color_pack_swap(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t v = ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
    return ((v & 0xFF) << 8) | (v >> 8);
}

static uint8_t ui_spectrum_hue = 0;

static void notification_ui_spectrum_tick(void* context) {
    furi_assert(context);
    NotificationApp* app = context;
    uint8_t r, g, b;
    /* Background (fg_color): hue at the current phase. */
    if(app->settings.ui_color_index == UI_COLOR_SPECTRUM_INDEX) {
        hsv_to_rgb(ui_spectrum_hue, 255, &r, &g, &b);
        furi_hal_display_set_fg_color(ui_color_pack_swap(r, g, b));
    }
    /* Foreground (bg_color): same hue offset by 180° for visual contrast.
     * If both are Spectrum the UI gets an opposing-hue cycle that stays
     * legible at every phase. */
    if(app->settings.ui_fg_color_index == UI_COLOR_SPECTRUM_INDEX) {
        hsv_to_rgb((uint8_t)(ui_spectrum_hue + 128), 255, &r, &g, &b);
        furi_hal_display_set_bg_color(ui_color_pack_swap(r, g, b));
    }
    ui_spectrum_hue++;
}

void notification_apply_ui_color(NotificationApp* app) {
    if(!app) return;
    uint8_t bg_idx = app->settings.ui_color_index;
    uint8_t fg_idx = app->settings.ui_fg_color_index;

    /* Push static colors immediately; defer animated ones to the timer.
     * The custom index pulls its color from settings.ui_custom_color (0x00RRGGBB)
     * instead of the fixed palette. */
    if(bg_idx == UI_COLOR_CUSTOM_INDEX) {
        uint32_t c = app->settings.ui_custom_color;
        furi_hal_display_set_fg_color(
            ui_color_pack_swap((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
    } else if(bg_idx < UI_COLOR_SPECTRUM_INDEX) {
        furi_hal_display_set_fg_color(ui_color_value[bg_idx]);
    }
    if(fg_idx < UI_COLOR_SPECTRUM_INDEX) {
        furi_hal_display_set_bg_color(ui_color_value[fg_idx]);
    }

    /* Run the timer if either color is on Spectrum. */
    bool need_timer =
        (bg_idx == UI_COLOR_SPECTRUM_INDEX) || (fg_idx == UI_COLOR_SPECTRUM_INDEX);

    if(need_timer) {
        if(app->ui_spectrum_timer && !furi_timer_is_running(app->ui_spectrum_timer)) {
            furi_timer_start(app->ui_spectrum_timer, furi_ms_to_ticks(UI_COLOR_TICK_MS));
        }
    } else {
        if(app->ui_spectrum_timer && furi_timer_is_running(app->ui_spectrum_timer)) {
            furi_timer_stop(app->ui_spectrum_timer);
        }
    }
}

static NotificationApp* notification_app_alloc(void) {
    NotificationApp* app = malloc(sizeof(NotificationApp));

    app->queue = furi_message_queue_alloc(8, sizeof(NotificationAppMessage));
    app->display_timer = furi_timer_alloc(notification_display_timer, FuriTimerTypeOnce, app);
    app->night_shift_timer = furi_timer_alloc(night_shift_timer_callback, FuriTimerTypePeriodic, app);
    app->night_shift_demo_timer = furi_timer_alloc(night_shift_demo_timer_callback, FuriTimerTypeOnce, app);
    app->led_off_timer = furi_timer_alloc(notification_led_off_timer_cb, FuriTimerTypeOnce, app);
    app->led_effect_timer =
        furi_timer_alloc(notification_led_effect_tick, FuriTimerTypePeriodic, app);
    app->ui_spectrum_timer =
        furi_timer_alloc(notification_ui_spectrum_tick, FuriTimerTypePeriodic, app);
    app->sleep_timer =
        furi_timer_alloc(notification_sleep_timer_callback, FuriTimerTypeOnce, app);
    app->status_led_timer =
        furi_timer_alloc(notification_status_led_tick, FuriTimerTypePeriodic, app);
    app->status_action_until = 0;
    app->status_bt_rec = NULL;
    /* Load the persisted Status LED enable flag now (before the first ambient LED
     * apply below) so the apply-guard sees the right value. */
    {
        uint8_t sl = 0;
        saved_struct_load(STATUS_LED_PATH, &sl, sizeof(sl), STATUS_LED_MAGIC, STATUS_LED_VERSION);
        app->status_led_on = sl ? true : false;
    }

    app->settings.display_brightness = 1.0f;
    app->settings.display_off_delay_ms = 30000;
    app->settings.speaker_volume = 1.0f;
    app->settings.vibro_on = true;
    app->settings.night_shift = 1.0f;
    app->settings.night_shift_start = 1020;
    app->settings.night_shift_end = 300;
    app->settings.led_color = 0;  /* Off by default */
    app->settings.led_brightness = 0.30f; /* 30% pre-gamma → ~2.7% drive after cubic gamma; gentle ambient */
    app->settings.led_off_delay_ms = 0; /* Always on by default; user can pick a timeout */
    app->settings.led_effect = (uint8_t)LedEffectSolid; /* Default: static color */
    app->settings.led_speed_tick_ms = 33; /* Default ~30fps */
    app->settings.ui_color_index = 1; /* Default: Orange (preserves stock Flipper UI) */
    app->settings.ui_fg_color_index = 0; /* Default: Black (preserves stock contrast) */
    app->settings.ui_custom_color = UI_CUSTOM_DEFAULT_RGB; /* picker start color */
    app->settings.ui_custom_color_set = false; /* no custom color defined yet */
    app->current_night_shift = 1.0f;

    /* Try to load persisted settings (NVS-backed via saved_struct).
     * Fails silently on first boot or version mismatch — defaults stay in place. */
    notification_load_settings(app);

    /* Push the loaded LED color to the WS2812 ring. */
    notification_apply_led_color(app);
    /* Push the loaded UI color to the LCD render pipeline. */
    notification_apply_ui_color(app);

    app->display.value[LayerInternal] = 0x00;
    app->display.value[LayerNotification] = 0x00;
    app->display.index = LayerInternal;
    app->display_led_lock = false;

    app->event_record = furi_record_open(RECORD_INPUT_EVENTS);
    furi_pubsub_subscribe(app->event_record, input_event_callback, app);
    notification_message(app, &sequence_display_backlight_on);

    /* Arm the idle → light-sleep countdown (reset by input_event_callback). */
    furi_timer_start(app->sleep_timer, furi_ms_to_ticks(NOTIFICATION_SLEEP_IDLE_MS));

    /* If Status LED was left enabled, start its periodic driver (first tick ~1s
     * later opens RECORD_BT once all services are up). */
    if(app->status_led_on && app->status_led_timer) {
        furi_timer_start(app->status_led_timer, furi_ms_to_ticks(1000));
    }

    if(app->settings.night_shift != 1.0f) {
        night_shift_timer_start(app);
    } else {
        night_shift_timer_stop(app);
    }

    return app;
}

int32_t notification_srv(void* p) {
    UNUSED(p);

    NotificationApp* app = notification_app_alloc();

    notification_apply_internal_display_layer(app, 0x00);

    furi_record_create(RECORD_NOTIFICATION, app);

    NotificationAppMessage message;
    while(true) {
        furi_check(furi_message_queue_get(app->queue, &message, FuriWaitForever) == FuriStatusOk);

        switch(message.type) {
        case NotificationLayerMessage:
            notification_process_notification_message(app, &message);
            break;
        case InternalLayerMessage:
            notification_process_internal_message(app, &message);
            break;
        case SaveSettingsMessage:
            notification_save_settings(app);
            break;
        }

        if(message.back_event != NULL) {
            furi_event_flag_set(message.back_event, NOTIFICATION_EVENT_COMPLETE);
        }
    }

    return 0;
}

void notification_message(NotificationApp* app, const NotificationSequence* sequence) {
    notification_message_send(app, NotificationLayerMessage, sequence, NULL);
}

void notification_message_block(NotificationApp* app, const NotificationSequence* sequence) {
    FuriEventFlag* back_event = furi_event_flag_alloc();

    notification_message_send(app, NotificationLayerMessage, sequence, back_event);
    furi_event_flag_wait(
        back_event, NOTIFICATION_EVENT_COMPLETE, FuriFlagWaitAny, FuriWaitForever);
    furi_event_flag_free(back_event);
}

void notification_internal_message(NotificationApp* app, const NotificationSequence* sequence) {
    notification_message_send(app, InternalLayerMessage, sequence, NULL);
}

void notification_internal_message_block(
    NotificationApp* app,
    const NotificationSequence* sequence) {
    FuriEventFlag* back_event = furi_event_flag_alloc();

    notification_message_send(app, InternalLayerMessage, sequence, back_event);
    furi_event_flag_wait(
        back_event, NOTIFICATION_EVENT_COMPLETE, FuriFlagWaitAny, FuriWaitForever);
    furi_event_flag_free(back_event);
}

void notification_message_save_settings(NotificationApp* app) {
    furi_assert(app);
    notification_message_send(app, SaveSettingsMessage, &sequence_empty, NULL);
}
