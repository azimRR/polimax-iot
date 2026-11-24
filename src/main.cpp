#include <Arduino.h>
#include <LiquidCrystal.h>

// LCD 1602 in 4-bit mode: RS, E, D4, D5, D6, D7
// RW is tied to GND (write-only), so the LCD never drives 5V into the ESP32.
LiquidCrystal lcd(32, 33, 25, 26, 27, 14);

unsigned long lastUpdateMs = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Hello Polimax!");

  lcd.begin(16, 2);
  lcd.print("Hello Polimax!");
}

void loop() {
  // Non-blocking: update the uptime once per second without delay()
  if (millis() - lastUpdateMs >= 1000) {
    lastUpdateMs = millis();
    unsigned long seconds = millis() / 1000;

    lcd.setCursor(0, 1);
    lcd.print("Uptime: ");
    lcd.print(seconds);
    lcd.print("s   ");

    Serial.print("Uptime: ");
    Serial.println(seconds);
  }
}
