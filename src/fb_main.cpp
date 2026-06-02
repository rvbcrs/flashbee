// ─────────────────────────────────────────────────────────────────
// Flash Bee — ESP32-S3 + CO5300 AMOLED firmware (layout B).
//   pio run -e s3 -t upload && pio device monitor -b 115200
// ─────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include "fb_config.h"
#include "fb_gfx.h"
#include "fb_as3935.h"
#include "fb_touch.h"
#include "fb_power.h"
#include "fb_display.h"
#include "fb_audio.h"
#include "fb_net.h"
#include "fb_time.h"

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\r\n=== Flash Bee (ESP32-S3 / CO5300) ===");

  bool gok = gfxInit();
  Serial.printf("[gfx] begin %s  PSRAM=%u\r\n", gok ? "ok" : "FAIL",
                (unsigned)ESP.getPsramSize());
  displayBegin();    // load saved theme
  drawBoot("FLASH BEE", "lightning detector", C_TEAL);

  touchBegin();
  powerBegin();
  timeBegin();       // PCF85063 RTC → system clock (real time for the strike log)
  audioBegin();      // ES8311 + I2S; thunder on a strike
  as3935Begin();
  // Tune the antenna on every boot (LCO sweep), like the original Flash Bee.
  // RunTune shows the tune screen, persists the best TUN_CAP, then re-inits
  // and starts listening. If the chip/INT is missing it shows an error and
  // the main loop retries via as3935Service().
  as3935RunTune();
  netBegin();        // non-blocking WiFi (captive portal 'FlashBee-setup') + web + MQTT
  delay(300);
}

static void pollSerialCmd() {
  static String buf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      buf.trim();
      if (buf.equalsIgnoreCase("tune")) as3935RunTune();
      else if (buf.equalsIgnoreCase("test"))  as3935InjectTest(random(2, 35), random(20000, 800000));
      else if (buf.equalsIgnoreCase("close")) as3935InjectTest(3, 600000);
      else if (buf.equalsIgnoreCase("stat"))  as3935PrintStatus();
      buf = "";
    } else if (buf.length() < 16) buf += c;
  }
}

void loop() {
  uint32_t now = millis();
  pollSerialCmd();
  as3935Service(now);

  // ── touch → activity / navigation ──────────────────────────────
  TouchEvent ev;
  bool hadEvent = touchPoll(now, ev);
  bool pressed  = touchPressed();
  static bool prev = false, armedOff = false;
  if (pressed && !prev) armedOff = !powerDisplayOn();   // press started while screen off?
  prev = pressed;
  if (pressed) powerMarkActivity();
  if (hadEvent) {
    powerMarkActivity();
    if (!armedOff) {                                    // ignore the gesture that woke the screen
      if (ev.type == TOUCH_SWIPE) uiSwipe();
      else if (ev.type == TOUCH_TAP && currentScreen == SCREEN_SETTINGS) uiHandleTap(ev.x, ev.y);
    }
    armedOff = false;
  }

  // AS3935 activity should wake the display: disturbers show storm activity,
  // validated strikes also get the full lightning flash.
  static int lastDisturbers = 0;
  static int lastStrikes = 0;
  static uint8_t closestKm = 255;             // closest strike this storm
  if (sensor.disturberCount != lastDisturbers) {
    lastDisturbers = sensor.disturberCount;
    powerMarkActivity();
  }
  // storm gone for a while → re-arm the approach alert for the next one
  if (closestKm != 255 && sensor.lastStrikeMs && (now - sensor.lastStrikeMs) > 900000UL)
    closestKm = 255;
  if (sensor.strikeCount != lastStrikes) {
    lastStrikes = sensor.strikeCount;
    powerMarkActivity();      // wake the screen
    uint8_t te = (uint8_t)constrain(sensor.lastEnergy / 4096UL, 45, 255);
    // approaching = this strike is closer than the closest so far this storm
    bool approaching = false;
    uint8_t d = sensor.lastDistRaw;
    if (d != DIST_UNKNOWN && d != DIST_OUT_OF_RANGE) {
      uint8_t km = (d == DIST_OVERHEAD) ? 1 : d;
      if (km < closestKm) { approaching = true; closestKm = km; }
    }
    audioStrike(te, approaching);   // alert chime (if approaching) + thunder, non-blocking
    netOnStrike(now, sensor.lastDistRaw, sensor.lastEnergy);  // web history + MQTT push
    lightningFlash();               // flash the screen like lightning hit it
  }

  powerService(now);
  netService(now);          // web clients + MQTT (no-op until WiFi connects)
  timeService();            // write the RTC once NTP first syncs

  if (powerDisplayOn()) {
    if (!sensor.sensorOk)                       drawSensorLost();
    else if (currentScreen == SCREEN_SETTINGS)  drawSettings(now);
    else                                        drawMain(now);
  }
  delay(33);
}
