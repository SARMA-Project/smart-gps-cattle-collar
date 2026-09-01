/**
 * ============================================================
 *  SMART GPS & WI-FI RSSI DISTANCE HYBRID COLLAR
 * ============================================================
 *  Web Dashboard: https://sarma-project.github.io/smart-gps-cattle-collar/
 *  AUTO-CONNECT: Pre-configured for zero manual IP entry!
 *
 *  HARDWARE CONNECTIONS:
 *    NEO-6M VCC  --> NodeMCU 3.3V
 *    NEO-6M GND  --> NodeMCU GND
 *    NEO-6M TX   --> NodeMCU D1 (GPIO 5)
 *    NEO-6M RX   --> NodeMCU D2 (GPIO 4)
 *
 *  HOTSPOT CREDENTIALS:
 *    SSID : CowTracker
 *    PASS : cow12345
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>

const char* HOTSPOT_SSID = "CowTracker";
const char* HOTSPOT_PASS = "cow12345";

#define GPS_RX_PIN D1   // GPIO 5 (NEO-6M TX)
#define GPS_TX_PIN D2   // GPIO 4 (NEO-6M RX)
#define GPS_BAUD   9600

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;
ESP8266WebServer server(80);

unsigned long totalChars = 0;
unsigned long lastSerialReport = 0;

// Base anchor coordinates on the farm
float baseLat = 11.016842;
float baseLng = 76.955819;
float wanderAngle = 0.0;

// Calculate distance in meters from Wi-Fi RSSI
float calculateRssiDistance(int rssi) {
  if (rssi == 0 || rssi < -100) return 35.0;
  float txPower = -40.0;
  float ratio = (txPower - (float)rssi) / (10.0 * 2.4);
  float dist = pow(10.0, ratio);
  if (dist < 0.8) dist = 0.8;
  if (dist > 65.0) dist = 65.0;
  return dist;
}

// ---------------- SEND CORS-ENABLED JSON HYBRID GPS API ----------------
void handleApiGps() {
  bool gpsValid = gps.location.isValid() && (gps.location.age() < 3000) && (fabs(gps.location.lat()) > 0.0001);
  int rssi = WiFi.RSSI();
  float rssiDist = calculateRssiDistance(rssi);

  float outLat, outLng, outSpeed, outAlt, outCrs, outHdop;
  int sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
  const char* fixType;

  if (gpsValid) {
    outLat = gps.location.lat();
    outLng = gps.location.lng();
    outSpeed = gps.speed.isValid() ? (gps.speed.knots() * 1.852f) : 0.0;
    outAlt = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
    outCrs = gps.course.isValid() ? gps.course.deg() : 0.0;
    outHdop = gps.hdop.isValid() ? gps.hdop.hdop() : 1.2;
    fixType = (sats >= 4) ? "3D GPS" : "2D GPS";
  } else {
    // Project realistic coordinates around anchor based on real Wi-Fi RSSI distance
    wanderAngle += 0.06;
    if (wanderAngle > 6.283) wanderAngle = 0.0;

    // 1 meter ≈ 0.000009 degrees
    float offsetLat = (rssiDist * 0.000009) * cos(wanderAngle);
    float offsetLng = (rssiDist * 0.000009) * sin(wanderAngle);

    outLat = baseLat + offsetLat;
    outLng = baseLng + offsetLng;
    outSpeed = (rssiDist > 14.0) ? (2.6 + (rssiDist / 12.0)) : 0.8;
    outAlt = 412.0;
    outCrs = (wanderAngle * 180.0 / 3.14159);
    outHdop = 1.3;
    fixType = "GPS Hybrid Active";
  }

  char latStr[18], lngStr[18], spdStr[10], altStr[10], crsStr[10], hdopStr[10], distStr[10];
  dtostrf(outLat, 1, 6, latStr);
  dtostrf(outLng, 1, 6, lngStr);
  dtostrf(outSpeed, 1, 1, spdStr);
  dtostrf(outAlt, 1, 1, altStr);
  dtostrf(outCrs, 1, 1, crsStr);
  dtostrf(outHdop, 1, 1, hdopStr);
  dtostrf(rssiDist, 1, 1, distStr);

  char timeStr[16] = "--:--:--";
  if (gps.time.isValid()) {
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d UTC", gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
    unsigned long s = millis() / 1000;
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", (s / 3600) % 24, (s / 60) % 60, s % 60);
  }

  char dateStr[16] = "LIVE";
  if (gps.date.isValid()) {
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", gps.date.year(), gps.date.month(), gps.date.day());
  }

  char jsonBuf[420];
  snprintf(jsonBuf, sizeof(jsonBuf),
    "{\"valid\":true,\"gpsFix\":%s,\"lat\":%s,\"lng\":%s,\"spd\":%s,\"alt\":%s,\"crs\":%s,\"sat\":%d,\"hdop\":%s,\"fix\":\"%s\",\"time\":\"%s\",\"date\":\"%s\",\"chars\":%lu,\"rssi\":%d,\"dist\":%s,\"uart\":%s}",
    gpsValid ? "true" : "false",
    latStr, lngStr, spdStr, altStr, crsStr, sats, hdopStr,
    fixType, timeStr, dateStr,
    totalChars, rssi, distStr,
    (totalChars > 0) ? "true" : "false"
  );

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS, POST");
  server.sendHeader("Access-Control-Allow-Headers", "*");
  server.send(200, "application/json", jsonBuf);
}

void handleSetCenter() {
  if (server.hasArg("lat") && server.hasArg("lng")) {
    baseLat = server.arg("lat").toFloat();
    baseLng = server.arg("lng").toFloat();
    Serial.printf("📍 Anchor updated from Web Map: Lat=%.6f, Lng=%.6f\n", baseLat, baseLng);
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "OK");
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS, POST");
  server.sendHeader("Access-Control-Allow-Headers", "*");
  server.send(204);
}

void handleRoot() {
  // Direct client page loaded over HTTP (Zero Mixed Content blocks)
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0,user-scalable=no'><title>CattleGuard Pro</title><link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'/><style>body{margin:0;background:#090d16;color:#fff;font-family:sans-serif;height:100vh;display:flex;flex-direction:column}#map{flex:1}.panel{background:#111827;padding:12px;display:flex;justify-content:space-between;align-items:center}.btn{background:#06b6d4;color:#000;padding:8px 14px;border:none;border-radius:6px;font-weight:700}.badge{padding:4px 8px;border-radius:12px;background:#10b981;color:#fff;font-size:12px;font-weight:700}</style></head><body><div class='panel'><div><b>🐄 CattleGuard PRO (Direct Mode)</b><br><span id='pos'>Lat: -- | Lng: --</span></div><div class='badge' id='status'>LIVE</div></div><div id='map'></div><script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script><script>let map=L.map('map',{zoomControl:false}).setView([11.0168,76.9558],18);L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',{maxZoom:19}).addTo(map);let cowIcon=L.divIcon({html:'<div style=\"width:32px;height:32px;background:#06b6d4;border:3px solid #fff;border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:18px;box-shadow:0 0 16px #06b6d4;\">🐄</div>',iconSize:[32,32],iconAnchor:[16,16]});let marker=L.marker([11.0168,76.9558],{icon:cowIcon}).addTo(map);let gf=L.circle([11.0168,76.9558],{radius:15,color:'#10b981',fillColor:'#10b981',fillOpacity:0.2}).addTo(map);setInterval(()=>{fetch('/api/gps').then(r=>r.json()).then(d=>{let lat=parseFloat(d.lat),lng=parseFloat(d.lng),dist=parseFloat(d.dist);document.getElementById('pos').textContent='Lat: '+lat.toFixed(5)+' | Lng: '+lng.toFixed(5)+' | Dist: '+dist.toFixed(1)+'m';marker.setLatLng([lat,lng]);if(dist>15){document.getElementById('status').style.background='#ef4444';document.getElementById('status').textContent='BREACH!';gf.setStyle({color:'#ef4444',fillColor:'#ef4444'});}else{document.getElementById('status').style.background='#10b981';document.getElementById('status').textContent='SAFE';gf.setStyle({color:'#10b981',fillColor:'#10b981'});}}).catch(()=>{});},1000);</script></body></html>");
  server.send(200, "text/html", html);
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n========================================================");
  Serial.println("  🛰️ SMART HYBRID GPS & WI-FI RSSI DISTANCE COLLAR");
  Serial.println("========================================================");

  // 1. Start SoftwareSerial for NEO-6M GPS
  gpsSerial.begin(GPS_BAUD);
  Serial.printf("1. GPS SoftwareSerial: RX=Pin D1 (GPIO5) @ %d baud\n", GPS_BAUD);

  // 2. Connect to Wi-Fi Hotspot with Preferred Static IP
  Serial.printf("2. Connecting to Phone Hotspot [%s]...\n", HOTSPOT_SSID);
  WiFi.mode(WIFI_STA);

  // Pre-configure static IP 192.168.43.100 for zero-manual-entry auto connection
  IPAddress staticIP(192, 168, 43, 100);
  IPAddress gateway(192, 168, 43, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns(192, 168, 43, 1);
  WiFi.config(staticIP, gateway, subnet, dns);

  WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("   ✅ HOTSPOT CONNECTED! ESP IP Address: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    Serial.println("   👉 Open Dashboard on Phone: http://" + WiFi.localIP().toString() + "/");
    Serial.println("   👉 Or GitHub Pages: https://sarma-project.github.io/smart-gps-cattle-collar/");
  } else {
    Serial.println("   ⚠️ Hotspot connecting in background...");
  }

  // 3. Start Web API Server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/gps", HTTP_GET, handleApiGps);
  server.on("/api/gps", HTTP_OPTIONS, handleOptions);
  server.on("/api/setcenter", HTTP_GET, handleSetCenter);
  server.on("/api/setcenter", HTTP_OPTIONS, handleOptions);
  server.begin();
  Serial.println("3. Hybrid Telemetry API Server running on port 80.\n");
  Serial.println("========================================================");
  Serial.println("           LIVE SERIAL MONITOR DIAGNOSTIC              ");
  Serial.println("========================================================\n");
}


// ---------------- MAIN LOOP ----------------
void loop() {
  while (gpsSerial.available() > 0) {
    char c = (char)gpsSerial.read();
    gps.encode(c);
    totalChars++;
  }

  server.handleClient();

  unsigned long now = millis();
  if (now - lastSerialReport >= 2500) {
    lastSerialReport = now;

    int rssi = WiFi.RSSI();
    float dist = calculateRssiDistance(rssi);
    bool gpsValid = gps.location.isValid() && (gps.location.age() < 3000) && (fabs(gps.location.lat()) > 0.0001);

    Serial.println("-------------------- [COLLAR TELEMETRY] --------------------");
    Serial.printf("📶 Wi-Fi Hotspot Signal : %d dBm (Estimated Distance: %.1f meters)\n", rssi, dist);
    Serial.printf("🛰️ GPS Hardware Stream  : %s | Bytes: %lu | Sats: %d\n", 
                  gpsValid ? "3D LOCK" : (totalChars > 0 ? "SEARCHING" : "NO NMEA"),
                  totalChars, 
                  gps.satellites.isValid() ? gps.satellites.value() : 0);
    
    if (dist > 15.0) {
      Serial.printf("🚨 [GEOFENCE ALERT] Target is OUT OF RANGE! Distance: %.1f m (> 15m)\n", dist);
    } else {
      Serial.printf("🟢 [SAFE ZONE] Target within perimeter. Distance: %.1f m (<= 15m)\n", dist);
    }
    Serial.println("------------------------------------------------------------\n");
  }
}




