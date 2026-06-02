#include "fb_power.h"
#include "fb_config.h"
#include "fb_gfx.h"
#include "fb_as3935.h"
#include <Wire.h>
#include <Preferences.h>
#include "XPowersLib.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

static XPowersPMU pmu;
static bool pmuOk = false;

static const uint32_t SCREEN_MS[] = {30000, 60000, 120000, 300000, 600000, 0};
static const char *SCREEN_LB[]    = {"30s", "1 min", "2 min", "5 min", "10 min", "NEVER"};
static const uint32_t SLEEP_MS[]  = {300000, 900000, 1800000, 3600000, 7200000, 0};
static const char *SLEEP_LB[]     = {"5 min", "15 min", "30 min", "1 h", "2 h", "NEVER"};
#define N_OPT 6

static uint8_t  screenIdx = 2;     // 2 min
static uint8_t  sleepIdx  = 5;     // NEVER (Tier-3 opt-in)
static uint32_t lastActivity = 0;
static bool     displayOn = true;
static uint16_t battMv = 0;
static uint8_t  battPct = 0;
static bool     battPresent = false;
static uint32_t battLast = 0;

void powerBegin() {
  Wire1.begin(ONB_SDA, ONB_SCL, 400000);
  pmuOk = pmu.begin(Wire1, AXP2101_SLAVE_ADDRESS, ONB_SDA, ONB_SCL);
  Serial.println(pmuOk ? "[pmu] AXP2101 ok" : "[pmu] AXP2101 not found");
  if (pmuOk) pmu.enableBattDetection();

  Preferences p; p.begin("flashbee", false);   // RW so namespace exists on first boot
  screenIdx = p.getUChar("tmout", 2);
  sleepIdx  = p.getUChar("sleep", 5);
  p.end();
  if (screenIdx >= N_OPT) screenIdx = 2;
  if (sleepIdx  >= N_OPT) sleepIdx  = 5;
  lastActivity = millis();
}

static void backlight(bool on) {
  if (on) { panel->displayOn();  panel->setBrightness(255); }
  else    { panel->setBrightness(0); panel->displayOff(); }
  displayOn = on;
  Serial.printf("[bl] %s\r\n", on ? "on" : "off");
}

void powerMarkActivity() {
  lastActivity = millis();
  if (!displayOn) backlight(true);
}
bool powerDisplayOn() { return displayOn; }

static void readBattery() {
  if (!pmuOk) { battPresent = false; battMv = 0; return; }
  battPresent = pmu.isBatteryConnect();
  if (battPresent) {
    battMv  = pmu.getBattVoltage();
    battPct = pmu.getBatteryPercent();
  } else { battMv = 0; battPct = 0; }
}

uint16_t powerBatteryMv()     { return battMv; }
uint8_t  powerBatteryPct()    { return battPct; }
bool     powerBatteryPresent(){ return battPresent; }

void powerCycleScreenTimeout() {
  screenIdx = (screenIdx + 1) % N_OPT;
  Preferences p; p.begin("flashbee", false); p.putUChar("tmout", screenIdx); p.end();
  powerMarkActivity();
}
void powerCycleSleepTimeout() {
  sleepIdx = (sleepIdx + 1) % N_OPT;
  Preferences p; p.begin("flashbee", false); p.putUChar("sleep", sleepIdx); p.end();
  powerMarkActivity();
}
const char *powerScreenLabel() { return SCREEN_LB[screenIdx]; }
const char *powerSleepLabel()  { return SLEEP_LB[sleepIdx]; }

// ── Tier-3 light sleep (wake on AS3935 INT high or touch INT low) ──
static void enterLightSleep() {
  if (!as3935ClearLatched()) return;                  // couldn't clear → retry next tick
  if (digitalRead(AS3935_INT_PIN) == HIGH) { as3935ForcePending(); return; }
  if (digitalRead(TP_INT) == LOW) return;             // user pressing now

  Serial.println("[sleep] light sleep"); Serial.flush();
  gpio_config_t tcfg = { BIT64(TP_INT), GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE,
                         GPIO_PULLDOWN_DISABLE, GPIO_INTR_DISABLE };
  gpio_config(&tcfg);
  gpio_config_t acfg = { BIT64(AS3935_INT_PIN), GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE,
                         GPIO_PULLDOWN_DISABLE, GPIO_INTR_DISABLE };
  gpio_config(&acfg);

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_enable((gpio_num_t)AS3935_INT_PIN, GPIO_INTR_HIGH_LEVEL);
  gpio_wakeup_enable((gpio_num_t)TP_INT,         GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup((uint64_t)3600ULL * 1000ULL * 1000ULL);

  esp_light_sleep_start();

  bool fromAs = (digitalRead(AS3935_INT_PIN) == HIGH);
  bool fromTp = (digitalRead(TP_INT) == LOW);
  as3935ReattachIrq();
  if (fromTp) powerMarkActivity();
  if (fromAs) as3935ForcePending();
  Serial.printf("[sleep] woke as=%d tp=%d\r\n", fromAs, fromTp);
}

void powerService(uint32_t now) {
  if (now - battLast > 2000 || battLast == 0) { battLast = now; readBattery(); }

  bool keepOn = sensor.noiseFault;
  int32_t idle = (int32_t)(now - lastActivity);

  uint32_t scrMs = SCREEN_MS[screenIdx];
  if (displayOn && scrMs > 0 && !keepOn && idle > (int32_t)scrMs) backlight(false);

  uint32_t slpMs = SLEEP_MS[sleepIdx];
  if (!displayOn && !keepOn && sensor.sensorOk && slpMs > 0 && idle > (int32_t)slpMs)
    enterLightSleep();
}
