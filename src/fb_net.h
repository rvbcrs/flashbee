// ─────────────────────────────────────────────────────────────────
// Flash Bee — networking: WiFiManager captive portal (WiFi + MQTT config),
// a small web UI (live data + strike history), and MQTT publishing with
// Home Assistant auto-discovery.
// ─────────────────────────────────────────────────────────────────
#pragma once
#include <Arduino.h>

void        netBegin();              // captive-portal autoconnect + web + MQTT setup
void        netService(uint32_t now);// web clients, MQTT loop, periodic publish
void        netOnStrike(uint32_t now, uint8_t km, uint32_t energy);  // record + publish a strike
bool        netConnected();
const char *netIP();                 // "" if offline
void        netStartPortal();        // (re)open the config portal on demand
