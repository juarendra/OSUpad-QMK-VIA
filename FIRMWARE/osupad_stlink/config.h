/* Copyright 2024 Juarendra Ramadhani <jrjuarendra@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#define USB_POLLING_INTERVAL_MS 1

/* key matrix size */
#define MATRIX_ROWS 2
#define MATRIX_COLS 3

/* Keyboard Matrix Assignments */
#define DIRECT_PINS { \
    { B0, A7, A6}, \
    { B12, B13, B14} \
}

/* Debounce reduces chatter (unintended double-presses). */
#define DEBOUNCE 5

#define LOCKING_RESYNC_ENABLE
#define FORCE_NKRO

/* ws2812 RGB LED */
#define WS2812_DI_PIN A5
#define RGBLED_NUM 8

#ifdef RGBLIGHT_ENABLE
#    define RGBLIGHT_DEFAULT_MODE RGBLIGHT_MODE_RAINBOW_SWIRL+2
#    define RGBLIGHT_EFFECT_ALTERNATING
#    define RGBLIGHT_EFFECT_RGB_TEST
#    define RGBLIGHT_EFFECT_RAINBOW_MOOD
#    define RGBLIGHT_EFFECT_STATIC_GRADIENT
#    define RGBLIGHT_EFFECT_RAINBOW_SWIRL
#    define RGBLIGHT_EFFECT_BREATHING
#    define RGBLIGHT_EFFECT_KNIGHT
#    define RGBLIGHT_EFFECT_SNAKE
#    define RGBLIGHT_EFFECT_CHRISTMAS
#endif
