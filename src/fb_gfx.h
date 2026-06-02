// ─────────────────────────────────────────────────────────────────
// Flash Bee — display backend: CO5300 QSPI panel + PSRAM Canvas
// framebuffer + a TFT_eSPI-style drawString(datum) helper so UI code
// can centre text the way the original firmware did.
// ─────────────────────────────────────────────────────────────────
#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// text datums (subset of TFT_eSPI's)
enum Datum : uint8_t { D_TL, D_TC, D_TR, D_ML, D_MC, D_MR, D_BL, D_BC, D_BR };

extern Arduino_CO5300 *panel;
extern Arduino_Canvas *gfx;       // the framebuffer we draw on

bool gfxInit();
void gfxString(const char *s, int16_t x, int16_t y, uint8_t datum);
void gfxString(const String &s, int16_t x, int16_t y, uint8_t datum);

uint16_t energyColor(uint32_t e);
int      energyPercent(uint32_t e);   // 0..100
