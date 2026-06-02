#include "fb_touch.h"
#include "fb_config.h"
#include <Wire.h>
#include "touch/TouchDrvCST92xx.hpp"

static TouchDrvCST92xx tp;
static bool     ok    = false;
static bool     down  = false;
static int16_t  downX = 0, downY = 0, lastX = 0, lastY = 0;
static uint32_t downMs = 0;

#define SWIPE_MIN_PX 60     // ~1.94× the original 240px threshold
#define TAP_MAX_PX   36
#define TAP_MAX_MS   700UL

void touchBegin() {
  Wire1.begin(ONB_SDA, ONB_SCL, 400000);
  tp.setPins(TP_RST, TP_INT);
  ok = tp.begin(Wire1, CST9217_ADDR, ONB_SDA, ONB_SCL);
  if (ok) {
    tp.setMaxCoordinates(LCD_W, LCD_H);
    tp.setMirrorXY(true, true);     // panel is 180° vs touch native orientation
    Serial.println("[touch] CST9217 ok");
  } else {
    Serial.println("[touch] CST9217 init FAILED");
  }
}

bool touchPressed() { return down; }

bool touchPoll(uint32_t now, TouchEvent &ev) {
  ev.type = TOUCH_NONE;
  if (!ok) return false;

  int16_t xs[5], ys[5];
  uint8_t n = tp.getPoint(xs, ys, 1);
  bool isDown = (n > 0);

  if (isDown) {
    lastX = xs[0]; lastY = ys[0];
    if (!down) { down = true; downX = xs[0]; downY = ys[0]; downMs = now; }
    return false;
  }

  if (down) {                       // finger just lifted → classify
    down = false;
    int16_t dx = lastX - downX, dy = lastY - downY;
    uint32_t dt = now - downMs;
    if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy)) {
      ev.type = TOUCH_SWIPE; ev.x = dx; ev.y = dy; return true;
    }
    if (abs(dx) <= TAP_MAX_PX && abs(dy) <= TAP_MAX_PX && dt <= TAP_MAX_MS) {
      ev.type = TOUCH_TAP; ev.x = downX; ev.y = downY; return true;
    }
  }
  return false;
}
