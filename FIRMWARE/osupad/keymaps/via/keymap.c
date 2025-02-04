/* Copyright 2024 Juarendra Ramadhani <jrjuarendra@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include QMK_KEYBOARD_H

enum layer_names {
  _MEDIA,
  _BROWSER,
  _MACRO,
  _RGBLIGHTS
};

#define LAYOUT_via( \
    k00, k01, k02,  \
    k10, k11, k12\
) \
{ \
    { k00, k01, k02 }, \
    { k10, k11, k12 } \
}


const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_MEDIA] = LAYOUT_via(
        KC_A, KC_B, KC_C,
        KC_E, KC_F, KC_G
    ),
    [_BROWSER] = LAYOUT_via(
        KC_A, KC_A, KC_A,
        KC_A, KC_A, KC_A
    ),
    [_MACRO] = LAYOUT_via(
        KC_A, KC_A, KC_A,
        KC_A, KC_A, KC_A
    ),
    [_RGBLIGHTS] = LAYOUT_via(
        KC_A, KC_A, KC_A, 
        KC_A, KC_A, KC_A
    ),

};
