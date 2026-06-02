// ─────────────────────────────────────────────────────────────────
// Flash Bee — wall-clock time: onboard PCF85063 RTC (Wire1) + NTP.
// Keeps real UTC epoch across reboots/flashes so the persisted strike
// log has meaningful timestamps. Device stays UTC; the browser/HA
// renders local time.
// ─────────────────────────────────────────────────────────────────
#pragma once
#include <Arduino.h>
#include <time.h>

void   timeBegin();      // read RTC → system clock (call after Wire1 is up)
void   timeNtpBegin();   // start SNTP (call once WiFi is connected; non-blocking)
void   timeService();    // call each loop; writes the RTC once NTP first syncs
bool   timeValid();      // true once we have a real wall-clock (year ≥ 2024)
time_t timeNow();        // UTC epoch (0-ish/invalid until timeValid())
