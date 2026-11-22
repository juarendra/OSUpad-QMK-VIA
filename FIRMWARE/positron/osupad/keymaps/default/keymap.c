// Copyright 2024-2026 Juarendra Ramadhani (@juarendra)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

enum layer_names {
    _OSU,
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_OSU] = LAYOUT(
        KC_VOLD, KC_MUTE, KC_VOLU,
        KC_Z,    KC_NO,   KC_X
    ),
};
