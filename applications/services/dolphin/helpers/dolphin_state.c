#include "dolphin_state.h"
#include "dolphin_state_filename.h"

#include <furi.h>
#include <furi_hal.h>
#include <esp_attr.h>
#include <esp_system.h>

#define DOLPHIN_RTC_MAGIC 0x444f4c50u // "DOLP"

typedef struct {
    uint32_t magic;
    uint32_t icounter;
    int32_t butthurt;
} DolphinRtcData;

static RTC_NOINIT_ATTR DolphinRtcData s_dolphin_rtc;

#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define TAG "DolphinState"

/* Persist to the SD card (/ext), not internal flash, to avoid flash wear.
 * Writes are already rare (RTC holds live XP/mood; SD is touched only on the
 * 1-min idle flush) and dolphin_init_state gates loading on SD presence. */
#define DOLPHIN_STATE_PATH           EXT_PATH(DOLPHIN_STATE_FILE_NAME)
#define DOLPHIN_STATE_HEADER_MAGIC   0xD0
#define DOLPHIN_STATE_HEADER_VERSION 0x01
#define LEVEL2_THRESHOLD             300
#define LEVEL3_THRESHOLD             1800
#define BUTTHURT_MAX                 14
#define BUTTHURT_MIN                 0

DolphinState* dolphin_state_alloc(void) {
    return malloc(sizeof(DolphinState));
}

void dolphin_state_free(DolphinState* dolphin_state) {
    free(dolphin_state);
}

void dolphin_state_save(DolphinState* dolphin_state) {
    if(!dolphin_state->dirty) {
        return;
    }

    bool success = saved_struct_save(
        DOLPHIN_STATE_PATH,
        &dolphin_state->data,
        sizeof(DolphinStoreData),
        DOLPHIN_STATE_HEADER_MAGIC,
        DOLPHIN_STATE_HEADER_VERSION);

    if(success) {
        FURI_LOG_I(TAG, "State saved");
        dolphin_state->dirty = false;

    } else {
        FURI_LOG_E(TAG, "Failed to save state");
    }
}

void dolphin_state_load(DolphinState* dolphin_state) {
    bool success = saved_struct_load(
        DOLPHIN_STATE_PATH,
        &dolphin_state->data,
        sizeof(DolphinStoreData),
        DOLPHIN_STATE_HEADER_MAGIC,
        DOLPHIN_STATE_HEADER_VERSION);

    if(success) {
        FURI_LOG_I(TAG, "Successfully loaded state from SD card (XP: %lu, BH: %ld)", 
                   dolphin_state->data.icounter, dolphin_state->data.butthurt);
        // Sync loaded values to active RTC memory
        s_dolphin_rtc.magic = DOLPHIN_RTC_MAGIC;
        s_dolphin_rtc.icounter = dolphin_state->data.icounter;
        s_dolphin_rtc.butthurt = dolphin_state->data.butthurt;
    } else {
        // Fallback to RTC RAM if SD load failed but RTC magic is valid
        if(s_dolphin_rtc.magic == DOLPHIN_RTC_MAGIC) {
            memset(&dolphin_state->data, 0, sizeof(DolphinStoreData));
            dolphin_state->data.icounter = s_dolphin_rtc.icounter;
            dolphin_state->data.butthurt = s_dolphin_rtc.butthurt;
            success = true;
            FURI_LOG_I(TAG, "Loaded state from RTC RAM fallback (XP: %lu, BH: %ld)", 
                       dolphin_state->data.icounter, dolphin_state->data.butthurt);
        }
    }

    if(success) {
        if((dolphin_state->data.butthurt > BUTTHURT_MAX) ||
           (dolphin_state->data.butthurt < BUTTHURT_MIN)) {
            success = false;
        } else {
            // Apply offline butthurt accumulation and limit clearing
            uint64_t now = dolphin_state_timestamp();
            uint64_t last = dolphin_state->data.timestamp;
            if (now > last) {
                uint64_t diff_sec = now - last;
                // BUTTHURT_INCREASE_PERIOD_TICKS is normally 48 hours (48 * 3600 seconds)
                uint64_t periods = diff_sec / (48UL * 3600UL);
                if (periods > 0) {
                    dolphin_state->data.butthurt = CLAMP(dolphin_state->data.butthurt + periods, BUTTHURT_MAX, BUTTHURT_MIN);
                    dolphin_state->data.timestamp = now;
                    dolphin_state->dirty = true;
                }
                // Clear limits if more than 24 hours (24 * 3600 seconds) have passed
                if (diff_sec >= (24UL * 3600UL)) {
                    dolphin_state_clear_limits(dolphin_state);
                }
                if (dolphin_state->dirty) {
                    dolphin_state_save(dolphin_state);
                }
            }
            // Sync current values to active RTC memory
            s_dolphin_rtc.magic = DOLPHIN_RTC_MAGIC;
            s_dolphin_rtc.icounter = dolphin_state->data.icounter;
            s_dolphin_rtc.butthurt = dolphin_state->data.butthurt;
        }
    }

    if(!success) {
        FURI_LOG_W(TAG, "Reset Dolphin state");
        memset(dolphin_state, 0, sizeof(DolphinState));

        dolphin_state->dirty = true;
        dolphin_state_save(dolphin_state);

        // Sync reset values to active RTC memory
        s_dolphin_rtc.magic = DOLPHIN_RTC_MAGIC;
        s_dolphin_rtc.icounter = dolphin_state->data.icounter;
        s_dolphin_rtc.butthurt = dolphin_state->data.butthurt;
    }
}

uint64_t dolphin_state_timestamp(void) {
    DateTime datetime;
    furi_hal_rtc_get_datetime(&datetime);
    return datetime_datetime_to_timestamp(&datetime);
}

bool dolphin_state_is_levelup(uint32_t icounter) {
    return (icounter == LEVEL2_THRESHOLD) || (icounter == LEVEL3_THRESHOLD);
}

uint8_t dolphin_get_level(uint32_t icounter) {
    if(icounter <= LEVEL2_THRESHOLD) {
        return 1;
    } else if(icounter <= LEVEL3_THRESHOLD) {
        return 2;
    } else {
        return 3;
    }
}

uint32_t dolphin_state_xp_above_last_levelup(uint32_t icounter) {
    uint32_t threshold = 0;
    if(icounter <= LEVEL2_THRESHOLD) {
        threshold = 0;
    } else if(icounter <= LEVEL3_THRESHOLD) {
        threshold = LEVEL2_THRESHOLD + 1;
    } else {
        threshold = LEVEL3_THRESHOLD + 1;
    }
    return icounter - threshold;
}

uint32_t dolphin_state_xp_to_levelup(uint32_t icounter) {
    uint32_t threshold = 0;
    if(icounter <= LEVEL2_THRESHOLD) {
        threshold = LEVEL2_THRESHOLD;
    } else if(icounter <= LEVEL3_THRESHOLD) {
        threshold = LEVEL3_THRESHOLD;
    } else {
        threshold = (uint32_t)-1;
    }
    return threshold - icounter;
}

void dolphin_state_on_deed(DolphinState* dolphin_state, DolphinDeed deed) {
    // Special case for testing
    if(deed > DolphinDeedMAX) {
        if(deed == DolphinDeedTestLeft) {
            dolphin_state->data.butthurt =
                CLAMP(dolphin_state->data.butthurt + 1, BUTTHURT_MAX, BUTTHURT_MIN);
            if(dolphin_state->data.icounter > 0) dolphin_state->data.icounter--;
            dolphin_state->data.timestamp = dolphin_state_timestamp();
            dolphin_state->dirty = true;
        } else if(deed == DolphinDeedTestRight) {
            dolphin_state->data.butthurt = BUTTHURT_MIN;
            if(dolphin_state->data.icounter < UINT32_MAX) dolphin_state->data.icounter++;
            dolphin_state->data.timestamp = dolphin_state_timestamp();
            dolphin_state->dirty = true;
        }
        return;
    }

    DolphinApp app = dolphin_deed_get_app(deed);
    int8_t weight_limit =
        dolphin_deed_get_app_limit(app) - dolphin_state->data.icounter_daily_limit[app];
    uint8_t deed_weight = CLAMP(dolphin_deed_get_weight(deed), weight_limit, 0);

    uint32_t xp_to_levelup = dolphin_state_xp_to_levelup(dolphin_state->data.icounter);
    if(xp_to_levelup) {
        deed_weight = MIN(xp_to_levelup, deed_weight);
        dolphin_state->data.icounter += deed_weight;
        dolphin_state->data.icounter_daily_limit[app] += deed_weight;
    }

    /* decrease butthurt:
     * 0 deeds accumulating --> 0 butthurt
     * +1....+15 deeds accumulating --> -1 butthurt
     * +16...+30 deeds accumulating --> -1 butthurt
     * +31...+45 deeds accumulating --> -1 butthurt
     * +46...... deeds accumulating --> -1 butthurt
     * -4 butthurt per day is maximum
     * */
    uint8_t butthurt_icounter_level_old = dolphin_state->data.butthurt_daily_limit / 15 +
                                          !!(dolphin_state->data.butthurt_daily_limit % 15);
    dolphin_state->data.butthurt_daily_limit =
        CLAMP(dolphin_state->data.butthurt_daily_limit + deed_weight, 46, 0);
    uint8_t butthurt_icounter_level_new = dolphin_state->data.butthurt_daily_limit / 15 +
                                          !!(dolphin_state->data.butthurt_daily_limit % 15);
    int32_t new_butthurt = ((int32_t)dolphin_state->data.butthurt) -
                           (butthurt_icounter_level_old != butthurt_icounter_level_new);
    new_butthurt = CLAMP(new_butthurt, BUTTHURT_MAX, BUTTHURT_MIN);

    dolphin_state->data.butthurt = new_butthurt;
    dolphin_state->data.timestamp = dolphin_state_timestamp();
    dolphin_state->dirty = true;

    // Sync to active RTC memory
    s_dolphin_rtc.magic = DOLPHIN_RTC_MAGIC;
    s_dolphin_rtc.icounter = dolphin_state->data.icounter;
    s_dolphin_rtc.butthurt = dolphin_state->data.butthurt;

    FURI_LOG_D(
        TAG,
        "icounter %lu, butthurt %ld",
        dolphin_state->data.icounter,
        dolphin_state->data.butthurt);
}

void dolphin_state_butthurted(DolphinState* dolphin_state) {
    if(dolphin_state->data.butthurt < BUTTHURT_MAX) {
        dolphin_state->data.butthurt++;
        dolphin_state->data.timestamp = dolphin_state_timestamp();
        dolphin_state->dirty = true;

        s_dolphin_rtc.butthurt = dolphin_state->data.butthurt;
    }
}

void dolphin_state_increase_level(DolphinState* dolphin_state) {
    furi_assert(dolphin_state_is_levelup(dolphin_state->data.icounter));
    ++dolphin_state->data.icounter;
    dolphin_state->dirty = true;

    s_dolphin_rtc.icounter = dolphin_state->data.icounter;
}

void dolphin_state_clear_limits(DolphinState* dolphin_state) {
    furi_assert(dolphin_state);

    for(int i = 0; i < DolphinAppMAX; ++i) {
        dolphin_state->data.icounter_daily_limit[i] = 0;
    }
    dolphin_state->data.butthurt_daily_limit = 0;
    dolphin_state->dirty = true;
}
