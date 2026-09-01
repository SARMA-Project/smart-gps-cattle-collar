#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Serial Debug
#define DEBUG_BAUD 115200

// GPS HardwareSerial Config (UART2)
#define GPS_RX_PIN 16  // ESP32 RX2 connected to NEO-6M TX
#define GPS_TX_PIN 17  // ESP32 TX2 connected to NEO-6M RX
#define GPS_BAUD 9600

// Wi-Fi Station Configuration
// Change these to your local Wi-Fi router settings if desired
static const char* WIFI_SSID = "YOUR_WIFI_SSID";
static const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
#define WIFI_STA_TIMEOUT_MS 10000

// Wi-Fi Access Point Fallback Configuration
static const char* AP_SSID = "GPS-TRACKER";
static const char* AP_PASSWORD = "GPS123456";
static const IPAddress AP_LOCAL_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

// Web Server & WebSocket Configuration
#define HTTP_PORT 80
#define WEBSOCKET_PATH "/ws"
#define BROADCAST_INTERVAL_MS 1000
#define DEBUG_PRINT_INTERVAL_MS 3000

// GPS Timeout thresholds (ms)
#define GPS_NO_DATA_TIMEOUT_MS 3000

#endif // CONFIG_H
