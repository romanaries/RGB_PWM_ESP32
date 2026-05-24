#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>

static const char* FW_VERSION = "0.1.4";
static const char* DEVICE_NAME = "RGB-LAB";
static const char* CONFIG_AP_NAME = "RGB-LAB-SETUP";
static const char* MDNS_NAME = "rgb-lab";
static const char* WIFI_PREF_NAMESPACE = "rgbpwm";
static const char* STATE_PREF_NAMESPACE = "rgbstate";

// LuatOS ESP32-C3 board: avoid strapping, USB, UART0 and on-board LED pins.
constexpr uint8_t PWM_R_PIN = 3;
constexpr uint8_t PWM_G_PIN = 4;
constexpr uint8_t PWM_B_PIN = 5;
constexpr uint8_t PWM_W_PIN = 7;
constexpr uint8_t WIFI_LED_PIN = 13;

constexpr uint8_t PWM_R_CH = 0;
constexpr uint8_t PWM_G_CH = 1;
constexpr uint8_t PWM_B_CH = 2;
constexpr uint8_t PWM_W_CH = 3;
constexpr uint32_t PWM_FREQ_HZ = 1000;
constexpr uint8_t PWM_BITS = 12;
constexpr uint16_t PWM_MAX = (1U << PWM_BITS) - 1;

constexpr float GAMMA = 2.2f;
constexpr uint32_t FADE_MS = 900;
constexpr uint32_t FRAME_MS = 20;
constexpr uint32_t SAVE_DELAY_MS = 600;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_S = 20;
constexpr uint32_t AP_LED_BLINK_MS = 500;

struct Channel {
  const char* id;
  const char* name;
  uint8_t pin;
  uint8_t pwmChannel;
  uint8_t percent;
  bool enabled;
  float currentPercent;
  float fadeStartPercent;
  float fadeTargetPercent;
  uint32_t fadeStartMs;
  uint16_t lastDuty;
};

Channel channels[] = {
  {"r", "Red", PWM_R_PIN, PWM_R_CH, 0, true, 0.0f, 0.0f, 0.0f, 0, UINT16_MAX},
  {"g", "Green", PWM_G_PIN, PWM_G_CH, 0, true, 0.0f, 0.0f, 0.0f, 0, UINT16_MAX},
  {"b", "Blue", PWM_B_PIN, PWM_B_CH, 0, true, 0.0f, 0.0f, 0.0f, 0, UINT16_MAX},
  {"w", "White", PWM_W_PIN, PWM_W_CH, 0, true, 0.0f, 0.0f, 0.0f, 0, UINT16_MAX},
};

constexpr size_t CHANNEL_COUNT = sizeof(channels) / sizeof(channels[0]);
constexpr uint32_t STATE_MAGIC = 0x52474257;  // RGBW
constexpr uint8_t STATE_VERSION = 1;

struct StoredState {
  uint32_t magic;
  uint8_t version;
  uint8_t master;
  uint8_t percent[CHANNEL_COUNT];
  uint8_t enabled[CHANNEL_COUNT];
};

WebServer server(80);
DNSServer dnsServer;

bool masterEnabled = true;
bool settingsDirty = false;
bool fallbackPortalActive = false;
uint32_t lastFrameMs = 0;
uint32_t dirtySinceMs = 0;
uint32_t lastWifiLedMs = 0;
bool wifiLedState = false;

Channel* findChannel(const String& id) {
  for (Channel& ch : channels) {
    if (id == ch.id) return &ch;
  }
  return nullptr;
}

uint8_t clampPercent(int value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return static_cast<uint8_t>(value);
}

float desiredPercent(const Channel& ch) {
  if (!masterEnabled || !ch.enabled) return 0.0f;
  return static_cast<float>(ch.percent);
}

uint16_t gammaDuty(float percent) {
  if (percent <= 0.0f) return 0;
  if (percent >= 100.0f) return PWM_MAX;
  const float normalized = percent / 100.0f;
  return static_cast<uint16_t>(roundf(powf(normalized, GAMMA) * PWM_MAX));
}

void markDirty() {
  settingsDirty = true;
  dirtySinceMs = millis();
}

void startFade(Channel& ch, float target) {
  ch.fadeStartPercent = ch.currentPercent;
  ch.fadeTargetPercent = target;
  ch.fadeStartMs = millis();
}

void refreshTargets(bool store) {
  for (Channel& ch : channels) {
    startFade(ch, desiredPercent(ch));
  }
  if (store) markDirty();
}

void applyPwm(Channel& ch) {
  const uint16_t duty = gammaDuty(ch.currentPercent);
  if (duty == ch.lastDuty) return;
  ledcWrite(ch.pwmChannel, duty);
  ch.lastDuty = duty;
}

void tickFades() {
  const uint32_t now = millis();
  if (now - lastFrameMs < FRAME_MS) return;
  lastFrameMs = now;

  for (Channel& ch : channels) {
    const uint32_t elapsed = now - ch.fadeStartMs;
    if (elapsed >= FADE_MS) {
      ch.currentPercent = ch.fadeTargetPercent;
    } else {
      const float t = static_cast<float>(elapsed) / static_cast<float>(FADE_MS);
      ch.currentPercent = ch.fadeStartPercent + (ch.fadeTargetPercent - ch.fadeStartPercent) * t;
    }
    applyPwm(ch);
  }
}

void loadSettings() {
  Preferences prefs;
  if (!prefs.begin(STATE_PREF_NAMESPACE, true)) {
    Serial.println("Failed to open settings storage, using defaults.");
    return;
  }

  StoredState state = {};
  const size_t storedLen = prefs.getBytesLength("state");
  const bool loaded = storedLen == sizeof(state) &&
                      prefs.getBytes("state", &state, sizeof(state)) == sizeof(state) &&
                      state.magic == STATE_MAGIC &&
                      state.version == STATE_VERSION;
  prefs.end();

  if (loaded) {
    masterEnabled = state.master != 0;
    for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
      channels[i].percent = clampPercent(state.percent[i]);
      channels[i].enabled = state.enabled[i] != 0;
    }
  }

  Serial.printf("Loaded master=%s", masterEnabled ? "on" : "off");
  for (Channel& ch : channels) {
    ch.currentPercent = desiredPercent(ch);
    ch.fadeStartPercent = ch.currentPercent;
    ch.fadeTargetPercent = ch.currentPercent;
    Serial.printf(", %s=%u%%/%s", ch.id, ch.percent, ch.enabled ? "on" : "off");
  }
  Serial.println(loaded ? " -> loaded" : " -> defaults");
}

void saveSettingsNow() {
  Preferences prefs;
  if (!prefs.begin(STATE_PREF_NAMESPACE, false)) {
    Serial.println("Failed to open settings storage for write.");
    return;
  }

  StoredState state = {};
  state.magic = STATE_MAGIC;
  state.version = STATE_VERSION;
  state.master = masterEnabled ? 1 : 0;
  for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
    state.percent[i] = channels[i].percent;
    state.enabled[i] = channels[i].enabled ? 1 : 0;
  }

  Serial.printf("Saving master=%s", masterEnabled ? "on" : "off");
  for (Channel& ch : channels) {
    Serial.printf(", %s=%u%%/%s", ch.id, ch.percent, ch.enabled ? "on" : "off");
  }

  const size_t written = prefs.putBytes("state", &state, sizeof(state));
  Serial.println(written == sizeof(state) ? " -> stored" : " -> failed");
  prefs.end();
  settingsDirty = written != sizeof(state);
}

void persistSettingsSoon() {
  markDirty();
  saveSettingsNow();
}

void saveSettingsIfNeeded() {
  if (settingsDirty && millis() - dirtySinceMs >= SAVE_DELAY_MS) {
    saveSettingsNow();
  }
}

String ipText() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  if (fallbackPortalActive) return WiFi.softAPIP().toString();
  return "not connected";
}

void writeStateJson() {
  JsonDocument doc;
  doc["device"] = DEVICE_NAME;
  doc["version"] = FW_VERSION;
  doc["ip"] = ipText();
  doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["master"] = masterEnabled;
  doc["fade_ms"] = FADE_MS;
  doc["gamma"] = GAMMA;
  JsonArray arr = doc["channels"].to<JsonArray>();
  for (Channel& ch : channels) {
    JsonObject item = arr.add<JsonObject>();
    item["id"] = ch.id;
    item["name"] = ch.name;
    item["value"] = ch.percent;
    item["enabled"] = ch.enabled;
    item["output"] = static_cast<int>(roundf(ch.currentPercent));
    item["duty"] = ch.lastDuty == UINT16_MAX ? gammaDuty(ch.currentPercent) : ch.lastDuty;
    item["pin"] = ch.pin;
    item["pwm_channel"] = ch.pwmChannel;
  }

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void sendNoCacheHeaders() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
}

void handleState() {
  sendNoCacheHeaders();
  writeStateJson();
}

void handleSet() {
  Channel* ch = findChannel(server.arg("ch"));
  if (!ch || !server.hasArg("value")) {
    server.send(400, "application/json", "{\"error\":\"bad request\"}");
    return;
  }
  ch->percent = clampPercent(server.arg("value").toInt());
  startFade(*ch, desiredPercent(*ch));
  persistSettingsSoon();
  writeStateJson();
}

void handleToggle() {
  Channel* ch = findChannel(server.arg("ch"));
  if (!ch) {
    server.send(400, "application/json", "{\"error\":\"bad channel\"}");
    return;
  }
  if (server.hasArg("on")) {
    ch->enabled = server.arg("on").toInt() != 0;
  } else {
    ch->enabled = !ch->enabled;
  }
  startFade(*ch, desiredPercent(*ch));
  persistSettingsSoon();
  writeStateJson();
}

void handleMaster() {
  if (server.hasArg("on")) {
    masterEnabled = server.arg("on").toInt() != 0;
  } else {
    masterEnabled = !masterEnabled;
  }
  refreshTargets(false);
  persistSettingsSoon();
  writeStateJson();
}

void handlePreset() {
  const String name = server.arg("name");
  if (name == "off") {
    masterEnabled = false;
  } else {
    masterEnabled = true;
    channels[0].percent = name == "red" || name == "white" ? 100 : 0;
    channels[1].percent = name == "green" || name == "white" ? 100 : 0;
    channels[2].percent = name == "blue" || name == "white" ? 100 : 0;
    channels[3].percent = name == "white" ? 100 : 0;
    for (Channel& ch : channels) ch.enabled = true;
  }
  refreshTargets(false);
  persistSettingsSoon();
  writeStateJson();
}

void handleResetWifi() {
  server.send(200, "text/plain", "WiFi credentials cleared. Rebooting to setup portal.");
  delay(300);
  Preferences prefs;
  if (!prefs.begin(WIFI_PREF_NAMESPACE, false)) {
    ESP.restart();
  }
  prefs.remove("wifi_ssid");
  prefs.remove("wifi_pass");
  prefs.end();
  ESP.restart();
}

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="sk">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RGB Lab PWM</title>
<style>
:root{color-scheme:dark;--bg:#0b0d10;--panel:#141820;--line:#29313d;--text:#e8edf2;--muted:#8f9aaa;--red:#ff475d;--green:#39d98a;--blue:#4a8dff;--white:#f3efe2;--accent:#f5c542}
*{box-sizing:border-box}body{margin:0;background:#0b0d10;color:var(--text);font-family:Inter,Segoe UI,Arial,sans-serif;letter-spacing:0}
.wrap{width:min(1120px,100%);margin:0 auto;padding:20px}.top{display:flex;align-items:center;justify-content:space-between;gap:16px;border-bottom:1px solid var(--line);padding-bottom:16px;margin-bottom:18px}
h1{font-size:24px;margin:0;font-weight:650}.meta{color:var(--muted);font-size:13px;margin-top:5px}.master{display:flex;gap:10px;align-items:center}
button{border:1px solid var(--line);background:#1b2029;color:var(--text);border-radius:8px;padding:10px 14px;font-weight:650;cursor:pointer;min-height:40px}button:hover{border-color:#536071}.primary{background:#243145;border-color:#40516a}.danger{background:#311b22;border-color:#64303c}.active{border-color:var(--accent);box-shadow:0 0 0 1px rgba(245,197,66,.35) inset}
.grid{display:grid;grid-template-columns:1fr;gap:14px}.channel{border:1px solid var(--line);background:var(--panel);border-radius:8px;padding:16px;display:grid;grid-template-columns:120px 1fr 92px 96px;gap:14px;align-items:center}
.label{display:flex;align-items:center;gap:10px;font-weight:750}.dot{width:14px;height:14px;border-radius:50%}.r .dot{background:var(--red)}.g .dot{background:var(--green)}.b .dot{background:var(--blue)}.w .dot{background:var(--white)}
input[type=range]{width:100%;accent-color:var(--accent)}input[type=number]{width:92px;background:#0f1218;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:10px;font-size:16px;text-align:right}
.pct{position:relative}.pct:after{content:'%';position:absolute;right:10px;top:50%;transform:translateY(-50%);color:var(--muted);pointer-events:none}.pct input{padding-right:28px}
.bar{height:8px;background:#0e1117;border-radius:999px;overflow:hidden;border:1px solid #202733}.fill{height:100%;width:0%;transition:width .16s linear}.r .fill{background:var(--red)}.g .fill{background:var(--green)}.b .fill{background:var(--blue)}.w .fill{background:var(--white)}
.tools{display:flex;flex-wrap:wrap;gap:10px;margin:18px 0}.status{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-top:18px}.cell{border:1px solid var(--line);border-radius:8px;padding:12px;background:#10141b}.cell span{display:block;color:var(--muted);font-size:12px}.cell strong{font-size:15px}
@media(max-width:760px){.wrap{padding:14px}.top{align-items:flex-start;flex-direction:column}.master{width:100%;display:grid;grid-template-columns:1fr 1fr}.channel{grid-template-columns:1fr;gap:12px}.channel button,.master button{width:100%}.status{grid-template-columns:1fr 1fr}h1{font-size:22px}}
</style>
</head>
<body>
<main class="wrap">
  <section class="top">
    <div><h1>RGB Lab PWM</h1><div class="meta" id="meta">ESP32-C3 controller</div></div>
    <div class="master"><button id="masterBtn" class="primary">Master</button><button onclick="resetWifi()" class="danger">Wi-Fi reset</button></div>
  </section>
  <section class="grid" id="channels"></section>
  <section class="tools">
    <button onclick="preset('white')">White</button><button onclick="preset('red')">Red</button><button onclick="preset('green')">Green</button><button onclick="preset('blue')">Blue</button><button onclick="preset('off')">Off</button>
  </section>
  <section class="status">
    <div class="cell"><span>IP</span><strong id="ip">-</strong></div>
    <div class="cell"><span>Wi-Fi RSSI</span><strong id="rssi">-</strong></div>
    <div class="cell"><span>Fade</span><strong id="fade">-</strong></div>
    <div class="cell"><span>Gamma</span><strong id="gamma">-</strong></div>
  </section>
</main>
<script>
let state=null,timers={};
function channelHtml(ch){return `<article class="channel ${ch.id}"><div><div class="label"><i class="dot"></i>${ch.name}</div><div class="meta">GPIO ${ch.pin}</div></div><div><input id="range_${ch.id}" type="range" min="0" max="100" value="${ch.value}" oninput="edit('${ch.id}',this.value)" onchange="commitEdit('${ch.id}',this.value)"><div class="bar"><div id="fill_${ch.id}" class="fill"></div></div></div><label class="pct"><input id="num_${ch.id}" type="number" min="0" max="100" value="${ch.value}" oninput="edit('${ch.id}',this.value)" onchange="commitEdit('${ch.id}',this.value)"></label><button id="en_${ch.id}" onclick="toggle('${ch.id}')"></button></article>`}
async function api(path){const r=await fetch(path,{cache:'no-store'});state=await r.json();render();}
function render(){if(!state)return;document.getElementById('meta').textContent=`${state.device} ${state.version}`;document.getElementById('ip').textContent=state.ip;document.getElementById('rssi').textContent=`${state.rssi} dBm`;document.getElementById('fade').textContent=`${state.fade_ms} ms`;document.getElementById('gamma').textContent=state.gamma;const m=document.getElementById('masterBtn');m.textContent=state.master?'Turn all off':'Turn all on';m.classList.toggle('active',state.master);const box=document.getElementById('channels');if(!box.childElementCount)box.innerHTML=state.channels.map(channelHtml).join('');for(const ch of state.channels){document.getElementById(`range_${ch.id}`).value=ch.value;document.getElementById(`num_${ch.id}`).value=ch.value;document.getElementById(`fill_${ch.id}`).style.width=`${ch.output}%`;const b=document.getElementById(`en_${ch.id}`);b.textContent=ch.enabled?'Turn off':'Turn on';b.classList.toggle('active',ch.enabled);}}
function clampValue(value){return Math.max(0,Math.min(100,parseInt(value||0,10)))}
function commitEdit(id,value){value=clampValue(value);clearTimeout(timers[id]);return api(`/api/set?ch=${id}&value=${value}`)}
function edit(id,value){value=clampValue(value);document.getElementById(`range_${id}`).value=value;document.getElementById(`num_${id}`).value=value;clearTimeout(timers[id]);timers[id]=setTimeout(()=>commitEdit(id,value),350);}
function toggle(id){api(`/api/toggle?ch=${id}`)}function preset(n){api(`/api/preset?name=${n}`)}document.getElementById('masterBtn').onclick=()=>api('/api/master');
function resetWifi(){if(confirm('Clear Wi-Fi settings and restart the ESP32?'))fetch('/api/reset-wifi',{method:'POST'}).then(()=>alert('The ESP32 will restart in setup mode.'))}
api('/api/state');setInterval(()=>api('/api/state'),2000);
</script>
</body>
</html>
)HTML";

const char FALLBACK_PORTAL_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="sk">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RGB Lab Wi-Fi Setup</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#0b0d10;color:#e8edf2;font-family:Segoe UI,Arial,sans-serif}.wrap{width:min(520px,100%);margin:0 auto;padding:22px}h1{font-size:24px;margin:0 0 8px}.muted{color:#8f9aaa;margin-bottom:18px}.box{border:1px solid #29313d;background:#141820;border-radius:8px;padding:16px}label{display:block;margin:12px 0 6px;color:#aab4c2}input{width:100%;padding:12px;border-radius:8px;border:1px solid #29313d;background:#0f1218;color:#e8edf2;font-size:16px}button{width:100%;margin-top:16px;padding:12px;border-radius:8px;border:1px solid #40516a;background:#243145;color:#e8edf2;font-weight:700}
</style>
</head>
<body><main class="wrap"><h1>RGB Lab Wi-Fi Setup</h1><p class="muted">Enter the local Wi-Fi network. The ESP32 will restart after saving.</p><form class="box" method="post" action="/wifi-save"><label>SSID</label><input name="ssid" autocomplete="off" required><label>Password</label><input name="pass" type="password"><button>Save and restart</button></form></main></body>
</html>
)HTML";

void handleRoot() {
  sendNoCacheHeaders();
  if (fallbackPortalActive) {
    server.send_P(200, "text/html", FALLBACK_PORTAL_HTML);
    return;
  }
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleFallbackPortal() {
  sendNoCacheHeaders();
  server.send_P(200, "text/html", FALLBACK_PORTAL_HTML);
}

void handleCaptiveProbe() {
  sendNoCacheHeaders();
  server.send_P(200, "text/html", FALLBACK_PORTAL_HTML);
}

void handleNotFound() {
  if (fallbackPortalActive) {
    handleCaptiveProbe();
    return;
  }
  server.send(404, "text/plain", "Not found");
}

void handleWifiSave() {
  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");
  if (ssid.length() == 0) {
    server.send(400, "text/plain", "SSID is required");
    return;
  }
  Preferences prefs;
  if (!prefs.begin(WIFI_PREF_NAMESPACE, false)) {
    server.send(500, "text/plain", "Failed to open Wi-Fi storage");
    return;
  }
  prefs.putString("wifi_ssid", ssid);
  prefs.putString("wifi_pass", pass);
  prefs.end();
  server.send(200, "text/plain", "Wi-Fi saved. The ESP32 will restart.");
  delay(300);
  ESP.restart();
}

void setupPwm() {
  for (Channel& ch : channels) {
    ledcSetup(ch.pwmChannel, PWM_FREQ_HZ, PWM_BITS);
    ledcAttachPin(ch.pin, ch.pwmChannel);
    applyPwm(ch);
  }
}

void setupStatusLed() {
  pinMode(WIFI_LED_PIN, OUTPUT);
  digitalWrite(WIFI_LED_PIN, LOW);
}

void updateStatusLed() {
  if (fallbackPortalActive) {
    const uint32_t now = millis();
    if (now - lastWifiLedMs >= AP_LED_BLINK_MS) {
      lastWifiLedMs = now;
      wifiLedState = !wifiLedState;
      digitalWrite(WIFI_LED_PIN, wifiLedState ? HIGH : LOW);
    }
    return;
  }

  digitalWrite(WIFI_LED_PIN, WiFi.status() == WL_CONNECTED ? HIGH : LOW);
}

void setupWifi() {
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  Preferences prefs;
  if (!prefs.begin(WIFI_PREF_NAMESPACE, true)) {
    Serial.println("Failed to open WiFi storage, starting setup AP.");
    fallbackPortalActive = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(CONFIG_AP_NAME);
    delay(100);
    dnsServer.start(53, "*", WiFi.softAPIP());
    return;
  }
  const String ssid = prefs.getString("wifi_ssid", "");
  const String pass = prefs.getString("wifi_pass", "");
  prefs.end();

  if (ssid.length() == 0) {
    Serial.println("No WiFi credentials saved, starting setup AP.");
    fallbackPortalActive = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(CONFIG_AP_NAME);
    delay(100);
    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial.printf("Setup AP active: %s, IP: %s\n", CONFIG_AP_NAME, WiFi.softAPIP().toString().c_str());
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_NAME);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_CONNECT_TIMEOUT_S * 1000UL) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connect failed, starting setup AP.");
    fallbackPortalActive = true;
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(CONFIG_AP_NAME);
    delay(100);
    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial.printf("Setup AP active: %s, IP: %s\n", CONFIG_AP_NAME, WiFi.softAPIP().toString().c_str());
    return;
  }

  Serial.printf("WiFi connected: %s, IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS active: http://%s.local/\n", MDNS_NAME);
  }
}

void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_GET, handleFallbackPortal);
  server.on("/generate_204", HTTP_GET, handleCaptiveProbe);
  server.on("/gen_204", HTTP_GET, handleCaptiveProbe);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe);
  server.on("/library/test/success.html", HTTP_GET, handleCaptiveProbe);
  server.on("/ncsi.txt", HTTP_GET, handleCaptiveProbe);
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveProbe);
  server.on("/fwlink", HTTP_GET, handleCaptiveProbe);
  server.on("/wifi-save", HTTP_POST, handleWifiSave);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/set", HTTP_GET, handleSet);
  server.on("/api/toggle", HTTP_GET, handleToggle);
  server.on("/api/master", HTTP_GET, handleMaster);
  server.on("/api/preset", HTTP_GET, handlePreset);
  server.on("/api/reset-wifi", HTTP_POST, handleResetWifi);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n\n%s RGB PWM controller %s\n", DEVICE_NAME, FW_VERSION);

  loadSettings();
  setupStatusLed();
  setupPwm();
  setupWifi();
  setupServer();
}

void loop() {
  if (fallbackPortalActive) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  tickFades();
  updateStatusLed();
  saveSettingsIfNeeded();
}
