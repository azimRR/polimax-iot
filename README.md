# Polimax Industrial IoT Device

ESP32 firmware that monitors a production machine and reports its state to a dashboard.

## Current features
- Machine RUNNING / STOPPED signal (debounced input on GPIO13, simulated with a push button)
- Production pulse counting from an IR sensor on GPIO34
- LCD 1602 status display (4-bit mode)
- Wi-Fi setup portal (`Polimax-Setup` hotspot, no credentials in code)
- Built-in web dashboard with working time, downtime, availability, pulse rate and event history

## Build
PlatformIO + Arduino framework, board `esp32dev`.

```sh
pio run -t upload
```

## Wiring (ESP32 DevKit 38-pin)
| Part | Pin | ESP32 |
|---|---|---|
| LCD RS / E | | G32 / G33 |
| LCD D4–D7 | | G25 / G26 / G27 / G14 |
| LCD VDD / VSS, RW, V0, BLK | | V5 / GND |
| LCD BLA | | 3V3 |
| Machine button | | G13 ↔ GND |
| IR sensor OUT / VCC / GND | | G34 / 3V3 / GND |

Monitoring only — never connect the ESP32 directly to industrial 24 V or mains signals.
