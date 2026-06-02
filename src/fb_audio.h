// ─────────────────────────────────────────────────────────────────
// Flash Bee — audio: ES8311 codec + I2S. Procedural thunder on a strike
// + a short "approaching" alert chime. Onboard speaker via the codec → PA
// (GPIO46) → speaker connector.
// ─────────────────────────────────────────────────────────────────
#pragma once
#include <Arduino.h>

bool audioBegin();                              // I2S + ES8311 init + playback task
void audioStrike(uint8_t energy, bool approaching); // alert (if approaching) + thunder
void audioAlertOnly();                          // just the rising alert chime
bool audioReady();                              // codec came up ok

// volume: 0..100 (0 = muted). Persisted in NVS.
void    audioSetVolume(uint8_t vol);
uint8_t audioVolume();
void    audioCycleVolume();                     // OFF → 33 → 66 → 100 → OFF
const char *audioVolumeLabel();                 // "OFF" / "LOW" / "MED" / "HIGH"
