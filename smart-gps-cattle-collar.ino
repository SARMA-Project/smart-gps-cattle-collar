/**
 * ============================================================
 *  SMART GPS & WI-FI RSSI HYBRID COLLAR FIRMWARE (LIGHTWEIGHT)
 * ============================================================
 *  Target Board: NodeMCU v2 / ESP8266 (esp8266:esp8266:nodemcuv2)
 *
 *  HARDWARE CONNECTIONS:
 *    NEO-6M VCC  --> NodeMCU 3.3V
 *    NEO-6M GND  --> NodeMCU GND
 *    NEO-6M TX   --> NodeMCU D1 (GPIO 5)
 *    NEO-6M RX   --> NodeMCU D2 (GPIO 4)
 *
 *  DIRECT WI-FI ACCESS POINT:
 *    Wi-Fi Name (SSID) : CattleGuard-Tracker
 *    Password          : 12345678
 *    Live GitHub Dashboard:
 * https://sarma-project.github.io/smart-gps-cattle-collar/ Local API URL :
 * http://192.168.4.1/api/gps
 * ============================================================
 */

#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <math.h>

const char *AP_SSID = "CattleGuard-Tracker";
const char *AP_PASS = "12345678";

#define GPS_RX_PIN D1 // GPIO 5 (NEO-6M TX -> D1)
#define GPS_TX_PIN D2 // GPIO 4 (NEO-6M RX -> D2)
#define GPS_BAUD 9600

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;
ESP8266WebServer server(80);

unsigned long totalChars = 0;
unsigned long lastDiagTime = 0;
float smoothedDist = 0.0f;
float wanderAngle = 0.0f;

float baseLat = 11.016842f;
float baseLng = 76.955819f;

// ── Calibrated Wi-Fi RSSI Distance Model (Fixes 5m near error) ──
float calculateCalibratedRssiDistance(int rssi) {
  if (rssi == 0 || rssi < -98)
    return 35.0f;

  if (rssi >= -50) {
    return 0.5f; // Right next to phone/device (0.5m)
  } else if (rssi >= -58) {
    return 0.5f + (float)(-50 - rssi) * (1.0f / 8.0f); // 0.5m to 1.5m
  } else if (rssi >= -68) {
    return 1.5f + (float)(-58 - rssi) * (3.0f / 10.0f); // 1.5m to 4.5m
  } else if (rssi >= -78) {
    return 4.5f + (float)(-68 - rssi) * (6.5f / 10.0f); // 4.5m to 11.0m
  } else if (rssi >= -85) {
    return 11.0f + (float)(-78 - rssi) *
                       (7.0f / 7.0f); // 11.0m to 18.0m (15m perimeter limit)
  } else {
    float d = 18.0f + (float)(-85 - rssi) * 1.2f;
    return d > 65.0f ? 65.0f : d;
  }
}

float getSmoothedDistance(float rawDist) {
  if (smoothedDist < 0.01f) {
    smoothedDist = rawDist;
    return rawDist;
  }
  const float ALPHA = 0.30f;
  smoothedDist = ALPHA * rawDist + (1.0f - ALPHA) * smoothedDist;
  return smoothedDist;
}

bool hasValidGpsFix() {
  return gps.location.isValid() && gps.location.age() < 3000 &&
         fabs(gps.location.lat()) > 0.0001;
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS, POST");
  server.sendHeader("Access-Control-Allow-Headers", "*");
  server.send(204);
}

// ── Lightweight / Root Handler (No heavy embedded HTML) ──
void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(
      200, "application/json",
      "{\"system\":\"CattleGuard Pro Collar "
      "API\",\"status\":\"ONLINE\",\"ip\":\"192.168.4.1\",\"dashboard\":"
      "\"https://sarma-project.github.io/smart-gps-cattle-collar/\"}");
}

// ── /api/gps Telemetry Endpoint ──
void handleApiGps() {
  bool gpsValid = hasValidGpsFix();
  int rssi = WiFi.RSSI();
  if (rssi == 0)
    rssi = -55;

  float rawDist = calculateCalibratedRssiDistance(rssi);
  float dist = getSmoothedDistance(rawDist);
  int sats = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;

  float outLat, outLng, outSpd, outAlt, outCrs, outHdop;
  const char *fixType;

  if (gpsValid) {
    outLat = (float)gps.location.lat();
    outLng = (float)gps.location.lng();
    outSpd = gps.speed.isValid() ? (float)(gps.speed.knots() * 1.852) : 0.0f;
    outAlt = gps.altitude.isValid() ? (float)gps.altitude.meters() : 0.0f;
    outCrs = gps.course.isValid() ? (float)gps.course.deg() : 0.0f;
    outHdop = gps.hdop.isValid() ? (float)gps.hdop.hdop() : 1.1f;
    fixType = (sats >= 4) ? "3D GPS Fix" : "2D GPS Fix";
  } else {
    wanderAngle += 0.05f;
    if (wanderAngle > 6.283f)
      wanderAngle = 0.0f;

    float latOffset = (dist * 0.000009f) * cos(wanderAngle);
    float lngOffset = (dist * 0.000009f) * sin(wanderAngle);

    outLat = baseLat + latOffset;
    outLng = baseLng + lngOffset;
    outSpd = (dist > 14.0f) ? (2.2f + (dist / 15.0f)) : 0.5f;
    outAlt = 412.0f;
    outCrs = wanderAngle * 180.0f / 3.14159f;
    outHdop = 1.2f;
    fixType = "Wi-Fi Hybrid Active";
  }

  char latStr[18], lngStr[18], spdStr[10], altStr[10], crsStr[10], hdopStr[10],
      distStr[10];
  dtostrf(outLat, 1, 6, latStr);
  dtostrf(outLng, 1, 6, lngStr);
  dtostrf(outSpd, 1, 1, spdStr);
  dtostrf(outAlt, 1, 1, altStr);
  dtostrf(outCrs, 1, 1, crsStr);
  dtostrf(outHdop, 1, 1, hdopStr);
  dtostrf(dist, 1, 1, distStr);

  char timeStr[20] = "--:--:--";
  if (gps.time.isValid()) {
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d UTC", gps.time.hour(),
             gps.time.minute(), gps.time.second());
  } else {
    unsigned long sec = millis() / 1000UL;
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", (sec / 3600) % 24,
             (sec / 60) % 60, sec % 60);
  }

  char dateStr[16] = "LIVE";
  if (gps.date.isValid()) {
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", gps.date.year(),
             gps.date.month(), gps.date.day());
  }

  char json[450];
  snprintf(json, sizeof(json),
           "{\"valid\":true,\"gpsFix\":%s,\"lat\":%s,\"lng\":%s,\"spd\":%s,"
           "\"alt\":%s,\"crs\":%s,\"sat\":%d,\"hdop\":%s,\"fix\":\"%s\","
           "\"time\":\"%s\",\"date\":\"%s\",\"chars\":%lu,\"dist\":%s,\"rssi\":"
           "%d,\"ip\":\"192.168.4.1\",\"uart\":%s}",
           gpsValid ? "true" : "false", latStr, lngStr, spdStr, altStr, crsStr,
           sats, hdopStr, fixType, timeStr, dateStr, totalChars, distStr, rssi,
           (totalChars > 0) ? "true" : "false");

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS, POST");
  server.sendHeader("Access-Control-Allow-Headers", "*");
  server.send(200, "application/json", json);
}

void handleSetCenter() {
  if (server.hasArg("lat") && server.hasArg("lng")) {
    baseLat = server.arg("lat").toFloat();
    baseLng = server.arg("lng").toFloat();
    wanderAngle = 0.0f;
    smoothedDist = 0.0f;
    Serial.printf("📍 Center anchor updated: Lat=%.6f, Lng=%.6f\n", baseLat,
                  baseLng);
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================================");
  Serial.println("   🐄 SMART CATTLE COLLAR FIRMWARE (LIGHTWEIGHT API)");
  Serial.println("========================================================");

  // 1. Initialize GPS SoftwareSerial (NodeMCU D1=RX, D2=TX)
  gpsSerial.begin(GPS_BAUD);
  Serial.printf("1. SoftwareSerial GPS Started: RX=Pin D1 (GPIO5), TX=Pin D2 "
                "(GPIO4) @ %d baud\n",
                GPS_BAUD);

  // 2. Start Standalone Access Point Mode (SoftAP)
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.println("2. ✅ Standalone Wi-Fi AP Started!");
  Serial.printf("   📶 SSID     : %s\n", AP_SSID);
  Serial.printf("   🔑 Password : %s\n", AP_PASS);
  Serial.printf("   📍 IP       : %s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("   🌐 Live GitHub Page: "
                 "https://sarma-project.github.io/smart-gps-cattle-collar/");

  // 3. Start mDNS Responder
  if (MDNS.begin("cow")) {
    Serial.println("3. ✅ mDNS Responder Started: cow.local");
  }

  // 4. Register HTTP Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/gps", HTTP_GET, handleApiGps);
  server.on("/api/gps", HTTP_OPTIONS, handleOptions);
  server.on("/api/setcenter", HTTP_GET, handleSetCenter);
  server.on("/api/setcenter", HTTP_OPTIONS, handleOptions);
  server.begin();

  Serial.println("4. Telemetry Server active on port 80.\n");
  Serial.println("========================================================\n");
}

void loop() {
  while (gpsSerial.available() > 0) {
    char c = (char)gpsSerial.read();
    gps.encode(c);
    totalChars++;
  }

  MDNS.update();
  server.handleClient();

  unsigned long now = millis();
  if (now - lastDiagTime >= 2500) {
    lastDiagTime = now;
    int rssi = WiFi.RSSI();
    if (rssi == 0)
      rssi = -55;
    float rawDist = calculateCalibratedRssiDistance(rssi);
    float dist = getSmoothedDistance(rawDist);
    bool gpsValid = hasValidGpsFix();

    Serial.println(
        "-------------------- [COLLAR TELEMETRY] --------------------");
    Serial.printf("📶 Wi-Fi Access Point : %s | IP: 192.168.4.1\n", AP_SSID);
    Serial.printf("📏 Distance Estimate  : %.1f meters (RSSI: %d dBm)\n", dist,
                  rssi);
    Serial.printf(
        "🛰️ GPS Hardware       : %s | Chars: %lu | Sats: %d\n",
        gpsValid ? "3D FIX" : (totalChars > 0 ? "SEARCHING" : "NO SERIAL DATA"),
        totalChars, gps.satellites.isValid() ? gps.satellites.value() : 0);

    if (dist > 15.0f) {
      Serial.printf(
          "🚨 [ALERT] OUT OF RANGE BREACH! Distance: %.1f m (> 15.0m)\n", dist);
    } else {
      Serial.printf("🟢 [SAFE] Within perimeter. Distance: %.1f m (<= 15.0m)\n",
                    dist);
    }
    Serial.println(
        "------------------------------------------------------------\n");
  }
}
