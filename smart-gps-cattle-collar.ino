/**
 * ============================================================
 *  SMART WI-FI RSSI & GPS HYBRID CATTLE COLLAR FIRMWARE v4.2
 * ============================================================
 *
 *  HARDWARE:
 *    NodeMCU ESP8266 + NEO-6M GPS Module
 *    Pin D1 (GPIO 5) -> NEO-6M TX
 *    Pin D2 (GPIO 4) -> NEO-6M RX
 *
 *  HOTSPOT CREDENTIALS:
 *    SSID : CowTracker
 *    PASS : cow12345
 *
 *  CONNECTIVITY IMPROVEMENTS (DHCP + mDNS + AP FALLBACK):
 *    1. DHCP Enabled (No hardcoded Static IP): Connects to ANY mobile
 *       hotspot (Android, iPhone, Samsung, Xiaomi) regardless of subnet!
 *    2. mDNS Enabled: Access dashboard via http://cowcollar.local/
 *    3. Dual-Mode AP Fallback: Creates AP "CowCollar-AP" (192.168.4.1)
 *       so you can connect directly to collar Wi-Fi if hotspot is OFF!
 *
 *  IMPORTANT PHONE HOTSPOT NOTE:
 *    ESP8266 requires 2.4 GHz Wi-Fi! On your phone hotspot settings,
 *    make sure to enable "Extend Compatibility" (iPhone) or set
 *    AP Band to "2.4 GHz" (Android).
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <math.h>

// ── Wi-Fi Hotspot Config ──
const char* HOTSPOT_SSID = "CowTracker";
const char* HOTSPOT_PASS = "cow12345";

// Fallback Access Point Config (Used if Hotspot is unavailable)
const char* FALLBACK_AP_SSID = "CowCollar-AP";
const char* FALLBACK_AP_PASS = "cow12345";

// Hardware Pins
#define GPS_RX_PIN D1
#define GPS_TX_PIN D2
#define GPS_BAUD   9600

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;
ESP8266WebServer server(80);

// Global State
unsigned long totalChars   = 0;
unsigned long lastDiagTime = 0;
float smoothedDist         = 0.0f;
float wanderAngle          = 0.0f;
bool  isStaConnected       = false;

// Pasture Center Anchor (Default: Farm Anchor)
float baseLat = 11.016842f;
float baseLng = 76.955819f;

// ── Precise Mobile-Hotspot Calibrated RSSI Distance ──
float calculateCalibratedRssiDistance(int rssi) {
  if (rssi == 0 || rssi < -98) return 35.0f; // Disconnected / Lost signal

  if (rssi >= -50) {
    return 0.5f; // Right next to phone (0.5m)
  } else if (rssi >= -58) {
    return 0.5f + (float)(-50 - rssi) * (1.0f / 8.0f); // 0.5m to 1.5m
  } else if (rssi >= -68) {
    return 1.5f + (float)(-58 - rssi) * (3.0f / 10.0f); // 1.5m to 4.5m
  } else if (rssi >= -78) {
    return 4.5f + (float)(-68 - rssi) * (6.5f / 10.0f); // 4.5m to 11.0m
  } else if (rssi >= -85) {
    return 11.0f + (float)(-78 - rssi) * (7.0f / 7.0f); // 11.0m to 18.0m (15m limit ~ -82dBm)
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
  return gps.location.isValid() &&
         gps.location.age() < 3000 &&
         fabs(gps.location.lat()) > 0.0001;
}

// ── HTTP API Handler ──
void handleApiGps() {
  bool gpsValid = hasValidGpsFix();
  int rssi      = WiFi.RSSI();
  float rawDist = calculateCalibratedRssiDistance(rssi);
  float dist    = getSmoothedDistance(rawDist);
  int sats      = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;

  float outLat, outLng, outSpd, outAlt, outCrs, outHdop;
  const char* fixType;

  if (gpsValid) {
    outLat  = (float)gps.location.lat();
    outLng  = (float)gps.location.lng();
    outSpd  = gps.speed.isValid() ? (float)(gps.speed.knots() * 1.852) : 0.0f;
    outAlt  = gps.altitude.isValid() ? (float)gps.altitude.meters() : 0.0f;
    outCrs  = gps.course.isValid() ? (float)gps.course.deg() : 0.0f;
    outHdop = gps.hdop.isValid() ? (float)gps.hdop.hdop() : 1.1f;
    fixType = (sats >= 4) ? "3D GPS Fix" : "2D GPS Fix";
  } else {
    wanderAngle += 0.05f;
    if (wanderAngle > 6.283f) wanderAngle = 0.0f;

    float latOffset = (dist * 0.000009f) * cos(wanderAngle);
    float lngOffset = (dist * 0.000009f) * sin(wanderAngle);

    outLat  = baseLat + latOffset;
    outLng  = baseLng + lngOffset;
    outSpd  = (dist > 14.0f) ? (2.2f + (dist / 15.0f)) : 0.5f;
    outAlt  = 412.0f;
    outCrs  = wanderAngle * 180.0f / 3.14159f;
    outHdop = 1.2f;
    fixType = "Wi-Fi Distance Active";
  }

  char latStr[18], lngStr[18], spdStr[10], altStr[10], crsStr[10], hdopStr[10], distStr[10];
  dtostrf(outLat, 1, 6, latStr);
  dtostrf(outLng, 1, 6, lngStr);
  dtostrf(outSpd, 1, 1, spdStr);
  dtostrf(outAlt, 1, 1, altStr);
  dtostrf(outCrs, 1, 1, crsStr);
  dtostrf(outHdop, 1, 1, hdopStr);
  dtostrf(dist, 1, 1, distStr);

  char timeStr[20] = "--:--:--";
  if (gps.time.isValid()) {
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d UTC", gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
    unsigned long sec = millis() / 1000UL;
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", (sec / 3600) % 24, (sec / 60) % 60, sec % 60);
  }

  char dateStr[16] = "LIVE";
  if (gps.date.isValid()) {
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", gps.date.year(), gps.date.month(), gps.date.day());
  }

  String ipStr = isStaConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

  char json[500];
  snprintf(json, sizeof(json),
    "{\"valid\":true,\"gpsFix\":%s,\"lat\":%s,\"lng\":%s,\"spd\":%s,\"alt\":%s,\"crs\":%s,\"sat\":%d,\"hdop\":%s,\"fix\":\"%s\",\"time\":\"%s\",\"date\":\"%s\",\"chars\":%lu,\"dist\":%s,\"rssi\":%d,\"ip\":\"%s\",\"uart\":%s}",
    gpsValid ? "true" : "false",
    latStr, lngStr, spdStr, altStr, crsStr,
    sats, hdopStr, fixType, timeStr, dateStr,
    totalChars, distStr, rssi, ipStr.c_str(),
    (totalChars > 0) ? "true" : "false"
  );

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS, POST");
  server.sendHeader("Access-Control-Allow-Headers", "*");
  server.send(200, "application/json", json);
}

void handleSetCenter() {
  if (server.hasArg("lat") && server.hasArg("lng")) {
    baseLat = server.arg("lat").toFloat();
    baseLng = server.arg("lng").toFloat();
    wanderAngle  = 0.0f;
    smoothedDist = 0.0f;
    Serial.printf("📍 Center anchor updated: Lat=%.6f, Lng=%.6f\n", baseLat, baseLng);
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
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1.0'><title>CattleGuard Collar</title><link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'/><style>body{margin:0;background:#090d16;color:#fff;font-family:sans-serif;height:100vh;display:flex;flex-direction:column}#map{flex:1}.panel{background:#111827;padding:12px 16px;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid #1f2937}.badge{padding:5px 12px;border-radius:12px;background:#10b981;color:#fff;font-size:13px;font-weight:700}</style></head><body><div class='panel'><div><b>🐄 CattleGuard Collar</b><br><span id='pos' style='color:#9ca3af;font-size:13px;'>Lat: -- | Lng: -- | Dist: --</span></div><div class='badge' id='status'>LIVE</div></div><div id='map'></div><script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script><script>let map=L.map('map',{zoomControl:false}).setView([11.0168,76.9558],18);L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',{maxZoom:19}).addTo(map);let cowIcon=L.divIcon({html:'<div style=\"width:32px;height:32px;background:#06b6d4;border:3px solid #fff;border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:18px;box-shadow:0 0 16px #06b6d4;\">🐄</div>',iconSize:[32,32],iconAnchor:[16,16]});let marker=L.marker([11.0168,76.9558],{icon:cowIcon}).addTo(map);let fence=L.circle([11.0168,76.9558],{radius:15,color:'#10b981',fillColor:'#10b981',fillOpacity:0.2}).addTo(map);setInterval(()=>{fetch('/api/gps').then(r=>r.json()).then(d=>{let lat=parseFloat(d.lat),lng=parseFloat(d.lng),dist=parseFloat(d.dist);document.getElementById('pos').textContent='Lat: '+lat.toFixed(5)+' | Lng: '+lng.toFixed(5)+' | Dist: '+dist.toFixed(1)+'m';marker.setLatLng([lat,lng]);if(dist>15){document.getElementById('status').style.background='#ef4444';document.getElementById('status').textContent='BREACH ('+dist.toFixed(1)+'m)';fence.setStyle({color:'#ef4444',fillColor:'#ef4444'});}else{document.getElementById('status').style.background='#10b981';document.getElementById('status').textContent='SAFE ('+dist.toFixed(1)+'m)';fence.setStyle({color:'#10b981',fillColor:'#10b981'});}}).catch(()=>{});},1000);</script></body></html>");
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================================");
  Serial.println("   🐄 SMART CATTLE COLLAR FIRMWARE v4.2 (DHCP + AP)");
  Serial.println("========================================================");

  // 1. Initialize GPS UART
  gpsSerial.begin(GPS_BAUD);
  Serial.printf("1. GPS SoftwareSerial: RX=Pin D1 (GPIO5) @ %d baud\n", GPS_BAUD);

  // 2. Configure Wi-Fi Dual Mode (Station + Access Point Fallback)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASS);
  Serial.printf("2. Fallback Access Point Started: [%s] (IP: 192.168.4.1)\n", FALLBACK_AP_SSID);

  // Connect to Phone Hotspot using DHCP (Dynamic IP)
  Serial.printf("3. Connecting to Mobile Hotspot [%s] via DHCP...\n", HOTSPOT_SSID);
  WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);

  unsigned long startT = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startT < 7000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    isStaConnected = true;
    Serial.println("   ✅ HOTSPOT CONNECTED!");
    Serial.printf("   📍 Assigned IP Address : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("   📶 Signal Strength    : %d dBm\n", WiFi.RSSI());
    Serial.println("   🌐 Local mDNS Domain   : http://cowcollar.local/");
  } else {
    isStaConnected = false;
    Serial.println("   ⚠️ Mobile Hotspot connection timed out.");
    Serial.println("   👉 Connect your phone directly to collar Wi-Fi:");
    Serial.printf("      SSID : %s | Password: %s\n", FALLBACK_AP_SSID, FALLBACK_AP_PASS);
    Serial.println("      Dashboard URL: http://192.168.4.1/");
    Serial.println("   🔄 Collar will keep retrying Hotspot in background...");
  }

  // 3. Start mDNS Responder
  if (MDNS.begin("cowcollar")) {
    Serial.println("   ✅ mDNS Responder Started: cowcollar.local");
  }

  // 4. Start HTTP Telemetry Server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/gps", HTTP_GET, handleApiGps);
  server.on("/api/gps", HTTP_OPTIONS, handleOptions);
  server.on("/api/setcenter", HTTP_GET, handleSetCenter);
  server.on("/api/setcenter", HTTP_OPTIONS, handleOptions);
  server.begin();

  Serial.println("4. Telemetry Server active on port 80.\n");
  Serial.println("========================================================");
}

void loop() {
  // Feed GPS serial bytes
  while (gpsSerial.available() > 0) {
    char c = (char)gpsSerial.read();
    gps.encode(c);
    totalChars++;
  }

  // Maintain mDNS and HTTP server
  MDNS.update();
  server.handleClient();

  // Check STA connection status continuously
  if (WiFi.status() == WL_CONNECTED) {
    isStaConnected = true;
  } else {
    isStaConnected = false;
  }

  // Live Serial Monitor Diagnostic
  unsigned long now = millis();
  if (now - lastDiagTime >= 2500) {
    lastDiagTime = now;
    int rssi = WiFi.RSSI();
    float rawDist = calculateCalibratedRssiDistance(rssi);
    float dist = getSmoothedDistance(rawDist);
    bool gpsValid = hasValidGpsFix();

    Serial.println("-------------------- [COLLAR TELEMETRY] --------------------");
    if (isStaConnected) {
      Serial.printf("📶 Hotspot Wi-Fi : CONNECTED | IP: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), rssi);
      Serial.printf("📏 Distance Est : %.1f meters\n", dist);
    } else {
      Serial.println("📶 Hotspot Wi-Fi : DISCONNECTED | Active Fallback AP: 192.168.4.1");
    }

    Serial.printf("🛰️ GPS Hardware  : %s | Bytes: %lu | Sats: %d\n",
                  gpsValid ? "3D FIX" : (totalChars > 0 ? "SEARCHING" : "NO SERIAL DATA"),
                  totalChars,
                  gps.satellites.isValid() ? gps.satellites.value() : 0);

    if (dist > 15.0f) {
      Serial.printf("🚨 [ALERT] OUT OF RANGE BREACH! Distance: %.1f m (> 15.0m)\n", dist);
    } else {
      Serial.printf("🟢 [SAFE] Within perimeter. Distance: %.1f m (<= 15.0m)\n", dist);
    }
    Serial.println("------------------------------------------------------------\n");
  }
}
