# 🦯 SmartCane — AI-Assisted Navigation Stick for the Visually Impaired

> **HackLondon 2026** — Built in 24 hours

**Team:** Name 1 · Name 2 · Name 3

---

## What is SmartCane?

SmartCane is a smart walking stick designed to help visually impaired individuals navigate indoor campus environments independently. It combines an ESP32 microcontroller, ultrasonic obstacle detection, BLE wireless communication, and a companion web app to provide real-time audio and haptic guidance.

---

## The Problem

Standard white canes help detect obstacles at ground level but provide no navigation guidance. Visually impaired students on a university campus face significant challenges:

- No awareness of obstacles at chest/head height
- No indoor navigation system (GPS is too inaccurate indoors)
- No instant way to call for help in an emergency

---

## Our Solution

A three-feature system built into a walking stick:

### 1. 🗺️ Indoor Campus Navigation
- Caregiver or user maps rooms by walking to each door and capturing GPS
- During navigation, phone compass + GPS auto-track direction every second
- ESP32 buzzer plays directional beeps (right/left/straight/arrived)
- No internet required after rooms are mapped

### 2. 📡 Ultrasonic Obstacle Detection
- HC-SR04 sensor detects obstacles at stick height continuously
- Three warning levels with distinct buzzer patterns on the stick
- Phone vibrates with increasing intensity as obstacle gets closer
- Navigation automatically pauses during danger alerts

### 3. 🆘 SOS Emergency Button
- Physical button on the stick triggers an emergency alert
- ESP32 sends signal via Bluetooth to companion phone app
- Phone automatically opens WhatsApp with GPS location pre-filled
- Message sent to saved emergency contact with one tap

---

## Hardware

| Component | Purpose | Pin |
|---|---|---|
| ESP32 Dev Module | Main microcontroller + BLE | — |
| HC-SR04 Ultrasonic | Obstacle detection | TRIG=GPIO4, ECHO=GPIO5 |
| Passive Buzzer | Audio directional feedback | GPIO21 |
| Push Button | SOS emergency trigger | GPIO2 |

**Wiring diagram:**
```
HC-SR04  TRIG → GPIO4
HC-SR04  ECHO → GPIO5   (add 1kΩ series resistor — ESP32 is 3.3V!)
HC-SR04  VCC  → 5V
HC-SR04  GND  → GND
Buzzer   (+)  → GPIO21
Buzzer   (-)  → GND
Button        → GPIO2 + GND  (internal pullup used)
```

---

## Software

### ESP32 Firmware (`SmartCane_Final.ino`)

Written in Arduino C++. Handles:
- BLE server advertising as `SmartCane`
- Receiving navigation commands from phone (`R/L/S/U/A/X`)
- Ultrasonic readings every 300ms with 3-sample averaging
- Obstacle level notifications to phone (`OBS:CLEAR/NOTICE/WARN/DANGER`)
- SOS button detection with 10-second cooldown
- All buzzer patterns for navigation + obstacle + SOS

**Libraries required:**
- `BLEDevice`, `BLEUtils`, `BLEServer`, `BLE2902` (built-in ESP32 BLE)

### Web App (`SmartCane_Final.html`)

Single-file Progressive Web App. No install needed — works in Android Chrome over HTTPS.

**Three tabs:**

| Tab | Function |
|---|---|
| NAVIGATE | Select destination room, compass auto-tracking, obstacle alert bar |
| 🆘 SOS | Save emergency contact, monitor SOS status |
| ROOMS | Add/delete campus rooms, export/import room maps |

**BLE Protocol:**

| Direction | Message | Meaning |
|---|---|---|
| Phone → Stick | `R` | Turn right |
| Phone → Stick | `L` | Turn left |
| Phone → Stick | `S` | Go straight |
| Phone → Stick | `A` | Arrived |
| Phone → Stick | `X` | Stop |
| Stick → Phone | `OBS:DANGER` | Obstacle < 30cm |
| Stick → Phone | `OBS:WARN` | Obstacle < 80cm |
| Stick → Phone | `OBS:NOTICE` | Obstacle < 150cm |
| Stick → Phone | `OBS:CLEAR` | Path clear |
| Stick → Phone | `SOS` | Button pressed |

---

## How to Run

### ESP32
1. Open `SmartCane_Final.ino` in Arduino IDE
2. Select board: `ESP32 Dev Module`
3. Upload to ESP32

### Web App
1. Host `SmartCane_Final.html` on GitHub Pages (HTTPS required)
2. Open on **Android Chrome** (iOS not supported — Web Bluetooth blocked)
3. Tap **BLE** → Connect to `SmartCane`
4. Go to **ROOMS** tab → Add campus rooms
5. Go to **🆘 SOS** tab → Save emergency contact
6. Go to **NAVIGATE** tab → Select destination

> ⚠️ Must use HTTPS — BLE and GPS APIs are blocked on `file://` and `http://`

---

## Navigation Algorithm

```
Every 1 second:
  1. Get current GPS position
  2. Calculate bearing to destination room
  3. Compare with phone compass heading
  4. Compute angle difference (diff)

  If |diff| < 45°  → STRAIGHT  (go forward)
  If diff > 45°   → RIGHT     (turn right)
  If diff < -45°  → LEFT      (turn left)

  Smoothing: direction must be consistent for 3 ticks before command fires
  Hysteresis: once turning, must come within 30° to re-center (prevents flip-flopping)
  Repeat interval: same direction re-sent every 5s as reminder
```

---

## Obstacle Detection Logic

```
Distance < 30cm  → DANGER   5 rapid beeps + phone vibrates 5x
Distance < 80cm  → WARNING  2 medium beeps + phone vibrates 2x
Distance < 150cm → NOTICE   1 soft beep + phone vibrates 1x
Distance > 150cm → CLEAR    Silent

Navigation beeps pause during DANGER/WARNING so alerts are not drowned out.
Buzzer only fires on level change (except DANGER which always fires).
```

---

## Limitations & Future Work

- **GPS accuracy indoors** — typical ±3–10m; arrival threshold set to 4m
- **Compass interference** — metal structures near the ESP32 can affect heading
- **BLE range** — approximately 10m; phone must stay near stick
- **WhatsApp SOS** — requires phone app open in background for auto-send

**Future improvements:**
- SIM800L module for standalone SOS calls without a phone
- OLED display on stick showing navigation status
- Machine learning for obstacle classification (stairs, doors, people)
- Native Android app for background BLE monitoring

---

## Tech Stack

| Layer | Technology |
|---|---|
| Microcontroller | ESP32 (Arduino framework) |
| Wireless | Bluetooth Low Energy (BLE 4.2) |
| Sensor | HC-SR04 Ultrasonic |
| Frontend | Vanilla HTML/CSS/JS (PWA) |
| BLE API | Web Bluetooth API |
| Maps/Location | Browser Geolocation API + Google Maps |
| Emergency | WhatsApp `wa.me` deep link |
| Hosting | GitHub Pages (HTTPS) |

---

## Built at HackLondon 2026

This project was built in 24 hours at HackLondon. We wanted to tackle a real accessibility problem using affordable hardware (total BOM cost < £15) and no backend infrastructure — everything runs on the ESP32 and a static web page.

---

*SmartCane — because independence shouldn't require perfect vision.*