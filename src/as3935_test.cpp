// ─────────────────────────────────────────────────────────────────
// Flash Bee — STANDALONE AS3935 serial bring-up test (ESP32-S3)
//
// Purpose: validate the AS3935 wiring + detect a REAL storm RIGHT NOW,
// with no display/touch. Strikes, disturbers and noise events are
// printed to the serial monitor at 115200 baud.
//
// Build/flash:  pio run -e s3test -t upload && pio device monitor -b 115200
//
// Wiring (WCMCU-3935 breakout → Waveshare ESP32-S3-Touch-AMOLED-1.75
// 8-pin header). I²C on a dedicated Wire1 bus so it never touches the
// onboard sensor bus:
//     VCC  → 3.3V        SI   → 3.3V   (selects I²C)
//     GND  → G           A0   → 3.3V   ┐ address 0x03
//     MOSI → GPIO17 (SDA) A1  → 3.3V   ┘
//     SCL  → GPIO18      CS   → GND
//     IRQ  → GPIO16      MISO → GND
//                        EN-V → 3.3V   (internal regulator on)
//
// The register logic here is copied verbatim from the project's
// datasheet-correct main.ino init sequence (§8.10/§8.11). Antenna
// auto-tune is skipped — distance is then biased by the untuned LC
// tank, but strike DETECTION is fully functional, which is what a
// live-storm test needs.
// ─────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>

// ── pins ───────────────────────────────────────────────────────────
#define PIN_SDA   17
#define PIN_SCL   18
#define PIN_INT   16

// ── AS3935 register map (datasheet §8.10) ──────────────────────────
#define AS3935_I2C_ADDR     0x03
#define REG_AFE_GAIN        0x00
  #define AFE_GB_OUTDOOR    (0b01110 << 1)   // 0x1C
  #define AFE_GB_INDOOR     (0b10010 << 1)   // 0x24
  #define PWD_BIT           0x01
  #define AFE_GB_FIELD_MASK (0x1F << 1)
#define REG_NF_WDG          0x01
  #define NF_MASK           (0x07 << 4)
  #define WDTH_MASK         0x0F
#define REG_CLSTAT_SREJ     0x02
  #define MIN_NUM_LIGH_MASK (0x03 << 4)
  #define SREJ_MASK         0x0F
#define REG_LCO_INT         0x03
  #define MASK_DIST_BIT     (1 << 5)
  #define INT_MASK          0x0F
  #define INT_NH            0x01
  #define INT_D             0x04
  #define INT_L             0x08
#define REG_ENERGY_LSB      0x04
#define REG_ENERGY_MID      0x05
#define REG_ENERGY_MSB      0x06
#define REG_DISTANCE        0x07
  #define DIST_FIELD_MASK   0x3F
  #define DIST_OUT_OF_RANGE 0x3F
  #define DIST_OVERHEAD     0x01
  #define DIST_UNKNOWN      0x00
#define REG_TUN_CAP         0x08
  #define TUN_CAP_MASK      0x0F
  #define DISP_TRCO_BIT     (1 << 5)
#define REG_TRCO_CAL        0x3A
#define REG_SRCO_CAL        0x3B
  #define CALIB_DONE_BIT    (1 << 7)
  #define CALIB_NOK_BIT     (1 << 6)
#define REG_PRESET_DEFAULT  0x3C
#define REG_CALIB_RCO       0x3D
#define DIRECT_CMD_VALUE    0x96

#define CLOSE_STRIKE_KM     10
#define NF_DEFAULT          2
#define WDTH_DEFAULT       1
#define SREJ_DEFAULT       0

// ── state ──────────────────────────────────────────────────────────
TwoWire &I2C = Wire;                      // proven-good bus (port 0) on GPIO17/18
volatile bool irqPending = false;
void IRAM_ATTR irqHandler() { irqPending = true; }
bool     sensorOk = false;
uint8_t  failStreak = 0;
int      strikeCount = 0;
uint32_t lastIrqMs = 0;

// ── I²C helpers ────────────────────────────────────────────────────
bool i2cRead(uint8_t reg, uint8_t &out) {
  I2C.beginTransmission(AS3935_I2C_ADDR);
  I2C.write(reg);
  if (I2C.endTransmission(false) != 0) { failStreak++; return false; }
  if (I2C.requestFrom((uint8_t)AS3935_I2C_ADDR, (uint8_t)1) != 1) { failStreak++; return false; }
  out = I2C.read(); failStreak = 0; return true;
}
uint8_t readReg(uint8_t reg) { uint8_t v = 0xFF; i2cRead(reg, v); return v; }
bool writeReg(uint8_t reg, uint8_t val) {
  I2C.beginTransmission(AS3935_I2C_ADDR);
  I2C.write(reg); I2C.write(val);
  if (I2C.endTransmission(true) != 0) { failStreak++; return false; }
  failStreak = 0; return true;
}
bool maskWrite(uint8_t reg, uint8_t mask, uint8_t val) {
  uint8_t cur; if (!i2cRead(reg, cur)) return false;
  return writeReg(reg, (cur & ~mask) | (val & mask));
}
uint32_t readEnergy() {
  return ((uint32_t)(readReg(REG_ENERGY_MSB) & 0x1F) << 16)
       | ((uint32_t) readReg(REG_ENERGY_MID) << 8)
       |             readReg(REG_ENERGY_LSB);
}

// ── init (datasheet §8.11) ─────────────────────────────────────────
bool initAS3935() {
  if (!writeReg(REG_PRESET_DEFAULT, DIRECT_CMD_VALUE)) return false;
  delay(3);
  if (!writeReg(REG_CALIB_RCO, DIRECT_CMD_VALUE)) return false;
  maskWrite(REG_TUN_CAP, DISP_TRCO_BIT, DISP_TRCO_BIT);
  delay(3);
  maskWrite(REG_TUN_CAP, DISP_TRCO_BIT, 0);

  uint8_t trco = readReg(REG_TRCO_CAL), srco = readReg(REG_SRCO_CAL);
  if (!(trco & CALIB_DONE_BIT) || (trco & CALIB_NOK_BIT)) {
    Serial.printf("[CAL] TRCO fail 0x%02X\r\n", trco); return false;
  }
  if (!(srco & CALIB_DONE_BIT) || (srco & CALIB_NOK_BIT)) {
    Serial.printf("[CAL] SRCO fail 0x%02X\r\n", srco); return false;
  }
  // outdoor gain (handheld) + power up
  if (!maskWrite(REG_AFE_GAIN, AFE_GB_FIELD_MASK | PWD_BIT, AFE_GB_OUTDOOR)) return false;
  // Sensitive outdoor storm profile: single-strike, low WDTH/SREJ.
  if (!writeReg(REG_NF_WDG, (NF_DEFAULT << 4) | WDTH_DEFAULT)) return false;
  if (!maskWrite(REG_CLSTAT_SREJ, MIN_NUM_LIGH_MASK | SREJ_MASK,
                 (0 << 4) | SREJ_DEFAULT)) return false;
  // DIAGNOSTIC: do NOT mask disturbers — we want to see every energy event
  // so we can tell whether the chip senses the storm at all.
  maskWrite(REG_LCO_INT, MASK_DIST_BIT, 0);

  pinMode(PIN_INT, INPUT);
  uint8_t discard; i2cRead(REG_LCO_INT, discard);          // clear latched event
  detachInterrupt(digitalPinToInterrupt(PIN_INT));
  attachInterrupt(digitalPinToInterrupt(PIN_INT), irqHandler, RISING);
  if (digitalRead(PIN_INT) == HIGH) irqPending = true;
  return true;
}

const char* distanceStr(uint8_t d, char* buf) {
  if (d == DIST_OUT_OF_RANGE) return ">40 km (out of range)";
  if (d == DIST_OVERHEAD)     return "OVERHEAD (<5 km)";
  if (d == DIST_UNKNOWN)      return "-- unknown";
  sprintf(buf, "~%u km", d);   // register value == km for in-range bins
  return buf;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\r\n=== Flash Bee AS3935 serial test (ESP32-S3) ===");
  Serial.printf("I2C: SDA=%d SCL=%d  INT=%d  addr=0x%02X\r\n", PIN_SDA, PIN_SCL, PIN_INT, AS3935_I2C_ADDR);

  I2C.begin(PIN_SDA, PIN_SCL, 100000);
  I2C.setTimeOut(50);

  // quick presence scan
  I2C.beginTransmission(AS3935_I2C_ADDR);
  if (I2C.endTransmission() == 0) Serial.println("[scan] device ACKed at 0x03  ✓");
  else Serial.println("[scan] NO ACK at 0x03 — check SI/A0/A1 (all 3.3V) and SDA=MOSI wiring");

  sensorOk = initAS3935();
  Serial.println(sensorOk ? "[init] AS3935 ready — listening for strikes…"
                          : "[init] FAILED — will retry every 3s");
  lastIrqMs = millis();
}

void loop() {
  uint32_t now = millis();

  if (!sensorOk) {
    static uint32_t nextRetry = 0;
    if ((int32_t)(now - nextRetry) >= 0) {
      sensorOk = initAS3935();
      nextRetry = now + 3000;
      if (sensorOk) { Serial.println("[AS3935] recovered"); lastIrqMs = now; }
    }
    delay(80);
    return;
  }

  // rescue level-latched INT that the edge ISR missed
  static uint32_t safeMs = 0;
  if (!irqPending && (now - safeMs) > 500) {
    safeMs = now;
    if (digitalRead(PIN_INT) == HIGH) irqPending = true;
  }

  if (irqPending) {
    irqPending = false;
    uint8_t intReg;
    if (!i2cRead(REG_LCO_INT, intReg)) {
      if (failStreak >= 8) { Serial.println("[I2C] sensor lost"); sensorOk = false; }
    } else {
      uint8_t reason = intReg & INT_MASK;
      if (reason) lastIrqMs = now;
      if (reason == INT_NH) {
        Serial.println("[NH] noise floor too high (EMI / indoor source)");
      } else if (reason == INT_D) {
        Serial.println("[D] disturber rejected");
      } else if (reason == INT_L) {
        uint32_t e = readEnergy();
        uint8_t  d = readReg(REG_DISTANCE) & DIST_FIELD_MASK;
        char buf[16];
        bool close = (d == DIST_OVERHEAD) || (d >= 5 && d <= CLOSE_STRIKE_KM);
        strikeCount++;
        Serial.printf("⚡ STRIKE #%d  dist=%s  energy=%lu%s\r\n",
                      strikeCount, distanceStr(d, buf), (unsigned long)e,
                      close ? "   ⚠⚠ CLOSE — SEEK SHELTER" : "");
      }
    }
  }

  // heartbeat so you know it's alive while waiting for the storm
  static uint32_t beatMs = 0;
  if (now - beatMs > 10000) {
    beatMs = now;
    Serial.printf("[alive] %lus  strikes=%d\r\n", now / 1000, strikeCount);
  }
  delay(20);
}
