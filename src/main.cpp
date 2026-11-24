#include <Arduino.h>
#include <ESPmDNS.h>
#include <LiquidCrystal.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>

// LCD 1602 in 4-bit mode: RS, E, D4, D5, D6, D7
// RW is tied to GND (write-only), so the LCD never drives 5V into the ESP32.
LiquidCrystal lcd(32, 33, 25, 26, 27, 14);

// Machine signal simulated by a push button between GPIO13 and GND.
// INPUT_PULLUP keeps the pin HIGH when released; pressing pulls it LOW.
const uint8_t MACHINE_PIN = 13;
const unsigned long DEBOUNCE_MS = 30;

// Wi-Fi credentials are never stored in code: on first boot the ESP32 opens
// the "Polimax-Setup" hotspot, you pick your network on the phone, and the
// ESP32 remembers it in flash.
const char* SETUP_AP_NAME = "Polimax-Setup";
const char* HOSTNAME = "polimax";  // dashboard: http://polimax.local

WiFiManager wifiManager;
WebServer server(80);

bool machineRunning = false;
bool lastRawReading = HIGH;
unsigned long lastRawChangeMs = 0;
unsigned long stateSinceMs = 0;
unsigned long lastScreenMs = 0;
bool networkReady = false;

const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Polimax Machine</title>
<style>
body{margin:0;font-family:system-ui,sans-serif;background:#10141a;color:#e8edf2;display:grid;place-items:center;min-height:100vh}
.card{width:min(92vw,420px);background:#19202a;border-radius:18px;padding:28px;box-shadow:0 10px 30px #0006}
h1{margin:0 0 4px;font-size:15px;letter-spacing:.08em;text-transform:uppercase;color:#8a97a6}
.state{font-size:44px;font-weight:800;margin:14px 0 4px}
.run{color:#3ddc84}.stop{color:#ff5b5b}.off{color:#8a97a6}
.row{display:flex;justify-content:space-between;padding:8px 0;border-top:1px solid #2a3340;font-variant-numeric:tabular-nums}
.row span:first-child{color:#8a97a6}
</style></head><body><div class="card">
<h1>Polimax · Machine 1</h1>
<div id="state" class="state off">…</div>
<div class="row"><span>In this state</span><span id="since">–</span></div>
<div class="row"><span>Device uptime</span><span id="uptime">–</span></div>
<div class="row"><span>Wi-Fi signal</span><span id="rssi">–</span></div>
<div class="row"><span>Last update</span><span id="seen">–</span></div>
</div><script>
const $=id=>document.getElementById(id);
async function tick(){
  try{
    const d=await (await fetch('/api/status',{cache:'no-store'})).json();
    const s=$('state');s.textContent=d.machine_status;s.className='state '+(d.machine_status==='RUNNING'?'run':'stop');
    $('since').textContent=d.state_seconds+' s';$('uptime').textContent=d.uptime_seconds+' s';
    $('rssi').textContent=d.wifi_rssi+' dBm';$('seen').textContent=new Date().toLocaleTimeString();
  }catch(e){const s=$('state');s.textContent='OFFLINE';s.className='state off';}
}
tick();setInterval(tick,1000);
</script></body></html>)HTML";

void showState() {
  lcd.setCursor(0, 0);
  lcd.print(machineRunning ? "RUNNING         " : "STOPPED         ");
}

void updateMachineState() {
  bool raw = digitalRead(MACHINE_PIN);
  if (raw != lastRawReading) {
    lastRawReading = raw;
    lastRawChangeMs = millis();
  }

  // Accept the new level only after it has been stable for DEBOUNCE_MS
  bool running = (lastRawReading == LOW);
  if (running != machineRunning && millis() - lastRawChangeMs >= DEBOUNCE_MS) {
    machineRunning = running;
    stateSinceMs = millis();
    Serial.println(machineRunning ? "Machine started" : "Machine stopped");
    showState();
  }
}

void handleStatus() {
  String json = "{\"machine_status\":\"";
  json += machineRunning ? "RUNNING" : "STOPPED";
  json += "\",\"state_seconds\":";
  json += (millis() - stateSinceMs) / 1000;
  json += ",\"uptime_seconds\":";
  json += millis() / 1000;
  json += ",\"wifi_rssi\":";
  json += WiFi.RSSI();
  json += "}";
  server.send(200, "application/json", json);
}

void startWebServer() {
  MDNS.begin(HOSTNAME);
  server.on("/", []() { server.send_P(200, "text/html", DASHBOARD_HTML); });
  server.on("/api/status", handleStatus);
  server.begin();
  networkReady = true;

  Serial.print("Dashboard: http://");
  Serial.println(WiFi.localIP());
  Serial.println("Dashboard: http://polimax.local");
}

void setup() {
  Serial.begin(115200);
  Serial.println("Polimax machine monitor");

  pinMode(MACHINE_PIN, INPUT_PULLUP);

  lcd.begin(16, 2);
  showState();

  // Non-blocking: if no saved network, the setup hotspot runs in the
  // background while the machine signal keeps being monitored.
  WiFi.setHostname(HOSTNAME);
  wifiManager.setConfigPortalBlocking(false);
  if (wifiManager.autoConnect(SETUP_AP_NAME)) {
    startWebServer();
  } else {
    Serial.print("Wi-Fi setup: connect your phone to ");
    Serial.println(SETUP_AP_NAME);
  }
}

void loop() {
  updateMachineState();

  if (!networkReady) {
    if (wifiManager.process()) {
      startWebServer();
    }
  } else {
    server.handleClient();
  }

  // Non-blocking: refresh the second line once per second without delay()
  if (millis() - lastScreenMs >= 1000) {
    lastScreenMs = millis();
    lcd.setCursor(0, 1);
    if (!networkReady) {
      lcd.print("WiFi: setup     ");
    } else {
      lcd.print("for ");
      lcd.print((millis() - stateSinceMs) / 1000);
      lcd.print("s        ");
    }
  }
}
