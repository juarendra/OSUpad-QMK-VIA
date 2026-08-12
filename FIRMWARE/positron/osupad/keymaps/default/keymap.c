// Copyright 2024-2026 Juarendra Ramadhani (@juarendra)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(KC_A, KC_B, KC_C, KC_E, KC_F, KC_G),
    [1] = LAYOUT(KC_Q, KC_W, KC_X, KC_Z, KC_Y, KC_U),
    [2] = LAYOUT(KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO),
    [3] = LAYOUT(KC_RGHT, KC_UP, KC_DOWN, KC_LEFT, KC_SPC, KC_ESC),
};
