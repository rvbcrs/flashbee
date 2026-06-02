// ─────────────────────────────────────────────────────────────────
// Flash Bee — power management: AXP2101 battery (XPowersLib),
// CO5300 backlight (Tier 2) and ESP32 light-sleep (Tier 3).
// ─────────────────────────────────────────────────────────────────
#pragma once
#include <Arduino.h>

void powerBegin();
void powerService(uint32_t now);   // battery refresh + backlight/sleep timers
void powerMarkActivity();          // reset inactivity timer, wake screen
bool powerDisplayOn();

void powerCycleScreenTimeout();
void powerCycleSleepTimeout();
const char *powerScreenLabel();
const char *powerSleepLabel();

uint16_t powerBatteryMv();         // 0 = no battery
uint8_t  powerBatteryPct();
bool     powerBatteryPresent();
