import os

MANIFEST_ROOTS = [
    "components",
    "applications",
    "applications_user",
    "port_apps",
]

APP_SOURCE_OVERRIDES = {
    "desktop": "applications",
    "storage": "applications",
}

APPS = [
    "input",
    "notification",
    "gui",
    "dialogs",
    "locale",
    "cli",
    "cli_vcp",
    "storage",
    "storage_start",
    "power",
    "power_start",
    "power_settings",
    "loader",
    "loader_start",
    "notification_settings",
    "desktop",
    "archive",
    "about",
    "module_pins_settings",
    "animation_customize",
    "bt_settings",
    "clock",
    "bad_usb",
    "subghz",
    "cli_subghz",
    "subghz_load_dangerous_settings",
    "passport",
    "nfc",
    "infrared",
    "lfrfid",
    "wlan",
    "nrf24",
    "ble_spam",
    "js_app",
    "js_event_loop",
    "js_gui",
    "js_gui__loading",
    "js_gui__empty_screen",
    "js_gui__submenu",
    "js_gui__text_input",
    "js_gui__number_input",
    "js_gui__button_panel",
    "js_gui__popup",
    "js_gui__button_menu",
    "js_gui__menu",
    "js_gui__vi_list",
    "js_gui__byte_input",
    "js_gui__text_box",
    "js_gui__dialog",
    "js_gui__file_picker",
    "js_gui__widget",
    "js_gui__icon",
    "js_notification",
    "js_math",
    "js_storage",
    "js_subghz",
    "js_infrared",
    "js_blebeacon",
    # js_serial, js_gpio, js_i2c, js_spi excluded - need HAL porting
    "key_copier",
    "asteroids",
    "blackjack",
    "blackjack_split",
    "dice_app",
    "doom_fap",
    "qrcode",
    "xyz_vh8t_slots",
    "flipper_xremote",
    "rgb_led",
    "game15",
    "fz_nrf24_jammer",
    "flipper_pong",
    "proto_pirate",
    "recorder",
    "snake20",
    "tagtinker",
    "tamagotchi_p1",
    "tetris",
    "texas_holdem",
    "weather_station",
    "zerofido",
    "findmy",
    "totp",
]

# Boards without NFC / IR hardware – exclude the corresponding apps
_board = os.environ.get("FLIPPER_BOARD", "")
_boards_without_nfc = set()
_boards_without_ir = set()
# Neither Cardputer has an RDM6300/RFID front-end (BOARD_HAS_RFID=0).
_boards_without_lfrfid = {"m5stack_cardputer", "m5stack_cardputer_adv"}

# Wolf3D shares Doom's requirements (PSRAM, ST7789 320xN, I2S speaker).
# Both Cardputer boards are excluded (no PSRAM / PSRAM disabled).
_boards_without_wolf3d = {"m5stack_cardputer", "m5stack_cardputer_adv"}

if _board in _boards_without_nfc:
    APPS = [a for a in APPS if a != "nfc"]

if _board in _boards_without_lfrfid:
    APPS = [a for a in APPS if a != "lfrfid"]

# m5stack_cardputer (standard) has no CC1101 (BOARD_HAS_SUBGHZ=0) → excluded.
# m5stack_cardputer_adv ships SubGHz (BOARD_HAS_SUBGHZ=1) → NOT excluded.
_boards_without_subghz = {"m5stack_cardputer"}

# NRF24 plugs into the slot. Standard Cardputer has none
# (BOARD_HAS_NRF24=0); Cardputer-ADV does (BOARD_HAS_NRF24=1) → NOT excluded.
_boards_without_nrf24 = {"m5stack_cardputer"}

if _board in _boards_without_ir:
    APPS = [a for a in APPS if a not in ("infrared", "js_infrared")]

if _board in _boards_without_subghz:
    APPS = [a for a in APPS if a not in ("subghz", "cli_subghz", "subghz_load_dangerous_settings", "js_subghz")]

if _board in _boards_without_nrf24:
    APPS = [a for a in APPS if a != "nrf24"]

# NOTE: cli_vcp and dolphin look like easy RAM wins (~4KB stack each) but both
# are hard-wired into the desktop service: desktop.c calls cli_vcp_enable/disable
# directly (USB-Storage/qFlipper toggles) and lists "dolphin" in its `requires`
# while opening RECORD_DOLPHIN. Dropping either breaks the link / hangs the UI,
# so they stay. The real RAM wins are BLE-off (~68KB) + smaller WiFi buffers.

# qFlipper, USB-Storage and "Switch to Bruce" live in the desktop lock menu
# (applications/services/desktop) which gates them itself.
# Bruce is gated behind a runtime ota_1-partition check.

if _board in _boards_without_wolf3d:
    APPS = [a for a in APPS if a != "wolf3d"]

EXTRA_EXT_APPS = []
TARGET_HW = 32
AUTORUN_APP = ""
