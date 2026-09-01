/**
 * ============================================================
 *  ESP8266 NEO-6M GPS - SERIAL DIAGNOSTIC & IOT NODE
 * ============================================================
 *  Web Dashboard is hosted at:
 *  https://sarma-project.github.io/smart-gps-cattle-collar/
 *
 *  HARDWARE CONNECTIONS:
 *    NEO-6M VCC  --> NodeMCU 3.3V (or 5V / VIN)
 *    NEO-6M GND  --> NodeMCU GND
 *    NEO-6M TX   --> NodeMCU D1 (GPIO 5)
 *    NEO-6M RX   --> NodeMCU D2 (GPIO 4)
 *
 *  PHONE HOTSPOT SETTINGS:
 *    Hotspot Name : CowTracker
 *    Password     : cow12345
 *
 *  ARDUINO IDE:
 *    - Board: NodeMCU 1.0 (ESP-12E Module)
 *    - Serial Monitor: 115200 baud
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>

// ---------------- CONFIGURATION ----------------
const char* HOTSPOT_SSID = "CowTracker";
const char* HOTSPOT_PASS = "cow12345";

#define GPS_RX_PIN D1   // GPIO 5 (Connected to NEO-6M TX)
#define GPS_TX_PIN D2   // GPIO 4 (Connected to NEO-6M RX)
#define GPS_BAUD   9600

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;
ESP8266WebServer server(80);

unsigned long totalChars = 0;
unsigned long lastSerialReport = 0;

// ---------------- SEND CORS-ENABLED JSON GPS API ----------------
void handleApiGps() {
  bool valid = gps.location.isValid() && (gps.location.age() < 3000) && (fabs(gps.location.lat()) > 0.0001);

  char latStr[18] = "0.000000";
  char lngStr[18] = "0.000000";
  char spdStr[10] = "0.0";
  char altStr[10] = "0.0";
  char crsStr[10] = "0.0";
  char hdopStr[10] = "99.9";

  if (valid) {
    dtostrf(gps.location.lat(), 1, 6, latStr);
    dtostrf(gps.location.lng(), 1, 6, lngStr);
    if (gps.speed.isValid())    dtostrf(gps.speed.knots() * 1.852f, 1, 1, spdStr);
    if (gps.altitude.isValid()) dtostrf(gps.altitude.meters(), 1, 1, altStr);
    if (gps.course.isValid())   dtostrf(gps.course.deg(), 1, 1, crsStr);
    if (gps.hdop.isValid())     dtostrf(gps.hdop.hdop(), 1, 1, hdopStr);
  }

  char timeStr[16] = "--:--:--";
  if (gps.time.isValid()) {
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d UTC", gps.time.hour(), gps.time.minute(), gps.time.second());
  }

  char dateStr[16] = "----/--/--";
  if (gps.date.isValid()) {
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", gps.date.year(), gps.date.month(), gps.date.day());
  }

  int sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
  const char* fixStr = valid ? (sats >= 4 ? "3D" : "2D") : (totalChars > 0 ? "SEARCHING" : "NO GPS DATA");

  char jsonBuf[380];
  snprintf(jsonBuf, sizeof(jsonBuf),
    "{\"valid\":%s,\"lat\":%s,\"lng\":%s,\"spd\":%s,\"alt\":%s,\"crs\":%s,\"sat\":%d,\"hdop\":%s,\"fix\":\"%s\",\"time\":\"%s\",\"date\":\"%s\",\"chars\":%lu,\"uart\":%s}",
    valid ? "true" : "false",
    latStr, lngStr, spdStr, altStr, crsStr, sats, hdopStr,
    fixStr, timeStr, dateStr,
    totalChars, (totalChars > 0) ? "true" : "false"
  );

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
  server.send(200, "application/json", jsonBuf);
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
  server.send(204);
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n========================================================");
  Serial.println("   🛰️ SMART GPS HARDWARE TEST & LIVE IOT NODE");
  Serial.println("========================================================");

  // 1. Start SoftwareSerial for NEO-6M GPS
  gpsSerial.begin(GPS_BAUD);
  Serial.printf("1. GPS SoftwareSerial: RX=Pin D1 (GPIO5), Baud=%d\n", GPS_BAUD);

  // 2. Connect to Wi-Fi Hotspot
  Serial.printf("2. Connecting to Phone Hotspot [%s]...\n", HOTSPOT_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("   ✅ HOTSPOT CONNECTED! ESP IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.println("   👉 Open Dashboard: https://sarma-project.github.io/smart-gps-cattle-collar/");
  } else {
    Serial.println("   ⚠️ Hotspot not connected. Continuing in GPS diagnostic mode...");
  }

  // 3. Start Web API Server
  server.on("/api/gps", HTTP_GET, handleApiGps);
  server.on("/api/gps", HTTP_OPTIONS, handleOptions);
  server.begin();
  Serial.println("3. GPS REST API Server started.\n");
  Serial.println("========================================================");
  Serial.println("           LIVE SERIAL MONITOR DIAGNOSTIC              ");
  Serial.println("========================================================\n");
}

// ---------------- MAIN LOOP ----------------
void loop() {
  // Read incoming GPS data continuously
  while (gpsSerial.available() > 0) {
    char c = (char)gpsSerial.read();
    gps.encode(c);
    totalChars++;
  }

  // Handle Web Client requests
  server.handleClient();

  // Print Live GPS Serial Monitor Status every 2.5 seconds
  unsigned long now = millis();
  if (now - lastSerialReport >= 2500) {
    lastSerialReport = now;

    Serial.println("-------------------- [GPS REPORT] --------------------");
    
    if (totalChars == 0) {
      Serial.println("❌ HARDWARE ERROR: No serial data received from NEO-6M!");
      Serial.println("   -> Check Wire: NEO-6M TX pin must connect to NodeMCU D1.");
      Serial.println("   -> Check Power: Ensure GPS module red power LED is lit.");
    } else {
      Serial.printf("✅ Hardware Serial OK | Bytes Received: %lu\n", totalChars);
      
      int sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
      Serial.printf("🛰️ Satellites in view: %d\n", sats);

      bool valid = gps.location.isValid() && (gps.location.age() < 3000) && (fabs(gps.location.lat()) > 0.0001);

      if (valid) {
        Serial.println("🎉 GPS FIX ACQUIRED (LIVE LOCATION):");
        Serial.printf("   📍 Latitude   : %.6f\n", gps.location.lat());
        Serial.printf("   📍 Longitude  : %.6f\n", gps.location.lng());
        Serial.printf("   ⚡ Speed      : %.1f km/h\n", gps.speed.isValid() ? (gps.speed.knots() * 1.852f) : 0.0);
        Serial.printf("   ⛰️ Altitude   : %.1f m\n", gps.altitude.isValid() ? gps.altitude.meters() : 0.0);
        Serial.printf("   🎯 HDOP       : %.1f\n", gps.hdop.isValid() ? gps.hdop.hdop() : 99.9);
        Serial.printf("   🔗 Google Maps: https://www.google.com/maps?q=%.6f,%.6f\n", gps.location.lat(), gps.location.lng());
        if (WiFi.status() == WL_CONNECTED) {
          Serial.printf("   🌐 Web Stream : http://%s/api/gps\n", WiFi.localIP().toString().c_str());
        }
      } else {
        Serial.println("⏳ STATUS: SEARCHING FOR SATELLITES...");
        Serial.println("   -> Please place the GPS antenna facing open sky outdoors.");
        Serial.println("   -> It takes 1 to 3 minutes for first satellite lock.");
      }
    }
    Serial.println("------------------------------------------------------\n");
  }
}


