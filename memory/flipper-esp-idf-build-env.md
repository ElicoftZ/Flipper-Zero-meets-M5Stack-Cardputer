---
name: flipper-esp-idf-build-env
description: How to build the Flipper ESP32 firmware on this machine (ESP-IDF 5.4.1 env quirks)
metadata: 
  node_type: memory
  type: reference
  originSessionId: 31fb26fd-5105-4003-ba49-daa1b28392ed
---

ESP-IDF **v5.4.1** is installed at `C:\Espressif` (official offline installer), framework at `C:\Espressif\frameworks\esp-idf-v5.4.1` — the path `winbuild.py` expects by default.

**Gotcha:** the system Python is **3.14**, which ESP-IDF 5.4.1 does NOT support. The build must use the bundled **idf-python 3.11.2** (`C:\Espressif\tools\idf-python\3.11.2`). Also, running from Git Bash leaks `MSYSTEM`, which makes `idf_tools.py` refuse with "MSys/Mingw is not supported". And the offline installer puts tools under `C:\Espressif`, so `IDF_TOOLS_PATH=C:\Espressif` must be set (default is `~/.espressif`).

Helper batches handle all of this (clear MSYSTEM, set IDF_TOOLS_PATH, put idf-python first on PATH, then call export.bat + winbuild.py):
- `C:\Espressif\flipper_build.bat <board>` — build one board (e.g. `cardputer`, `cardputer_adv`, `t_embed`, `esp32s3`, `waveshare_c6`). **Board arg drops the `m5stack_` prefix**: use `cardputer_adv`, NOT `m5stack_cardputer_adv` (that internal FLIPPER_BOARD name is rejected — `winbuild.py` only accepts `cardputer`/`cardputer_adv`). Output dir is still `build_cardputer_adv/`.
- `C:\Espressif\flipper_crossbuild.bat <boards...>` — cross-build several.

From a normal "ESP-IDF 5.4 CMD" prompt (Start Menu), `python winbuild.py build --board cardputer` works directly without the wrapper. Build outputs land in `build_<board>/furi_esp32.bin` (gitignored). Project context: [[flipper-cardputer-merge]].
