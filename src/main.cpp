#include <Arduino.h>
#include <LiquidCrystal.h>

// LCD 1602 in 4-bit mode: RS, E, D4, D5, D6, D7
// RW is tied to GND (write-only), so the LCD never drives 5V into the ESP32.
LiquidCrystal lcd(32, 33, 25, 26, 27, 14);

// Machine signal simulated by a push button between GPIO13 and GND.
// INPUT_PULLUP keeps the pin HIGH when released; pressing pulls it LOW.
const uint8_t MACHINE_PIN = 13;
const unsigned long DEBOUNCE_MS = 30;

bool machineRunning = false;
bool lastRawReading = HIGH;
unsigned long lastRawChangeMs = 0;
unsigned long stateSinceMs = 0;
unsigned long lastScreenMs = 0;

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

void setup() {
  Serial.begin(115200);
  Serial.println("Polimax machine monitor");

  pinMode(MACHINE_PIN, INPUT_PULLUP);

  lcd.begin(16, 2);
  showState();
}

void loop() {
  updateMachineState();

  // Non-blocking: refresh the timer line once per second without delay()
  if (millis() - lastScreenMs >= 1000) {
    lastScreenMs = millis();
    lcd.setCursor(0, 1);
    lcd.print("for ");
    lcd.print((millis() - stateSinceMs) / 1000);
    lcd.print("s        ");
  }
}
