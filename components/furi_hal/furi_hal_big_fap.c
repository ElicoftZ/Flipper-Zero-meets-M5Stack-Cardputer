#include "furi_hal_big_fap.h"

#include <furi.h>
#include <esp_attr.h>
#include <esp_system.h>

#define TAG "FuriHalBigFap"
#define BIG_FAP_MAGIC 0xB16FA9ADu

/* RTC memory: retained across esp_restart(), uninitialised on power-on. */
static RTC_NOINIT_ATTR uint32_t s_magic;

static bool s_checked = false;
static bool s_active = false;

static void big_fap_check_once(void) {
    if(s_checked) return;
    s_checked = true;
    /* Only a controlled software reset keeps the mode alive. Any other reset
     * cause (power-on, brownout, external reset button, panic, watchdog) means
     * the user wants out — clear the flag and boot normally. */
    if(esp_reset_reason() == ESP_RST_SW && s_magic == BIG_FAP_MAGIC) {
        s_active = true;
    } else {
        s_magic = 0;
        s_active = false;
    }
}

bool furi_hal_big_fap_is_active(void) {
    big_fap_check_once();
    return s_active;
}

void furi_hal_big_fap_enter(void) {
    s_magic = BIG_FAP_MAGIC;
    s_active = true;
    s_checked = true;
    FURI_LOG_I(TAG, "Entering Big FAP Mode -> reboot");
    furi_delay_ms(50);
    esp_restart();
}

void furi_hal_big_fap_exit(void) {
    s_magic = 0;
    s_active = false;
    s_checked = true;
    FURI_LOG_I(TAG, "Leaving Big FAP Mode -> reboot");
    furi_delay_ms(50);
    esp_restart();
}
