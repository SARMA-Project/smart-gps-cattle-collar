# ESP32 NEO-6M Smart GPS Live Tracking Dashboard

A production-grade real-time GPS telemetry and geofencing dashboard built for **ESP32 DevKit** and the **u-blox NEO-6M GPS module**. The ESP32 parses NMEA sentences via HardwareSerial, manages Wi-Fi connectivity (Station Mode with Access Point fallback), and hosts a responsive web dashboard with **WebSockets**, **Leaflet Interactive Maps**, **Path Tracking**, and **Custom Geofence / Range Alerting (e.g. 15m diameter/radius)**.

---

## Key Features

- 🔵 **100% Bluetooth Operation**: All Wi-Fi SSIDs, passwords, and hotspot requirements removed! Uses ESP32 Bluetooth Serial (SPP) `CowCollar-BT`.
- 🛰️ **Hardware NMEA Parsing**: Powered by `TinyGPSPlus` on ESP32 HardwareSerial (`UART2` on GPIO16/GPIO17).
- 📡 **Bluetooth Proximity & Distance Calibration**:
  - Calibrated Bluetooth RSSI path-loss model tuned specifically for ESP32 Bluetooth SPP:
    - High Signal / Close Range (-48 dBm) $\rightarrow$ 0.5m
    - Medium Range (-58 to -78 dBm) $\rightarrow$ 1.5m to 11.0m
    - Perimeter Boundary (-79 to -85 dBm) $\rightarrow$ 11.0m to 18.0m (15m geofence breach alert)
- 🛡️ **Custom Geofence & Range Breach Alert**:
  - Set custom safe perimeter radius (e.g. 15 meters).
  - Triggers a **glowing red visual alert banner** + **audio alarm tone** when device goes out of range.
- 📶 **Calibrated Mobile Hotspot Wi-Fi RSSI Distance**:
  - Precision piecewise path-loss curve tuned for mobile phone hotspots:
    - Right next to phone (-45 to -50 dBm) $\rightarrow$ 0.5m (0–1m accuracy)
    - Near range (-51 to -68 dBm) $\rightarrow$ 0.5m to 4.5m
    - Boundary threshold (-79 to -85 dBm) $\rightarrow$ 11m to 18m (15m alert limit at ~ -82 dBm)
  - Eliminates near-field 5-meter error when holding collar close to the hotspot phone.
- 🗺️ **Interactive Leaflet Map**: Displays dark theme map tiles, live location marker, accuracy circle (HDOP based), auto-center control, and dynamic Google Maps external link button.
- 🛣️ **Route Path Tracking**: Start/Stop/Clear path tracking polyline capped at 500 valid points to optimize ESP32 memory.
- 📊 **9 Live Telemetry Cards**: Latitude, Longitude, Speed (converted Knots -> km/h), Altitude (m), Satellites count, HDOP, Course (°), Fix Type (3D/2D/NO FIX), and UTC Time.
- 🔒 **Zero Fake Data**: Strictly distinguishes `NO GPS DATA`, `SEARCHING FOR SATELLITES`, and `GPS FIXED`. Never displays `0.000000, 0.000000` as a fake location.

---

## Hardware Assembly & Wiring Diagram

### Hardware Components
1. **ESP32 DevKit** Microcontroller
2. **u-blox NEO-6M GPS Module** + Passive GPS Patch Antenna
3. **18650 Li-ion Battery** + **HW-131 Charger/Protection Board** (5V / 3.3V Output)

### ASCII Wiring Schematic

```
                   +-----------------------+
                   |  HW-131 Battery Board |
                   |  (18650 Battery)      |
                   +-----------+-----------+
                               |
                               | 5V / 3.3V Power & GND
                               v
                       +---------------+
                       |  ESP32 DevKit |
                       +-------+-------+
                               |
       +-----------------------+-----------------------+
       | Power: 3.3V/5V -> VCC                         |
       | Ground: GND -> GND                            |
       | UART2 RX: GPIO 16  <================ TX  NEO-6M GPS
       | UART2 TX: GPIO 17  ================> RX  Breakout
       +-----------------------------------------------+
                               |
                               | RF Connector / IPEX
                               v
                    [ GPS Patch Antenna ]
                  (Point upward to sky)
```

### Pin Mapping Table

| NEO-6M Pin | ESP32 Pin | Logic Level / Details |
| :--- | :--- | :--- |
| **VCC** | **3.3V** (or 5V) | Power input for module board regulator |
| **GND** | **GND** | Common ground connection |
| **TX** | **GPIO 16 (RX2)** | ESP32 receives raw NMEA serial telemetry (3.3V logic) |
| **RX** | **GPIO 17 (TX2)** | ESP32 transmits commands to GPS module |
| **Antenna** | **U.FL / IPEX** | Connect passive patch antenna directly to RF connector |

> ⚠️ **Important Wiring Note**: ESP32 GPIOs use **3.3V logic**. The NEO-6M TX pin outputs 3.3V logic signals, which are directly safe for ESP32 GPIO16 (RX2).

---

## Project File Structure

```
c:\Users\sarma\Desktop\projects\cow collar with gps\
├── platformio.ini         # PlatformIO build configuration & library dependencies
├── README.md              # Documentation & hardware guide
├── src/
│   ├── config.h           # Centralized configuration (Wi-Fi, Pins, Intervals)
│   ├── gps_manager.h      # GPS hardware parser & state machine header
│   ├── gps_manager.cpp    # NMEA parsing & JSON formatting implementation
│   ├── wifi_manager.h     # Wi-Fi Station & AP fallback manager header
│   ├── wifi_manager.cpp   # Wi-Fi connection handler implementation
│   ├── web_server.h       # AsyncWebServer & WebSocket manager header
│   ├── web_server.cpp     # Web routes & WebSocket broadcast implementation
│   └── main.cpp           # Main firmware setup & non-blocking execution loop
└── data/
    ├── index.html         # Responsive dark dashboard frontend layout
    ├── style.css          # Glassmorphic CSS style & alert animations
    └── app.js             # Client JS engine, Leaflet map & Geofence alert engine
```

---

## Configuration (`src/config.h`)

You can customize Wi-Fi router credentials and default settings in `src/config.h`:

```cpp
// Wi-Fi Station Configuration
static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Fallback Access Point Configuration
static const char* AP_SSID = "GPS-TRACKER";
static const char* AP_PASSWORD = "GPS123456";

// Hardware Serial Pins
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define GPS_BAUD   9600
```

---

## Compilation & Upload Instructions

### Prerequisites
- [Python 3.x](https://www.python.org/)
- [PlatformIO CLI](https://platformio.org/) (`python -m pip install platformio`)

### Step 1: Compile Firmware
To compile the firmware executable binary:
```bash
python -m platformio run
```

### Step 2: Upload Firmware to ESP32
Connect your ESP32 via USB and execute:
```bash
python -m platformio run --target upload
```

### Step 3: Upload Web Dashboard Files (LittleFS)
Upload the `/data` folder static web assets (`index.html`, `style.css`, `app.js`) to the ESP32 flash memory:
```bash
python -m platformio run --target uploadfs
```

---

## First-Time GPS Fix & Cold Start

When powering on the NEO-6M for the first time:
1. **Antenna Placement**: Position the ceramic patch antenna facing upward with an **unobstructed view of the open sky** (outdoors or near a window).
2. **Cold Start Timing**: The initial satellite lock (Cold Start) typically takes **30 seconds to 3 minutes** depending on satellite visibility.
3. **LED Indicators**:
   - **Solid Red LED on NEO-6M**: Power OK, searching for satellites.
   - **Blinking Red LED on NEO-6M**: Valid 3D GPS satellite fix acquired!
4. **Dashboard Display**:
   - While acquiring satellites, the dashboard top bar shows `GPS: SEARCHING` and telemetry cards display `--`.
   - Once locked, status turns green `GPS: FIXED`, and live coordinates appear on the map.

---

## Web API & WebSocket Specifications

### WebSocket Stream (`ws://<ESP32_IP>/ws`)
Broadcasts a JSON object approximately once per second:

```json
{
  "valid": true,
  "state": "GPS FIXED",
  "quality": "Excellent",
  "latitude": 11.0168,
  "longitude": 76.9558,
  "altitude": 412.3,
  "speedKmh": 42.6,
  "course": 127.4,
  "satellites": 8,
  "hdop": 1.2,
  "fixType": "3D",
  "gpsTime": "10:42:31 UTC",
  "gpsDate": "2026-09-01",
  "charsProcessed": 18450,
  "failedChecksum": 0,
  "uartActive": true
}
```

### REST Endpoint `GET /api/gps`
Returns the current GPS state JSON.

---

## Troubleshooting Guide

| Issue | Cause | Solution |
| :--- | :--- | :--- |
| **Serial Monitor says "NO DATA RECEIVED"** | Swapped RX/TX wires | Connect **NEO-6M TX -> ESP32 GPIO16 (RX2)** and **NEO-6M RX -> ESP32 GPIO17 (TX2)** |
| **Dashboard says "SEARCHING FOR SATELLITES"** | Indoors / blocked sky | Move antenna outdoors or near a window with clear sky view |
| **Wi-Fi failed to connect** | Incorrect SSID/password | Check credentials in `src/config.h` or connect directly to AP hotspot (`GPS-TRACKER`) |
| **Dashboard page won't load** | Filesystem not flashed | Run `python -m platformio run --target uploadfs` to upload LittleFS data |

---

## License & Credits
Built for custom IoT hardware tracking applications using open-source libraries: `TinyGPSPlus`, `ESPAsyncWebServer`, `ArduinoJson`, and `Leaflet.js`.
