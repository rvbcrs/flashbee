#include "fb_time.h"
#include <Wire.h>
#include <sys/time.h>

// PCF85063 RTC on the onboard I²C bus (Wire1, GPIO15/14), addr 0x51.
#define RTC_ADDR     0x51
#define RTC_REG_SEC  0x04        // sec,min,hour,day,wday,month,year (BCD)
#define EPOCH_2024   1704067200  // 2024-01-01 — "time is real" threshold

static inline uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static inline uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

static bool rtcRead(struct tm &t) {
  Wire1.beginTransmission(RTC_ADDR);
  Wire1.write(RTC_REG_SEC);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((int)RTC_ADDR, 7) != 7) return false;
  uint8_t s = Wire1.read(), mi = Wire1.read(), h = Wire1.read();
  uint8_t d = Wire1.read(), wd = Wire1.read(), mo = Wire1.read(), y = Wire1.read();
  (void)wd;
  if (s & 0x80) return false;                       // OS flag: oscillator stopped → invalid
  t.tm_sec  = bcd2dec(s & 0x7F);
  t.tm_min  = bcd2dec(mi & 0x7F);
  t.tm_hour = bcd2dec(h & 0x3F);
  t.tm_mday = bcd2dec(d & 0x3F);
  t.tm_mon  = bcd2dec(mo & 0x1F) - 1;
  t.tm_year = 2000 + bcd2dec(y) - 1900;
  t.tm_isdst = 0;
  return t.tm_year >= 124;                           // ≥2024
}

static void rtcWrite(const struct tm &t) {
  Wire1.beginTransmission(RTC_ADDR);
  Wire1.write(RTC_REG_SEC);
  Wire1.write(dec2bcd(t.tm_sec));
  Wire1.write(dec2bcd(t.tm_min));
  Wire1.write(dec2bcd(t.tm_hour));
  Wire1.write(dec2bcd(t.tm_mday));
  Wire1.write(dec2bcd(t.tm_wday));
  Wire1.write(dec2bcd(t.tm_mon + 1));
  Wire1.write(dec2bcd((t.tm_year + 1900) - 2000));
  Wire1.endTransmission();
}

bool   timeValid() { return time(nullptr) > EPOCH_2024; }
time_t timeNow()   { return time(nullptr); }

void timeBegin() {
  setenv("TZ", "UTC0", 1); tzset();                  // keep the system clock in UTC
  struct tm t = {};
  if (rtcRead(t)) {
    time_t e = mktime(&t);                           // UTC (TZ=UTC0)
    if (e > EPOCH_2024) {
      struct timeval tv = { e, 0 };
      settimeofday(&tv, nullptr);
      Serial.printf("[time] RTC -> %ld\r\n", (long)e);
    }
  } else {
    Serial.println("[time] RTC not set yet (waiting for NTP)");
  }
}

void timeNtpBegin() {
  configTime(0, 0, "pool.ntp.org", "time.google.com");  // UTC, non-blocking
}

void timeService() {
  static bool rtcSynced = false;
  if (!rtcSynced && timeValid()) {                   // NTP just gave us real time → persist to RTC
    rtcSynced = true;
    time_t e = time(nullptr);
    struct tm t; gmtime_r(&e, &t);
    rtcWrite(t);
    Serial.printf("[time] NTP synced, RTC written: %ld\r\n", (long)e);
  }
}
