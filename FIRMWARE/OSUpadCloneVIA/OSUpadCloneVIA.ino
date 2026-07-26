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

#include "via_raw_hid.h"

static const uint8_t KEY_COUNT = 6;
static const uint8_t LAYER_COUNT = 4;
static const uint8_t MACRO_COUNT = 16;
static const uint16_t MACRO_BYTES = 512;
static const uint16_t LEGACY_MACRO_BYTES = 192;
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

/* The generic F103C6 build reserves a 32 KiB application region.  These are
 * its final two 1 KiB pages.  The current binary is ~24 KiB, leaving more than
 * 6 KiB before these pages even if this particular clone exposes more flash. */
static const uint32_t SETTINGS_PAGE_A = 0x08007800UL;
static const uint32_t SETTINGS_PAGE_B = 0x08007C00UL;
static const uint16_t SETTINGS_PAGE_BYTES = 1024;
static const uint32_t SETTINGS_MAGIC = 0x4F535650UL;  // "OSVP"
static const uint16_t SETTINGS_VERSION = 2;
static const uint16_t SETTINGS_V1_VERSION = 1;

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
static uint16_t keymap[LAYER_COUNT][KEY_COUNT];
static const uint16_t default_keymap[LAYER_COUNT][KEY_COUNT] = {
    {0x0004, 0x0005, 0x0006, 0x0008, 0x0009, 0x000A}, // A B C E F G
    {0x0014, 0x001A, 0x001B, 0x001D, 0x001C, 0x0018}, // Q W X Z Y U
    {0x7700, 0x7701, 0x7702, 0x7703, 0x7704, 0x7705}, // QMK Macro 0..5
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
static const RgbState default_rgb = {48, 1, 80, 0, 255};
static RgbState rgb = default_rgb;
static uint32_t last_rgb_frame = 0;
static uint8_t last_rgb_effect = 1;
static bool device_indication = false;

struct PersistentPayload {
  uint16_t keymap[LAYER_COUNT][KEY_COUNT];
  uint8_t macro_buffer[MACRO_BYTES];
  RgbState rgb;
  /* Occupies the alignment byte that was previously unused, preserving the
   * serialized payload size of existing settings records. */
  uint8_t default_layer;
};

/* Version-1 releases used a 192-byte macro area. Keep this exact layout so
 * an installed device can migrate its saved map, macros, and RGB state. */
struct LegacyPersistentPayload {
  uint16_t keymap[LAYER_COUNT][KEY_COUNT];
  uint8_t macro_buffer[LEGACY_MACRO_BYTES];
  RgbState rgb;
};

struct PersistentImage {
  uint32_t magic;
  uint16_t version;
  uint16_t payload_size;
  uint32_t sequence;
  uint32_t crc32;
  PersistentPayload payload;
};

struct LegacyPersistentImage {
  uint32_t magic;
  uint16_t version;
  uint16_t payload_size;
  uint32_t sequence;
  uint32_t crc32;
  LegacyPersistentPayload payload;
};

static_assert(sizeof(PersistentImage) <= SETTINGS_PAGE_BYTES,
              "persistent settings exceed reserved flash page");
static_assert((sizeof(PersistentImage) & 1U) == 0,
              "persistent settings must be programmable as half-words");
static uint32_t settings_sequence = 0;
static uint32_t settings_active_page = 0;
static bool settings_dirty = false;
static uint32_t settings_save_at = 0;

static uint32_t crc32(const uint8_t *data, uint16_t size) {
  uint32_t crc = 0xFFFFFFFFUL;
  while (size--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
  }
  return ~crc;
}

static bool settings_image_valid(const PersistentImage *image) {
  return image->magic == SETTINGS_MAGIC &&
         image->version == SETTINGS_VERSION &&
         image->payload_size == sizeof(PersistentPayload) &&
         image->crc32 == crc32(reinterpret_cast<const uint8_t *>(&image->payload),
                               sizeof(image->payload));
}

/* Version 1 already used the 512-byte payload, but stored compact clone RGB
 * mode numbers. Keep it readable so an update preserves the user's map,
 * macros, and lighting state. */
static bool settings_v1_image_valid(const PersistentImage *image) {
  return image->magic == SETTINGS_MAGIC &&
         image->version == SETTINGS_V1_VERSION &&
         image->payload_size == sizeof(PersistentPayload) &&
         image->crc32 == crc32(reinterpret_cast<const uint8_t *>(&image->payload),
                               sizeof(image->payload));
}

static bool legacy_settings_image_valid(const LegacyPersistentImage *image) {
  return image->magic == SETTINGS_MAGIC &&
         image->version == SETTINGS_V1_VERSION &&
         image->payload_size == sizeof(LegacyPersistentPayload) &&
         image->crc32 == crc32(reinterpret_cast<const uint8_t *>(&image->payload),
                               sizeof(image->payload));
}

static uint8_t migrate_v1_rgb_effect(uint8_t effect) {
  /* V1 used 1..10 as a compact set. Version 2 uses QMK RGBLight mode IDs. */
  switch (effect) {
    case 0: return 0;
    case 1: return 1;   // static
    case 2: return 2;   // breathing
    case 3: return 6;   // rainbow mood
    case 4: return 9;   // rainbow swirl
    case 5: return 15;  // snake
    case 6: return 21;  // knight
    case 7: return 24;  // Christmas
    case 8: return 25;  // static gradient
    case 9: return 35;  // RGB test
    case 10: return 37; // twinkle
    default: return 1;
  }
}

static void settings_load() {
  const PersistentImage *a = reinterpret_cast<const PersistentImage *>(SETTINGS_PAGE_A);
  const PersistentImage *b = reinterpret_cast<const PersistentImage *>(SETTINGS_PAGE_B);
  const bool a_valid = settings_image_valid(a);
  const bool b_valid = settings_image_valid(b);
  const PersistentImage *chosen = nullptr;
  if (a_valid && (!b_valid || a->sequence >= b->sequence)) {
    chosen = a;
    settings_active_page = SETTINGS_PAGE_A;
  } else if (b_valid) {
    chosen = b;
    settings_active_page = SETTINGS_PAGE_B;
  }
  if (chosen != nullptr) {
    memcpy(keymap, chosen->payload.keymap, sizeof(keymap));
    memcpy(macro_buffer, chosen->payload.macro_buffer, sizeof(macro_buffer));
    rgb = chosen->payload.rgb;
    default_layer = chosen->payload.default_layer < LAYER_COUNT ? chosen->payload.default_layer : 0;
    last_rgb_effect = rgb.effect != 0 ? rgb.effect : 1;
    settings_sequence = chosen->sequence;
    return;
  }

  const PersistentImage *v1_a = reinterpret_cast<const PersistentImage *>(SETTINGS_PAGE_A);
  const PersistentImage *v1_b = reinterpret_cast<const PersistentImage *>(SETTINGS_PAGE_B);
  const bool v1_a_valid = settings_v1_image_valid(v1_a);
  const bool v1_b_valid = settings_v1_image_valid(v1_b);
  const PersistentImage *v1 = nullptr;
  if (v1_a_valid && (!v1_b_valid || v1_a->sequence >= v1_b->sequence)) {
    v1 = v1_a;
    settings_active_page = SETTINGS_PAGE_A;
  } else if (v1_b_valid) {
    v1 = v1_b;
    settings_active_page = SETTINGS_PAGE_B;
  }
  if (v1 != nullptr) {
    memcpy(keymap, v1->payload.keymap, sizeof(keymap));
    memcpy(macro_buffer, v1->payload.macro_buffer, sizeof(macro_buffer));
    rgb = v1->payload.rgb;
    rgb.effect = migrate_v1_rgb_effect(rgb.effect);
    last_rgb_effect = rgb.effect != 0 ? rgb.effect : 1;
    default_layer = v1->payload.default_layer < LAYER_COUNT ? v1->payload.default_layer : 0;
    settings_sequence = v1->sequence;
    settings_dirty = true;
    settings_save_at = millis() + SETTINGS_SAVE_DELAY_MS;
    return;
  }

  const LegacyPersistentImage *legacy_a = reinterpret_cast<const LegacyPersistentImage *>(SETTINGS_PAGE_A);
  const LegacyPersistentImage *legacy_b = reinterpret_cast<const LegacyPersistentImage *>(SETTINGS_PAGE_B);
  const bool legacy_a_valid = legacy_settings_image_valid(legacy_a);
  const bool legacy_b_valid = legacy_settings_image_valid(legacy_b);
  const LegacyPersistentImage *legacy = nullptr;
  if (legacy_a_valid && (!legacy_b_valid || legacy_a->sequence >= legacy_b->sequence)) legacy = legacy_a;
  else if (legacy_b_valid) legacy = legacy_b;
  if (legacy != nullptr) {
    memcpy(keymap, legacy->payload.keymap, sizeof(keymap));
    memset(macro_buffer, 0, sizeof(macro_buffer));
    memcpy(macro_buffer, legacy->payload.macro_buffer, LEGACY_MACRO_BYTES);
    rgb = legacy->payload.rgb;
    rgb.effect = migrate_v1_rgb_effect(rgb.effect);
    last_rgb_effect = rgb.effect != 0 ? rgb.effect : 1;
    default_layer = 0;
    settings_sequence = legacy->sequence;
    /* Commit the enlarged record after USB has had time to enumerate. */
    settings_dirty = true;
    settings_save_at = millis() + SETTINGS_SAVE_DELAY_MS;
  }
}

static bool settings_commit() {
  PersistentImage image = {};
  image.magic = SETTINGS_MAGIC;
  image.version = SETTINGS_VERSION;
  image.payload_size = sizeof(PersistentPayload);
  image.sequence = settings_sequence + 1;
  memcpy(image.payload.keymap, keymap, sizeof(keymap));
  memcpy(image.payload.macro_buffer, macro_buffer, sizeof(macro_buffer));
  image.payload.rgb = rgb;
  image.payload.default_layer = default_layer;
  image.crc32 = crc32(reinterpret_cast<const uint8_t *>(&image.payload),
                      sizeof(image.payload));

  const uint32_t target_page = settings_active_page == SETTINGS_PAGE_A
                                   ? SETTINGS_PAGE_B : SETTINGS_PAGE_A;
  bool ok = true;
  noInterrupts();
  FLASH_Unlock();
  if (FLASH_ErasePage(target_page) != FLASH_COMPLETE) ok = false;
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&image);
  for (uint16_t offset = 0; ok && offset < sizeof(image); offset += 2) {
    const uint16_t word = bytes[offset] | ((uint16_t)bytes[offset + 1] << 8);
    if (FLASH_ProgramHalfWord(target_page + offset, word) != FLASH_COMPLETE) ok = false;
  }
  FLASH_Lock();
  interrupts();
  const PersistentImage *written = reinterpret_cast<const PersistentImage *>(target_page);
  if (!ok || !settings_image_valid(written) || written->sequence != image.sequence) return false;
  settings_active_page = target_page;
  settings_sequence = image.sequence;
  settings_dirty = false;
  return true;
}

static void settings_mark_dirty() {
  settings_dirty = true;
  settings_save_at = millis() + SETTINGS_SAVE_DELAY_MS;
}

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
  settings_mark_dirty();
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
  settings_mark_dirty();
  return true;
}

static void keymap_reset() {
  memcpy(keymap, default_keymap, sizeof(keymap));
}

static void macro_reset() {
  memset(macro_buffer, 0, sizeof(macro_buffer));
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
    const uint16_t keycode = keymap[layer][key];
    if (keycode != 0x0001) return keycode; // KC_TRNS
  }
  return keymap[default_layer][key];
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
      settings_mark_dirty();
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

static void copy_keymap_to_buffer(uint16_t offset, uint8_t size, uint8_t *out) {
  const uint16_t bytes = sizeof(keymap);
  for (uint8_t i = 0; i < size; ++i) {
    const uint16_t index = offset + i;
    if (index >= bytes) {
      out[i] = 0;
      continue;
    }
    const uint16_t keycode = keymap[index / 2 / KEY_COUNT][(index / 2) % KEY_COUNT];
    out[i] = (index & 1) ? keycode : keycode >> 8;
  }
}

static void copy_buffer_to_keymap(uint16_t offset, uint8_t size, const uint8_t *in) {
  const uint16_t bytes = sizeof(keymap);
  for (uint8_t i = 0; i < size; ++i) {
    const uint16_t index = offset + i;
    if (index >= bytes) continue;
    uint16_t &keycode = keymap[index / 2 / KEY_COUNT][(index / 2) % KEY_COUNT];
    keycode = (index & 1) ? (keycode & 0xFF00) | in[i]
                          : ((uint16_t)in[i] << 8) | (keycode & 0x00FF);
  }
  settings_mark_dirty();
}

/* VIA protocol v13 uses the QMK custom-value command layout:
 * [command, channel, value, value-data...].  Channel 2 is qmk_rgblight.
 * This matches the V3 definition's `qmk_rgblight` menu. */
static bool via_set_rgblight(const uint8_t *data) {
  if (data[1] != 0x02) return false;
  switch (data[2]) {
    case 0x01: rgb.brightness = data[3]; break;
    case 0x02:
      if (!rgb_effect_valid(data[3])) return false;
      rgb.effect = data[3];
      if (rgb.effect != 0) last_rgb_effect = rgb.effect;
      break;
    case 0x03: rgb.speed = data[3]; break;
    case 0x04: rgb.hue = data[3]; rgb.saturation = data[4]; break;
    default: return false;
  }
  rgb_render();
  settings_mark_dirty();
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
      } else if (data[1] == 0x06) { // QMK keycodes version 0.0.8
        data[2] = data[3] = data[4] = 0; data[5] = 8;
      } else data[0] = 0xFF;
      break;
    case 0x03: // set keyboard value
      if (data[1] == 0x05) { // VIA selects the device: flash LEDs six times
        device_indication = !device_indication;
        rgb_render();
      } else if (data[1] != 0x02) data[0] = 0xFF;
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
      if (layer < LAYER_COUNT && key < KEY_COUNT) {
        keymap[layer][key] = ((uint16_t)data[4] << 8) | data[5];
        settings_mark_dirty();
      }
      break;
    }
    case 0x06: keymap_reset(); settings_mark_dirty(); break;
    case 0x07: if (!via_set_rgblight(data)) data[0] = 0xFF; break;
    case 0x08: if (!via_get_rgblight(data)) data[0] = 0xFF; break;
    case 0x09:
      if (data[1] != 0x02 || !settings_commit()) data[0] = 0xFF;
      break;
    case 0x0A:
      keymap_reset();
      macro_reset();
      rgb = default_rgb;
      last_rgb_effect = rgb.effect;
      default_layer = 0;
      layer_state = 0;
      settings_mark_dirty();
      rgb_render();
      break;
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
      settings_mark_dirty();
      break;
    }
    case 0x10: macro_reset(); settings_mark_dirty(); break;
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
  settings_load();
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
  HID.setReportDescriptor(keyboard_report_descriptor, sizeof(keyboard_report_descriptor));
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

  if (settings_dirty && (int32_t)(now - settings_save_at) >= 0 &&
      !settings_commit()) {
    /* A bad SWD/flash condition must not erase the settings page continuously
     * in the main loop. Keep the last complete page and retry at a sane rate. */
    settings_save_at = now + SETTINGS_RETRY_DELAY_MS;
  }
}
