/* Copyright 2024 Juarendra Ramadhani <jrjuarendra@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */
#include QMK_KEYBOARD_H

enum layer_names {
    _MEDIA,
    _BROWSER,
    _MACRO,
    _RGBLIGHTS,
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_MEDIA] = LAYOUT(
        KC_A, KC_B, KC_C,
        KC_E, KC_F, KC_G
    ),
    [_BROWSER] = LAYOUT(
        KC_A, KC_A, KC_A,
        KC_A, KC_A, KC_A
    ),
    [_MACRO] = LAYOUT(
        KC_A, KC_A, KC_A,
        KC_A, KC_A, KC_A
    ),
    [_RGBLIGHTS] = LAYOUT(
        KC_A, KC_A, KC_A,
        KC_A, KC_A, KC_A
    ),
};
