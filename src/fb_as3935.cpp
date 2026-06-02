#include "fb_as3935.h"
#include "fb_config.h"
#include "fb_display.h"
#include <Wire.h>
#include <Preferences.h>
#include <string.h>

// ── antenna-tune (LCO) constants ───────────────────────────────────
#define LCO_EXPECTED_HZ   (500000UL >> 7)   // 500 kHz / 128 ≈ 3906
#define LCO_TOL_HZ        ((LCO_EXPECTED_HZ * 35) / 1000)
#define TUNE_WINDOW_MS    200
#define TUNE_SETTLE_MS    60

// ── register map (datasheet §8.10) ─────────────────────────────────
#define AS3935_I2C_ADDR     0x03
#define REG_AFE_GAIN        0x00
  #define AFE_GB_OUTDOOR    (0b01110 << 1)
  #define AFE_GB_INDOOR     (0b10010 << 1)
  #define PWD_BIT           0x01
  #define AFE_GB_FIELD_MASK (0x1F << 1)
#define REG_NF_WDG          0x01
  #define NF_MASK           (0x07 << 4)
  #define WDTH_MASK         0x0F
#define REG_CLSTAT_SREJ     0x02
  #define CL_STAT_BIT       (1 << 6)
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

#define I2C_FAIL_LIMIT      8
#define SENSE_EASE_MS       15000UL
#define NF_DECAY_MS         60000UL
#define SENSOR_WDT_MS       600000UL
#define DANGER_WINDOW_MS    (30UL * 60UL * 1000UL)
#define FILTER_FLOOR        2
#define NOISE_FLOOR_FLOOR   2
#define MIN_STRIKE_ENERGY   1     // a validated strike with energy < this is spurious (EMI), dropped
#define NF_DEFAULT          2
#define WDTH_DEFAULT       1
#define SREJ_DEFAULT       0

SensorState sensor;

static uint8_t  failStreak = 0;
static uint32_t senseLastAdj = 0, nfLastDecay = 0, lastIrqMs = 0;
static uint8_t  activeTunCap = 0;
// Freeze the auto-tighten/loosen + noise-floor adaption (upstream's Stormchase
// "pin"): keep wd/sr/nf fixed at the max-sensitivity storm values so a
// disturber/noise flood can't ratchet the chip deaf and miss real strikes.
static bool     s_pinned = true;
static volatile bool irqPending = false;
static void IRAM_ATTR irqHandler() { irqPending = true; }
static volatile uint32_t lcoEdges = 0;
static void IRAM_ATTR lcoISR() { lcoEdges++; }

// ── I²C helpers ────────────────────────────────────────────────────
// A storm's disturber flood occasionally trips the ESP32 I²C-NG driver
// (ESP_ERR_INVALID_STATE), but it's transient: failStreak resets on the
// next good read, and only 8-in-a-row marks the sensor lost. So no special
// bus recovery is needed — keep it simple.
static bool i2cRead(uint8_t reg, uint8_t &out) {
  Wire.beginTransmission(AS3935_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) { failStreak++; return false; }
  if (Wire.requestFrom((uint8_t)AS3935_I2C_ADDR, (uint8_t)1) != 1) { failStreak++; return false; }
  out = Wire.read(); failStreak = 0; return true;
}
static uint8_t readReg(uint8_t reg) { uint8_t v = 0xFF; i2cRead(reg, v); return v; }
static bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(AS3935_I2C_ADDR);
  Wire.write(reg); Wire.write(val);
  if (Wire.endTransmission(true) != 0) { failStreak++; return false; }
  failStreak = 0; return true;
}
static bool maskWrite(uint8_t reg, uint8_t mask, uint8_t val) {
  uint8_t cur; if (!i2cRead(reg, cur)) return false;
  return writeReg(reg, (cur & ~mask) | (val & mask));
}
static uint32_t readEnergy() {
  return ((uint32_t)(readReg(REG_ENERGY_MSB) & 0x1F) << 16)
       | ((uint32_t) readReg(REG_ENERGY_MID) << 8)
       |             readReg(REG_ENERGY_LSB);
}
static void pushTick(uint32_t e) {
  sensor.tickEnergy[sensor.tickHead] = e;
  sensor.tickHead = (sensor.tickHead + 1) % MAX_TICKS;
  if (sensor.tickCount < MAX_TICKS) sensor.tickCount++;
}

// ── filter adjustments (datasheet §8.4) ────────────────────────────
static void applyWatchdogSpike() {
  maskWrite(REG_NF_WDG, WDTH_MASK, sensor.watchdogLvl & WDTH_MASK);
  maskWrite(REG_CLSTAT_SREJ, SREJ_MASK, sensor.spikeRej & SREJ_MASK);
}
static void loosenFilters() {
  if (s_pinned) return;
  if (sensor.spikeRej > sensor.watchdogLvl) { if (sensor.spikeRej > FILTER_FLOOR) sensor.spikeRej--; }
  else                                      { if (sensor.watchdogLvl > FILTER_FLOOR) sensor.watchdogLvl--; }
  applyWatchdogSpike();
}
static void raiseNoiseFloor() {
  if (s_pinned) return;
  if (sensor.noiseFloor < 7) {
    sensor.noiseFloor++;
    maskWrite(REG_NF_WDG, NF_MASK, (sensor.noiseFloor << 4) & NF_MASK);
    sensor.noiseFault = false;
  } else sensor.noiseFault = true;
}
static void lowerNoiseFloor() {
  if (s_pinned) return;
  if (sensor.noiseFloor > NOISE_FLOOR_FLOOR) {
    sensor.noiseFloor--;
    maskWrite(REG_NF_WDG, NF_MASK, (sensor.noiseFloor << 4) & NF_MASK);
  }
  sensor.noiseFault = false;
}

// ── sensitivity profiles ───────────────────────────────────────────
// One tap-selectable preset bundles watchdog/spike/noise-floor + disturber
// mask + the auto-adapt "pin". Ported from upstream haklein/flashbee's
// Station/Portable/Stormchase profiles, exposed as a manual choice.
struct SensProfileDef { const char *name; uint8_t wd, sr, nf, mnl; bool mask, pin; };
static const SensProfileDef PROFILES[PROFILE_COUNT] = {
  // name      wd sr nf mnl mask   pin
  { "STORM",    1, 0, 2, 0, false, true  },  // max sensitivity, frozen, activity counted
  { "NORMAL",   2, 2, 2, 0, true,  false },  // balanced, adaptive, disturbers hidden
  { "NOISY",    5, 3, 3, 0, true,  false },  // reject interference (bench / near electronics)
};
uint8_t sensProfile = PROFILE_STORM;

const char *as3935ProfileName(uint8_t p) { return PROFILES[p % PROFILE_COUNT].name; }

// Push the current profile's knobs into the chip + runtime state.
static void applyProfile() {
  const SensProfileDef &k = PROFILES[sensProfile % PROFILE_COUNT];
  sensor.noiseFloor  = k.nf;
  sensor.watchdogLvl = k.wd;
  sensor.spikeRej    = k.sr;
  s_pinned           = k.pin;
  writeReg(REG_NF_WDG, (k.nf << 4) | (k.wd & WDTH_MASK));
  maskWrite(REG_CLSTAT_SREJ, MIN_NUM_LIGH_MASK | SREJ_MASK, (k.mnl << 4) | (k.sr & SREJ_MASK));
  maskWrite(REG_LCO_INT, MASK_DIST_BIT, k.mask ? MASK_DIST_BIT : 0);
}

// ── public API ─────────────────────────────────────────────────────
static void loadPrefs();
void as3935Begin() {
  loadPrefs();
  Wire.begin(AS_SDA, AS_SCL, 100000);
  Wire.setTimeOut(50);
  pinMode(AS3935_INT_PIN, INPUT);
}

bool as3935Init() {
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
  uint8_t gain = sensor.outdoorMode ? AFE_GB_OUTDOOR : AFE_GB_INDOOR;
  if (!maskWrite(REG_AFE_GAIN, AFE_GB_FIELD_MASK | PWD_BIT, gain)) return false;
  maskWrite(REG_TUN_CAP, TUN_CAP_MASK, activeTunCap & TUN_CAP_MASK);  // antenna trim from NVS
  applyProfile();   // NF/WDTH/SREJ/MIN_NUM_LIGH/MASK_DIST + pin from the chosen profile

  uint8_t discard; i2cRead(REG_LCO_INT, discard);   // clear latched event
  static bool isrAttached = false;
  if (isrAttached) detachInterrupt(digitalPinToInterrupt(AS3935_INT_PIN));
  attachInterrupt(digitalPinToInterrupt(AS3935_INT_PIN), irqHandler, RISING);
  isrAttached = true;
  if (digitalRead(AS3935_INT_PIN) == HIGH) irqPending = true;
  sensor.sensorOk = true;
  lastIrqMs = millis();
  return true;
}

bool as3935InShelter(uint32_t now) {
  return sensor.lastCloseStrikeMs != 0 &&
         (now - sensor.lastCloseStrikeMs) < DANGER_WINDOW_MS;
}

bool as3935Service(uint32_t now) {
  bool newStrike = false;

  if (!sensor.sensorOk) {
    static uint32_t nextRetry = 0;
    if ((int32_t)(now - nextRetry) >= 0) {
      sensor.sensorOk = as3935Init();
      nextRetry = now + 3000;
      if (sensor.sensorOk) Serial.println("[AS3935] recovered");
    }
    return false;
  }

  // rescue level-latched INT the edge ISR missed
  static uint32_t safeMs = 0;
  if (!irqPending && (now - safeMs) > 500) {
    safeMs = now;
    if (digitalRead(AS3935_INT_PIN) == HIGH) irqPending = true;
  }

  if (irqPending) {
    irqPending = false;
    uint8_t intReg;
    if (!i2cRead(REG_LCO_INT, intReg)) {
      if (failStreak >= I2C_FAIL_LIMIT) { Serial.println("[I2C] sensor lost"); sensor.sensorOk = false; }
    } else {
      uint8_t reason = intReg & INT_MASK;
      if (reason) lastIrqMs = now;
      if (reason == INT_NH) {
        raiseNoiseFloor(); senseLastAdj = now; nfLastDecay = now;
      } else if (reason == INT_D) {
        // Storm activity (distant/in-cloud) AND man-made noise both land here.
        // The original storm logs showed that tightening WD/SR on every
        // disturber can suppress later distant INT_L events, so this branch is
        // visibility-only: count it, show activity, but do not touch filters.
        sensor.disturberCount++;
        sensor.lastActivityMs = now;
        Serial.printf("[D] disturber (activity)  wd=%u sr=%u nf=%u\r\n",
                      sensor.watchdogLvl, sensor.spikeRej, sensor.noiseFloor);
      } else if (reason == INT_L) {
        uint32_t e = readEnergy();
        uint8_t  d = readReg(REG_DISTANCE) & DIST_FIELD_MASK;
        // A validated strike with ~zero energy is spurious (local EMI, e.g. the
        // I2S/audio or USB), not lightning. Drop it so it never counts/flashes.
        if (e < MIN_STRIKE_ENERGY) {
          sensor.spuriousCount++;
          Serial.printf("[L] spurious ignored  e=%lu d=0x%02X (total %d)\r\n",
                        (unsigned long)e, d, sensor.spuriousCount);
        } else {
        sensor.strikeCount++;
        sensor.lastEnergy = e;
        sensor.lastDistRaw = d;
        sensor.lastStrikeMs = now;
        sensor.lastActivityMs = now;
        if (e > sensor.maxEnergy) sensor.maxEnergy = e;
        pushTick(e);
        bool close = (d == DIST_OVERHEAD) || (d >= 1 && d <= CLOSE_STRIKE_KM);
        if (close) sensor.lastCloseStrikeMs = now;
        Serial.printf("⚡ STRIKE #%d d=0x%02X e=%lu%s\r\n",
                      sensor.strikeCount, d, (unsigned long)e, close ? " [CLOSE]" : "");
        newStrike = true;
        }
      }
    }
  }

  if (now - senseLastAdj > SENSE_EASE_MS) { senseLastAdj = now; loosenFilters(); }
  if (now - nfLastDecay  > NF_DECAY_MS)   { nfLastDecay  = now; lowerNoiseFloor(); }
  if (now - lastIrqMs    > SENSOR_WDT_MS) {
    Serial.println("[WDT] re-init"); sensor.sensorOk = as3935Init(); lastIrqMs = now;
  }
  return newStrike;
}

// ── settings hooks ─────────────────────────────────────────────────
void as3935SetMode(bool outdoor) {
  sensor.outdoorMode = outdoor;
  if (sensor.sensorOk)
    maskWrite(REG_AFE_GAIN, AFE_GB_FIELD_MASK | PWD_BIT,
              outdoor ? AFE_GB_OUTDOOR : AFE_GB_INDOOR);
  Preferences p; p.begin("flashbee", false);
  p.putUChar("afe", outdoor ? 1 : 0); p.end();
  Serial.printf("[afe] -> %s\r\n", outdoor ? "OUTDOOR" : "INDOOR");
}

void as3935SetProfile(uint8_t prof) {
  sensProfile = prof % PROFILE_COUNT;
  if (sensor.sensorOk) applyProfile();
  Preferences p; p.begin("flashbee", false);
  p.putUChar("profile", sensProfile); p.end();
  const SensProfileDef &k = PROFILES[sensProfile];
  Serial.printf("[profile] -> %s (wd=%u sr=%u nf=%u mask=%u pin=%u)\r\n",
                k.name, k.wd, k.sr, k.nf, k.mask ? 1 : 0, k.pin ? 1 : 0);
}

void as3935ResetFilters() {
  if (sensor.sensorOk) {
    applyProfile();                       // back to the current profile's knobs
    maskWrite(REG_CLSTAT_SREJ, CL_STAT_BIT, CL_STAT_BIT); delay(2);
    maskWrite(REG_CLSTAT_SREJ, CL_STAT_BIT, 0);           delay(2);
    maskWrite(REG_CLSTAT_SREJ, CL_STAT_BIT, CL_STAT_BIT);
  }
  sensor.noiseFault = false;
  sensor.strikeCount = 0; sensor.lastEnergy = 0; sensor.maxEnergy = 0;
  sensor.lastDistRaw = DIST_UNKNOWN; sensor.lastStrikeMs = 0;
  sensor.lastActivityMs = 0; sensor.disturberCount = 0; sensor.spuriousCount = 0;
  sensor.tickHead = 0; sensor.tickCount = 0;
  memset(sensor.tickEnergy, 0, sizeof(sensor.tickEnergy));
  Serial.println("[filters] reset");
}

void as3935InjectTest(uint8_t km, uint32_t energy) {
  uint32_t now = millis();
  sensor.strikeCount++;
  sensor.lastEnergy = energy;
  sensor.lastDistRaw = km;
  sensor.lastStrikeMs = now;
  sensor.lastActivityMs = now;
  if (energy > sensor.maxEnergy) sensor.maxEnergy = energy;
  pushTick(energy);
  bool close = (km == DIST_OVERHEAD) || (km >= 1 && km <= CLOSE_STRIKE_KM);
  if (close) sensor.lastCloseStrikeMs = now;
  Serial.printf("⚡ TEST strike d=%u e=%lu%s\r\n", km, (unsigned long)energy,
                close ? " [CLOSE → SHELTER]" : "");
}

bool as3935ClearLatched() {
  uint8_t tmp; return i2cRead(REG_LCO_INT, tmp);
}
void as3935ForcePending()  { irqPending = true; }
void as3935ReattachIrq() {
  attachInterrupt(digitalPinToInterrupt(AS3935_INT_PIN), irqHandler, RISING);
}

void as3935PrintStatus() {
  uint8_t r0 = 0, r1 = 0, r2 = 0, r3 = 0, r7 = 0, r8 = 0;
  i2cRead(0x00, r0); i2cRead(0x01, r1); i2cRead(0x02, r2);
  i2cRead(0x03, r3); i2cRead(0x07, r7); i2cRead(0x08, r8);
  Serial.printf("[stat] AFE=0x%02X NF_WDG=0x%02X CLSTAT_SREJ=0x%02X "
                "LCO_INT=0x%02X DIST=0x%02X TUN=0x%02X\r\n",
                r0, r1, r2, r3, r7, r8);
  Serial.printf("[stat] runtime: NF=%u WD=%u SR=%u AFE=%s strikes=%d disturbers=%d\r\n",
                sensor.noiseFloor, sensor.watchdogLvl, sensor.spikeRej,
                sensor.outdoorMode ? "OUT" : "IN", sensor.strikeCount,
                sensor.disturberCount);
}

// load persisted AFE mode (call from begin)
static void loadPrefs() {
  Preferences p; p.begin("flashbee", false);   // RW so the namespace is created on first boot
  sensor.outdoorMode = p.getUChar("afe", 1) != 0;
  activeTunCap = p.getUChar("tuncap", 0) & 0x0F;
  sensProfile = p.getUChar("profile", PROFILE_STORM) % PROFILE_COUNT;
  p.end();
}

// ── antenna tune (LCO sweep, datasheet §8.10/8.11) ────────────────
static bool enableLcoOut()  { return maskWrite(REG_LCO_INT, 0xC0, 0xC0) &&  // FDIV=128
                                     maskWrite(REG_TUN_CAP, 0x80, 0x80); }   // DISP_LCO
static bool disableLcoOut() { return maskWrite(REG_TUN_CAP, 0x80, 0); }

static uint32_t measureLcoHz(uint8_t cap, uint16_t windowMs) {
  maskWrite(REG_TUN_CAP, TUN_CAP_MASK, cap & TUN_CAP_MASK);
  delay(TUNE_SETTLE_MS);
  noInterrupts(); lcoEdges = 0; interrupts();
  uint32_t t0 = millis();
  while (millis() - t0 < windowMs) delay(1);
  noInterrupts(); uint32_t n = lcoEdges; interrupts();
  return (uint32_t)((uint64_t)n * 1000UL / windowMs);
}

void as3935RunTune() {
  Serial.println("\r\n=== Antenna tune ===");
  if (!writeReg(REG_PRESET_DEFAULT, DIRECT_CMD_VALUE)) { drawTuneError("I2C FAIL", "no ACK"); return; }
  delay(3);
  writeReg(REG_CALIB_RCO, DIRECT_CMD_VALUE); delay(3);
  if (!enableLcoOut()) { drawTuneError("I2C FAIL", "DISP_LCO failed"); return; }

  detachInterrupt(digitalPinToInterrupt(AS3935_INT_PIN));
  attachInterrupt(digitalPinToInterrupt(AS3935_INT_PIN), lcoISR, RISING);

  uint32_t freqs[16], maxHz = 0; int32_t bestDev = INT32_MAX; uint8_t bestCap = 0;
  for (uint8_t cap = 0; cap < 16; cap++) {
    freqs[cap] = measureLcoHz(cap, TUNE_WINDOW_MS);
    int32_t dev = (int32_t)freqs[cap] - (int32_t)LCO_EXPECTED_HZ;
    Serial.printf("TUN_CAP=%2u -> %5lu Hz (%+5ld)\r\n", cap, (unsigned long)freqs[cap], (long)dev);
    if (freqs[cap] > maxHz) maxHz = freqs[cap];
    int32_t adev = dev < 0 ? -dev : dev;
    if (adev < bestDev) { bestDev = adev; bestCap = cap; }
    drawTuneProgress(cap, freqs[cap], dev, LCO_EXPECTED_HZ, LCO_TOL_HZ);
  }

  detachInterrupt(digitalPinToInterrupt(AS3935_INT_PIN));
  disableLcoOut();

  if (maxHz < 500) {                       // no LCO edges → INT wire missing
    Serial.println("ERROR: no LCO edges — check INT wire");
    drawTuneError("NO SIGNAL", "check INT wire");
    delay(3500);
    sensor.sensorOk = as3935Init();
    return;
  }

  bool inTol = bestDev <= (int32_t)LCO_TOL_HZ;
  activeTunCap = bestCap;
  Serial.printf("best: TUN_CAP=%u @ %lu Hz (%s)\r\n", bestCap,
                (unsigned long)freqs[bestCap], inTol ? "WITHIN TOL" : "OUT OF TOL");
  Preferences p; p.begin("flashbee", false);
  p.putUChar("tuncap", bestCap); p.end();

  drawTuneResult(inTol, bestCap, freqs[bestCap]);
  delay(3000);
  sensor.sensorOk = as3935Init();          // resume normal detection with new cap
}
