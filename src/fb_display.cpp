#include "fb_display.h"
#include "fb_gfx.h"
#include "fb_config.h"
#include "fb_as3935.h"
#include "fb_power.h"
#include "fb_audio.h"
#include <math.h>
#include <string.h>
#include <Preferences.h>

uint8_t currentScreen = SCREEN_MAIN;

#define DIST_STALE_MS 300000UL
#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// A reading "expires" → revert to listening when: no strikes yet, distance
// unknown, OR (once out of the shelter danger window) the last strike is older
// than DIST_STALE_MS. Stops a stale OVERHEAD/km lingering for ages after the
// storm passed — no reboot needed. Inside the shelter window we keep showing it.
static bool distExpired(uint32_t now) {
  if (sensor.strikeCount == 0 || sensor.lastDistRaw == DIST_UNKNOWN) return true;
  return !as3935InShelter(now) && (now - sensor.lastStrikeMs) > DIST_STALE_MS;
}

// thick arc, North-origin, clockwise (matches the original TFT_eSPI gauge)
static void thickArc(int cx, int cy, int rIn, int rOut,
                     float startDeg, float sweepDeg, uint16_t col) {
  for (float a = startDeg; a <= startDeg + sweepDeg; a += 0.8f) {
    float r = a * PI / 180.0f, s = sinf(r), c = cosf(r);
    gfx->drawLine(cx + (int)(rIn * s), cy - (int)(rIn * c),
                  cx + (int)(rOut * s), cy - (int)(rOut * c), col);
  }
}

// full-screen lightning flash + jagged bolt — fired on a real strike.
// Blocking (~150 ms); strikes are infrequent so it doesn't disturb the loop.
void lightningFlash() {
  for (int pass = 0; pass < 2; pass++) {
    gfx->fillScreen(C_WHITE);                       // bright flash
    gfx->flush();
    delay(28);
    gfx->fillScreen(RGB(4, 6, 20));                 // dark blue frame with the bolt
    int x = LCD_CX + (int)random(-25, 25), y = 24;
    while (y < LCD_H - 24) {
      int nx = x + (int)random(-38, 38);
      int ny = y + (int)random(38, 78);
      for (int w = -1; w <= 1; w++)                 // thick white bolt
        gfx->drawLine(x + w, y, nx + w, ny, C_WHITE);
      x = nx; y = ny;
    }
    gfx->flush();
    delay(45);
  }
  gfx->fillScreen(C_BLACK);
  gfx->flush();
}

static void drawMainLegacy(uint32_t now) {
  const int CX = LCD_CX, CY = LCD_CY;
  bool shelter = as3935InShelter(now);
  bool stale = sensor.strikeCount > 0 && (now - sensor.lastStrikeMs) > DIST_STALE_MS;
  uint16_t heroCol = shelter ? C_RED : (stale ? C_DIM : C_GOLD);

  gfx->fillScreen(C_BLACK);

  // ── radar background: concentric rings + radial spokes ─────────
  gfx->drawCircle(CX, CY, 231, 0x2124);
  gfx->drawCircle(CX, CY, 175, C_DIM);
  gfx->drawCircle(CX, CY, 113, C_DIM);
  for (int k = 0; k < 8; k++) {
    float r = k * 45 * PI / 180.0f;
    gfx->drawLine(CX + (int)(40 * sinf(r)), CY - (int)(40 * cosf(r)),
                  CX + (int)(175 * sinf(r)), CY - (int)(175 * cosf(r)), 0x1882);
  }
  // rotating radar sweep with a fading afterglow trail
  float sweep = (now % 3500) / 3500.0f * 360.0f;
  for (int t = 13; t >= 0; t--) {
    float a = (sweep - t * 4) * PI / 180.0f;
    uint8_t v = (uint8_t)(95 - t * 6);
    gfx->drawLine(CX, CY, CX + (int)(173 * sinf(a)), CY - (int)(173 * cosf(a)),
                  shelter ? RGB(v, v / 4, 0) : RGB(0, v, v / 3));
  }

  // ── energy gauge arc (right C, 225° + 270°) ────────────────────
  float pct = sensor.lastEnergy > 0 ? sensor.lastEnergy / 2097151.0f : 0;
  if (pct > 1) pct = 1;
  thickArc(CX, CY, 205, 226, 225, 270, 0x2124);                 // track
  if (pct > 0.01f)
    thickArc(CX, CY, 205, 226, 225, pct * 270, energyColor(sensor.lastEnergy));
  for (int i = 0; i <= 10; i++)                                  // ticks
    thickArc(CX, CY, 200, 230, 225 + i * 27, 0.1f, 0x4208);

  // ── header + status ────────────────────────────────────────────
  gfx->setTextSize(2);
  gfx->setTextColor(C_GREY);                 // shelter shown by the shared banner
  gfxString("LIGHTNING DETECTION", CX, 34, D_MC);
  if (sensor.noiseFault) {
    gfx->setTextColor(C_RED);
    gfxString("ENV TOO NOISY", CX, 62, D_MC);
  } else {
    bool tight = sensor.watchdogLvl > 5 || sensor.spikeRej > 5;
    char st[28];
    snprintf(st, sizeof st, "%s  WD:%u SR:%u", sensor.outdoorMode ? "OUT" : "IN ",
             sensor.watchdogLvl, sensor.spikeRej);
    gfx->setTextColor(tight ? C_ORANGE : C_DIM);
    gfxString(st, CX, 62, D_MC);
  }

  // ── hero distance ──────────────────────────────────────────────
  if (distExpired(now)) {
    gfx->setTextColor(C_DIM); gfx->setTextSize(7);
    gfxString("--", CX, 150, D_MC);
    gfx->setTextColor(C_GREY); gfx->setTextSize(2);
    gfxString("all clear", CX, 212, D_MC);
  } else if (sensor.lastDistRaw == DIST_OUT_OF_RANGE) {
    gfx->setTextColor(heroCol); gfx->setTextSize(7);
    gfxString(">40", CX, 150, D_MC);
    gfx->setTextSize(4); gfxString("km", CX, 215, D_MC);
  } else if (sensor.lastDistRaw == DIST_OVERHEAD) {
    gfx->setTextColor(C_REDORG); gfx->setTextSize(4);
    gfxString("OVERHEAD", CX, 145, D_MC);
    gfx->setTextColor(heroCol); gfx->setTextSize(3);
    gfxString("< 6 km", CX, 200, D_MC);
  } else {
    char d[6]; snprintf(d, sizeof d, "%u", sensor.lastDistRaw);
    gfx->setTextColor(heroCol); gfx->setTextSize(10);
    gfxString(d, CX, 166, D_MC);
    gfx->setTextColor(stale ? C_DIM : C_DIMGOLD); gfx->setTextSize(4);
    gfxString("km", CX, 234, D_MC);
  }

  // ── STRIKES / ENERGY row ───────────────────────────────────────
  gfx->drawFastHLine(86, 252, 294, C_DIM);
  gfx->setTextColor(C_GREY); gfx->setTextSize(2);
  gfxString("STRIKES", 86, 274, D_ML);
  gfx->setTextColor(heroCol); gfx->setTextSize(4);
  char sc[8]; snprintf(sc, sizeof sc, "%d", sensor.strikeCount);
  gfxString(sc, 86, 308, D_ML);

  {
    gfx->setTextColor(C_GREY); gfx->setTextSize(2);
    gfxString("ENERGY", 380, 274, D_MR);
    char e[12];
    if (sensor.lastEnergy > 999) snprintf(e, sizeof e, "%luK", (unsigned long)(sensor.lastEnergy / 1000));
    else                         snprintf(e, sizeof e, "%lu", (unsigned long)sensor.lastEnergy);
    gfx->setTextColor(energyColor(sensor.lastEnergy)); gfx->setTextSize(4);
    gfxString(e, 380, 308, D_MR);
  }

  // ── energy history bar chart ───────────────────────────────────
  const int bx = 82, by = 348, bw = 302, bh = 52;
  int barW = bw / MAX_TICKS - 1;
  for (int i = 0; i < sensor.tickCount; i++) {
    int idx = (sensor.tickHead - sensor.tickCount + i + MAX_TICKS) % MAX_TICKS;
    float bpct = sensor.tickEnergy[idx] / 2097151.0f; if (bpct > 1) bpct = 1;
    int h = max(2, (int)(bpct * bh));
    uint16_t c = (i == sensor.tickCount - 1) ? C_GOLD : energyColor(sensor.tickEnergy[idx]);
    gfx->fillRect(bx + i * (barW + 1), by + bh - h, barW, h, c);
  }
  gfx->setTextColor(C_GREY); gfx->setTextSize(2);
  gfxString(stale ? "HISTORY (stale)" : "ENERGY HISTORY", CX, 422, D_MC);
}

// ─────────────────────────────────────────────────────────────────
// Theme system: shared helpers + Aurora / Neon / Watch renderers
// ─────────────────────────────────────────────────────────────────
uint8_t currentTheme = THEME_LEGACY;
const char *themeName(uint8_t t) {
  switch (t) {
    case THEME_AURORA: return "Aurora";
    case THEME_NEON:   return "Neon";
    case THEME_WATCH:  return "Watch";
    default:           return "Legacy";
  }
}
void displayBegin() {
  Preferences p; p.begin("flashbee", false);
  currentTheme = p.getUChar("theme", THEME_LEGACY);
  p.end();
  if (currentTheme >= THEME_COUNT) currentTheme = THEME_LEGACY;
}

static uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}
static void thickArcGrad(int cx, int cy, int rIn, int rOut,
                         float startDeg, float sweepDeg, uint16_t ca, uint16_t cb) {
  if (sweepDeg <= 0) return;
  for (float a = startDeg; a <= startDeg + sweepDeg; a += 0.8f) {
    float t = (a - startDeg) / sweepDeg, r = a * PI / 180.0f, s = sinf(r), c = cosf(r);
    gfx->drawLine(cx + (int)(rIn * s), cy - (int)(rIn * c),
                  cx + (int)(rOut * s), cy - (int)(rOut * c), lerp565(ca, cb, t));
  }
}
// text "glow": dim halo offsets then bright core (size/datum already set)
static void glowString(const char *s, int x, int y, uint8_t datum, uint16_t core, uint16_t glow) {
  static const int off[4][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}};
  gfx->setTextColor(glow);
  for (int i = 0; i < 4; i++) gfxString(s, x + off[i][0], y + off[i][1], datum);
  gfx->setTextColor(core);
  gfxString(s, x, y, datum);
}
// hero text → kind: 0 number(km) · 1 ">40" · 2 OVERHEAD · 3 none/unknown
static int heroText(char *out, uint32_t now) {
  if (distExpired(now))                       { strcpy(out, "--");  return 3; }
  uint8_t d = sensor.lastDistRaw;
  if (d == DIST_OUT_OF_RANGE)                 { strcpy(out, ">40"); return 1; }
  if (d == DIST_OVERHEAD)                     { strcpy(out, "OVHD");return 2; }
  sprintf(out, "%u", d); return 0;
}
static void energyKStr(char *out) {
  if (sensor.lastEnergy > 999) sprintf(out, "%luK", (unsigned long)(sensor.lastEnergy / 1000));
  else                         sprintf(out, "%lu", (unsigned long)sensor.lastEnergy);
}
static float energyFrac() { float p = sensor.lastEnergy / 2097151.0f; return p > 1 ? 1 : p; }
static uint32_t lastTickIdx(int i) { return (sensor.tickHead - sensor.tickCount + i + MAX_TICKS) % MAX_TICKS; }

// ── Aurora: minimalist glow ────────────────────────────────────────
static void drawMainAurora(uint32_t now) {
  const int CX = LCD_CX, CY = LCD_CY, HY = 196;
  bool shelter = as3935InShelter(now);
  bool stale = sensor.strikeCount > 0 && (now - sensor.lastStrikeMs) > DIST_STALE_MS;
  gfx->fillScreen(C_BLACK);

  // breathing radial glow halo behind the number
  float br = 0.5f + 0.5f * sinf(now / 1500.0f);          // 0..1, ~9s cycle
  int r1 = 104 + (int)(18 * br), r2 = 78 + (int)(12 * br), r3 = 50 + (int)(8 * br);
  int k = 16 + (int)(26 * br);                            // intensity
  gfx->fillCircle(CX, HY, r1, shelter ? RGB(2 * k, 0, 0)     : RGB(2 * k, 3 * k / 2, 0));
  gfx->fillCircle(CX, HY, r2, shelter ? RGB(7 * k / 2, 0, 0) : RGB(7 * k / 2, 5 * k / 2, 0));
  gfx->fillCircle(CX, HY, r3, shelter ? RGB(5 * k, 6, 0)     : RGB(5 * k, 7 * k / 2, 0));

  // thin energy gradient ring (top-right sweep)
  thickArc(CX, CY, 216, 222, 0, 360, 0x18E3);
  thickArcGrad(CX, CY, 215, 223, 130, energyFrac() * 250, RGB(255, 176, 26), RGB(255, 70, 40));

  gfx->setTextSize(2); gfx->setTextColor(C_GREY);
  gfxString(shelter ? "SEEK SHELTER" : "LIGHTNING", CX, 40, D_MC);

  char h[8]; int kind = heroText(h, now);
  uint16_t core = shelter ? C_RED : (stale ? C_DIMGOLD : C_GOLD);
  uint16_t glow = shelter ? RGB(80, 0, 0) : RGB(90, 60, 0);
  gfx->setTextSize(kind == 0 ? 11 : 6);
  glowString(h, CX, HY, D_MC, core, glow);
  gfx->setTextColor(C_DIMGOLD); gfx->setTextSize(2);
  if (kind == 0 || kind == 1) gfxString("KILOMETERS", CX, HY + 70, D_MC);
  else if (kind == 2)         gfxString("< 6 KM",     CX, HY + 70, D_MC);   // overhead = very close

  char e[12]; energyKStr(e);
  char foot[28];
  if (shelter) {
    uint32_t s = (now - sensor.lastCloseStrikeMs) / 1000;
    snprintf(foot, sizeof foot, "SHELTER  %lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    gfx->setTextColor(C_RED);
  } else {
    snprintf(foot, sizeof foot, "%d strikes  -  %s", sensor.strikeCount, e);
    gfx->setTextColor(C_GREY);
  }
  gfx->setTextSize(2);
  gfxString(foot, CX, 430, D_MC);
}

// ── Neon: cyberpunk HUD ────────────────────────────────────────────
static void drawMainNeon(uint32_t now) {
  const int CX = LCD_CX, CY = LCD_CY;
  bool shelter = as3935InShelter(now);
  uint16_t neon = shelter ? RGB(255, 45, 95) : RGB(0, 234, 255);
  uint16_t neonDim = shelter ? RGB(70, 12, 28) : RGB(0, 60, 70);
  gfx->fillScreen(C_BLACK);

  for (int g = 60; g < 466; g += 40) {            // faint grid
    gfx->drawFastHLine(40, g, 386, RGB(0, 30, 34));
    gfx->drawFastVLine(g, 60, 360, RGB(0, 30, 34));
  }
  gfx->drawCircle(CX, CY, 228, neonDim);

  // glowing energy arc (wide dim behind, bright thin on top)
  thickArc(CX, CY, 196, 226, 215, 270, RGB(0, 30, 36));
  float sw = energyFrac() * 270;
  thickArc(CX, CY, 198, 224, 215, sw, neonDim);
  thickArc(CX, CY, 205, 218, 215, sw, neon);

  gfx->setTextSize(2); gfx->setTextColor(neon);
  gfxString(shelter ? "WARNING // SHELTER" : "DETECTOR // ONLINE", CX, 38, D_MC);

  char h[8]; int kind = heroText(h, now);
  gfx->setTextSize(kind == 0 ? 10 : 6);
  glowString(h, CX, 192, D_MC, RGB(234, 253, 255), neon);
  gfx->setTextColor(neon); gfx->setTextSize(3);
  if (kind == 0 || kind == 1) gfxString("KM",     CX, 252, D_MC);
  else if (kind == 2)         gfxString("< 6 KM", CX, 252, D_MC);   // overhead = very close

  // glowing history bars
  const int bx = 96, bw = 274, n = MAX_TICKS, bwidth = bw / n - 1, base = 416, maxH = 56;
  for (int i = 0; i < sensor.tickCount; i++) {
    float f = sensor.tickEnergy[lastTickIdx(i)] / 2097151.0f; if (f > 1) f = 1;
    int hh = max(3, (int)(f * maxH));
    int x = bx + i * (bwidth + 1);
    uint16_t c = (i == sensor.tickCount - 1) ? RGB(255, 45, 95) : neon;
    gfx->fillRect(x - 1, base - hh - 1, bwidth + 2, hh + 2, neonDim);   // glow
    gfx->fillRect(x, base - hh, bwidth, hh, c);
  }

  // descending scanline
  int scanY = 60 + (int)((now / 12) % 346);
  gfx->drawFastHLine(40, scanY, 386, neonDim);

  // comet orbiting the outer ring with a fading tail
  float ca = (now % 2500) / 2500.0f * 360.0f;
  for (int t = 15; t >= 0; t--) {
    float a = (ca - t * 3) * PI / 180.0f;
    uint8_t v = (uint8_t)(255 - t * 15);
    uint16_t col = shelter ? RGB(v, v / 5, v / 3) : RGB(0, v * 230 / 255, v);
    gfx->fillCircle(CX + (int)(228 * sinf(a)), CY - (int)(228 * cosf(a)), t == 0 ? 4 : 2, col);
  }
}

// ── Watch: clean Apple-Watch-style ────────────────────────────────
static void drawMainWatch(uint32_t now) {
  const int CX = LCD_CX, CY = LCD_CY;
  bool shelter = as3935InShelter(now);
  gfx->fillScreen(C_BLACK);

  for (int r = 205; r <= 217; r++) gfx->drawCircle(CX, CY, r, RGB(28, 28, 30));  // track
  // progress ring: fuller + redder the closer/stronger
  float frac = energyFrac();
  uint16_t c0 = RGB(48, 209, 88), c1 = RGB(255, 69, 58);     // green → red
  thickArcGrad(CX, CY, 205, 217, 0, frac * 330, c0, shelter ? RGB(255, 0, 0) : c1);

  // pulsing glowing bead at the progress tip
  if (frac > 0.01f) {
    float ea = frac * 330 * PI / 180.0f;
    int ex = CX + (int)(211 * sinf(ea)), ey = CY - (int)(211 * cosf(ea));
    int pr = 6 + (int)(2 * sinf(now / 250.0f));
    gfx->fillCircle(ex, ey, pr + 3, RGB(60, 60, 60));
    gfx->fillCircle(ex, ey, pr, C_WHITE);
    gfx->fillCircle(ex, ey, pr - 3, shelter ? RGB(255, 0, 0) : lerp565(c0, c1, frac));
  }

  char h[8]; int kind = heroText(h, now);
  gfx->setTextColor(shelter ? C_RED : C_WHITE);
  gfx->setTextSize(kind == 0 ? 12 : 6);
  gfxString(h, CX, 196, D_MC);
  gfx->setTextColor(RGB(142, 142, 147)); gfx->setTextSize(2);
  gfxString(kind == 0 ? "km away" : (kind == 2 ? "overhead · < 6 km" : (kind == 1 ? "km away" : "all clear")), CX, 262, D_MC);

  // bottom chips
  char e[12]; energyKStr(e);
  char a[12]; snprintf(a, sizeof a, "%d hits", sensor.strikeCount);
  auto chip = [&](int cx, const char *txt, uint16_t fg) {
    int w = strlen(txt) * 12 + 28;
    gfx->fillRoundRect(cx - w / 2, 330, w, 40, 20, RGB(28, 28, 30));
    gfx->setTextColor(fg); gfx->setTextSize(2);
    gfxString(txt, cx, 350, D_MC);
  };
  chip(160, a, C_WHITE);
  chip(306, e, C_GOLD);
}

// shared SHELTER alert (blinking banner + m:ss countdown), drawn on EVERY
// theme so the safety info is consistent.
static void shelterBanner(uint32_t now) {
  if (!as3935InShelter(now)) return;
  uint32_t s = (now - sensor.lastCloseStrikeMs) / 1000;
  char b[28];
  snprintf(b, sizeof b, "!! SHELTER %lu:%02lu !!",
           (unsigned long)(s / 60), (unsigned long)(s % 60));
  bool blink = (now / 500) & 1;
  if (blink) {
    gfx->fillRoundRect(68, 74, 330, 42, 9, C_RED);
    gfx->setTextColor(C_WHITE);
  } else {
    gfx->drawRoundRect(68, 74, 330, 42, 9, C_RED);
    gfx->setTextColor(C_RED);
  }
  gfx->setTextSize(2);
  gfxString(b, LCD_CX, 95, D_MC);
}

// expanding "ping" rings: a dim teal one on any storm activity (disturber),
// and a bright one when a real strike lands (all themes).
static void strikeRipple(uint32_t now) {
  if (sensor.lastActivityMs) {                       // activity (disturber) ping
    uint32_t da = now - sensor.lastActivityMs;
    if (da <= 700) {
      float t = da / 700.0f;
      uint8_t v = (uint8_t)((1 - t) * 150);
      gfx->drawCircle(LCD_CX, LCD_CY, (int)(t * 235), RGB(0, v, v / 2));
    }
  }
  if (sensor.strikeCount) {                           // bright strike ping
    uint32_t dt = now - sensor.lastStrikeMs;
    if (dt <= 800) {
      float t = dt / 800.0f;
      int r = (int)(t * 235);
      uint8_t v = (uint8_t)((1 - t) * 255);
      uint16_t col = as3935InShelter(now) ? RGB(v, 0, 0) : RGB(v, v, v / 2);
      gfx->drawCircle(LCD_CX, LCD_CY, r, col);
      gfx->drawCircle(LCD_CX, LCD_CY, r - 1, col);
    }
  }
}

void drawMain(uint32_t now) {
  switch (currentTheme) {
    case THEME_AURORA: drawMainAurora(now); break;
    case THEME_NEON:   drawMainNeon(now);   break;
    case THEME_WATCH:  drawMainWatch(now);  break;
    default:           drawMainLegacy(now); break;
  }
  shelterBanner(now);   // consistent, prominent SHELTER alert on every theme
  strikeRipple(now);
  gfx->flush();
}

void drawSensorLost() {
  gfx->fillScreen(C_BLACK);
  gfx->drawCircle(LCD_CX, LCD_CY, 220, C_DKGREY);
  gfx->setTextColor(C_RED); gfx->setTextSize(4);
  gfxString("SENSOR", LCD_CX, LCD_CY - 30, D_MC);
  gfxString("LOST",   LCD_CX, LCD_CY + 20, D_MC);
  gfx->setTextColor(C_GREY); gfx->setTextSize(1);
  gfxString("I2C fault - retrying", LCD_CX, LCD_CY + 70, D_MC);
  gfx->flush();
}

void drawBoot(const char *l1, const char *l2, uint16_t color) {
  gfx->fillScreen(C_BLACK);
  gfx->drawCircle(LCD_CX, LCD_CY, 220, C_DKGREY);
  gfx->setTextColor(C_GOLD); gfx->setTextSize(3);
  gfxString(l1, LCD_CX, LCD_CY - 18, D_MC);
  gfx->setTextColor(color); gfx->setTextSize(2);
  gfxString(l2, LCD_CX, LCD_CY + 24, D_MC);
  gfx->flush();
}

// ── settings screen (466) ──────────────────────────────────────────
// tap zones (x0,y0,x1,y1)
#define Z_INDOOR   96, 86, 229, 118
#define Z_OUTDOOR  237, 86, 370, 118
#define Z_SENS     96, 122, 370, 154
#define Z_SOUND    96, 158, 370, 190
#define Z_THEME    96, 194, 370, 226
#define Z_SCREEN   96, 230, 370, 262
#define Z_SLEEP    96, 266, 370, 298
#define Z_RESET    96, 302, 184, 334
#define Z_TUNE     190, 302, 278, 334
#define Z_TEST     284, 302, 372, 334
static bool inZone(int16_t x, int16_t y, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}
static void pill(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                 const char *label, bool active) {
  uint16_t bg = active ? C_TEAL : C_DKGREY;
  uint16_t fg = active ? C_BLACK : C_GREY;
  gfx->fillRoundRect(x0, y0, x1 - x0, y1 - y0, 10, bg);
  gfx->setTextColor(fg); gfx->setTextSize(2);
  gfxString(label, (x0 + x1) / 2, (y0 + y1) / 2, D_MC);
}
static void tile(int16_t x0, int16_t y0, int16_t x1, int16_t y1, const char *text) {
  gfx->drawRoundRect(x0, y0, x1 - x0, y1 - y0, 10, C_DKGREY);
  gfx->setTextColor(C_WHITE); gfx->setTextSize(2);
  gfxString(text, (x0 + x1) / 2, (y0 + y1) / 2, D_MC);
}

void drawSettings(uint32_t now) {
  (void)now;
  gfx->fillScreen(C_BLACK);
  gfx->setTextColor(C_GOLD); gfx->setTextSize(3);
  gfxString("SETTINGS", LCD_CX, 58, D_MC);

  pill(Z_INDOOR,  "INDOOR",  !sensor.outdoorMode);
  pill(Z_OUTDOOR, "OUTDOOR",  sensor.outdoorMode);

  char buf[40];
  snprintf(buf, sizeof buf, "SENS  %s", as3935ProfileName(sensProfile));
  tile(Z_SENS, buf);
  snprintf(buf, sizeof buf, "SOUND  %s", audioVolumeLabel());
  tile(Z_SOUND, buf);
  snprintf(buf, sizeof buf, "THEME  %s", themeName(currentTheme));
  tile(Z_THEME, buf);
  snprintf(buf, sizeof buf, "SCREEN  %s", powerScreenLabel());
  tile(Z_SCREEN, buf);
  snprintf(buf, sizeof buf, "SLEEP  %s", powerSleepLabel());
  tile(Z_SLEEP, buf);
  tile(Z_RESET, "RESET");
  tile(Z_TUNE,  "TUNE");
  tile(Z_TEST,  "TEST");

  // footer: battery + filter state
  gfx->setTextColor(C_GREY); gfx->setTextSize(2);
  if (powerBatteryPresent()) {
    snprintf(buf, sizeof buf, "BAT %.2f V  %u%%",
             powerBatteryMv() / 1000.0, powerBatteryPct());
  } else {
    strcpy(buf, "BAT --");
  }
  gfxString(buf, LCD_CX, 356, D_MC);
  snprintf(buf, sizeof buf, "NF %u   WD %u   SR %u",
           sensor.noiseFloor, sensor.watchdogLvl, sensor.spikeRej);
  gfxString(buf, LCD_CX, 384, D_MC);

  gfx->flush();
}

void uiSwipe() {
  currentScreen = (currentScreen == SCREEN_MAIN) ? SCREEN_SETTINGS : SCREEN_MAIN;
}

// ── antenna-tune screens (466) ─────────────────────────────────────
void drawTuneProgress(uint8_t cap, uint32_t hz, int32_t dev, uint32_t expHz, uint32_t tolHz) {
  gfx->fillScreen(C_BLACK);
  gfx->drawCircle(LCD_CX, LCD_CY, 220, C_DKGREY);
  gfx->setTextColor(C_GOLD); gfx->setTextSize(3);
  gfxString("ANTENNA TUNE", LCD_CX, 70, D_MC);

  char buf[28];
  gfx->setTextColor(C_GREY); gfx->setTextSize(2);
  snprintf(buf, sizeof buf, "TUN_CAP %u / 15", cap);
  gfxString(buf, LCD_CX, 150, D_MC);

  bool inTol = (dev < 0 ? -dev : dev) <= (int32_t)tolHz;
  gfx->setTextColor(inTol ? C_GREEN : C_ORANGE); gfx->setTextSize(6);
  snprintf(buf, sizeof buf, "%lu", (unsigned long)hz);
  gfxString(buf, LCD_CX, 210, D_MC);
  gfx->setTextColor(C_GREY); gfx->setTextSize(2);
  gfxString("Hz", LCD_CX, 250, D_MC);
  snprintf(buf, sizeof buf, "target %lu +-%lu", (unsigned long)expHz, (unsigned long)tolHz);
  gfxString(buf, LCD_CX, 290, D_MC);

  int bx = 93, by = 330, bw = 280, bh = 22;
  gfx->drawRoundRect(bx - 3, by - 3, bw + 6, bh + 6, 4, C_DIM);
  gfx->fillRect(bx, by, (cap + 1) * bw / 16, bh, C_GOLD);
  gfx->flush();
}

void drawTuneResult(bool ok, uint8_t cap, uint32_t hz) {
  gfx->fillScreen(C_BLACK);
  gfx->drawCircle(LCD_CX, LCD_CY, 220, C_DKGREY);
  gfx->setTextColor(ok ? C_GREEN : C_RED); gfx->setTextSize(4);
  gfxString(ok ? "TUNED" : "OUT OF RANGE", LCD_CX, 90, D_MC);
  char buf[28];
  gfx->setTextColor(C_GOLD); gfx->setTextSize(5);
  snprintf(buf, sizeof buf, "CAP %u", cap);
  gfxString(buf, LCD_CX, 200, D_MC);
  gfx->setTextColor(C_GREY); gfx->setTextSize(2);
  snprintf(buf, sizeof buf, "%lu Hz", (unsigned long)hz);
  gfxString(buf, LCD_CX, 280, D_MC);
  gfxString(ok ? "saved to NVS" : "not saved", LCD_CX, 330, D_MC);
  gfx->flush();
}

void drawTuneError(const char *l1, const char *l2) {
  gfx->fillScreen(C_BLACK);
  gfx->drawCircle(LCD_CX, LCD_CY, 220, C_DKGREY);
  gfx->setTextColor(C_RED); gfx->setTextSize(4);
  gfxString(l1, LCD_CX, LCD_CY - 24, D_MC);
  gfx->setTextColor(C_GREY); gfx->setTextSize(2);
  gfxString(l2, LCD_CX, LCD_CY + 24, D_MC);
  gfx->flush();
}

void uiHandleTap(int16_t x, int16_t y) {
  if (inZone(x, y, Z_INDOOR))       as3935SetMode(false);
  else if (inZone(x, y, Z_OUTDOOR)) as3935SetMode(true);
  else if (inZone(x, y, Z_SENS))    as3935SetProfile((sensProfile + 1) % PROFILE_COUNT);
  else if (inZone(x, y, Z_SOUND))   audioCycleVolume();
  else if (inZone(x, y, Z_THEME)) {
    currentTheme = (currentTheme + 1) % THEME_COUNT;
    Preferences p; p.begin("flashbee", false); p.putUChar("theme", currentTheme); p.end();
  }
  else if (inZone(x, y, Z_SCREEN))  powerCycleScreenTimeout();
  else if (inZone(x, y, Z_SLEEP))   powerCycleSleepTimeout();
  else if (inZone(x, y, Z_RESET))   as3935ResetFilters();
  else if (inZone(x, y, Z_TUNE))  { as3935RunTune(); currentScreen = SCREEN_MAIN; }
  else if (inZone(x, y, Z_TEST))  { as3935InjectTest(random(2, 35), random(20000, 800000));
                                    currentScreen = SCREEN_MAIN; }   // sim strike, jump to main to see it
}
