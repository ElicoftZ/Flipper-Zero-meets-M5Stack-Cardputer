#!/usr/bin/env bash
set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
EXPORT_SCRIPT="${ESP_IDF_EXPORT_SCRIPT:-${HOME}/esp/esp-idf/export.sh}"

# Default State
PORT="${ESPPORT:-}"
RUN_MONITOR=0
BUILD_ONLY=0
SELECTED_BOARD=""

# Hardware Definitions
declare -A TARGETS=(
    ["cardputer"]="esp32s3"
    ["cardputer_adv"]="esp32s3"
)
declare -A NAMES=(
    ["cardputer"]="m5stack_cardputer"
    ["cardputer_adv"]="m5stack_cardputer_adv"
)
declare -A DIRS=(
    ["cardputer"]="build_cardputer"
    ["cardputer_adv"]="build_cardputer_adv"
)

usage() {
    cat <<EOF
Usage: $(basename "$0") --board <name> [options]
Boards: cardputer, cardputer_adv
Options: -p|--port, -m|--monitor, --build-only
EOF
}

detect_port() {
    local matches=()
    shopt -s nullglob
    matches=(/dev/cu.usbmodem* /dev/cu.usbserial* /dev/ttyACM*)
    shopt -u nullglob
    if [[ "${#matches[@]}" -eq 1 ]]; then
        printf '%s\n' "${matches[0]}"
    elif [[ "${#matches[@]}" -gt 1 ]]; then
        echo "Multiple ports found: ${matches[*]}. Specify with --port." >&2
        return 1
    else
        [[ "${BUILD_ONLY}" -eq 0 ]] && echo "No device found." >&2
        return 1
    fi
}

release_port() {
    local port="$1"
    [[ -z "${port}" || ! -e "${port}" ]] && return 0
    if command -v lsof >/dev/null 2>&1; then
        local pids
        pids="$(lsof -t "${port}" 2>/dev/null || true)"
        [[ -n "${pids}" ]] && kill -9 ${pids} && sleep 0.3
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -b|--board)   SELECTED_BOARD="$2"; shift 2 ;;
        -p|--port)    PORT="$2"; shift 2 ;;
        -m|--monitor) RUN_MONITOR=1; shift ;;
        --build-only) BUILD_ONLY=1; shift ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "Unknown: $1"; usage; exit 1 ;;
    esac
done

if [[ -z "${SELECTED_BOARD}" || -z "${NAMES[$SELECTED_BOARD]+x}" ]]; then
    echo "Error: Valid --board required (esp32s3, waveshare_c6, t_embed, cardputer, cardputer_adv)." >&2
    exit 1
fi

BOARD="${NAMES[$SELECTED_BOARD]}"
BUILD_DIR="${DIRS[$SELECTED_BOARD]}"
TARGET="${TARGETS[$SELECTED_BOARD]}"

# Per-board sdkconfig overrides. ESP-IDF auto-applies sdkconfig.defaults.<target>
# (e.g. sdkconfig.defaults.esp32s3 -> PSRAM on, 16 MB flash). A board such as the
# Cardputer has no PSRAM / 8 MB flash and ships sdkconfig.defaults.<flipper_board>
# which must be applied LAST to override the target defaults. Skip if absent.
SDKCONFIG_DEFAULTS="sdkconfig.defaults"
if [[ -f "sdkconfig.defaults.${BOARD}" ]]; then
    SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.${BOARD}"
fi

# Force environment to match board target
export IDF_TARGET="${TARGET}"

if [[ -z "${PORT}" && "${BUILD_ONLY}" -eq 0 ]]; then
    PORT="$(detect_port || echo "")"
    [[ -z "${PORT}" ]] && exit 1
fi

[[ ! -f "${EXPORT_SCRIPT}" ]] && echo "IDF export script missing." >&2 && exit 1
# shellcheck source=/dev/null
source "${EXPORT_SCRIPT}"

cd "${SCRIPT_DIR}"

[[ "${BUILD_ONLY}" -eq 0 ]] && release_port "${PORT}"

# Remove root sdkconfig if it belongs to a different target
if [[ -f "sdkconfig" ]]; then
    CURRENT_CONFIG_TARGET=$(grep -oP '(?<=CONFIG_IDF_TARGET=")[^"]+' sdkconfig 2>/dev/null || echo "")
    if [[ -z "${CURRENT_CONFIG_TARGET}" || "${CURRENT_CONFIG_TARGET}" != "${TARGET}" ]]; then
        echo "Root sdkconfig mismatch (Config: '${CURRENT_CONFIG_TARGET}', Needed: '${TARGET}'). Removing..."
        rm -f sdkconfig
    fi
fi

# Remove build-dir sdkconfig if it belongs to a different target
if [[ -f "${BUILD_DIR}/sdkconfig" ]]; then
    BD_TARGET=$(grep -oP '(?<=CONFIG_IDF_TARGET=")[^"]+' "${BUILD_DIR}/sdkconfig" 2>/dev/null || echo "")
    if [[ -z "${BD_TARGET}" || "${BD_TARGET}" != "${TARGET}" ]]; then
        echo "Build dir sdkconfig mismatch (Config: '${BD_TARGET}', Needed: '${TARGET}'). Removing..."
        rm -f "${BUILD_DIR}/sdkconfig"
    fi
fi

# Set target (creates/updates sdkconfig)
idf.py -B "${BUILD_DIR}" -DSDKCONFIG_DEFAULTS="${SDKCONFIG_DEFAULTS}" set-target "${TARGET}"

# Construct command
COMMANDS=("reconfigure" "build")
PY_OPTS=("-B" "${BUILD_DIR}" "-DFLIPPER_BOARD=${BOARD}" "-DSDKCONFIG_DEFAULTS=${SDKCONFIG_DEFAULTS}")

if [[ "${BUILD_ONLY}" -eq 0 ]]; then
    COMMANDS+=("flash")
    PY_OPTS+=("-p" "${PORT}")
    [[ "${RUN_MONITOR}" -eq 1 ]] && COMMANDS+=("monitor")
fi

idf.py "${PY_OPTS[@]}" "${COMMANDS[@]}"

# Generate merged binary (bootloader + partition-table + app → single .bin)
if [[ -f "${BUILD_DIR}/flasher_args.json" ]]; then
    CHIP=$(python3 -c "import json; print(json.load(open('${BUILD_DIR}/flasher_args.json'))['extra_esptool_args']['chip'])")
    FLASH_MODE=$(python3 -c "import json; print(json.load(open('${BUILD_DIR}/flasher_args.json'))['flash_settings']['flash_mode'])")
    FLASH_SIZE=$(python3 -c "import json; print(json.load(open('${BUILD_DIR}/flasher_args.json'))['flash_settings']['flash_size'])")
    MERGE_PARTS=$(python3 -c "
import json
info = json.load(open('${BUILD_DIR}/flasher_args.json'))
for off, path in sorted(info['flash_files'].items(), key=lambda x: int(x[0], 16)):
    print(f'{off} ${BUILD_DIR}/{path}')
" | tr '\n' ' ')
    MERGED_NAME="Flipper-${SELECTED_BOARD}-merged.bin"
    esptool.py --chip "${CHIP}" merge_bin \
        --flash_mode "${FLASH_MODE}" --flash_size "${FLASH_SIZE}" \
        -o "${MERGED_NAME}" ${MERGE_PARTS}
    echo "[merge] Created ${MERGED_NAME}"
fi