#include <assert.h>
#include <string.h>
#include "osupad_via_adapters.h"

static int applyCount = 0;
static void onApply() { applyCount++; }

int main() {
  OsupadRgbState rgb = {0, 0, 0, 0, 0};
  uint8_t defaultLayer = 0;
  OsupadCustomValue cv(rgb, defaultLayer, &onApply);

  // set channel 2 qmk_rgblight
  uint8_t p[via::kPacketSize];
  memset(p, 0, sizeof(p));
  p[0] = 0x07; p[1] = 0x02; p[2] = 0x01; p[3] = 42;  // brightness 42
  assert(cv.set(p));
  assert(rgb.brightness == 42);
  p[2] = 0x02; p[3] = 9;                              // effect 9
  assert(cv.set(p));
  assert(rgb.effect == 9);
  p[2] = 0x04; p[3] = 10; p[4] = 200;                 // hue/saturation
  assert(cv.set(p));
  assert(rgb.hue == 10 && rgb.saturation == 200);
  assert(applyCount == 3);
  p[2] = 0x99;                                        // unknown channel -> reject
  assert(!cv.set(p));

  // get
  memset(p, 0, sizeof(p));
  p[0] = 0x08; p[1] = 0x02; p[2] = 0x01;
  assert(cv.get(p));
  assert(p[3] == 42);

  // save/load round trip
  rgb = {1, 2, 3, 4, 5};
  defaultLayer = 3;
  uint8_t state[kCustomBytes];
  assert(cv.saveState(state, kCustomBytes));
  assert(!cv.saveState(state, kCustomBytes - 1));
  OsupadRgbState rgb2 = {0, 0, 0, 0, 0};
  uint8_t dl2 = 0;
  OsupadCustomValue cv2(rgb2, dl2, nullptr);
  assert(cv2.loadState(state, kCustomBytes));
  assert(rgb2.brightness == 1 && rgb2.effect == 2 && rgb2.speed == 3 &&
         rgb2.hue == 4 && rgb2.saturation == 5 && dl2 == 3);
  assert(!cv2.loadState(state, kCustomBytes - 1));
  assert(cv2.validateState(state, kCustomBytes));
  assert(!cv2.validateState(state, 5));

  return 0;
}
