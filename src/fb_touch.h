// ─────────────────────────────────────────────────────────────────
// Flash Bee — CST9217 capacitive touch (SensorLib) on the onboard
// I²C bus (Wire1, GPIO15/14). Classifies tap vs horizontal swipe.
// ─────────────────────────────────────────────────────────────────
#pragma once
#include <Arduino.h>

enum TouchType : uint8_t { TOUCH_NONE, TOUCH_TAP, TOUCH_SWIPE };
struct TouchEvent { TouchType type; int16_t x, y; };  // tap: x,y point · swipe: x,y delta

void touchBegin();
bool touchPoll(uint32_t now, TouchEvent &ev);   // true when a gesture completes (on release)
bool touchPressed();                            // currently pressed (for sleep abort)
