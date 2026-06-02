#include "fb_audio.h"
#include <Wire.h>
#include <ESP_I2S.h>
#include <Preferences.h>
#include <math.h>
#include "es8311.h"

// ── pins (Waveshare ESP32-S3-Touch-AMOLED-1.75, from pin_config.h) ──
#define AUD_MCLK 42
#define AUD_BCLK 9
#define AUD_WS   45
#define AUD_DOUT 8      // ESP32 → codec (speaker)
#define AUD_DIN  10     // codec → ESP32 (mic, unused)
#define AUD_PA   46     // power-amp enable (HIGH = on)
#define AUD_SR   16000  // sample rate

static I2SClass     i2s;
static es8311_handle_t es = nullptr;
static bool         s_ready = false;
static TaskHandle_t s_task = nullptr;
static volatile uint8_t s_energy = 200;
static volatile bool    s_alert  = false;
static uint8_t      s_volume = 70;     // 0..100, persisted

bool audioReady() { return s_ready; }
uint8_t audioVolume() { return s_volume; }
const char *audioVolumeLabel() {
  return s_volume == 0 ? "OFF" : s_volume <= 40 ? "LOW" : s_volume <= 70 ? "MED" : "HIGH";
}

void audioSetVolume(uint8_t vol) {
  if (vol > 100) vol = 100;
  s_volume = vol;
  if (s_ready && es && vol > 0) es8311_voice_volume_set(es, vol, nullptr);
  Preferences p; p.begin("flashbee", false); p.putUChar("vol", vol); p.end();
  Serial.printf("[audio] volume -> %u (%s)\r\n", vol, audioVolumeLabel());
}
void audioCycleVolume() {
  uint8_t v = s_volume == 0 ? 33 : s_volume <= 40 ? 66 : s_volume <= 70 ? 100 : 0;
  audioSetVolume(v);
}

// ── synthesis helpers ───────────────────────────────────────────────
static uint32_t s_rng = 0x9e3779b9u;
static inline float noise() {                  // white noise, -1..1
  s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
  return (int32_t)s_rng / 2147483648.0f;
}
static inline void writeMono(int16_t v, int16_t *buf, int j) { buf[2 * j] = v; buf[2 * j + 1] = v; }

// Procedural thunder: a sharp crack over a long low-pass-filtered rumble with
// exponential decay + a slow rolling amplitude wobble. Streamed in chunks.
static void playThunder(uint8_t energy) {
  const float amp  = 0.40f + 0.60f * (energy / 255.0f);
  const float dur  = 1.3f + 1.2f * (energy / 255.0f);
  const int   total = (int)(AUD_SR * dur);
  const float aLow = 0.085f;
  float lp1 = 0, lp2 = 0;
  int16_t buf[256 * 2];
  for (int i = 0; i < total; ) {
    int n = (total - i < 256) ? (total - i) : 256;
    for (int j = 0; j < n; j++) {
      float t = (float)(i + j) / AUD_SR, w = noise();
      lp1 += aLow * (w - lp1); lp2 += aLow * (lp1 - lp2);
      float crack = (t < 0.06f) ? w * (1.0f - t / 0.06f) * 0.9f : 0.0f;
      float env = (t < 0.06f) ? 1.0f : expf(-(t - 0.06f) * 2.0f);
      float roll = 0.55f + 0.30f * sinf(6.2832f * 4.0f * t) + 0.15f * sinf(6.2832f * 0.9f * t + 1.0f);
      float s = (lp2 * 3.2f * roll + crack) * env * amp;
      if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
      writeMono((int16_t)(s * 30000.0f), buf, j);
    }
    i2s.write((uint8_t *)buf, n * 2 * sizeof(int16_t));
    i += n;
  }
}

// Short rising two-tone "storm approaching" chime.
static void playAlert() {
  const float tones[2] = {880.0f, 1318.0f};    // A5 → E6
  const int   toneN = (int)(AUD_SR * 0.13f);   // 130 ms each
  int16_t buf[256 * 2];
  for (int ti = 0; ti < 2; ti++) {
    for (int i = 0; i < toneN; ) {
      int n = (toneN - i < 256) ? (toneN - i) : 256;
      for (int j = 0; j < n; j++) {
        float t = (float)(i + j) / AUD_SR;
        float envA = t < 0.008f ? t / 0.008f : expf(-(t - 0.008f) * 9.0f);  // pluck
        float s = sinf(6.2832f * tones[ti] * t) * envA * 0.5f;
        writeMono((int16_t)(s * 30000.0f), buf, j);
      }
      i2s.write((uint8_t *)buf, n * 2 * sizeof(int16_t));
      i += n;
    }
  }
}

static void flushSilence() {
  int16_t z[256 * 2]; memset(z, 0, sizeof z);
  for (int k = 0; k < 4; k++) i2s.write((uint8_t *)z, sizeof z);
}

static void audioTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (s_volume == 0) continue;               // muted → nothing
    digitalWrite(AUD_PA, HIGH);
    delay(8);                                  // amp settle
    if (s_alert) playAlert();
    playThunder(s_energy);
    flushSilence();
    digitalWrite(AUD_PA, LOW);                 // kill idle hiss
  }
}

bool audioBegin() {
  { Preferences p; p.begin("flashbee", false); s_volume = p.getUChar("vol", 70); p.end(); }

  pinMode(AUD_PA, OUTPUT);
  digitalWrite(AUD_PA, LOW);

  i2s.setPins(AUD_BCLK, AUD_WS, AUD_DOUT, AUD_DIN, AUD_MCLK);
  if (!i2s.begin(I2S_MODE_STD, AUD_SR, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("[audio] I2S init FAILED"); return false;
  }
  es = es8311_create(1, ES8311_ADDRRES_0);     // onboard I²C (Wire1 / port 1)
  if (!es) { Serial.println("[audio] ES8311 create FAILED"); return false; }
  es8311_clock_config_t clk = {
    .mclk_inverted = false, .sclk_inverted = false, .mclk_from_mclk_pin = true,
    .mclk_frequency = AUD_SR * 256, .sample_frequency = AUD_SR,
  };
  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
    Serial.println("[audio] ES8311 init FAILED"); return false;
  }
  es8311_sample_frequency_config(es, AUD_SR * 256, AUD_SR);
  es8311_microphone_config(es, false);
  es8311_voice_volume_set(es, s_volume > 0 ? s_volume : 1, nullptr);

  xTaskCreatePinnedToCore(audioTask, "fbaudio", 4096, nullptr, 1, &s_task, 1);
  s_ready = true;
  Serial.printf("[audio] ES8311 ready (vol %u) — thunder armed\r\n", s_volume);
  return true;
}

void audioStrike(uint8_t energy, bool approaching) {
  if (!s_ready || !s_task || s_volume == 0) return;
  s_energy = energy;
  s_alert  = approaching;
  xTaskNotifyGive(s_task);                      // ignored if already playing
}
void audioAlertOnly() {
  if (!s_ready || !s_task || s_volume == 0) return;
  s_energy = 0; s_alert = true;
  xTaskNotifyGive(s_task);
}
