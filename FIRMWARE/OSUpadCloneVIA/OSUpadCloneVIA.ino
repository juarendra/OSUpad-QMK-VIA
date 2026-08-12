/*
 * OSUpadCloneVIA
 *
 * VIA-compatible Arduino firmware for the STM32F103-compatible clone used
 * by OSUpad. It deliberately uses STM32duino/libmaple USBComposite instead
 * of QMK/ChibiOS; that is the USB stack verified on this board.
 *
 * Features: six-key direct matrix, four editable layers, 16 VIA macros,
 * VIA Raw-HID command subset, and eight WS2812 LEDs on PA5.
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <USBComposite.h>
#include <libmaple/gpio.h>

#include <VIA_Protocol.h>
#include "osupad_via_adapters.h"
#include "stm32_flash_memory.h"
#include "via_raw_hid.h"

static const uint8_t KEY_COUNT = 6;
static const uint8_t LAYER_COUNT = 4;
static const uint8_t MACRO_COUNT = 16;
static const uint16_t MACRO_BYTES = 512;
static const uint8_t RGB_LED_COUNT = 8;
static const uint8_t DEBOUNCE_MS = 5;
static const uint8_t RAW_REPORT_BYTES = 32;
static const uint32_t SETTINGS_SAVE_DELAY_MS = 750;
static const uint32_t SETTINGS_RETRY_DELAY_MS = 1000;
static const uint8_t MACRO_TAP_DELAY_MS = 5;
static const uint16_t TAPPING_TERM_MS = 175;
static const uint8_t MOUSEKEY_INTERVAL_MS = 20;

/* QMK send-string bytecode. VIA stores dynamic macros in exactly this
 * NUL-separated byte stream.  The values match QMK's
 * quantum/send_string/send_string_keycodes.h. */
static const uint8_t SS_QMK_PREFIX = 0x01;
static const uint8_t SS_TAP_CODE = 0x01;
static const uint8_t SS_DOWN_CODE = 0x02;
static const uint8_t SS_UP_CODE = 0x03;
static const uint8_t SS_DELAY_CODE = 0x04;

/* The generic F103C6 build reserves a 32 KiB application region.  The
 * settings pages live at the top of it and are handled by OsupadStorage. */
static bool local_dirty = false;
static uint32_t local_save_at = 0;
static void markLocalDirty() {
  local_dirty = true;
  local_save_at = millis() + SETTINGS_SAVE_DELAY_MS;
}

static const uint8_t key_pins[KEY_COUNT] = {PB0, PA7, PA6, PB12, PB13, PB14};

USBHID HID;
/* A single HID interface carries keyboard, 8-button mouse (including
 * horizontal wheel), consumer, and system-control reports. VIA Raw HID
 * remains its own vendor-defined interface. */
static const uint8_t keyboard_report_descriptor[] = {
    /* Report ID 1: 8 mouse buttons, X/Y, vertical wheel, AC Pan. */
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, HID_MOUSE_REPORT_ID,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x08, 0x75, 0x01, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0x05, 0x0C, 0x0A, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0,
    HID_CONSUMER_REPORT_DESCRIPTOR(),
    HID_KEYBOARD_REPORT_DESCRIPTOR(),
    /* Report ID 4: System Power Down, Sleep, and Wake Up. */
    0x05, 0x01, 0x09, 0x80, 0xA1, 0x01, 0x85, 0x04,
    0x19, 0x81, 0x29, 0x83, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
    0x75, 0x05, 0x95, 0x01, 0x81, 0x03, 0xC0,
};
static uint8_t mouse_report[6];
HIDReporter Mouse(HID, mouse_report, sizeof(mouse_report), HID_MOUSE_REPORT_ID, true);
HIDConsumer Consumer(HID);
static uint8_t system_report[2];
HIDReporter SystemControl(HID, system_report, sizeof(system_report), 4, true);
/* HID_KEYBOARD uses USBComposite's standard keyboard descriptor, whose
 * keyboard report ID is 2. Keep the sender and descriptor aligned. */
HIDKeyboard Keyboard(HID, HID_KEYBOARD_REPORT_ID);

static bool stable_state[KEY_COUNT];
static bool sampled_state[KEY_COUNT];
static uint32_t changed_at[KEY_COUNT];
static uint8_t layer_state = 0;
static uint8_t default_layer = 0;
static uint8_t oneshot_layer = 0xFF;
static uint8_t oneshot_mods = 0;
static uint16_t pressed_keycode[KEY_COUNT];
static bool tap_hold_active[KEY_COUNT];
static bool tap_hold_is_held[KEY_COUNT];
static uint32_t pressed_at[KEY_COUNT];
static uint8_t tt_tap_count[KEY_COUNT];
static uint32_t tt_last_tap_at[KEY_COUNT];
static uint8_t mouse_buttons = 0;
static uint8_t mouse_motion = 0;
static uint8_t mouse_acceleration = 1;
static uint32_t last_mouse_report = 0;

/* QMK keycodes, stored in the same big-endian order returned by VIA. */
static uint16_t keymap[LAYER_COUNT * KEY_COUNT];
static const uint16_t default_keymap[LAYER_COUNT * KEY_COUNT] = {
    0x0004, 0x0005, 0x0006, 0x0008, 0x0009, 0x000A, // A B C E F G
    0x0014, 0x001A, 0x001B, 0x001D, 0x001C, 0x0018, // Q W X Z Y U
    0x7700, 0x7701, 0x7702, 0x7703, 0x7704, 0x7705, // QMK Macro 0..5
    0x004F, 0x0050, 0x0051, 0x0052, 0x002C, 0x0029, // arrows, space, esc
};
static uint8_t macro_buffer[MACRO_BYTES];

static OsupadRgbState rgb = {120, 1, 80, 0, 255};
static const OsupadRgbState default_rgb = {120, 1, 80, 0, 255};
static uint32_t last_rgb_frame = 0;
static uint8_t last_rgb_effect = 1;
static bool device_indication = false;
static uint32_t device_indication_until = 0;

static void ws2812_delay(uint8_t cycles) {
  while (cycles--) {
    __asm__ volatile("nop");
  }
}

/* WS2812B 800 kHz bit-bang timing at 72 MHz.  Each loop iteration costs
 * roughly 3-4 core cycles (~42-56 ns).  Spec windows:
 *   bit 1 high 0.55-0.85 us, low 0.45-0.70 us
 *   bit 0 high 0.20-0.50 us, low 0.75-1.00 us
 * Old 22/8 produced a ~0.9+ us bit-1 HIGH (over the 0.85 us max), so LEDs
 * misread every bit as 1 and rendered solid white. */
static void ws2812_write_byte(uint8_t value) {
  const uint32_t pin = 1U << 5;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    GPIOA->regs->BSRR = pin;
    if (value & 0x80) {
      ws2812_delay(14);
      GPIOA->regs->BRR = pin;
      ws2812_delay(11);
    } else {
      ws2812_delay(6);
      GPIOA->regs->BRR = pin;
      ws2812_delay(16);
    }
    value <<= 1;
  }
}

static void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v,
                       uint8_t *r, uint8_t *g, uint8_t *b) {
  if (s == 0) {
    *r = *g = *b = v;
    return;
  }
  const uint8_t region = h / 43;
  const uint8_t remainder = (h - region * 43) * 6;
  const uint8_t p = (uint16_t)v * (255 - s) >> 8;
  const uint8_t q = (uint16_t)v * (255 - ((uint16_t)s * remainder >> 8)) >> 8;
  const uint8_t t = (uint16_t)v * (255 - ((uint16_t)s * (255 - remainder) >> 8)) >> 8;
  switch (region) {
    default:
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    case 5: *r = v; *g = p; *b = q; break;
  }
}

static void rgb_render() {
  uint8_t base_hue = rgb.hue;
  uint8_t value = rgb.brightness;
  const uint8_t frame = millis() / (257 - rgb.speed);
  const uint8_t effect = rgb.effect;
  if (effect == 0) value = 0;
  if (effect >= 2 && effect <= 5) { // QMK breathing variants
    const uint8_t phase = frame;
    const uint8_t wave = phase < 128 ? phase * 2 : (255 - phase) * 2;
    value = ((uint16_t)value * wave) >> 8;
  }
  if ((effect >= 6 && effect <= 14) || effect == 35) base_hue += frame;
  noInterrupts();
  for (uint8_t led = 0; led < RGB_LED_COUNT; ++led) {
    uint8_t r, g, b;
    uint8_t hue = base_hue;
    uint8_t led_value = value;
    if (effect >= 9 && effect <= 14) { // rainbow swirl variants
      hue += led * (16 + (effect - 9) * 8);
    } else if (effect >= 15 && effect <= 20) { // snake variants
        const uint8_t head = frame % RGB_LED_COUNT;
        const uint8_t distance = (uint8_t)(led + RGB_LED_COUNT - head) % RGB_LED_COUNT;
        led_value = distance < 1 + (effect - 15) / 2 ? value : 0;
    } else if (effect >= 21 && effect <= 23) { // knight rider variants
        const uint8_t step = frame % ((RGB_LED_COUNT - 1) * 2);
        const uint8_t head = step < RGB_LED_COUNT ? step : (RGB_LED_COUNT - 1) * 2 - step;
        const uint8_t distance = led > head ? led - head : head - led;
        led_value = distance <= (effect - 21) ? value : 0;
    } else if (effect == 24) { // Christmas
      hue = (led & 1) ? 85 : 0;
    } else if (effect >= 25 && effect <= 34) { // static gradient variants
      static const uint8_t gradient_steps[] = {32, 43, 51, 64, 85, 96, 112, 127, 170, 255};
      hue += led * gradient_steps[effect - 25];
    } else if (effect == 35) { // RGB test: red, green, blue
      hue = ((frame >> 5) % 3) * 85;
    } else if (effect == 36) { // alternating
      hue += (led & 1) ? 128 : 0;
    } else if (effect >= 37 && effect <= 42) { // deterministic twinkle variants
        const uint8_t sparkle = (uint8_t)(frame * 37 + led * 67);
        const uint8_t threshold = 224 - (effect - 37) * 12;
        led_value = sparkle > threshold ? value : value / 12;
    }
    if (device_indication) hsv_to_rgb(0, 0, 96, &r, &g, &b);
    else hsv_to_rgb(hue, rgb.saturation, led_value, &r, &g, &b);
    ws2812_write_byte(g);
    ws2812_write_byte(r);
    ws2812_write_byte(b);
  }
  interrupts();
  delayMicroseconds(80);
}

static bool rgb_effect_valid(uint8_t effect) {
  return effect <= 42;
}

static void rgb_set_effect(uint8_t effect) {
  rgb.effect = rgb_effect_valid(effect) ? effect : 1;
  if (effect != 0) last_rgb_effect = effect;
  rgb_render();
  markLocalDirty();
}

static void rgb_adjust(uint8_t *value, int16_t delta) {
  int16_t adjusted = *value + delta;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;
  *value = adjusted;
}

static bool process_rgb_keycode(uint16_t keycode, bool pressed) {
  if (!pressed) return keycode >= 0x7820 && keycode <= 0x7834;
  switch (keycode) {
    case 0x7820: rgb_set_effect(rgb.effect == 0 ? last_rgb_effect : 0); return true;
    case 0x7821: rgb_set_effect(rgb.effect >= 42 ? 1 : rgb.effect + 1); return true;
    case 0x7822: rgb_set_effect(rgb.effect <= 1 ? 42 : rgb.effect - 1); return true;
    case 0x7823: rgb.hue += 8; break;
    case 0x7824: rgb.hue -= 8; break;
    case 0x7825: rgb_adjust(&rgb.saturation, 8); break;
    case 0x7826: rgb_adjust(&rgb.saturation, -8); break;
    case 0x7827: rgb_adjust(&rgb.brightness, 8); break;
    case 0x7828: rgb_adjust(&rgb.brightness, -8); break;
    case 0x7829: rgb_adjust(&rgb.speed, 8); break;
    case 0x782A: rgb_adjust(&rgb.speed, -8); break;
    default:
      switch (keycode) {
        case 0x782B: rgb.effect = 1; break;  // RGB_MODE_PLAIN
        case 0x782C: rgb.effect = 2; break;  // RGB_MODE_BREATHE
        case 0x782D: rgb.effect = 6; break;  // RGB_MODE_RAINBOW
        case 0x782E: rgb.effect = 9; break;  // RGB_MODE_SWIRL
        case 0x782F: rgb.effect = 15; break; // RGB_MODE_SNAKE
        case 0x7830: rgb.effect = 21; break; // RGB_MODE_KNIGHT
        case 0x7831: rgb.effect = 24; break; // RGB_MODE_XMAS
        case 0x7832: rgb.effect = 25; break; // RGB_MODE_GRADIENT
        case 0x7833: rgb.effect = 35; break; // RGB_MODE_RGBTEST
        case 0x7834: rgb.effect = 37; break; // RGB_MODE_TWINKLE
        default: return false;
      }
      break;
  }
  if (rgb.effect != 0) last_rgb_effect = rgb.effect;
  rgb_render();
  markLocalDirty();
  return true;
}

/* USBComposite accepts ordinary keys in Arduino's 0x88-based form and
 * modifiers in its 0x80-based form.  Convert the USB usages embedded in
 * QMK's send-string commands without passing overflowed values to it. */
static bool qmk_usage_to_arduino(uint8_t usage, uint8_t *arduino_key) {
  if (usage >= 0x04 && usage <= 0x77) {
    *arduino_key = usage + 0x88;
    return true;
  }
  if (usage >= 0xE0 && usage <= 0xE7) {
    *arduino_key = 0x80 + (usage - 0xE0);
    return true;
  }
  return false;
}

static uint16_t qmk_consumer_usage(uint8_t usage) {
  switch (usage) {
    case 0xA8: return HIDConsumer::MUTE;
    case 0xA9: return HIDConsumer::VOLUME_UP;
    case 0xAA: return HIDConsumer::VOLUME_DOWN;
    case 0xAB: return HIDConsumer::NEXT_TRACK;
    case 0xAC: return HIDConsumer::PREVIOUS_TRACK;
    case 0xAD: return 0x00B7; // Consumer Stop
    case 0xAE: return HIDConsumer::PLAY_OR_PAUSE;
    case 0xAF: return 0x0183; // Consumer Media Select
    case 0xB0: return 0x00B8; // Consumer Eject
    case 0xB1: return 0x018A; // Consumer AL Email Reader
    case 0xB2: return 0x0192; // Consumer AL Calculator
    case 0xB3: return 0x0194; // Consumer AL File Browser (My Computer)
    case 0xB4: return 0x0221; // Consumer AC Search
    case 0xB5: return 0x0223; // Consumer AC Home
    case 0xB6: return 0x0224; // Consumer AC Back
    case 0xB7: return 0x0225; // Consumer AC Forward
    case 0xB8: return 0x0226; // Consumer AC Stop
    case 0xB9: return 0x0227; // Consumer AC Refresh
    case 0xBA: return 0x022A; // Consumer AC Bookmarks
    case 0xBB: return HIDConsumer::FAST_FORWARD;
    case 0xBC: return HIDConsumer::REWIND;
    case 0xBD: return HIDConsumer::BRIGHTNESS_UP;
    case 0xBE: return HIDConsumer::BRIGHTNESS_DOWN;
    case 0xBF: return 0x019F; // Consumer AL Control Panel
    default: return 0;
  }
}

enum MouseMotion : uint8_t {
  MOUSE_MOVE_UP = 1 << 0,
  MOUSE_MOVE_DOWN = 1 << 1,
  MOUSE_MOVE_LEFT = 1 << 2,
  MOUSE_MOVE_RIGHT = 1 << 3,
  MOUSE_WHEEL_UP = 1 << 4,
  MOUSE_WHEEL_DOWN = 1 << 5,
  MOUSE_WHEEL_LEFT = 1 << 6,
  MOUSE_WHEEL_RIGHT = 1 << 7,
};

static void mouse_send(int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  mouse_report[1] = mouse_buttons;
  mouse_report[2] = (uint8_t)x;
  mouse_report[3] = (uint8_t)y;
  mouse_report[4] = (uint8_t)wheel;
  mouse_report[5] = (uint8_t)pan;
  Mouse.sendReport();
}

static bool process_mouse_keycode(uint8_t usage, bool pressed) {
  uint8_t motion_bit = 0;
  switch (usage) {
    case 0xCD: motion_bit = MOUSE_MOVE_UP; break;
    case 0xCE: motion_bit = MOUSE_MOVE_DOWN; break;
    case 0xCF: motion_bit = MOUSE_MOVE_LEFT; break;
    case 0xD0: motion_bit = MOUSE_MOVE_RIGHT; break;
    case 0xD9: motion_bit = MOUSE_WHEEL_UP; break;
    case 0xDA: motion_bit = MOUSE_WHEEL_DOWN; break;
    case 0xDB: motion_bit = MOUSE_WHEEL_LEFT; break;
    case 0xDC: motion_bit = MOUSE_WHEEL_RIGHT; break;
    default: break;
  }
  if (motion_bit != 0) {
    if (pressed) mouse_motion |= motion_bit;
    else mouse_motion &= (uint8_t)~motion_bit;
    return true;
  }
  if (usage >= 0xD1 && usage <= 0xD8) {
    const uint8_t button = (uint8_t)(1U << (usage - 0xD1));
    if (pressed) mouse_buttons |= button;
    else mouse_buttons &= (uint8_t)~button;
    mouse_send(0, 0, 0, 0);
    return true;
  }
  if (usage >= 0xDD && usage <= 0xDF) {
    if (pressed) mouse_acceleration = usage - 0xDD;
    return true;
  }
  return false;
}

static void mouse_task(uint32_t now) {
  if (mouse_motion == 0 || now - last_mouse_report < MOUSEKEY_INTERVAL_MS) return;
  static const int8_t speeds[] = {4, 8, 16};
  const int8_t speed = speeds[mouse_acceleration];
  const int8_t x = (mouse_motion & MOUSE_MOVE_RIGHT ? speed : 0) -
                   (mouse_motion & MOUSE_MOVE_LEFT ? speed : 0);
  const int8_t y = (mouse_motion & MOUSE_MOVE_DOWN ? speed : 0) -
                   (mouse_motion & MOUSE_MOVE_UP ? speed : 0);
  const int8_t wheel = (mouse_motion & MOUSE_WHEEL_UP ? 1 : 0) -
                       (mouse_motion & MOUSE_WHEEL_DOWN ? 1 : 0);
  const int8_t pan = (mouse_motion & MOUSE_WHEEL_RIGHT ? 1 : 0) -
                     (mouse_motion & MOUSE_WHEEL_LEFT ? 1 : 0);
  mouse_send(x, y, wheel, pan);
  last_mouse_report = now;
}

static bool process_system_keycode(uint8_t usage, bool pressed) {
  uint8_t bit = 0;
  if (usage == 0xA5) bit = 1;      // KC_PWR: System Power Down
  else if (usage == 0xA6) bit = 2; // KC_SLEP: System Sleep
  else if (usage == 0xA7) bit = 4; // KC_WAKE: System Wake Up
  else return false;
  system_report[1] = pressed ? bit : 0;
  SystemControl.sendReport();
  return true;
}

static bool send_qmk_usage(uint8_t usage, bool pressed) {
  uint8_t arduino_key;
  if (qmk_usage_to_arduino(usage, &arduino_key)) {
    if (pressed) Keyboard.press(arduino_key);
    else Keyboard.release(arduino_key);
    return true;
  }

  if (process_system_keycode(usage, pressed)) return true;

  const uint16_t consumer_usage = qmk_consumer_usage(usage);
  if (consumer_usage != 0) {
    if (pressed) Consumer.press(consumer_usage);
    else Consumer.release();
    return true;
  }

  /* USB keyboard usages 0x78-0xA7 cannot be represented by Arduino's
   * 0x88-offset API, but are valid in the boot keyboard report. */
  if (usage >= 0x78 && usage <= 0xA7) {
    if (pressed) {
      for (uint8_t i = 0; i < HID_KEYBOARD_ROLLOVER; ++i) {
        if (Keyboard.keyReport.keys[i] == usage) return true;
        if (Keyboard.keyReport.keys[i] == 0) {
          Keyboard.keyReport.keys[i] = usage;
          Keyboard.sendReport();
          return true;
        }
      }
      return false;
    }
    for (uint8_t i = 0; i < HID_KEYBOARD_ROLLOVER; ++i) {
      if (Keyboard.keyReport.keys[i] == usage) {
        Keyboard.keyReport.keys[i] = 0;
        Keyboard.sendReport();
        break;
      }
    }
    return true;
  }

  return process_mouse_keycode(usage, pressed);
}

static void macro_tap_usage(uint8_t usage) {
  if (!send_qmk_usage(usage, true)) return;
  delay(MACRO_TAP_DELAY_MS);
  send_qmk_usage(usage, false);
}

static void macro_key_usage(uint8_t usage, bool pressed) {
  send_qmk_usage(usage, pressed);
}

static void run_macro(uint8_t macro_id) {
  if (macro_id >= MACRO_COUNT) return;

  /* This is QMK's interrupted-write guard. VIA sends a macro buffer in
   * chunks; a non-NUL final byte means the host has not finished writing it. */
  if (macro_buffer[MACRO_BYTES - 1] != 0) return;

  uint16_t offset = 0;
  uint8_t remaining_id = macro_id;
  while (remaining_id > 0) {
    if (offset >= MACRO_BYTES) return;
    if (macro_buffer[offset++] == 0) --remaining_id;
  }

  while (offset < MACRO_BYTES) {
    const uint8_t value = macro_buffer[offset++];
    if (value == 0) return;
    if (value != SS_QMK_PREFIX) {
      Keyboard.write(value);
      continue;
    }

    if (offset >= MACRO_BYTES) return;
    const uint8_t command = macro_buffer[offset++];
    if (command == SS_TAP_CODE || command == SS_DOWN_CODE || command == SS_UP_CODE) {
      if (offset >= MACRO_BYTES || macro_buffer[offset] == 0) return;
      const uint8_t usage = macro_buffer[offset++];
      if (command == SS_TAP_CODE) macro_tap_usage(usage);
      else macro_key_usage(usage, command == SS_DOWN_CODE);
      continue;
    }
    if (command == SS_DELAY_CODE) {
      uint32_t milliseconds = 0;
      bool has_digit = false;
      while (offset < MACRO_BYTES && macro_buffer[offset] >= '0' && macro_buffer[offset] <= '9') {
        has_digit = true;
        const uint8_t digit = macro_buffer[offset++] - '0';
        if (milliseconds > (0xFFFFFFFFUL - digit) / 10UL) milliseconds = 0xFFFFFFFFUL;
        else milliseconds = milliseconds * 10UL + digit;
      }
      /* QMK terminates SS_DELAY() with '|'. Reject a malformed command rather
       * than accidentally typing its remaining bytes as text. */
      if (!has_digit || offset >= MACRO_BYTES || macro_buffer[offset++] != '|') return;
      delay(milliseconds);
      continue;
    }
    return;
  }
}

static void set_layer(uint8_t layer, bool enabled) {
  if (layer >= LAYER_COUNT) return;
  if (enabled) layer_state |= (uint8_t)(1U << layer);
  else layer_state &= (uint8_t)~(1U << layer);
}

static void send_modifiers(uint8_t mods, bool pressed) {
  const bool right = (mods & 0x10) != 0;
  const uint8_t base = right ? 0xE4 : 0xE0;
  for (uint8_t bit = 0; bit < 4; ++bit) {
    if (mods & (1U << bit)) macro_key_usage(base + bit, pressed);
  }
}

static uint16_t resolved_keycode(uint8_t key) {
  for (int8_t layer = LAYER_COUNT - 1; layer >= 0; --layer) {
    if ((layer_state & (1U << layer)) == 0) continue;
    const uint16_t keycode = keymap[layer * KEY_COUNT + key];
    if (keycode != 0x0001) return keycode; // KC_TRNS
  }
  return keymap[default_layer * KEY_COUNT + key];
}

static bool is_tap_hold_keycode(uint16_t keycode) {
  return (keycode >= 0x2000 && keycode <= 0x3FFF) ||
         (keycode >= 0x4000 && keycode <= 0x4FFF) ||
         (keycode >= 0x52C0 && keycode <= 0x52DF);
}

static void send_keycode(uint16_t keycode, bool pressed) {
  if (process_rgb_keycode(keycode, pressed)) return;
  /* QMK/VIA V3 macro keycodes are QK_MACRO_0..QK_MACRO_15. */
  if (keycode >= 0x7700 && keycode < 0x7700 + MACRO_COUNT) {
    if (pressed) run_macro(keycode - 0x7700);
    return;
  }

  /* QMK's modified keycodes: C(KC_C), LSG(KC_TAB), and friends. */
  if (keycode >= 0x0100 && keycode <= 0x1FFF) {
    const uint8_t mods = (keycode >> 8) & 0x1F;
    const uint8_t basic = keycode & 0xFF;
    if (pressed) {
      send_modifiers(mods, true);
      macro_key_usage(basic, true);
    } else {
      macro_key_usage(basic, false);
      send_modifiers(mods, false);
    }
    return;
  }

  /* Core QMK layer actions, matching the encodings in quantum_keycodes.h. */
  if (keycode >= 0x5000 && keycode <= 0x51FF) { // LM(layer, mods)
    const uint8_t layer = (keycode >> 5) & 0x0F;
    const uint8_t mods = keycode & 0x1F;
    set_layer(layer, pressed);
    send_modifiers(mods, pressed);
    return;
  }
  if (keycode >= 0x5200 && keycode <= 0x521F) { // TO(layer)
    if (pressed && (keycode & 0x1F) < LAYER_COUNT) {
      layer_state = 0;
      set_layer(keycode & 0x1F, true);
    }
    return;
  }
  if (keycode >= 0x5220 && keycode <= 0x523F) { // MO(layer)
    set_layer(keycode & 0x1F, pressed);
    return;
  }
  if (keycode >= 0x5240 && keycode <= 0x525F) { // DF(layer)
    if (pressed && (keycode & 0x1F) < LAYER_COUNT) default_layer = keycode & 0x1F;
    return;
  }
  if (keycode >= 0x52E0 && keycode <= 0x52FF) { // PDF(layer)
    if (pressed && (keycode & 0x1F) < LAYER_COUNT) {
      default_layer = keycode & 0x1F;
      markLocalDirty();
    }
    return;
  }
  if (keycode >= 0x5260 && keycode <= 0x527F) { // TG(layer)
    if (pressed && (keycode & 0x1F) < LAYER_COUNT) layer_state ^= (uint8_t)(1U << (keycode & 0x1F));
    return;
  }
  if (keycode >= 0x5280 && keycode <= 0x529F) { // OSL(layer)
    if (pressed && (keycode & 0x1F) < LAYER_COUNT) {
      oneshot_layer = keycode & 0x1F;
      set_layer(oneshot_layer, true);
    }
    return;
  }
  if (keycode >= 0x52A0 && keycode <= 0x52BF) { // OSM(mods)
    if (pressed) oneshot_mods = keycode & 0x1F;
    return;
  }

  /* Kept for existing user maps created before standard layer actions were
   * added. New VIA maps should use TO/MO/TG/OSL instead. */
  if (keycode >= 0x5F40 && keycode < 0x5F40 + LAYER_COUNT) {
    if (pressed) {
      layer_state = 0;
      set_layer(keycode - 0x5F40, true);
    }
    return;
  }
  if (keycode <= 0x00FF) {
    macro_key_usage((uint8_t)keycode, pressed);
  }
}

static void activate_tap_hold(uint8_t key) {
  if (!tap_hold_active[key] || tap_hold_is_held[key]) return;
  const uint16_t keycode = pressed_keycode[key];
  tap_hold_is_held[key] = true;
  if (keycode >= 0x2000 && keycode <= 0x3FFF) { // MT(mods, kc)
    send_modifiers((keycode >> 8) & 0x1F, true);
  } else if (keycode >= 0x4000 && keycode <= 0x4FFF) { // LT(layer, kc)
    set_layer((keycode >> 8) & 0x0F, true);
  } else if (keycode >= 0x52C0 && keycode <= 0x52DF) { // TT(layer), hold half
    set_layer(keycode & 0x1F, true);
  }
}

static void process_key(uint8_t key, bool pressed) {
  if (pressed) {
    for (uint8_t other = 0; other < KEY_COUNT; ++other) {
      if (other != key) activate_tap_hold(other);
    }
    const uint16_t keycode = resolved_keycode(key);
    pressed_keycode[key] = keycode;
    pressed_at[key] = millis();
    tap_hold_active[key] = is_tap_hold_keycode(keycode);
    tap_hold_is_held[key] = false;
    if (tap_hold_active[key]) return;
    const bool is_oneshot_key = (keycode >= 0x5280 && keycode <= 0x529F) ||
                                (keycode >= 0x52A0 && keycode <= 0x52BF);
    if (!is_oneshot_key && oneshot_mods != 0) send_modifiers(oneshot_mods, true);
    send_keycode(keycode, true);
    return;
  }

  const uint16_t keycode = pressed_keycode[key];
  if (tap_hold_active[key]) {
    if (tap_hold_is_held[key]) {
      if (keycode >= 0x2000 && keycode <= 0x3FFF) send_modifiers((keycode >> 8) & 0x1F, false);
      else if (keycode >= 0x4000 && keycode <= 0x4FFF) set_layer((keycode >> 8) & 0x0F, false);
      else if (keycode >= 0x52C0 && keycode <= 0x52DF) set_layer(keycode & 0x1F, false);
    } else if (keycode >= 0x2000 && keycode <= 0x3FFF) {
      const uint8_t tap_keycode = keycode & 0xFF;
      send_keycode(tap_keycode, true);
      send_keycode(tap_keycode, false);
    } else if (keycode >= 0x4000 && keycode <= 0x4FFF) {
      const uint8_t tap_keycode = keycode & 0xFF;
      send_keycode(tap_keycode, true);
      send_keycode(tap_keycode, false);
    } else if (keycode >= 0x52C0 && keycode <= 0x52DF) {
      /* QMK's default TAPPING_TOGGLE is five quick taps. */
      const uint32_t now = millis();
      if (now - tt_last_tap_at[key] > TAPPING_TERM_MS) tt_tap_count[key] = 0;
      tt_last_tap_at[key] = now;
      if (++tt_tap_count[key] >= 5) {
        const uint8_t layer = keycode & 0x1F;
        if (layer < LAYER_COUNT) layer_state ^= (uint8_t)(1U << layer);
        tt_tap_count[key] = 0;
      }
    }
    tap_hold_active[key] = false;
    return;
  }

  send_keycode(keycode, false);
  const bool is_oneshot_key = (keycode >= 0x5280 && keycode <= 0x529F) ||
                              (keycode >= 0x52A0 && keycode <= 0x52BF);
  if (!is_oneshot_key) {
    if (oneshot_mods != 0) {
      send_modifiers(oneshot_mods, false);
      oneshot_mods = 0;
    }
    if (oneshot_layer != 0xFF) {
      set_layer(oneshot_layer, false);
      oneshot_layer = 0xFF;
    }
  }
}

// --- VIA-Arduino protocol wiring ---
class OsupadCallbacks : public via::Callbacks {
 public:
  uint32_t matrixRow(uint8_t row) const override {
    if (row >= 2) return 0;
    return (stable_state[row * 3] ? 1 : 0) |
           (stable_state[row * 3 + 1] ? 2 : 0) |
           (stable_state[row * 3 + 2] ? 4 : 0);
  }
  void deviceIndication(uint8_t value) override {
    device_indication = value != 0;
    if (value != 0) device_indication_until = millis() + 2000;
    rgb_render();
  }
};

static void applyRgb() {
  if (rgb.effect != 0) last_rgb_effect = rgb.effect;
  rgb_render();
}

OsupadTransport transport;
Stm32FlashMemory flashMemory;
static uint8_t storageBuffer[kRecordSize];
OsupadStorage storage(flashMemory, storageBuffer, sizeof(storageBuffer));
static uint8_t loadBuffer[kPayloadBytes];
OsupadCustomValue customValue(rgb, default_layer, &applyRgb);
OsupadCallbacks callbacks;

via::Config protocolConfig = via::Config(
    2, 3, LAYER_COUNT, keymap, default_keymap,
    macro_buffer, MACRO_BYTES, MACRO_COUNT, 1, SETTINGS_SAVE_DELAY_MS,
    0, 0, nullptr, nullptr,
    loadBuffer, sizeof(loadBuffer), true, true, false);

via::Protocol protocol(protocolConfig, transport, &storage, &customValue, &callbacks);

void setup() {
  pinMode(PA5, OUTPUT);
  digitalWrite(PA5, LOW);
  rgb_render();
  storage.begin();           // NEW: scan flash slots before protocol load
  protocol.begin(millis());

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    pinMode(key_pins[i], INPUT_PULLUP);
    stable_state[i] = digitalRead(key_pins[i]) == LOW;
    sampled_state[i] = stable_state[i];
    changed_at[i] = millis();
  }

  USBComposite.setVendorId(0x7877);
  USBComposite.setProductId(0x1000);
  USBComposite.setManufacturerString("OSUpad");
  USBComposite.setProductString("OSUpad Clone VIA");
  USBComposite.setSerialString("OSUPAD-C6-VIA");
  USBComposite.setDisconnectDelay(500);
  /* Keep keyboard and VIA Raw HID as two independent HID interfaces. */
  USBComposite.clear();
  HID.setReportDescriptor(keyboard_report_descriptor, sizeof(keyboard_report_descriptor));
  HID.registerComponent();
  via_raw_hid_register();
  USBComposite.begin();
  Keyboard.begin();
}

void loop() {
  protocol.task(millis());

  const uint32_t now = millis();
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    const bool reading = digitalRead(key_pins[i]) == LOW;
    if (reading != sampled_state[i]) {
      sampled_state[i] = reading;
      changed_at[i] = now;
    }
    if (stable_state[i] != sampled_state[i] && now - changed_at[i] >= DEBOUNCE_MS) {
      stable_state[i] = sampled_state[i];
      process_key(i, stable_state[i]);
    }
  }

  mouse_task(now);

  if (rgb.effect >= 2 && now - last_rgb_frame >= 20) {
    last_rgb_frame = now;
    rgb_render();
  }

  for (uint8_t key = 0; key < KEY_COUNT; ++key) {
    if (tap_hold_active[key] && stable_state[key] && !tap_hold_is_held[key] &&
        now - pressed_at[key] >= TAPPING_TERM_MS) {
      activate_tap_hold(key);
    }
  }

  if (local_dirty && (int32_t)(now - local_save_at) >= 0) {
    if (protocol.save()) local_dirty = false;
    else local_save_at = now + SETTINGS_RETRY_DELAY_MS;
  }

  if (device_indication && (int32_t)(now - device_indication_until) >= 0) {
    device_indication = false;
    rgb_render();
  }
}
