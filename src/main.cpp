#include <Arduino.h>
#include <ESPmDNS.h>
#include <LiquidCrystal.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "dashboard.h"

// LCD 1602 in 4-bit mode: RS, E, D4, D5, D6, D7
// RW is tied to GND (write-only), so the LCD never drives 5V into the ESP32.
LiquidCrystal lcd(32, 33, 25, 26, 27, 14);

// Machine signal simulated by a push button between GPIO13 and GND.
// INPUT_PULLUP keeps the pin HIGH when released; pressing pulls it LOW.
const uint8_t MACHINE_PIN = 13;
const unsigned long DEBOUNCE_MS = 30;

// Production pulses from the IR sensor on GPIO34 (input-only pin).
// The sensor pulls OUT low while a mark passes. A pulse counts only when the
// level stays LOW for PULSE_STABLE_MS, so flicker at the detection edge is ignored.
const uint8_t PULSE_PIN = 34;
const unsigned long PULSE_STABLE_MS = 20;

uint32_t pulseCount = 0;
bool pulseActive = false;
bool lastPulseRaw = HIGH;
unsigned long pulseRawChangeMs = 0;

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

// Working time / downtime totals since boot
unsigned long workingMs = 0;
unsigned long downtimeMs = 0;
unsigned long lastTickMs = 0;

// History for the dashboard charts: one sample every 10 s, last 30 minutes.
const unsigned long SAMPLE_INTERVAL_MS = 10000;
const uint16_t HISTORY_SIZE = 180;

struct Sample {
  uint32_t t;       // device uptime in seconds at the end of the interval
  uint16_t pulses;  // pulses counted during the interval
  uint8_t runPct;   // % of the interval the machine was running
};

Sample history[HISTORY_SIZE];
uint16_t historyHead = 0;
uint16_t historyCount = 0;
unsigned long lastSampleMs = 0;
unsigned long intervalRunMs = 0;
uint32_t pulsesAtLastSample = 0;

// Last starts/stops for the event list
const uint8_t EVENT_SIZE = 20;

struct Event {
  uint32_t t;
  bool running;
};

Event events[EVENT_SIZE];
uint8_t eventHead = 0;
uint8_t eventCount = 0;

// Pulses in the last minute: sliding window of 6 x 10 s buckets
uint16_t minuteBuckets[6] = {0};

void showState() {
  lcd.setCursor(0, 0);
  lcd.print(machineRunning ? "RUNNING         " : "STOPPED         ");
}

void recordEvent(bool running) {
  events[eventHead] = {millis() / 1000, running};
  eventHead = (eventHead + 1) % EVENT_SIZE;
  if (eventCount < EVENT_SIZE) eventCount++;
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
    recordEvent(machineRunning);
    Serial.println(machineRunning ? "Machine started" : "Machine stopped");
    showState();
  }
}

void updatePulses() {
  bool raw = digitalRead(PULSE_PIN);
  if (raw != lastPulseRaw) {
    lastPulseRaw = raw;
    pulseRawChangeMs = millis();
  }
  if (millis() - pulseRawChangeMs >= PULSE_STABLE_MS) {
    bool active = (lastPulseRaw == LOW);
    if (active && !pulseActive) {
      pulseCount++;
    }
    pulseActive = active;
  }
}

void updateTimers() {
  unsigned long now = millis();
  unsigned long elapsed = now - lastTickMs;
  lastTickMs = now;
  if (machineRunning) {
    workingMs += elapsed;
    intervalRunMs += elapsed;
  } else {
    downtimeMs += elapsed;
  }

  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    unsigned long interval = now - lastSampleMs;
    lastSampleMs = now;

    uint32_t delta = pulseCount - pulsesAtLastSample;
    pulsesAtLastSample = pulseCount;

    history[historyHead] = {now / 1000, (uint16_t)min<uint32_t>(delta, 65535),
                            (uint8_t)(intervalRunMs * 100 / interval)};
    historyHead = (historyHead + 1) % HISTORY_SIZE;
    if (historyCount < HISTORY_SIZE) historyCount++;
    intervalRunMs = 0;

    memmove(minuteBuckets, minuteBuckets + 1, sizeof(minuteBuckets) - sizeof(minuteBuckets[0]));
    minuteBuckets[5] = (uint16_t)min<uint32_t>(delta, 65535);
  }
}

uint32_t pulsesLastMinute() {
  uint32_t sum = pulseCount - pulsesAtLastSample;  // current, unfinished bucket
  for (uint8_t i = 1; i < 6; i++) sum += minuteBuckets[i];
  return sum;
}

void handleStatus() {
  String json = "{\"machine_status\":\"";
  json += machineRunning ? "RUNNING" : "STOPPED";
  json += "\",\"state_seconds\":";
  json += (millis() - stateSinceMs) / 1000;
  json += ",\"working_seconds\":";
  json += workingMs / 1000;
  json += ",\"downtime_seconds\":";
  json += downtimeMs / 1000;
  json += ",\"pulse_count\":";
  json += pulseCount;
  json += ",\"pulses_last_minute\":";
  json += pulsesLastMinute();
  json += ",\"uptime_seconds\":";
  json += millis() / 1000;
  json += ",\"wifi_rssi\":";
  json += WiFi.RSSI();
  json += "}";
  server.send(200, "application/json", json);
}

void handleHistory() {
  String json;
  json.reserve(4096);
  json = "{\"now\":";
  json += millis() / 1000;
  json += ",\"interval\":";
  json += SAMPLE_INTERVAL_MS / 1000;
  json += ",\"capacity\":";
  json += HISTORY_SIZE;
  json += ",\"samples\":[";
  for (uint16_t i = 0; i < historyCount; i++) {
    const Sample& s = history[(historyHead + HISTORY_SIZE - historyCount + i) % HISTORY_SIZE];
    if (i) json += ',';
    json += '[';
    json += s.t;
    json += ',';
    json += s.pulses;
    json += ',';
    json += s.runPct;
    json += ']';
  }
  json += "],\"events\":[";
  for (uint8_t i = 0; i < eventCount; i++) {
    const Event& e = events[(eventHead + EVENT_SIZE - eventCount + i) % EVENT_SIZE];
    if (i) json += ',';
    json += '[';
    json += e.t;
    json += ',';
    json += e.running ? 1 : 0;
    json += ']';
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void startWebServer() {
  MDNS.begin(HOSTNAME);
  server.on("/", []() { server.send_P(200, "text/html", DASHBOARD_HTML); });
  server.on("/api/status", handleStatus);
  server.on("/api/history", handleHistory);
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
  pinMode(PULSE_PIN, INPUT);  // the sensor module has its own pull-up

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
  updatePulses();
  updateTimers();

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
      lcd.print("Pulses: ");
      lcd.print(pulseCount);
      lcd.print("        ");
    }
  }
}
