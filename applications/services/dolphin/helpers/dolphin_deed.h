#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DolphinAppSubGhz,
    DolphinAppRfid,
    DolphinAppNfc,
    DolphinAppIr,
    DolphinAppIbutton,
    DolphinAppBadusb,
    DolphinAppPlugin,
    DolphinAppWifi,
    DolphinAppBluetooth,
    DolphinAppMAX,
} DolphinApp;

typedef enum {
    DolphinDeedSubGhzReceiverInfo,
    DolphinDeedSubGhzSave,
    DolphinDeedSubGhzRawRec,
    DolphinDeedSubGhzAddManually,
    DolphinDeedSubGhzSend,
    DolphinDeedSubGhzFrequencyAnalyzer,

    DolphinDeedRfidRead,
    DolphinDeedRfidReadSuccess,
    DolphinDeedRfidSave,
    DolphinDeedRfidEmulate,
    DolphinDeedRfidAdd,

    DolphinDeedNfcRead,
    DolphinDeedNfcReadSuccess,
    DolphinDeedNfcSave,
    DolphinDeedNfcDetectReader,
    DolphinDeedNfcEmulate,
    DolphinDeedNfcKeyAdd,
    DolphinDeedNfcAddSave,
    DolphinDeedNfcAddEmulate,

    DolphinDeedIrSend,
    DolphinDeedIrLearnSuccess,
    DolphinDeedIrSave,

    DolphinDeedIbuttonRead,
    DolphinDeedIbuttonReadSuccess,
    DolphinDeedIbuttonSave,
    DolphinDeedIbuttonEmulate,
    DolphinDeedIbuttonAdd,

    DolphinDeedBadUsbPlayScript,

    DolphinDeedU2fAuthorized,
    DolphinDeedGpioUartBridge,

    DolphinDeedPluginStart,
    DolphinDeedPluginGameStart,
    DolphinDeedPluginGameWin,

    DolphinDeedWifiScan,
    DolphinDeedWifiDeauth,
    DolphinDeedWifiPortal,
    DolphinDeedBleSpam,
    DolphinDeedBleScan,

    DolphinDeedNrf24Send, /* NRF24 — shares the SubGHz daily XP bucket */
    DolphinDeedIrSendExt, /* IR sent via an external module (higher reward) */

    DolphinDeedMAX,

    DolphinDeedTestLeft,
    DolphinDeedTestRight,
} DolphinDeed;

typedef struct {
    uint8_t icounter;
    DolphinApp app;
} DolphinDeedWeight;

typedef struct {
    DolphinApp app;
    uint8_t icounter_limit;
} DolphinDeedLimits;

DolphinApp dolphin_deed_get_app(DolphinDeed deed);
uint8_t dolphin_deed_get_app_limit(DolphinApp app);
uint8_t dolphin_deed_get_weight(DolphinDeed deed);

#ifdef __cplusplus
}
#endif
