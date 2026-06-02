// ─────────────────────────────────────────────────────────────────
// Flash Bee — board + UI configuration for the Waveshare
// ESP32-S3-Touch-AMOLED-1.75 (CO5300 QSPI 466×466, CST9217 touch).
// ─────────────────────────────────────────────────────────────────
#pragma once
#include <Arduino.h>

// ── Display (CO5300 QSPI) — from Waveshare pin_config.h ────────────
#define LCD_SDIO0   4
#define LCD_SDIO1   5
#define LCD_SDIO2   6
#define LCD_SDIO3   7
#define LCD_SCLK    38
#define LCD_CS      12
#define LCD_RST     39
#define LCD_W       466
#define LCD_H       466
#define LCD_CX      (LCD_W / 2)     // 233
#define LCD_CY      (LCD_H / 2)     // 233

// ── Onboard I²C (touch CST9217, IMU, RTC, AXP2101) ────────────────
#define ONB_SDA     15
#define ONB_SCL     14
#define TP_INT      11
#define TP_RST      40
#define CST9217_ADDR 0x5A           // verified at runtime; common for CST92xx

// ── AS3935 on its own header bus (proven working) ─────────────────
#define AS_SDA      17
#define AS_SCL      18
#ifndef AS3935_INT_PIN
  #define AS3935_INT_PIN 16
#endif

// ── UI palette (RGB565) ───────────────────────────────────────────
#define C_BLACK     0x0000
#define C_WHITE     0xFFFF
#define C_GOLD      0xFD20         // distance hero
#define C_DIMGOLD   0xC560
#define C_GREEN     0x07E6
#define C_TEAL      0x0651
#define C_DKGREEN   0x0320
#define C_AMBER     0xFD00
#define C_RED       0xF904
#define C_DKRED     0x6000
#define C_GREY      0x8C71
#define C_DKGREY    0x2104
#define C_BLUE      0x5DDF
#define C_ORANGE    0xFC60
#define C_REDORG    0xFB00
#define C_DIM       0x4208

// ── UI geometry (466 round) ───────────────────────────────────────
#define RING_OUTER  220            // listening ring radii (from centre)
#define RING_MID    178
#define PANEL_TOP   330            // bottom chord data panel
