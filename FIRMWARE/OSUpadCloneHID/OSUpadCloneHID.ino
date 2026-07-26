/*
 * OSUpadCloneHID
 *
 * USB HID keyboard firmware for STM32F103 clones which work with the
 * STM32duino/libmaple USB stack but fail to enumerate QMK/ChibiOS USB.
 *
 * The six direct-wired keys are sent as: A, B, C, E, F, G.
 */

#include <USBComposite.h>

USBHID HID;
HIDKeyboard Keyboard(HID);

static const uint8_t key_pins[] = {PB0, PA7, PA6, PB12, PB13, PB14};
static const uint8_t key_values[] = {'a', 'b', 'c', 'e', 'f', 'g'};
static const uint8_t KEY_COUNT = sizeof(key_pins) / sizeof(key_pins[0]);
static const uint32_t DEBOUNCE_MS = 5;

bool stable_state[KEY_COUNT];
bool sampled_state[KEY_COUNT];
uint32_t changed_at[KEY_COUNT];

void setup() {
  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    pinMode(key_pins[i], INPUT_PULLUP);
    stable_state[i] = digitalRead(key_pins[i]) == LOW;
    sampled_state[i] = stable_state[i];
    changed_at[i] = millis();
  }

  USBComposite.setVendorId(0x7877);
  USBComposite.setProductId(0x1003);
  USBComposite.setManufacturerString("OSUpad");
  USBComposite.setProductString("OSUpad Clone HID");
  USBComposite.setSerialString("OSUPAD-C6");

  HID.begin(HID_KEYBOARD);
  Keyboard.begin();
}

void loop() {
  const uint32_t now = millis();

  for (uint8_t i = 0; i < KEY_COUNT; ++i) {
    const bool reading = digitalRead(key_pins[i]) == LOW;

    if (reading != sampled_state[i]) {
      sampled_state[i] = reading;
      changed_at[i] = now;
    }

    if (stable_state[i] != sampled_state[i] &&
        now - changed_at[i] >= DEBOUNCE_MS) {
      stable_state[i] = sampled_state[i];
      if (stable_state[i]) {
        Keyboard.press(key_values[i]);
      } else {
        Keyboard.release(key_values[i]);
      }
    }
  }
}
