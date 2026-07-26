#pragma once

/* Equivalent legacy-QMK configuration for the direct six-key HID probe. */
#define VENDOR_ID       0x7877
#define PRODUCT_ID      0x1002
#define DEVICE_VER      0x0001
#define MANUFACTURER    positron_electronic
#define PRODUCT         osupad_c6_stlink_probe

#define MATRIX_ROWS 2
#define MATRIX_COLS 3
#define DIRECT_PINS { \
    { B0, A7, A6 }, \
    { B12, B13, B14 } \
}

#define USB_POLLING_INTERVAL_MS 1
