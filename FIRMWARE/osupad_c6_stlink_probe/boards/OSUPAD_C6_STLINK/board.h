#pragma once

#include "../../../../platforms/chibios/boards/STM32_F103_STM32DUINO/board/board.h"

/*
 * STM32duino/libmaple deliberately discharges D+ before it enables USB.
 * The F103-compatible clone on this board only enumerates with that path.
 * Keep this override local to the six-key diagnostic target: it lets us test
 * the attach timing independently from the endpoint driver.
 */
#undef usb_lld_connect_bus
#define usb_lld_connect_bus(usbp)                                      \
    do {                                                               \
        volatile uint32_t osupad_usb_delay;                            \
        (void)(usbp);                                                  \
        palSetPadMode(GPIOA, 12, PAL_MODE_OUTPUT_PUSHPULL);            \
        palClearPad(GPIOA, 12);                                        \
        for (osupad_usb_delay = 0; osupad_usb_delay < 12000U;         \
             osupad_usb_delay++) {                                    \
            __asm__ volatile("nop");                                 \
        }                                                              \
        palSetPadMode(GPIOA, 12, PAL_MODE_INPUT);                      \
    } while (0)
