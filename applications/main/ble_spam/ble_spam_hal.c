#include "ble_spam_hal.h"

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <furi.h>
#include <furi_hal_random.h>
#include <btshim.h>
#include <dolphin/dolphin.h>
#include <string.h>

/* Minimum free internal heap required to bring up the BLE controller +
 * Bluedroid host for spam. Below this, esp_bluedroid_enable()'s internal
 * BTU_StartUp runs out of RAM and asserts instead of returning an error.
 *
 * Measured (cardputer_adv, serial capture 2026-07-02): a running controller
 * consumes only ~36 KB (free goes 74 KB -> 37.7 KB at "HAL ready"), and the
 * FIRST Bluedroid init on a boot costs a one-time ~13 KB of persistent globals
 * that deinit does not reclaim (so the first teardown lands at ~60 KB, not 74).
 * The old 72 KB threshold was ~2x the real need, which meant that after the
 * first spam session the ~60 KB left could never clear the gate again until a
 * reboot. 54 KB sits above the ~36 KB steady cost with headroom for the init
 * transient, yet below the ~60 KB post-session floor so subsequent sessions
 * still start. */
#define BLE_SPAM_MIN_FREE_INTERNAL (54 * 1024)

#define TAG "BleSpamHal"

static volatile bool s_adv_configured = false;
static volatile bool s_advertising = false;
static volatile bool s_rand_addr_pending = false;
/* The BLE controller + Bluedroid deinit leaks ~13-20 KB per init/deinit cycle
 * on this no-PSRAM IDF build, so cycling it per spam attack quickly drops below
 * the 72 KB start threshold and the next start fails. Keep the stack up across
 * attacks: start() is idempotent, the running scene only stops *advertising*
 * on exit, and full teardown happens once when leaving the spam section. */
static bool s_hal_started = false;

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min = 0x20,               // 20ms
    .adv_int_max = 0x40,               // 40ms
    .adv_type = ADV_TYPE_IND,          // Connectable (allows pairing)
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void spam_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch(event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        s_adv_configured = true;
        if(param->adv_data_raw_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            esp_ble_gap_start_advertising(&s_adv_params);
        } else {
            ESP_LOGW(TAG, "adv data set failed: %d", param->adv_data_raw_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        s_advertising = (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS);
        if(!s_advertising) {
            ESP_LOGW(TAG, "adv start failed: %d", param->adv_start_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        s_advertising = false;
        break;
    case ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT:
        s_rand_addr_pending = false;
        break;
    default:
        break;
    }
}

bool ble_spam_hal_have_ram(void) {
    /* WiFi released the BLE controller's RAM this boot — it cannot be re-inited
     * until reboot. Report "no RAM" so the app gates off gracefully. */
    if(bt_is_mem_released()) return false;
    /* If the btshim serial stack is currently up (Bluetooth enabled in
     * Settings), it is holding ~64 KB right now — but ble_spam_hal_start()
     * stops it first and reclaims that RAM before it needs it. So don't refuse
     * at the launch gate just because free looks low; there will be plenty once
     * the serial stack is swapped out. The phone's serial link drops while
     * spamming and is restored on exit (ble_spam_hal_stop -> bt_start_stack).
     * The strict free check only applies when BT is already off (stack down,
     * nothing to reclaim) — this is what start() itself sees after bt_stop_stack. */
    if(esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        return true;
    }
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= BLE_SPAM_MIN_FREE_INTERNAL;
}

bool ble_spam_hal_start(void) {
    // Already up from a previous attack in this session: reuse it. This is what
    // keeps repeated attacks from cycling (and leaking) the controller.
    if(s_hal_started) {
        ESP_LOGI(TAG, "BLE spam HAL already started, reusing");
        return true;
    }

    // WiFi released the BLE controller's RAM this boot — esp_bt_controller_init()
    // would fault. Refuse cleanly; the radio needs a reboot to come back.
    if(bt_is_mem_released()) {
        ESP_LOGE(TAG, "BLE controller RAM was released for WiFi; reboot to use BLE");
        return false;
    }

    ESP_LOGI(TAG, "Starting BLE spam HAL...");

    // Stop btshim BLE stack
    Bt* bt = furi_record_open(RECORD_BT);
    bt_stop_stack(bt);
    furi_record_close(RECORD_BT);
    furi_delay_ms(100);

    // Memory gate: bringing up Bluedroid needs ~64 KB internal RAM. If it fails
    // mid-init it asserts inside ESP-IDF instead of returning an error, taking
    // the whole device down. Check now (after freeing the serial stack) and bail
    // cleanly if there isn't enough -- the caller returns to the menu.
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if(free_internal < BLE_SPAM_MIN_FREE_INTERNAL) {
        ESP_LOGE(
            TAG,
            "Not enough RAM for BLE spam: %u free, need >= %u",
            (unsigned)free_internal,
            (unsigned)BLE_SPAM_MIN_FREE_INTERNAL);
        return false;
    }

    // Init BLE controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_bt_controller_init(&bt_cfg);
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "controller init: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "controller enable: %s", esp_err_to_name(err));
        return false;
    }

    // Init Bluedroid
    esp_bluedroid_config_t bd_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    err = esp_bluedroid_init_with_cfg(&bd_cfg);
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "bluedroid init: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_bluedroid_enable();
    if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "bluedroid enable: %s", esp_err_to_name(err));
        return false;
    }

    // Register our GAP callback
    err = esp_ble_gap_register_callback(spam_gap_event_handler);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "gap register: %s", esp_err_to_name(err));
        return false;
    }

    // Set TX power to maximum for all BLE power types
#ifdef ESP_PWR_LVL_P21
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P21);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P21);
#else
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P20);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P20);
#endif

    s_adv_configured = false;
    s_advertising = false;
    s_rand_addr_pending = false;

    // Set an initial random static address so that the first start_advertising()
    // call (triggered asynchronously from ADV_DATA_RAW_SET_COMPLETE_EVT) can use
    // BLE_ADDR_TYPE_RANDOM without falling back to an invalid zero address.
    ble_spam_hal_set_random_addr();

    s_hal_started = true;
    ESP_LOGI(TAG, "BLE spam HAL ready");
    dolphin_deed(DolphinDeedBleSpam);
    return true;
}

void ble_spam_hal_stop(void) {
    if(!s_hal_started) {
        return; // nothing to tear down (idempotent — safe from scene_main on_enter)
    }
    s_hal_started = false;
    ESP_LOGI(TAG, "Stopping BLE spam HAL...");

    // Stop advertising
    if(s_advertising) {
        esp_ble_gap_stop_advertising();
        furi_delay_ms(50);
    }

    // Teardown Bluedroid
    if(esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        esp_bluedroid_disable();
        furi_delay_ms(50);
        esp_bluedroid_deinit();
        furi_delay_ms(50);
    }

    // Teardown controller
    if(esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_disable();
        furi_delay_ms(50);
    }
    if(esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_deinit();
        furi_delay_ms(50);
    }

    // Restart btshim
    Bt* bt = furi_record_open(RECORD_BT);
    bt_start_stack(bt);
    furi_record_close(RECORD_BT);

    ESP_LOGI(TAG, "BLE spam HAL stopped, btshim restored");
}

bool ble_spam_hal_set_adv_data(const uint8_t* data, uint8_t len) {
    if(len > 31) return false;

    s_adv_configured = false;
    esp_err_t err = esp_ble_gap_config_adv_data_raw((uint8_t*)data, len);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "config_adv_data_raw failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void ble_spam_hal_stop_adv(void) {
    if(s_advertising) {
        esp_ble_gap_stop_advertising();
        for(int i = 0; i < 20 && s_advertising; i++) {
            furi_delay_ms(2);
        }
    }
}

void ble_spam_hal_set_random_addr(void) {
    esp_bd_addr_t addr;
    furi_hal_random_fill_buf(addr, 6);
    // Random static address: top two bits of MSB must be 11 (0xC0..0xFF).
    addr[0] = (addr[0] & 0x3F) | 0xC0;
    s_rand_addr_pending = true;
    esp_err_t err = esp_ble_gap_set_rand_addr(addr);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "set_rand_addr: %s", esp_err_to_name(err));
        s_rand_addr_pending = false;
        return;
    }
    // Wait up to 40ms for SET_STATIC_RAND_ADDR_EVT so the subsequent
    // start_advertising() call has a valid own-address to use.
    for(int i = 0; i < 20 && s_rand_addr_pending; i++) {
        furi_delay_ms(2);
    }
}

void ble_spam_hal_set_addr(const uint8_t addr[6]) {
    esp_bd_addr_t a;
    memcpy(a, addr, 6);
    // Ensure valid random static format
    a[0] = (a[0] & 0x3F) | 0xC0;
    esp_err_t err = esp_ble_gap_set_rand_addr(a);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "set_addr: %s", esp_err_to_name(err));
    }
    furi_delay_ms(2);
}
