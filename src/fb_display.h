// ─────────────────────────────────────────────────────────────────
// Flash Bee — UI rendering (layout B + Alarm-B shelter banner) for
// the 466×466 round AMOLED. Reads the shared SensorState.
// ─────────────────────────────────────────────────────────────────
#pragma once
#include <Arduino.h>

#define SCREEN_MAIN     0
#define SCREEN_SETTINGS 1
extern uint8_t currentScreen;

// selectable visual themes for the main screen
enum Theme : uint8_t { THEME_LEGACY, THEME_AURORA, THEME_NEON, THEME_WATCH, THEME_COUNT };
extern uint8_t currentTheme;
const char *themeName(uint8_t t);
void displayBegin();             // load persisted theme

void lightningFlash();           // full-screen flash + jagged bolt on a real strike
void drawMain(uint32_t now);     // main screen (layout B)
void drawSettings(uint32_t now); // settings screen
void drawSensorLost();           // I²C-fault overlay
void drawBoot(const char *l1, const char *l2, uint16_t color);

void uiSwipe();                       // toggle main/settings
void uiHandleTap(int16_t x, int16_t y);  // settings-screen taps

// antenna-tune screens
void drawTuneProgress(uint8_t cap, uint32_t hz, int32_t dev, uint32_t expHz, uint32_t tolHz);
void drawTuneResult(bool ok, uint8_t cap, uint32_t hz);
void drawTuneError(const char *l1, const char *l2);
