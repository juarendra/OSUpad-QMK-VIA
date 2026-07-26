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
#include <USBComposite.h>
#include <libmaple/gpio.h>

#include "via_raw_hid.h"

static const uint8_t KEY_COUNT = 6;
static const uint8_t LAYER_COUNT = 4;
static const uint8_t MACRO_COUNT = 16;
static const uint16_t MACRO_BYTES = 192;
static const uint8_t RGB_LED_COUNT = 8;
static const uint8_t DEBOUNCE_MS = 5;
static const uint8_t RAW_REPORT_BYTES = 32;

static const uint8_t key_pins[KEY_COUNT] = {PB0, PA7, PA6, PB12, PB13, PB14};

USBHID HID;
/* HID_KEYBOARD uses USBComposite's standard keyboard descriptor, whose
 * keyboard report ID is 2. Keep the sender and descriptor aligned. */
HIDKeyboard Keyboard(HID, HID_KEYBOARD_REPORT_ID);

static bool stable_state[KEY_COUNT];
static bool sampled_state[KEY_COUNT];
static uint32_t changed_at[KEY_COUNT];
static uint8_t active_layer = 0;

/* QMK keycodes, stored in the same big-endian order returned by VIA. */
static uint16_t keymap[LAYER_COUNT][KEY_COUNT];
static const uint16_t default_keymap[LAYER_COUNT][KEY_COUNT] = {
    {0x0004, 0x0005, 0x0006, 0x0008, 0x0009, 0x000A}, // A B C E F G
    {0x0014, 0x001A, 0x001B, 0x001D, 0x001C, 0x0018}, // Q W X Z Y U
    {0x5F12, 0x5F13, 0x5F14, 0x5F15, 0x5F16, 0x5F17}, // Macro 0..5
    {0x004F, 0x0050, 0x0051, 0x0052, 0x002C, 0x0029}, // arrows, space, esc
};
static uint8_t macro_buffer[MACRO_BYTES];

struct RgbState {
  uint8_t brightness;
  uint8_t effect;
  uint8_t speed;
  uint8_t hue;
  uint8_t saturation;
};
static RgbState rgb = {48, 1, 80, 0, 255};
static uint32_t last_rgb_frame = 0;

static void ws2812_delay(uint8_t cycles) {
  while (cycles--) {
    __asm__ volatile("nop");
  }
}

/* PA5 is the actual RGB DIN on this PCB. The short critical section keeps
 * WS2812 timing stable without touching the USB peripheral configuration. */
static void ws2812_write_byte(uint8_t value) {
  const uint32_t pin = 1U << 5;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    GPIOA->regs->BSRR = pin;
    if (value & 0x80) {
      ws2812_delay(22);
      GPIOA->regs->BRR = pin;
      ws2812_delay(8);
    } else {
      ws2812_delay(8);
      GPIOA->regs->BRR = pin;
      ws2812_delay(22);
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
  if (rgb.effect == 1) {
    base_hue += millis() / (257 - rgb.speed);
  }
  noInterrupts();
  for (uint8_t led = 0; led < RGB_LED_COUNT; ++led) {
    uint8_t r, g, b;
    const uint8_t hue = rgb.effect == 1 ? base_hue + led * 32 : base_hue;
    hsv_to_rgb(hue, rgb.saturation, rgb.brightness, &r, &g, &b);
    ws2812_write_byte(g);
    ws2812_write_byte(r);
    ws2812_write_byte(b);
  }
  interrupts();
  delayMicroseconds(80);
}

static void keymap_reset() {
  memcpy(keymap, default_keymap, sizeof(keymap));
}

static void macro_reset() {
  memset(macro_buffer, 0, sizeof(macro_buffer));
}

static void tap_hid_usage(uint8_t usage) {
  Keyboard.press(usage + 0x88);
  Keyboard.release(usage + 0x88);
}

static void run_macro(uint8_t macro_id) {
  if (macro_id >= MACRO_COUNT) return;
  uint8_t seen = 0;
  for (uint16_t i = 0; i < MACRO_BYTES; ++i) {
    const uint8_t value = macro_buffer[i];
    if (seen == macro_id && value == 0) return;
    if (value == 0) {
      ++seen;
      continue;
    }
    if (seen == macro_id && value >= 0x20 && value <= 0x7E) {
      Keyboard.write(value);
    }
  }
}

static void send_keycode(uint16_t keycode, bool pressed) {
  /* VIA macro keycodes begin at 0x5F12. */
  if (keycode >= 0x5F12 && keycode < 0x5F12 + MACRO_COUNT) {
    if (pressed) run_macro(keycode - 0x5F12);
    return;
  }
  /* A compact hardware layer selector for the default fourth layer. */
  if (keycode >= 0x5F40 && keycode < 0x5F40 + LAYER_COUNT) {
    if (pressed) active_layer = keycode - 0x5F40;
    return;
  }
  if (keycode >= 0x0004 && keycode <= 0x0065) {
    if (pressed) Keyboard.press((uint8_t)(keycode + 0x88));
    else Keyboard.release((uint8_t)(keycode + 0x88));
  }
}

static void process_key(uint8_t key, bool pressed) {
  send_keycode(keymap[active_layer][key], pressed);
}

static void copy_keymap_to_buffer(uint16_t offset, uint8_t size, uint8_t *out) {
  const uint16_t bytes = sizeof(keymap);
  const uint8_t *source = reinterpret_cast<const uint8_t *>(keymap);
  for (uint8_t i = 0; i < size; ++i) out[i] = offset + i < bytes ? source[offset + i] : 0;
}

static void copy_buffer_to_keymap(uint16_t offset, uint8_t size, const uint8_t *in) {
  const uint16_t bytes = sizeof(keymap);
  uint8_t *target = reinterpret_cast<uint8_t *>(keymap);
  for (uint8_t i = 0; i < size; ++i) if (offset + i < bytes) target[offset + i] = in[i];
}

/* VIA protocol v13 uses the QMK custom-value command layout:
 * [command, channel, value, value-data...].  Channel 2 is qmk_rgblight.
 * This matches the V3 definition's `qmk_rgblight` menu. */
static bool via_set_rgblight(const uint8_t *data) {
  if (data[1] != 0x02) return false;
  switch (data[2]) {
    case 0x01: rgb.brightness = data[3]; break;
    case 0x02: rgb.effect = data[3]; break;
    case 0x03: rgb.speed = data[3]; break;
    case 0x04: rgb.hue = data[3]; rgb.saturation = data[4]; break;
    default: return false;
  }
  rgb_render();
  return true;
}

static bool via_get_rgblight(uint8_t *data) {
  if (data[1] != 0x02) return false;
  switch (data[2]) {
    case 0x01: data[3] = rgb.brightness; break;
    case 0x02: data[3] = rgb.effect; break;
    case 0x03: data[3] = rgb.speed; break;
    case 0x04: data[3] = rgb.hue; data[4] = rgb.saturation; break;
    default: return false;
  }
  return true;
}

/* VIA protocol v13 command subset. It returns the same 32-byte packet given
 * by the host, as QMK's raw_hid_receive() does. */
static void handle_via(uint8_t *data) {
  switch (data[0]) {
    case 0x01: data[1] = 0x00; data[2] = 0x0D; break; // protocol v13 / VIA V3
    case 0x02: // keyboard value
      if (data[1] == 0x01) {
        const uint32_t up = millis();
        data[2] = up >> 24; data[3] = up >> 16; data[4] = up >> 8; data[5] = up;
      } else if (data[1] == 0x02) { // layout options: none on this fixed layout
        data[2] = data[3] = data[4] = data[5] = 0;
      } else if (data[1] == 0x03) {
        const uint8_t row_offset = data[2];
        if (row_offset < 2) data[3] = (stable_state[row_offset * 3] ? 1 : 0) |
                                      (stable_state[row_offset * 3 + 1] ? 2 : 0) |
                                      (stable_state[row_offset * 3 + 2] ? 4 : 0);
        if (row_offset + 1 < 2) data[4] = (stable_state[(row_offset + 1) * 3] ? 1 : 0) |
                                          (stable_state[(row_offset + 1) * 3 + 1] ? 2 : 0) |
                                          (stable_state[(row_offset + 1) * 3 + 2] ? 4 : 0);
      } else if (data[1] == 0x04) { // firmware version, matching the V3 definition
        data[2] = data[3] = data[4] = 0; data[5] = 1;
      } else if (data[1] == 0x06) { // QMK keycodes version; zero means standard set
        data[2] = data[3] = data[4] = data[5] = 0;
      } else data[0] = 0xFF;
      break;
    case 0x03: // set keyboard value
      if (data[1] != 0x02 && data[1] != 0x05) data[0] = 0xFF;
      break;
    case 0x04: { // get keycode: layer, row, column
      const uint8_t layer = data[1], row = data[2], column = data[3];
      const uint8_t key = row * 3 + column;
      const uint16_t code = layer < LAYER_COUNT && key < KEY_COUNT ? keymap[layer][key] : 0;
      data[4] = code >> 8; data[5] = code;
      break;
    }
    case 0x05: { // set keycode
      const uint8_t layer = data[1], row = data[2], column = data[3];
      const uint8_t key = row * 3 + column;
      if (layer < LAYER_COUNT && key < KEY_COUNT) keymap[layer][key] = ((uint16_t)data[4] << 8) | data[5];
      break;
    }
    case 0x06: keymap_reset(); break;
    case 0x07: if (!via_set_rgblight(data)) data[0] = 0xFF; break;
    case 0x08: if (!via_get_rgblight(data)) data[0] = 0xFF; break;
    case 0x09: if (data[1] != 0x02) data[0] = 0xFF; break; // RAM-backed save
    case 0x0A: keymap_reset(); macro_reset(); break;
    case 0x0C: data[1] = MACRO_COUNT; break;
    case 0x0D: data[1] = MACRO_BYTES >> 8; data[2] = MACRO_BYTES; break;
    case 0x0E: {
      const uint16_t offset = ((uint16_t)data[1] << 8) | data[2];
      const uint8_t size = data[3] > 28 ? 28 : data[3];
      for (uint8_t i = 0; i < size; ++i) data[4 + i] = offset + i < MACRO_BYTES ? macro_buffer[offset + i] : 0;
      break;
    }
    case 0x0F: {
      const uint16_t offset = ((uint16_t)data[1] << 8) | data[2];
      const uint8_t size = data[3] > 28 ? 28 : data[3];
      for (uint8_t i = 0; i < size; ++i) if (offset + i < MACRO_BYTES) macro_buffer[offset + i] = data[4 + i];
      break;
    }
    case 0x10: macro_reset(); break;
    case 0x11: data[1] = LAYER_COUNT; break;
    case 0x12: {
      const uint16_t offset = ((uint16_t)data[1] << 8) | data[2];
      copy_keymap_to_buffer(offset, data[3] > 28 ? 28 : data[3], &data[4]);
      break;
    }
    case 0x13: {
      const uint16_t offset = ((uint16_t)data[1] << 8) | data[2];
      copy_buffer_to_keymap(offset, data[3] > 28 ? 28 : data[3], &data[4]);
      break;
    }
    default: data[0] = 0xFF; break;
  }
  via_raw_hid_send(data);
}

void setup() {
  keymap_reset();
  macro_reset();
  pinMode(PA5, OUTPUT);
  digitalWrite(PA5, LOW);
  rgb_render();

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    pinMode(key_pins[i], INPUT_PULLUP);
    stable_state[i] = digitalRead(key_pins[i]) == LOW;
    sampled_state[i] = stable_state[i];
    changed_at[i] = millis();
  }

  USBComposite.setVendorId(0x7877);
  USBComposite.setProductId(0x1004);
  USBComposite.setManufacturerString("OSUpad");
  USBComposite.setProductString("OSUpad Clone VIA");
  USBComposite.setSerialString("OSUPAD-C6-VIA");
  USBComposite.setDisconnectDelay(500);
  /* Keep keyboard and VIA Raw HID as two independent HID interfaces. */
  USBComposite.clear();
  HID.setReportDescriptor(HID_KEYBOARD);
  HID.registerComponent();
  via_raw_hid_register();
  USBComposite.begin();
  Keyboard.begin();
}

void loop() {
  uint8_t report[RAW_REPORT_BYTES];
  if (via_raw_hid_receive(report)) handle_via(report);

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

  if (rgb.effect == 1 && now - last_rgb_frame >= 20) {
    last_rgb_frame = now;
    rgb_render();
  }
}
