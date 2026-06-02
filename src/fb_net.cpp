#include "fb_net.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <LittleFS.h>
#include "fb_as3935.h"
#include "fb_power.h"
#include "fb_time.h"

#define STRIKE_LOG   "/strikes.csv"     // persistent log: "epoch,km,energy" per line
#define LOG_MAX_BYTES 1000000UL         // ~1 MB; rotate (keep recent half) past this

static WebServer    server(80);
static WiFiClient   espClient;
static PubSubClient mqtt(espClient);

static char     s_id[13] = {0};               // chip-id hex (device id)
static String   mqttHost, mqttUser, mqttPass;
static uint16_t mqttPort = 1883;
static uint32_t s_lastPub = 0, s_lastMqttTry = 0;
static bool     s_began = false;
static bool     s_resetWifi = false;          // deferred WiFi-credentials wipe + reboot
static uint32_t s_resetAt = 0;

// ── strike history ring ─────────────────────────────────────────────
struct HistEntry { uint32_t t; uint8_t km; uint32_t e; };
#define HIST_N 64
static HistEntry s_hist[HIST_N];
static int       s_histHead = 0, s_histCount = 0;
static uint8_t   s_nearest = 255;
static uint32_t  s_total = 0;        // lifetime strike count (from the persisted log)
static bool      s_fs = false;       // LittleFS mounted

bool netConnected() { return WiFi.status() == WL_CONNECTED; }
const char *netIP() {
  static String ip; ip = netConnected() ? WiFi.localIP().toString() : String("");
  return ip.c_str();
}

static char s_topState[40], s_topAvail[40];   // precomputed once (no per-publish String churn)

// ── NVS config ───────────────────────────────────────────────────────
static void loadMqttCfg() {
  Preferences p; p.begin("flashbee", true);
  mqttHost = p.getString("mqtt_host", "");
  mqttPort = p.getUShort("mqtt_port", 1883);
  mqttUser = p.getString("mqtt_user", "");
  mqttPass = p.getString("mqtt_pass", "");
  p.end();
}
static void saveMqttCfg() {
  Preferences p; p.begin("flashbee", false);
  p.putString("mqtt_host", mqttHost);
  p.putUShort("mqtt_port", mqttPort);
  p.putString("mqtt_user", mqttUser);
  p.putString("mqtt_pass", mqttPass);
  p.end();
}

// ── persistent strike log (LittleFS) ─────────────────────────────────
static void pushHist(uint32_t t, uint8_t km, uint32_t e) {
  s_hist[s_histHead] = { t, km, e };
  s_histHead = (s_histHead + 1) % HIST_N;
  if (s_histCount < HIST_N) s_histCount++;
  if (km != DIST_OUT_OF_RANGE && km != DIST_UNKNOWN) {
    uint8_t k = (km == DIST_OVERHEAD) ? 1 : km;
    if (k < s_nearest) s_nearest = k;
  }
}

static void logBegin() {
  s_fs = LittleFS.begin(true);                       // format on first mount
  if (!s_fs) { Serial.println("[log] LittleFS mount FAILED"); return; }
  File f = LittleFS.open(STRIKE_LOG, "r");
  if (!f) { Serial.println("[log] no strike log yet"); return; }
  while (f.available()) {                             // count total + keep last HIST_N in the ring
    String line = f.readStringUntil('\n');
    int c1 = line.indexOf(','), c2 = line.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) continue;
    pushHist((uint32_t)strtoul(line.c_str(), nullptr, 10),
             (uint8_t)atoi(line.c_str() + c1 + 1),
             (uint32_t)strtoul(line.c_str() + c2 + 1, nullptr, 10));
    s_total++;
  }
  f.close();
  Serial.printf("[log] loaded %lu strikes (ring %d)\r\n", (unsigned long)s_total, s_histCount);
}

static void logRotate() {                            // keep the most recent ~half
  File in = LittleFS.open(STRIKE_LOG, "r");
  if (!in) return;
  in.seek(in.size() / 2);
  in.readStringUntil('\n');                          // realign to a line boundary
  File out = LittleFS.open("/strikes.tmp", "w");
  if (!out) { in.close(); return; }
  uint8_t buf[512]; int n;
  while ((n = in.read(buf, sizeof buf)) > 0) out.write(buf, n);
  in.close(); out.close();
  LittleFS.remove(STRIKE_LOG);
  LittleFS.rename("/strikes.tmp", STRIKE_LOG);
  Serial.println("[log] rotated");
}

static void logAppend(uint32_t t, uint8_t km, uint32_t e) {
  if (!s_fs) return;
  File f = LittleFS.open(STRIKE_LOG, "a");
  if (!f) return;
  f.printf("%lu,%u,%lu\n", (unsigned long)t, km, (unsigned long)e);
  size_t sz = f.size();
  f.close();
  if (sz > LOG_MAX_BYTES) logRotate();
}

// ── state snapshot helpers ───────────────────────────────────────────
static int distKm() {
  uint8_t d = sensor.lastDistRaw;
  if (d == DIST_UNKNOWN)      return -1;
  if (d == DIST_OVERHEAD)     return 0;     // overhead
  if (d == DIST_OUT_OF_RANGE) return 99;    // >40 km
  return d;
}

// Build into a static buffer — NO Arduino String in the hot path (the web UI
// polls every 2 s; String churn fragments the heap and hangs the server after
// hours). Single-threaded (loop task), so one shared static buffer is safe.
static const char *stateJson(uint32_t now) {
  static char s[224];
  snprintf(s, sizeof s,
    "{\"distance\":%d,\"energy\":%lu,\"strikes\":%d,\"nearest\":%d,\"shelter\":%s,"
    "\"last_age\":%ld,\"profile\":\"%s\",\"battery\":%d,\"batt_mv\":%d,\"rssi\":%d}",
    distKm(), (unsigned long)sensor.lastEnergy, sensor.strikeCount,
    s_nearest == 255 ? -1 : (int)s_nearest, as3935InShelter(now) ? "true" : "false",
    sensor.lastStrikeMs ? (long)((now - sensor.lastStrikeMs) / 1000) : -1L,
    as3935ProfileName(sensProfile),
    powerBatteryPresent() ? powerBatteryPct() : 0, (int)powerBatteryMv(), (int)WiFi.RSSI());
  return s;
}

// ── web UI ───────────────────────────────────────────────────────────
static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Flash Bee</title><style>
body{font-family:system-ui,sans-serif;background:#0c0e12;color:#e8e8ea;margin:0;padding:16px}
h1{font-size:20px;color:#ffb01a;margin:0 0 12px}
.g{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px;margin-bottom:18px}
.c{background:#171a21;border-radius:12px;padding:14px}
.k{font-size:11px;color:#8a8f99;text-transform:uppercase;letter-spacing:.5px}
.v{font-size:26px;font-weight:700;margin-top:4px}
.sh{background:#7a1020;color:#ff9}.al{color:#ff6}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid #222}
th{color:#8a8f99;font-weight:600}
nav{display:flex;gap:6px;margin-bottom:14px}
nav button{background:#171a21;color:#8a8f99;border:0;padding:9px 18px;border-radius:8px;font-size:14px;cursor:pointer}
nav button.on{background:#ffb01a;color:#111;font-weight:700}
.tab{display:none}.tab.on{display:block}
input{background:#0c0e12;color:#e8e8ea;border:1px solid #333;border-radius:8px;padding:9px}
</style></head><body>
<h1>⚡ Flash Bee <span id=mq style="font-size:12px;font-weight:400"></span></h1>
<nav id=nav>
<button data-t=live class=on onclick="show('live')">Live</button>
<button data-t=hist onclick="show('hist')">History</button>
<button data-t=cfg onclick="show('cfg')">Settings</button></nav>
<div id=live class="tab on"><div class=g id=cards></div></div>
<div id=hist class=tab>
<div style="font-size:13px;color:#8a8f99;margin-bottom:8px">Total logged: <b id=total style="color:#e8e8ea">—</b> · <a href="/strikes.csv" style="color:#ffb01a">download CSV</a></div>
<table><thead><tr><th>time</th><th>ago</th><th>distance</th><th>energy</th></tr></thead><tbody id=histb></tbody></table></div>
<div id=cfg class=tab>
<div class=c><div class=k style="margin-bottom:10px">MQTT broker (Home Assistant)</div>
<div style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:8px">
<input id=host placeholder="broker IP" style="flex:1;min-width:140px">
<input id=port placeholder="1883" style="width:70px"></div>
<div style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px">
<input id=user placeholder="user (optional)" style="flex:1">
<input id=pass type=password placeholder="pass (blank = unchanged)" style="flex:1"></div>
<button onclick=save() style="padding:9px 18px;border:0;border-radius:8px;background:#ffb01a;font-weight:700;cursor:pointer">Save</button>
<span id=msg style="margin-left:8px;color:#8a8f99"></span></div>
<div class=c style="margin-top:12px"><div class=k style="margin-bottom:8px">WiFi</div>
<div style="font-size:12px;color:#8a8f99;margin-bottom:8px" id=wifi></div>
<button onclick=rstwifi() style="padding:9px 18px;border:0;border-radius:8px;background:#7a1020;color:#fee;font-weight:700;cursor:pointer">Reset WiFi</button>
<span style="margin-left:8px;color:#8a8f99;font-size:12px">reboots → opens 'FlashBee-setup' to pick a new network</span></div>
</div>
<script>
function show(t){for(let b of nav.children)b.classList.toggle('on',b.dataset.t==t);
 for(let id of['live','hist','cfg'])document.getElementById(id).classList.toggle('on',id==t)}
function km(d){return d<0?'—':d==0?'overhead':d==99?'>40 km':d+' km'}
function ago(s){if(s<0||s>2592000)return'—';s=Math.floor(s);let h=s/3600|0,m=s%3600/60|0,x=s%60;
 return h?`${h}h ${m}m ${x}s`:m?`${m}m ${x}s`:`${x}s`}
function eng(e){return e>999?Math.floor(e/1000)+'K':''+e}
function dt(ts){if(!ts)return'—';return new Date(ts*1000).toLocaleString([],{month:'short',day:'numeric',hour:'2-digit',minute:'2-digit'})}
async function tick(){
 let d=await(await fetch('/api')).json(),st=d.state;
 let cs=[['Distance',km(st.distance),st.shelter?'sh':''],
   ['Nearest',km(st.nearest),''],['Strikes',st.strikes,''],
   ['Energy',eng(st.energy),''],['Last strike',ago(st.last_age),''],
   ['Shelter',st.shelter?'SEEK SHELTER':'clear',st.shelter?'sh':''],
   ['Battery',st.battery+'%',''],['Profile',st.profile,''],['WiFi',st.rssi+' dBm','']];
 cards.innerHTML=cs.map(c=>`<div class="c ${c[2]}"><div class=k>${c[0]}</div><div class=v>${c[1]}</div></div>`).join('');
 histb.innerHTML=d.history.length?d.history.map(h=>`<tr><td>${dt(h.ts)}</td><td>${ago(h.ago)}</td><td>${km(h.km)}</td><td>${eng(h.e)}</td></tr>`).join('')
   :'<tr><td colspan=4 style="color:#666">no strikes yet</td></tr>';
 total.textContent=d.total;
 let c=d.cfg;
 mq.textContent=c.mqtt?'● MQTT connected':(c.host?'○ MQTT not connected':'○ MQTT not configured');
 mq.style.color=c.mqtt?'#3fd07b':'#ff6';
 if(!host._init){host.value=c.host;port.value=c.port;user.value=c.user;host._init=1}
 wifi.textContent='connected to: '+(c.ssid||'—');
}
async function save(){
 let b=new URLSearchParams();b.set('host',host.value);b.set('port',port.value||'1883');
 b.set('user',user.value);if(pass.value)b.set('pass',pass.value);
 msg.textContent='saving…';await fetch('/save',{method:'POST',body:b});pass.value='';msg.textContent='saved ✓'}
async function rstwifi(){if(!confirm('Reset WiFi? The device reboots and opens the FlashBee-setup access point so you can choose a new network.'))return;
 await fetch('/resetwifi',{method:'POST'});alert('Resetting — join the FlashBee-setup WiFi to configure a new network.')}
tick();setInterval(tick,2000);
</script></body></html>)HTML";

static void handleRoot() { server.send_P(200, "text/html", PAGE); }

static void handleApi() {
  uint32_t now = millis();
  time_t   ep  = timeNow();
  bool     tv  = timeValid();
  static char buf[4096];      // state + up to HIST_N history entries + cfg
  int n = snprintf(buf, sizeof buf, "{\"state\":%s,\"history\":[", stateJson(now));
  for (int i = 0; i < s_histCount && n < (int)sizeof buf - 80; i++) {
    int idx = (s_histHead - 1 - i + HIST_N * 2) % HIST_N;     // newest first
    HistEntry &h = s_hist[idx];
    long ago = (h.t && tv) ? (long)(ep - h.t) : -1;
    n += snprintf(buf + n, sizeof buf - n, "%s{\"ts\":%lu,\"ago\":%ld,\"km\":%u,\"e\":%lu}",
                  i ? "," : "", (unsigned long)h.t, ago, h.km, (unsigned long)h.e);
  }
  snprintf(buf + n, sizeof buf - n,
    "],\"total\":%lu,\"cfg\":{\"host\":\"%s\",\"port\":%u,\"user\":\"%s\",\"mqtt\":%s,\"ssid\":\"%s\"}}",
    (unsigned long)s_total, mqttHost.c_str(), mqttPort, mqttUser.c_str(),
    mqtt.connected() ? "true" : "false", WiFi.SSID().c_str());
  server.send(200, "application/json", buf);
}

// stream the raw CSV log for download/backup
static void handleLog() {
  if (!s_fs || !LittleFS.exists(STRIKE_LOG)) { server.send(404, "text/plain", "no log"); return; }
  File f = LittleFS.open(STRIKE_LOG, "r");
  server.streamFile(f, "text/csv");
  f.close();
}

static void handleSave() {
  if (server.hasArg("host")) mqttHost = server.arg("host");
  if (server.hasArg("port")) { int p = server.arg("port").toInt(); mqttPort = p ? (uint16_t)p : 1883; }
  if (server.hasArg("user")) mqttUser = server.arg("user");
  if (server.hasArg("pass") && server.arg("pass").length()) mqttPass = server.arg("pass");
  saveMqttCfg();
  if (mqttHost.length()) { mqtt.setServer(mqttHost.c_str(), mqttPort); mqtt.setBufferSize(1024); }
  if (mqtt.connected()) mqtt.disconnect();      // reconnect with the new settings
  s_lastMqttTry = 0;                            // retry now
  server.send(200, "text/html",
    "<meta http-equiv=refresh content='1;url=/'>Saved — reconnecting to MQTT…");
}

static void handleResetWifi() {
  server.send(200, "text/html",
    "WiFi reset. Rebooting — connect to the <b>FlashBee-setup</b> WiFi to pick a new network.");
  s_resetWifi = true;                 // deferred so the HTTP reply flushes first
  s_resetAt = millis();
}

// ── MQTT + Home Assistant discovery ──────────────────────────────────
static void pubDiscovery() {
  char dev[180];
  snprintf(dev, sizeof dev,
    "\"dev\":{\"ids\":[\"flashbee_%s\"],\"name\":\"Flash Bee\",\"mdl\":\"ESP32-S3 AS3935\",\"mf\":\"DIY\"}", s_id);
  String avt = s_topAvail, stt = s_topState;   // String OK here (connect-time only, not per-loop)
  struct S { const char *key, *name, *unit, *dc, *tmpl; bool bin; };
  const S items[] = {
    {"distance", "Lightning distance", "km",  "distance", "{{ none if value_json.distance < 0 else value_json.distance }}", false},
    {"nearest",  "Nearest strike",     "km",  "distance", "{{ none if value_json.nearest < 0 else value_json.nearest }}",  false},
    {"energy",   "Lightning energy",   "",    "",                 "{{ value_json.energy }}",   false},
    {"strikes",  "Strike count",       "",    "",                 "{{ value_json.strikes }}",  false},
    {"battery",  "Battery",            "%",   "battery",          "{{ value_json.battery }}",  false},
    {"rssi",     "WiFi signal",        "dBm", "signal_strength",  "{{ value_json.rssi }}",     false},
    {"shelter",  "Shelter",            "",    "safety",           "{{ 'ON' if value_json.shelter else 'OFF' }}", true},
  };
  for (auto &it : items) {
    String topic = String("homeassistant/") + (it.bin ? "binary_sensor" : "sensor")
                 + "/flashbee_" + s_id + "_" + it.key + "/config";
    String p = "{\"name\":\"" + String(it.name) + "\",";
    p += "\"uniq_id\":\"flashbee_" + String(s_id) + "_" + it.key + "\",";
    p += "\"stat_t\":\"" + stt + "\",\"avty_t\":\"" + avt + "\",";
    p += "\"val_tpl\":\"" + String(it.tmpl) + "\",";
    if (it.unit[0]) p += "\"unit_of_meas\":\"" + String(it.unit) + "\",";
    if (it.dc[0])   p += "\"dev_cla\":\"" + String(it.dc) + "\",";
    p += String(dev) + "}";
    mqtt.publish(topic.c_str(), p.c_str(), true);
  }
}

static void mqttEnsure(uint32_t now) {
  if (mqttHost.length() == 0 || mqtt.connected()) return;
  if (now - s_lastMqttTry < 5000) return;
  s_lastMqttTry = now;
  String cid = String("flashbee-") + s_id;
  bool ok = mqtt.connect(cid.c_str(),
                         mqttUser.length() ? mqttUser.c_str() : nullptr,
                         mqttPass.length() ? mqttPass.c_str() : nullptr,
                         s_topAvail, 0, true, "offline");
  if (ok) {
    Serial.println("[mqtt] connected");
    mqtt.publish(s_topAvail, "online", true);
    pubDiscovery();
    mqtt.publish(s_topState, stateJson(now), true);
  } else {
    Serial.printf("[mqtt] connect failed rc=%d\r\n", mqtt.state());
  }
}

// ── public API (non-blocking: WiFi never stalls lightning detection) ──
static WiFiManager          *s_wm = nullptr;
static WiFiManagerParameter *pHost, *pPort, *pUser, *pPass;
static bool                  s_setupDone = false;

static void onParamsSave() {
  mqttHost = pHost->getValue();
  mqttPort = (uint16_t)atoi(pPort->getValue());
  if (mqttPort == 0) mqttPort = 1883;
  mqttUser = pUser->getValue();
  mqttPass = pPass->getValue();
  saveMqttCfg();
  if (mqttHost.length()) { mqtt.setServer(mqttHost.c_str(), mqttPort); mqtt.setBufferSize(1024); }
  Serial.printf("[net] MQTT cfg saved: %s:%u\r\n", mqttHost.c_str(), mqttPort);
}

void netBegin() {
  // Stable device id from the factory-burned eFuse MAC (always available, never
  // zero) — WiFi.macAddress() can read 0 right after boot → a changing id would
  // spawn duplicate devices in Home Assistant.
  uint64_t efuse = ESP.getEfuseMac();
  snprintf(s_id, sizeof s_id, "%04x%08x",
           (uint16_t)(efuse >> 32), (uint32_t)efuse);
  snprintf(s_topState, sizeof s_topState, "flashbee/%s/state", s_id);   // precompute once
  snprintf(s_topAvail, sizeof s_topAvail, "flashbee/%s/status", s_id);
  loadMqttCfg();
  if (mqttHost.length()) { mqtt.setServer(mqttHost.c_str(), mqttPort); mqtt.setBufferSize(1024); }
  logBegin();          // mount LittleFS + load persisted strike history into the ring

  // register HTTP routes once (handlers are stored now; server is (re)started in
  // startServices() on each WiFi (re)connect so a blip can't leave it dead).
  server.on("/", handleRoot);
  server.on("/api", handleApi);
  server.on("/strikes.csv", handleLog);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/resetwifi", HTTP_POST, handleResetWifi);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);          // keep WiFi responsive (no modem-sleep stalls)
  s_wm = new WiFiManager();
  s_wm->setConfigPortalBlocking(false);          // NEVER block the main loop
  s_wm->setConfigPortalTimeout(0);               // keep the setup AP available
  static char portStr[8]; snprintf(portStr, sizeof portStr, "%u", mqttPort);
  pHost = new WiFiManagerParameter("mhost", "MQTT broker IP", mqttHost.c_str(), 40);
  pPort = new WiFiManagerParameter("mport", "MQTT port", portStr, 6);
  pUser = new WiFiManagerParameter("muser", "MQTT user (optional)", mqttUser.c_str(), 24);
  pPass = new WiFiManagerParameter("mpass", "MQTT pass (optional)", mqttPass.c_str(), 24);
  s_wm->addParameter(pHost); s_wm->addParameter(pPort);
  s_wm->addParameter(pUser); s_wm->addParameter(pPass);
  s_wm->setSaveParamsCallback(onParamsSave);
  s_wm->autoConnect("FlashBee-setup");           // returns immediately
  Serial.println("[net] WiFi starting — connect to AP 'FlashBee-setup' to configure");
}

void netStartPortal() { if (s_wm) s_wm->startConfigPortal("FlashBee-setup"); }

// (Re)start mDNS + the web server. Called on EVERY WiFi (re)connect — a WiFi
// blip over hours kills the listening socket + the mDNS responder, so just
// re-running it once (the old code) left the UI dead while the device ran on.
static void startServices() {
  Serial.printf("[net] WiFi '%s'  IP %s  (http://flashbee.local)\r\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  MDNS.end();                                    // drop the stale responder, re-announce
  if (MDNS.begin("flashbee")) MDNS.addService("http", "tcp", 80);
  server.close();                                // drop any stale listen socket…
  server.begin();                                // …and re-bind port 80
  timeNtpBegin();                                // (re)start SNTP
}

void netService(uint32_t now) {
  if (s_resetWifi && now - s_resetAt > 800) {    // deferred WiFi wipe + reboot
    if (s_wm) s_wm->resetSettings();
    WiFi.disconnect(true, true);
    delay(150);
    ESP.restart();
  }
  if (s_wm) s_wm->process();                     // service portal / reconnect in bg

  bool conn = netConnected();
  if (conn && !s_setupDone) startServices();     // (re)connected → (re)bind server + mDNS
  s_setupDone = conn;                            // track WiFi state (re-arm on drop)

  // WiFi-stuck watchdog: auto-reconnect normally recovers, but if we've been
  // down for >90 s, force a reconnect (and reboot if that keeps failing).
  static uint32_t downSince = 0;
  if (!conn) {
    if (!downSince) downSince = now ? now : 1;
    else if (now - downSince > 90000) { Serial.println("[net] WiFi down >90s — reconnect"); WiFi.reconnect(); downSince = now; }
    return;
  }
  downSince = 0;

  server.handleClient();
  if (mqttHost.length()) {
    mqttEnsure(now);
    mqtt.loop();
    if (mqtt.connected() && now - s_lastPub > 10000) {
      s_lastPub = now;
      mqtt.publish(s_topState, stateJson(now), true);
    }
  }
  static uint32_t lastHeap = 0;
  if (now - lastHeap > 60000) {                  // heap-health heartbeat + self-heal
    lastHeap = now;
    uint32_t fh = ESP.getFreeHeap();
    Serial.printf("[net] heap free=%u min=%u up=%lus strikes=%d spurious=%d\r\n",
                  (unsigned)fh, (unsigned)ESP.getMinFreeHeap(),
                  (unsigned long)(now / 1000), sensor.strikeCount, sensor.spuriousCount);
    if (fh < 20000) {                            // a slow leak would hang the server → reboot
      Serial.println("[net] heap critically low — restarting (strike log + RTC persist)");
      delay(80); ESP.restart();
    }
  }
}

void netOnStrike(uint32_t now, uint8_t km, uint32_t energy) {
  uint32_t ts = timeValid() ? (uint32_t)timeNow() : 0;   // real epoch (0 = clock not set yet)
  pushHist(ts, km, energy);
  logAppend(ts, km, energy);                             // persist to LittleFS (survives reboot/flash)
  s_total++;
  if (mqtt.connected()) mqtt.publish(s_topState, stateJson(now), true);
}
