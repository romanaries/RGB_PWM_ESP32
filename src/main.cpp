#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <math.h>

static const char* FW_VERSION = "0.1.0";
static const char* DEVICE_NAME = "RGB-LAB";
static const char* CONFIG_AP_NAME = "RGB-LAB-SETUP";

constexpr uint8_t BOOT_BUTTON_PIN = 9;

// LuatOS ESP32-C3 board, pins 1-16 side.
constexpr uint8_t PWM_R_PIN = 0;
constexpr uint8_t PWM_G_PIN = 1;
constexpr uint8_t PWM_B_PIN = 12;

constexpr uint8_t PWM_R_CH = 0;
constexpr uint8_t PWM_G_CH = 1;
constexpr uint8_t PWM_B_CH = 2;
constexpr uint32_t PWM_FREQ_HZ = 1000;
constexpr uint8_t PWM_BITS = 12;
constexpr uint16_t PWM_MAX = (1U << PWM_BITS) - 1;

constexpr float GAMMA = 2.2f;
constexpr uint32_t FADE_MS = 900;
constexpr uint32_t FRAME_MS = 20;
constexpr uint32_t SAVE_DELAY_MS = 600;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_S = 20;

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
};

Channel channels[] = {
  {"r", "Red", PWM_R_PIN, PWM_R_CH, 0, true, 0.0f, 0.0f, 0.0f, 0},
  {"g", "Green", PWM_G_PIN, PWM_G_CH, 0, true, 0.0f, 0.0f, 0.0f, 0},
  {"b", "Blue", PWM_B_PIN, PWM_B_CH, 0, true, 0.0f, 0.0f, 0.0f, 0},
};

Preferences prefs;
WebServer server(80);

bool masterEnabled = true;
bool settingsDirty = false;
uint32_t lastFrameMs = 0;
uint32_t dirtySinceMs = 0;

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
  ledcWrite(ch.pwmChannel, gammaDuty(ch.currentPercent));
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
  prefs.begin("rgbpwm", false);
  masterEnabled = prefs.getBool("master", true);
  for (Channel& ch : channels) {
    String valueKey = String(ch.id) + "_pct";
    String enabledKey = String(ch.id) + "_en";
    ch.percent = clampPercent(prefs.getUChar(valueKey.c_str(), 0));
    ch.enabled = prefs.getBool(enabledKey.c_str(), true);
    ch.currentPercent = desiredPercent(ch);
    ch.fadeStartPercent = ch.currentPercent;
    ch.fadeTargetPercent = ch.currentPercent;
  }
}

void saveSettingsNow() {
  prefs.putBool("master", masterEnabled);
  for (Channel& ch : channels) {
    String valueKey = String(ch.id) + "_pct";
    String enabledKey = String(ch.id) + "_en";
    prefs.putUChar(valueKey.c_str(), ch.percent);
    prefs.putBool(enabledKey.c_str(), ch.enabled);
  }
  settingsDirty = false;
}

void saveSettingsIfNeeded() {
  if (settingsDirty && millis() - dirtySinceMs >= SAVE_DELAY_MS) {
    saveSettingsNow();
  }
}

String ipText() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
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
    item["pin"] = ch.pin;
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
  markDirty();
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
  markDirty();
  writeStateJson();
}

void handleMaster() {
  if (server.hasArg("on")) {
    masterEnabled = server.arg("on").toInt() != 0;
  } else {
    masterEnabled = !masterEnabled;
  }
  refreshTargets(true);
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
    for (Channel& ch : channels) ch.enabled = true;
  }
  refreshTargets(true);
  writeStateJson();
}

void handleResetWifi() {
  server.send(200, "text/plain", "WiFi credentials cleared. Rebooting to setup portal.");
  delay(300);
  WiFiManager wm;
  wm.resetSettings();
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
:root{color-scheme:dark;--bg:#0b0d10;--panel:#141820;--line:#29313d;--text:#e8edf2;--muted:#8f9aaa;--red:#ff475d;--green:#39d98a;--blue:#4a8dff;--accent:#f5c542}
*{box-sizing:border-box}body{margin:0;background:#0b0d10;color:var(--text);font-family:Inter,Segoe UI,Arial,sans-serif;letter-spacing:0}
.wrap{width:min(1120px,100%);margin:0 auto;padding:20px}.top{display:flex;align-items:center;justify-content:space-between;gap:16px;border-bottom:1px solid var(--line);padding-bottom:16px;margin-bottom:18px}
h1{font-size:24px;margin:0;font-weight:650}.meta{color:var(--muted);font-size:13px;margin-top:5px}.master{display:flex;gap:10px;align-items:center}
button{border:1px solid var(--line);background:#1b2029;color:var(--text);border-radius:8px;padding:10px 14px;font-weight:650;cursor:pointer;min-height:40px}button:hover{border-color:#536071}.primary{background:#243145;border-color:#40516a}.danger{background:#311b22;border-color:#64303c}.active{border-color:var(--accent);box-shadow:0 0 0 1px rgba(245,197,66,.35) inset}
.grid{display:grid;grid-template-columns:1fr;gap:14px}.channel{border:1px solid var(--line);background:var(--panel);border-radius:8px;padding:16px;display:grid;grid-template-columns:120px 1fr 92px 96px;gap:14px;align-items:center}
.label{display:flex;align-items:center;gap:10px;font-weight:750}.dot{width:14px;height:14px;border-radius:50%}.r .dot{background:var(--red)}.g .dot{background:var(--green)}.b .dot{background:var(--blue)}
input[type=range]{width:100%;accent-color:var(--accent)}input[type=number]{width:92px;background:#0f1218;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:10px;font-size:16px;text-align:right}
.pct{position:relative}.pct:after{content:'%';position:absolute;right:10px;top:50%;transform:translateY(-50%);color:var(--muted);pointer-events:none}.pct input{padding-right:28px}
.bar{height:8px;background:#0e1117;border-radius:999px;overflow:hidden;border:1px solid #202733}.fill{height:100%;width:0%;transition:width .16s linear}.r .fill{background:var(--red)}.g .fill{background:var(--green)}.b .fill{background:var(--blue)}
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
const ids=['r','g','b'];
function channelHtml(ch){return `<article class="channel ${ch.id}"><div><div class="label"><i class="dot"></i>${ch.name}</div><div class="meta">GPIO ${ch.pin}</div></div><div><input id="range_${ch.id}" type="range" min="0" max="100" value="${ch.value}" oninput="edit('${ch.id}',this.value)"><div class="bar"><div id="fill_${ch.id}" class="fill"></div></div></div><label class="pct"><input id="num_${ch.id}" type="number" min="0" max="100" value="${ch.value}" oninput="edit('${ch.id}',this.value)"></label><button id="en_${ch.id}" onclick="toggle('${ch.id}')"></button></article>`}
async function api(path){const r=await fetch(path,{cache:'no-store'});state=await r.json();render();}
function render(){if(!state)return;document.getElementById('meta').textContent=`${state.device} ${state.version}`;document.getElementById('ip').textContent=state.ip;document.getElementById('rssi').textContent=`${state.rssi} dBm`;document.getElementById('fade').textContent=`${state.fade_ms} ms`;document.getElementById('gamma').textContent=state.gamma;const m=document.getElementById('masterBtn');m.textContent=state.master?'Master ON':'Master OFF';m.classList.toggle('active',state.master);const box=document.getElementById('channels');if(!box.childElementCount)box.innerHTML=state.channels.map(channelHtml).join('');for(const ch of state.channels){document.getElementById(`range_${ch.id}`).value=ch.value;document.getElementById(`num_${ch.id}`).value=ch.value;document.getElementById(`fill_${ch.id}`).style.width=`${ch.output}%`;const b=document.getElementById(`en_${ch.id}`);b.textContent=ch.enabled?'ON':'OFF';b.classList.toggle('active',ch.enabled);}}
function edit(id,value){value=Math.max(0,Math.min(100,parseInt(value||0,10)));document.getElementById(`range_${id}`).value=value;document.getElementById(`num_${id}`).value=value;clearTimeout(timers[id]);timers[id]=setTimeout(()=>api(`/api/set?ch=${id}&value=${value}`),120);}
function toggle(id){api(`/api/toggle?ch=${id}`)}function preset(n){api(`/api/preset?name=${n}`)}document.getElementById('masterBtn').onclick=()=>api('/api/master');
function resetWifi(){if(confirm('Resetovat Wi-Fi nastavenia a restartovat ESP32?'))fetch('/api/reset-wifi',{method:'POST'}).then(()=>alert('ESP32 sa restartuje do setup rezimu.'))}
api('/api/state');setInterval(()=>api('/api/state'),2000);
</script>
</body>
</html>
)HTML";

void handleRoot() {
  sendNoCacheHeaders();
  server.send_P(200, "text/html", INDEX_HTML);
}

void setupPwm() {
  for (Channel& ch : channels) {
    ledcSetup(ch.pwmChannel, PWM_FREQ_HZ, PWM_BITS);
    ledcAttachPin(ch.pin, ch.pwmChannel);
    applyPwm(ch);
  }
}

void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setHostname(DEVICE_NAME);
  wm.setConfigPortalTimeout(180);
  wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_S);

  const bool forcePortal = digitalRead(BOOT_BUTTON_PIN) == LOW;
  bool connected = false;
  if (forcePortal) {
    connected = wm.startConfigPortal(CONFIG_AP_NAME);
  } else {
    connected = wm.autoConnect(CONFIG_AP_NAME);
  }

  if (!connected) {
    Serial.println("WiFi setup timeout, restarting.");
    delay(500);
    ESP.restart();
  }

  Serial.printf("WiFi connected: %s, IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void setupServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/set", HTTP_GET, handleSet);
  server.on("/api/toggle", HTTP_GET, handleToggle);
  server.on("/api/master", HTTP_GET, handleMaster);
  server.on("/api/preset", HTTP_GET, handlePreset);
  server.on("/api/reset-wifi", HTTP_POST, handleResetWifi);
  server.begin();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n\n%s RGB PWM controller %s\n", DEVICE_NAME, FW_VERSION);

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  loadSettings();
  setupPwm();
  setupWifi();
  setupServer();
}

void loop() {
  server.handleClient();
  tickFades();
  saveSettingsIfNeeded();
}
