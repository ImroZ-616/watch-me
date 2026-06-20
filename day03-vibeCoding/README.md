# ⌚ ESP32 Multi-Function Smartwatch Prototype

A modular, multi-state embedded firmware blueprint for a custom ESP32-based smartwatch. [cite_start]This project handles responsive menu navigation across multiple distinct applications utilizing a non-blocking execution cycle, software debouncing, and automated network time synchronization[cite: 8, 9, 20, 24, 25].

---

## 🚀 Core Architectural Features

* [cite_start]**Non-Blocking Execution Engine:** Built without software delays (`delay()`), enabling the processor to update time variations, refresh visual buffers, and read inputs concurrently[cite: 20].
* [cite_start]**Polled Software Input Filter:** Implements uniform hardware debouncing via state tracking structures to prevent switch mechanical ripple and duplicate triggering[cite: 8, 9, 59].
* [cite_start]**Automated NTP Time Sync:** Utilizes internal Wi-Fi to establish a connection at launch, pools atomic time via Network Time Protocol (NTP) to configure local variables to Indian Standard Time (IST), and turns off the radio peripheral to maximize battery conservation[cite: 16, 17, 18, 19].

---

## 📱 Watch Operating System Modes
[MODE 0: WATCH] ──> [MODE 1: AI ASSIST] ──> [MODE 2: MORSE COMM] ──> [MODE 3: TIMER]

1. **`MODE_WATCH` (Home Screen):** Displays a live digital precision clock format (HH:MM:SS) synced from global NTP nodes[cite: 3, 18, 21, 22].
2. **`MODE_AI` (Voice Gateway):** Visual canvas interface structured to support streaming audio packets[cite: 3, 23, 46].
3. **`MODE_MORSE` (Morse Encoder):** Captures hardware tapping thresholds to log raw dots and dashes directly to a terminal stream[cite: 3, 14, 23, 37, 38, 39, 40].
4. **`MODE_TIMER` (Countdown Timer):** Allows targeted selection of hours, minutes, and seconds[cite: 3, 4, 7, 23, 51, 52, 53]. Once execution completes, the screen invokes an inverted high-contrast visual alarm alert[cite: 4, 6, 7, 41, 42, 48, 49, 50, 51].

---

## 🔌 Hardware Configuration & Pin Map

### 1. OLED Display Connections
| Component Pin | ESP32 Pin | Purpose |
| :---: | :---: | :---: |
| **VCC** | **3V3** | Logic Power |
| **GND** | **GND** | Common Ground |
| **SDA** | **GPIO 21** | I2C Data Line |
| **SCK** | **GPIO 22** | I2C Clock Line |

### 2. Tactile Push Buttons (`INPUT_PULLUP`) [cite: 19]
| Button Pin | Variable Label | System Action | Action Inside `TIMER` Mode |
| :---: | :---: | :--- | :--- |
| **GPIO 14** [cite: 3] | `BTN_MODE` [cite: 3] | Cycle OS Applications [cite: 25] | Cycle OS Applications [cite: 25] |
| **GPIO 4** [cite: 3] | `BTN_B1` [cite: 3] | *Disabled* | Start / Pause / Reset Alarm [cite: 26, 27, 28, 29] |
| **GPIO 18** [cite: 3] | `BTN_B2` [cite: 3] | *Disabled* | Increment Selected Value (+1) [cite: 30, 31, 32] |
| **GPIO 19** [cite: 3] | `BTN_B3` [cite: 3] | *Disabled* | Switch Target Field (HH ➔ MM ➔ SS) [cite: 33, 34] |
| **GPIO 23** [cite: 3] | `BTN_B4` [cite: 3] | Morse Signal Input Key [cite: 3, 37, 38] | Clear Values / Reset Timer [cite: 35, 36] |

---

## 🛠️ Software Integration & Installation

### Dependency Requirements
Ensure the following libraries are installed inside your Arduino IDE manager panel:
* `Wire.h` (Built-In microcontroller library) [cite: 1]
* `Adafruit_GFX.h` [cite: 1]
* `Adafruit_SSD1306.h` [cite: 1]
* `WiFi.h` (Built-In core library) [cite: 1]

### Deployment Instructions
1. Open the project file `time-v4.ino` inside your IDE environment.
2. Modify the target Wi-Fi definitions at the top of the file to match your routing gateway details[cite: 2]:
   ```cpp
   const char* ssid     = "YOUR_NETWORK_NAME";     
   const char* password = "YOUR_NETWORK_PASSWORD";