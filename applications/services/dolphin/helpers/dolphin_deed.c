#include "dolphin_deed.h"
#include <furi.h>

static const DolphinDeedWeight dolphin_deed_weights[] = {
    {5, DolphinAppSubGhz}, // DolphinDeedSubGhzReceiverInfo (CC1101: 5 XP/use)
    {5, DolphinAppSubGhz}, // DolphinDeedSubGhzSave
    {5, DolphinAppSubGhz}, // DolphinDeedSubGhzRawRec
    {5, DolphinAppSubGhz}, // DolphinDeedSubGhzAddManually
    {5, DolphinAppSubGhz}, // DolphinDeedSubGhzSend
    {5, DolphinAppSubGhz}, // DolphinDeedSubGhzFrequencyAnalyzer

    {1, DolphinAppRfid}, // DolphinDeedRfidRead
    {3, DolphinAppRfid}, // DolphinDeedRfidReadSuccess
    {3, DolphinAppRfid}, // DolphinDeedRfidSave
    {2, DolphinAppRfid}, // DolphinDeedRfidEmulate
    {2, DolphinAppRfid}, // DolphinDeedRfidAdd

    {1, DolphinAppNfc}, // DolphinDeedNfcRead
    {3, DolphinAppNfc}, // DolphinDeedNfcReadSuccess
    {3, DolphinAppNfc}, // DolphinDeedNfcSave
    {1, DolphinAppNfc}, // DolphinDeedNfcDetectReader
    {2, DolphinAppNfc}, // DolphinDeedNfcEmulate
    {2, DolphinAppNfc}, // DolphinDeedNfcKeyAdd
    {1, DolphinAppNfc}, // DolphinDeedNfcAddSave
    {1, DolphinAppNfc}, // DolphinDeedNfcAddEmulate

    {2, DolphinAppIr}, // DolphinDeedIrSend (built-in emitter: 2 XP)
    {3, DolphinAppIr}, // DolphinDeedIrLearnSuccess
    {3, DolphinAppIr}, // DolphinDeedIrSave

    {1, DolphinAppIbutton}, // DolphinDeedIbuttonRead
    {3, DolphinAppIbutton}, // DolphinDeedIbuttonReadSuccess
    {3, DolphinAppIbutton}, // DolphinDeedIbuttonSave
    {2, DolphinAppIbutton}, // DolphinDeedIbuttonEmulate
    {2, DolphinAppIbutton}, // DolphinDeedIbuttonAdd

    {3, DolphinAppBadusb}, // DolphinDeedBadUsbPlayScript
    {3, DolphinAppPlugin}, // DolphinDeedU2fAuthorized

    {1, DolphinAppPlugin}, // DolphinDeedGpioUartBridge

    {2, DolphinAppPlugin}, // DolphinDeedPluginStart
    {1, DolphinAppPlugin}, // DolphinDeedPluginGameStart
    {10, DolphinAppPlugin}, // DolphinDeedPluginGameWin

    {2, DolphinAppWifi}, // DolphinDeedWifiScan (WiFi: 2 XP/use)
    {2, DolphinAppWifi}, // DolphinDeedWifiDeauth
    {2, DolphinAppWifi}, // DolphinDeedWifiPortal
    {1, DolphinAppBluetooth}, // DolphinDeedBleSpam (Bluetooth: 1 XP/use)
    {1, DolphinAppBluetooth}, // DolphinDeedBleScan

    {5, DolphinAppSubGhz}, // DolphinDeedNrf24Send (NRF24: 5 XP, shares SubGHz bucket)
    {4, DolphinAppIr}, // DolphinDeedIrSendExt (IR via external module: 4 XP)
};

static uint8_t dolphin_deed_limits[] = {
    20, // DolphinAppSubGhz
    20, // DolphinAppRfid
    20, // DolphinAppNfc
    20, // DolphinAppIr
    20, // DolphinAppIbutton
    20, // DolphinAppBadusb
    20, // DolphinAppPlugin
    20, // DolphinAppWifi
    35, // DolphinAppBluetooth (higher daily cap)
};

_Static_assert(COUNT_OF(dolphin_deed_weights) == DolphinDeedMAX, "dolphin_deed_weights size error");
_Static_assert(COUNT_OF(dolphin_deed_limits) == DolphinAppMAX, "dolphin_deed_limits size error");

uint8_t dolphin_deed_get_weight(DolphinDeed deed) {
    furi_check(deed < DolphinDeedMAX);
    return dolphin_deed_weights[deed].icounter;
}

DolphinApp dolphin_deed_get_app(DolphinDeed deed) {
    furi_check(deed < DolphinDeedMAX);
    return dolphin_deed_weights[deed].app;
}

uint8_t dolphin_deed_get_app_limit(DolphinApp app) {
    furi_check(app < DolphinAppMAX);
    return dolphin_deed_limits[app];
}
