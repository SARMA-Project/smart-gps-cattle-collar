#include <Arduino.h>
#include "config.h"
#include "gps_manager.h"
#include "wifi_manager.h"
#include "web_server.h"

GpsManager gpsManager;
WifiManager wifiManager;
WebServerManager webServerManager(gpsManager, wifiManager);

uint32_t lastDebugPrintTime = 0;

void setup() {
    Serial.begin(DEBUG_BAUD);
    delay(1000);

    Serial.println("\n=================================");
    Serial.println(" ESP32 NEO-6M GPS TRACKER ");
    Serial.println("=================================");

    // 1. Initialize GPS HardwareSerial UART2 (GPIO16 RX2, GPIO17 TX2)
    Serial.print("GPS UART: INITIALIZING (RX: GPIO");
    Serial.print(GPS_RX_PIN);
    Serial.print(", TX: GPIO");
    Serial.print(GPS_TX_PIN);
    Serial.println(")...");
    gpsManager.begin();
    Serial.println("GPS UART: OK (9600 Baud)");

    // 2. Initialize Wi-Fi (STA mode with fallback AP mode)
    wifiManager.begin();

    // 3. Initialize Web Server & WebSockets
    webServerManager.begin();

    Serial.println("=================================");
    Serial.print("SYSTEM READY. ACCESS DASHBOARD AT: ");
    Serial.println(wifiManager.getIpString());
    Serial.println("=================================\n");
}

void loop() {
    // 1. Process incoming GPS serial data continuously (non-blocking)
    gpsManager.update();

    // 2. Keep Wi-Fi connection healthy (non-blocking)
    wifiManager.update();

    // 3. Manage WebSockets & periodic client updates (non-blocking)
    webServerManager.update();

    // 4. Diagnostic debug log to Serial Monitor
    uint32_t now = millis();
    if (now - lastDebugPrintTime >= DEBUG_PRINT_INTERVAL_MS) {
        lastDebugPrintTime = now;

        GpsData data = gpsManager.getData();
        Serial.println("--- [DIAGNOSTIC STATUS] ---");
        Serial.printf("ESP32 Uptime: %lu s | Wi-Fi: %s (%s) | WS Clients: %u\n",
                      now / 1000,
                      wifiManager.getModeString(),
                      wifiManager.getIpString().c_str(),
                      webServerManager.getConnectedClientsCount());

        if (!gpsManager.hasHardwareDataReceived()) {
            Serial.println("GPS UART Status: NO DATA RECEIVED (Check wiring: NEO-6M TX -> ESP32 GPIO16)");
        } else {
            Serial.printf("GPS UART Status: DATA STREAMING (Chars parsed: %u)\n", data.charsProcessed);
        }

        Serial.printf("GPS Fix State  : %s | Satellites: %u | HDOP: %.2f\n",
                      gpsManager.getStateString(),
                      data.satellites,
                      data.hdop);

        if (data.valid) {
            Serial.printf("Coordinates    : Lat: %.6f°, Lon: %.6f°\n", data.latitude, data.longitude);
            Serial.printf("Speed / Alt    : %.1f km/h | Alt: %.1f m | Course: %.1f°\n",
                          data.speedKmh, data.altitude, data.course);
            Serial.printf("GPS UTC Time   : %s (%s)\n", data.gpsTime.c_str(), data.gpsDate.c_str());
        } else {
            Serial.println("Position       : WAITING FOR GPS FIX...");
        }
        Serial.println("---------------------------\n");
    }
}
