#pragma once

typedef enum {
    GpioStartEventOtgOff = 0,
    GpioStartEventOtgOn,
    GpioStartEventManualControl,
    GpioStartEventCustomPin,
    GpioStartEventUsbUart,

    GpioCustomEventErrorBack,

    GpioUsbUartEventConfig,
    GpioUsbUartEventConfigSet,
} GpioCustomEvent;
