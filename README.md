# ⚙️ ECO CARE SYSTEMS — Motor Rotation Controller

> **Industrial-Grade Motor Control System with Safety Interlocks**

A professional motor rotation control system built for **Eco Care Systems** featuring password protection, Hall sensor feedback, emergency stop, LCD interface, and dual-relay direction control — designed for real industrial deployment.

---

## ✨ Features

| Feature | Description |
|---|---|
| 🔐 **Password Protection** | 5-digit PIN entry on boot |
| 🔄 **3 Motor Modes** | Clockwise, Anti-Clockwise, CW+CCW |
| 📡 **Hall Sensor Feedback** | Precise rotation counting |
| 🛑 **Emergency Stop** | Hardware button + triple keypress |
| 🖥️ **20×4 LCD Interface** | Real-time progress display |
| ⌨️ **4×4 Keypad** | Full menu navigation |
| 🔒 **Safety Self-Test** | Motor check on every boot |
| 🔁 **Repeat Last Operation** | One-key repeat functionality |

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────┐
│              STATE MACHINE                   │
│                                             │
│  STARTUP → PASSWORD → MODE_SELECT           │
│      ↓           ↓           ↓              │
│  DANGER    WRONG_PWD    MODE_CONFIRM        │
│                              ↓              │
│                          RUNNING            │
│                         ↙       ↘           │
│                      DONE    EMERGENCY      │
└─────────────────────────────────────────────┘
```

---

## 🔌 Hardware

### Pin Configuration
| Component | Pin |
|---|---|
| CW Relay | D2 |
| CCW Relay | D3 |
| Keypad Rows | D4, D5, D6, D7 |
| Keypad Cols | D8, D9, D10, D11 |
| Emergency Stop | D12 (INPUT_PULLUP) |
| Start Button | D13 (INPUT_PULLUP) |
| Hall Sensor | A0 (Analog) |
| LCD I2C | SDA=A4, SCL=A5 |

### Components Required
- Arduino UNO
- 20×4 I2C LCD (address 0x27)
- 4×4 Matrix Keypad
- 2× Relay Module (Active HIGH)
- Hall Effect Sensor (49E)
- 2× Momentary Push Buttons (NO)
- DC Motor

---

## 🔒 Safety Features

### Hardware Safety
- **Both relays NEVER on simultaneously** — hardcoded protection
- **500µs delay** between relay switching — prevents shoot-through
- **Emergency Stop Button** on Pin 12 — instant motor kill
- **Boot self-test** — verifies motor runs in both directions before operation

### Software Safety
- Debounced button reading (50ms)
- Hall sensor hysteresis (600/550 threshold)
- State machine prevents invalid transitions
- Password locks system from unauthorized use

---

## 📋 Keypad Controls

```
PASSWORD SCREEN:        OPERATION SCREEN:
  0-9  = Enter digit      1    = Clockwise mode
  A    = Confirm          2    = Anti-Clockwise mode
  B    = Backspace        3    = CW + CCW mode
                          0-9  = Enter rotation count
DURING MOTOR RUN:         B    = Backspace
  C×3  = Emergency Stop   A    = Confirm & Start
  ESTOP BTN = Instant     C×1  = Set to 0
              Stop         C×2  = Clear all
```

---

## 📊 LCD Display Screens

**Password Entry**
```
 ECO CARE SYSTEMS   
--------------------
Enter Password: [O] 
PWD: *****          
```

**Motor Running**
```
>> RUNNING : CW     
Dir:CW    0045/0100 
[########--]  80%   
[ESTOP-BTN] = STOP  
```

**Done Screen**
```
-- OPERATION DONE --
--------------------
 [1] Back to Menu   
 [2] Repeat Last Op 
```

---

## 🛠️ Libraries Required

```cpp
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
```

Install via Arduino IDE Library Manager:
- `LiquidCrystal I2C` by Frank de Brabander
- `Keypad` by Mark Stanley

---

## 🚀 Getting Started

1. Wire components per pin map above
2. Install required libraries
3. Upload `eco_care_motor.ino` to Arduino UNO
4. Power on — self-test runs automatically
5. Enter password: `13579`
6. Select mode and enter rotation count

---

## 🛠️ Skills Demonstrated

`Arduino UNO` `State Machine Design` `Relay Control` `Hall Sensor` `I2C LCD` `4×4 Keypad` `Debouncing` `Safety Interlock` `Industrial Control` `Embedded C`

---

## 👨‍💻 Developer

**Bhagathi Gangadhar**
B.Tech ECE — RVR & JC College of Engineering, Guntur
📧 gangadharbhagathi@gmail.com
🔗 [LinkedIn](https://linkedin.com/in/gangadhara-bagathi-44657429a)
