// ─────────────────────────────────────────────────────────────────
// Flash Bee — I²C bus scanner (ESP32-S3, AS3935 bring-up diagnostic)
//
//   pio run -e s3scan -t upload && pio device monitor -b 115200
//
// Scans the whole 0x01..0x7E range on the GPIO17(SDA)/GPIO18(SCL)
// bus and prints every address that ACKs. Tells us definitively:
//   • nothing found        → SDA/SCL not making contact, or no pull-ups
//   • found at 0x03        → AS3935 in I²C mode, address correct
//   • found at other addr  → A0/A1 strap wrong
// ─────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>

#define PIN_SDA 17
#define PIN_SCL 18

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\r\n=== Flash Bee I2C scanner ===");
  Serial.printf("SDA=GPIO%d  SCL=GPIO%d  @100kHz\r\n", PIN_SDA, PIN_SCL);
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  Wire.setTimeOut(50);
}

void loop() {
  Serial.println("scanning 0x01..0x7E …");
  int found = 0;
  for (uint8_t addr = 1; addr < 0x7F; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("  ✓ device at 0x%02X%s\r\n", addr,
                    addr == 0x03 ? "   ← AS3935!" : "");
      found++;
    }
  }
  if (found == 0)
    Serial.println("  ✗ nothing on the bus — check SDA→MOSI / SCL contact, "
                   "GND, and SI=high (I2C select)");
  else
    Serial.printf("  %d device(s) found\r\n", found);
  Serial.println();
  delay(3000);
}
