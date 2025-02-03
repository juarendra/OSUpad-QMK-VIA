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
//#define NO_ACTION_ONESHOT

//#define NO_ACTION_ONESHOT


/* key matrix size */
#define MATRIX_ROWS 2
#define MATRIX_COLS 3

/* Keyboard Matrix Assignments */
#define DIRECT_PINS { \
    { A7, A6, A5}, \
    { B12, B13, B14} \
}
/* Debounce reduces chatter (unintended double-presses) - set 0 if debouncing is not needed */
#define DEBOUNCE 5

/* Mechanical locking support. Use KC_LCAP, KC_LNUM or KC_LSCR instead in keymap */
//#define LOCKING_SUPPORT_ENABLE
/* Locking resynchronize hack */
#define LOCKING_RESYNC_ENABLE

#define FORCE_NKRO

/* ws2812 RGB LED */
#define WS2812_DI_PIN A4 //D3 - underglow C7 - backlight
#define RGBLED_NUM 8    // Number of L

#ifdef RGBLIGHT_ENABLE
#    define RGBLIGHT_DEFAULT_MODE RGBLIGHT_MODE_RAINBOW_SWIRL+2

// Disabling some of these is a good way to save flash space.
#    define RGBLIGHT_EFFECT_ALTERNATING     // 108
#    define RGBLIGHT_EFFECT_RGB_TEST        // 158
#    define RGBLIGHT_EFFECT_RAINBOW_MOOD    // 160
#    define RGBLIGHT_EFFECT_STATIC_GRADIENT // 168
#    define RGBLIGHT_EFFECT_RAINBOW_SWIRL   // 192
#    define RGBLIGHT_EFFECT_BREATHING       // 348
#    define RGBLIGHT_EFFECT_KNIGHT          // 336
#    define RGBLIGHT_EFFECT_SNAKE           // 406
#    define RGBLIGHT_EFFECT_CHRISTMAS       // 508
/*
#define RGBLIGHT_EFFECT_TWINKLE         // 1156
*/
#endif