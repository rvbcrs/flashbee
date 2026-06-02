// ─────────────────────────────────────────────────────────────────
// Flash Bee — AS3935 sensor module. Pure hardware logic ported
// verbatim from the project's datasheet-correct main.ino (§8.10/8.11),
// on the GPIO17/18 Wire bus proven by the bring-up scanner.
// ─────────────────────────────────────────────────────────────────
#pragma once
#include <Arduino.h>

#define DIST_OUT_OF_RANGE 0x3F
#define DIST_OVERHEAD     0x01
#define DIST_UNKNOWN      0x00
#define CLOSE_STRIKE_KM   10
#define MAX_TICKS         20

struct SensorState {
  bool     sensorOk          = false;
  bool     noiseFault        = false;
  bool     outdoorMode       = true;
  int      strikeCount       = 0;
  uint32_t lastEnergy        = 0;
  uint32_t maxEnergy         = 0;
  uint8_t  lastDistRaw       = DIST_UNKNOWN;
  uint32_t lastStrikeMs      = 0;
  uint32_t lastCloseStrikeMs = 0;     // 0 = none yet
  uint32_t lastActivityMs    = 0;     // last disturber/strike (storm activity)
  int      disturberCount    = 0;
  int      spuriousCount     = 0;     // INT_L events with ~zero energy (EMI, dropped)
  uint8_t  noiseFloor        = 2;
  uint8_t  watchdogLvl       = 2;
  uint8_t  spikeRej          = 2;
  uint32_t tickEnergy[MAX_TICKS] = {0};
  int      tickHead          = 0;
  int      tickCount         = 0;
};

extern SensorState sensor;

void as3935Begin();                 // Wire.begin + attach IRQ pin
bool as3935Init();                  // full init/calibration sequence
bool as3935Service(uint32_t now);   // process IRQ + decay timers; true if a new strike landed
bool as3935InShelter(uint32_t now); // within the 30-min danger window

// ── sensitivity profiles (wd/sr/nf/mask/pin presets) ──
enum { PROFILE_STORM = 0, PROFILE_NORMAL = 1, PROFILE_NOISY = 2, PROFILE_COUNT = 3 };
extern uint8_t sensProfile;
void        as3935SetProfile(uint8_t p);   // apply + persist
const char *as3935ProfileName(uint8_t p);

void as3935SetMode(bool outdoor);   // AFE gain + persist
void as3935ResetFilters();          // NF/WD/SR defaults + clear stats + counters
bool as3935ClearLatched();          // read INT reg to clear latch (for sleep); false on I²C fail
void as3935ForcePending();          // mark an IRQ pending (post-wake)
void as3935ReattachIrq();           // re-arm RISING ISR after light sleep
void as3935RunTune();               // LCO antenna sweep; persists TUN_CAP, then re-inits
void as3935InjectTest(uint8_t km, uint32_t energy);  // simulate a strike (for testing)
void as3935PrintStatus();          // dump AS3935 registers + runtime counters
