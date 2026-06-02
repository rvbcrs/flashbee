#include "fb_gfx.h"
#include "fb_config.h"

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RST, 0, LCD_W, LCD_H, 6, 0, 0, 0);
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_W, LCD_H, panel);

bool gfxInit() {
  bool ok = gfx->begin();
  panel->setBrightness(255);
  gfx->fillScreen(C_BLACK);
  gfx->flush();
  return ok;
}

void gfxString(const char *s, int16_t x, int16_t y, uint8_t datum) {
  int16_t bx, by; uint16_t bw, bh;
  gfx->getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  int16_t cx, cy;
  if      (datum == D_TC || datum == D_MC || datum == D_BC) cx = x - bw / 2 - bx;
  else if (datum == D_TR || datum == D_MR || datum == D_BR) cx = x - bw - bx;
  else                                                      cx = x - bx;
  if      (datum == D_ML || datum == D_MC || datum == D_MR) cy = y - bh / 2 - by;
  else if (datum == D_BL || datum == D_BC || datum == D_BR) cy = y - bh - by;
  else                                                      cy = y - by;
  gfx->setCursor(cx, cy);
  gfx->print(s);
}
void gfxString(const String &s, int16_t x, int16_t y, uint8_t datum) {
  gfxString(s.c_str(), x, y, datum);
}

uint16_t energyColor(uint32_t e) {
  if      (e > 500000) return C_WHITE;
  else if (e > 150000) return C_GOLD;
  else if (e > 75000)  return C_ORANGE;
  else if (e > 20000)  return C_REDORG;
  else                 return C_DIM;
}

int energyPercent(uint32_t e) {
  uint32_t pct = (uint32_t)((uint64_t)e * 100 / 2097151UL);  // 21-bit word → %
  return pct > 100 ? 100 : (int)pct;
}
