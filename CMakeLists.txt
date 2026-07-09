cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "components")

# ---------------------------------------------------------------------------
# Patch libnet80211.a to allow raw TX of deauth/disassoc management frames.
# The closed-source WiFi blob rejects frame types 0xC0/0xA0; we patch
# ieee80211_raw_frame_sanity_check() to return 0. Idempotent — skips if
# already patched.  Runs at configure time (before library path resolution).
# ---------------------------------------------------------------------------
set(_wifi_lib "$ENV{IDF_PATH}/components/esp_wifi/lib/esp32s3/libnet80211.a")

# Locate the xtensa toolchain (ar/objcopy) + a python interpreter. Note:
#  - find_program does NOT expand a literal "*" in PATHS, so glob the versioned
#    install dir with file(GLOB) first and pass the results as HINTS.
#  - Do NOT use NO_DEFAULT_PATH: the toolchain bin is on PATH during an IDF
#    build, so PATH is the reliable fallback. The install roots differ by OS
#    (Windows offline installer: IDF_TOOLS_PATH=C:\Espressif; Linux/mac:
#    ~/.espressif) — probe both.
file(GLOB _xt_bindirs
    "$ENV{IDF_TOOLS_PATH}/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin"
    "$ENV{HOME}/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin")
find_program(_xt_ar NAMES xtensa-esp32s3-elf-ar HINTS ${_xt_bindirs})
find_program(_xt_objcopy NAMES xtensa-esp32s3-elf-objcopy HINTS ${_xt_bindirs})
find_program(_py NAMES python python3)

if(_xt_ar AND _xt_objcopy AND _py AND EXISTS "${_wifi_lib}")
    execute_process(
        COMMAND "${_py}" "${CMAKE_SOURCE_DIR}/tools/patch_wifi_lib.py"
            "${_wifi_lib}" "${_xt_objcopy}" "${_xt_ar}" "${_wifi_lib}"
        RESULT_VARIABLE _patch_rc OUTPUT_VARIABLE _patch_out ERROR_VARIABLE _patch_err)
    if(_patch_rc EQUAL 0)
        message(STATUS "deauth WiFi-lib patch: ${_patch_out}")
    else()
        message(WARNING "deauth WiFi-lib patch FAILED (rc=${_patch_rc}): ${_patch_err}")
    endif()
else()
    # Loud, not silent: an unpatched blob makes every deauth/disassoc TX fail.
    message(WARNING "deauth WiFi-lib patch SKIPPED — deauth/disassoc frames will be "
        "REJECTED by the closed-source WiFi blob. ar='${_xt_ar}' objcopy='${_xt_objcopy}' "
        "python='${_py}' lib='${_wifi_lib}'")
endif()

add_compile_options(-Wno-error)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(furi_esp32)
