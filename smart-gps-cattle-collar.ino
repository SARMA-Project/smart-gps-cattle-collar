/**
 * ============================================================
 *  ESP8266 NEO-6M GPS - PURE IOT HARDWARE NODE
 * ============================================================
 *  Web Dashboard is hosted at:
 *  https://sarma-project.github.io/smart-gps-cattle-collar/
 *
 *  HARDWARE CONNECTIONS:
 *    NEO-6M VCC  --> NodeMCU 3.3V (or 5V/VIN)
 *    NEO-6M GND  --> NodeMCU GND
 *    NEO-6M TX   --> NodeMCU D1 (GPIO 5)
 *    NEO-6M RX   --> NodeMCU D2 (GPIO 4)
 *
 *  PHONE HOTSPOT SETTINGS:
 *    Hotspot Name : CowTracker
 *    Password     : cow12345
 *
 *  ONLY 1 LIBRARY REQUIRED IN ARDUINO IDE:
 *    - TinyGPSPlus by Mikal Hart
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
unsigned long lastDebug = 0;

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

  // Enable CORS so the GitHub Pages dashboard can fetch data from any origin
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
  delay(300);

  Serial.println("\n\n========================================");
  Serial.println(" ESP8266 GPS IOT SENSOR NODE");
  Serial.println("========================================");

  // Start GPS SoftwareSerial
  gpsSerial.begin(GPS_BAUD);
  Serial.printf("GPS Serial: RX=D1 (GPIO 5), TX=D2 (GPIO 4) @ %d baud\n", GPS_BAUD);

  // Connect to Phone Hotspot
  Serial.printf("Connecting to Phone Hotspot [%s]...\n", HOTSPOT_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\n✅ CONNECTED TO HOTSPOT! ESP IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.println("Enter this IP in your GitHub Pages dashboard to connect!");
  } else {
    Serial.println("\n⚠️ Hotspot not found. Retrying in background...");
  }

  // Setup REST API routes
  server.on("/api/gps", HTTP_GET, handleApiGps);
  server.on("/api/gps", HTTP_OPTIONS, handleOptions);
  server.begin();
  Serial.println("GPS API Server running on port 80.\n");
}

// ---------------- MAIN LOOP ----------------
void loop() {
  // 1. Process GPS Data continuously
  while (gpsSerial.available() > 0) {
    char c = (char)gpsSerial.read();
    gps.encode(c);
    totalChars++;
  }

  // 2. Handle HTTP API requests
  server.handleClient();

  // 3. Serial Debug Output every 3 seconds
  unsigned long now = millis();
  if (now - lastDebug >= 3000) {
    lastDebug = now;
    bool valid = gps.location.isValid() && (gps.location.age() < 3000) && (fabs(gps.location.lat()) > 0.0001);
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[HOTSPOT OK] IP: %s | ", WiFi.localIP().toString().c_str());
    } else {
      Serial.print("[HOTSPOT DISCONNECTED] | ");
    }
    
    Serial.printf("GPS: %s | Chars: %lu | Sats: %d",
      valid ? "FIXED" : (totalChars > 0 ? "SEARCHING" : "NO DATA"),
      totalChars,
      gps.satellites.isValid() ? gps.satellites.value() : 0
    );
    if (valid) {
      Serial.printf(" | Lat: %.6f, Lng: %.6f", gps.location.lat(), gps.location.lng());
    }
    Serial.println();
  }
}

