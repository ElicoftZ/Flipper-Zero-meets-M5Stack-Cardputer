@echo off
REM Build Cardputer ADV firmware and produce merged binary.
set "MSYSTEM="
set "MINGW_PREFIX="
set "MSYS2_PATH_TYPE="
set "IDF_TOOLS_PATH=C:\Espressif"
set "FLIPPER_BOARD=m5stack_cardputer_adv"
set "PATH=C:\Espressif\tools\idf-python\3.11.2;%PATH%"
call "C:\Espressif\frameworks\esp-idf-v5.4.1\export.bat"
cd /d "%~dp0"

echo === set-target ===
idf.py -B build_cardputer_adv -DFLIPPER_BOARD=m5stack_cardputer_adv -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.m5stack_cardputer_adv" set-target esp32s3
if %errorlevel% neq 0 goto :fail

echo === reconfigure + build ===
idf.py -B build_cardputer_adv -DFLIPPER_BOARD=m5stack_cardputer_adv -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.m5stack_cardputer_adv" reconfigure build
if %errorlevel% neq 0 goto :fail

echo === merge_bin ===
python -m esptool --chip esp32s3 merge_bin -o "Flipper-cardputer_adv-merged.bin" ^
  --flash_mode dio --flash_size 8MB --flash_freq 80m ^
  0x0 "build_cardputer_adv\bootloader\bootloader.bin" ^
  0x8000 "build_cardputer_adv\partition_table\partition-table.bin" ^
  0x10000 "build_cardputer_adv\furi_esp32.bin"
if %errorlevel% neq 0 goto :fail

echo === SUCCESS ===
dir "Flipper-cardputer_adv-merged.bin"
goto :end

:fail
echo === BUILD FAILED (exit %errorlevel%) ===
:end
pause
