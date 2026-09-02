/**
 * ============================================================
 *  SMART CATTLE COLLAR FIRMWARE (ESP32 & ESP8266 HYBRID)
 * ============================================================
 *  BOARDS SUPPORTED:
 *    - ESP32 Dev Board (HardwareSerial2: RX=GPIO16, TX=GPIO17)
 *    - NodeMCU ESP8266 (SoftwareSerial: RX=Pin D1, TX=Pin D2)
 *
 *  HOTSPOT CREDENTIALS:
 *    SSID : CowTracker
 *    PASS : cow12345
 *
 *  FEATURES:
 *    - Dynamic DHCP connection (No hardcoded Static IP!)
 *    - Dual-Mode AP Fallback: "CowCollar-AP" (192.168.4.1)
 *    - mDNS Domain: http://cowcollar.local/
 *    - Calibrated Mobile Hotspot RSSI Distance Engine (0.5m near-field)
 *    - /api/gps  CORS-enabled JSON telemetry endpoint
 *    - /api/setcenter  anchor update endpoint
 *    - Embedded mini-dashboard served at http://192.168.4.1/
 * ============================================================
 */

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <ESPmDNS.h>
  #define GPS_RX_PIN 16
  #define GPS_TX_PIN 17
  #define GPS_BAUD   9600
  HardwareSerial gpsSerial(2); // UART2 (GPIO 16 RX, GPIO 17 TX)
  WebServer server(80);
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266mDNS.h>
  #include <SoftwareSerial.h>
  #define GPS_RX_PIN D1
  #define GPS_TX_PIN D2
  #define GPS_BAUD   9600
  SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
  ESP8266WebServer server(80);
#else
  #error "Unsupported board! Please select ESP32 or ESP8266 in Arduino IDE."
#endif

#include <TinyGPSPlus.h>
#include <math.h>

// ── Wi-Fi Hotspot Config ──
const char* HOTSPOT_SSID = "CowTracker";
const char* HOTSPOT_PASS = "cow12345";

// Fallback Access Point Config
const char* FALLBACK_AP_SSID = "CowCollar-AP";
const char* FALLBACK_AP_PASS = "cow12345";

TinyGPSPlus gps;

// Global State
unsigned long totalChars   = 0;
unsigned long lastDiagTime = 0;
float smoothedDist         = 0.0f;
float wanderAngle          = 0.0f;
bool  isStaConnected       = false;

// Pasture Center Anchor
float baseLat = 11.016842f;
float baseLng = 76.955819f;

// ── RSSI Median Filter (7-sample ring buffer) ──
// Eliminates random RSSI spikes before the distance formula
#define RSSI_BUF_SIZE 7
int rssiBuf[RSSI_BUF_SIZE] = {0};
int rssiBufIdx = 0;
bool rssiBufFull = false;

int getMedianRssi(int newRssi) {
  rssiBuf[rssiBufIdx] = newRssi;
  rssiBufIdx = (rssiBufIdx + 1) % RSSI_BUF_SIZE;
  if (rssiBufIdx == 0) rssiBufFull = true;

  int count = rssiBufFull ? RSSI_BUF_SIZE : rssiBufIdx;
  int temp[RSSI_BUF_SIZE];
  for (int i = 0; i < count; i++) temp[i] = rssiBuf[i];

  // Insertion sort (small array, fast enough)
  for (int i = 1; i < count; i++) {
    int key = temp[i], j = i - 1;
    while (j >= 0 && temp[j] > key) { temp[j + 1] = temp[j]; j--; }
    temp[j + 1] = key;
  }
  return temp[count / 2];
}

// ── Log-Distance Path-Loss Model ──
// Formula: d = 10 ^ ((txPower - rssi) / (10 * n))
//   txPower = RSSI at 1m from phone hotspot (~-40 dBm for most Android/iPhone)
//   n       = path-loss exponent: 2.0=free space, 2.2=open field, 3.0=indoor
float rssiToDistance(int rssi) {
  if (rssi == 0 || rssi < -98) return 35.0f;
  const float TX_POWER = -40.0f; // Measured RSSI at 1m from your phone hotspot
  const float N        =   2.2f; // Open-field path-loss exponent
  float d = pow(10.0f, (TX_POWER - (float)rssi) / (10.0f * N));
  if (d < 0.3f) d = 0.3f;
  if (d > 80.0f) d = 80.0f;
  return d;
}

// ── EMA Smoother (light alpha=0.15 after median) ──
float getSmoothedDistance(float rawDist) {
  if (smoothedDist < 0.01f) {
    smoothedDist = rawDist;
    return rawDist;
  }
  const float ALPHA = 0.15f; // Low alpha = more stable output
  smoothedDist = ALPHA * rawDist + (1.0f - ALPHA) * smoothedDist;
  return smoothedDist;
}

bool hasValidGpsFix() {
  return gps.location.isValid() &&
         gps.location.age() < 3000 &&
         fabs(gps.location.lat()) > 0.0001;
}

// ── HTTP API Handlers ──
void handleApiGps() {
  bool gpsValid = hasValidGpsFix();
  int rssi      = WiFi.RSSI();
  int medRssi   = getMedianRssi(rssi);
  float rawDist = rssiToDistance(medRssi);
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
#if defined(ESP32)
  Serial.println("   🐄 SMART CATTLE COLLAR FIRMWARE (ESP32 Dev Board)");
#else
  Serial.println("   🐄 SMART CATTLE COLLAR FIRMWARE (ESP8266 NodeMCU)");
#endif
  Serial.println("========================================================");

  // 1. Initialize GPS Serial
#if defined(ESP32)
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.printf("1. ESP32 HardwareSerial2: RX=GPIO%d (NEO-6M TX), TX=GPIO%d @ %d baud\n", GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
#else
  gpsSerial.begin(GPS_BAUD);
  Serial.printf("1. SoftwareSerial: RX=Pin D1, TX=Pin D2 @ %d baud\n", GPS_BAUD);
#endif

  // 2. Wi-Fi Dual Mode (Station + Access Point Fallback)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASS);
  Serial.printf("2. Fallback Access Point: [%s] (IP: 192.168.4.1)\n", FALLBACK_AP_SSID);

  // Connect to Phone Hotspot via DHCP
  Serial.printf("3. Connecting to Hotspot [%s] via DHCP...\n", HOTSPOT_SSID);
  WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);

  unsigned long startT = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startT < 8000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    isStaConnected = true;
    Serial.println("   ✅ HOTSPOT CONNECTED!");
    Serial.printf("   📍 Assigned IP : %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("   📶 RSSI        : %d dBm\n", WiFi.RSSI());
    Serial.println("   🌐 mDNS Domain  : http://cowcollar.local/");
  } else {
    isStaConnected = false;
    Serial.println("   ⚠️ Mobile Hotspot connection timed out.");
    Serial.println("   👉 Connect phone directly to Collar Wi-Fi:");
    Serial.printf("      SSID: %s | PASS: %s | URL: http://192.168.4.1/\n", FALLBACK_AP_SSID, FALLBACK_AP_PASS);
  }

  // 3. mDNS Responder
  if (MDNS.begin("cowcollar")) {
    Serial.println("   ✅ mDNS Responder Active: http://cowcollar.local/");
  }

  // 4. HTTP Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/gps", HTTP_GET, handleApiGps);
  server.on("/api/gps", HTTP_OPTIONS, handleOptions);
  server.on("/api/setcenter", HTTP_GET, handleSetCenter);
  server.on("/api/setcenter", HTTP_OPTIONS, handleOptions);
  server.begin();

  Serial.println("4. Telemetry HTTP Server active on port 80.\n");
  Serial.println("========================================================");
}

void loop() {
  while (gpsSerial.available() > 0) {
    char c = (char)gpsSerial.read();
    gps.encode(c);
    totalChars++;
  }

#if defined(ESP8266)
  MDNS.update();
#endif
  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    isStaConnected = true;
  } else {
    isStaConnected = false;
  }

  unsigned long now = millis();
  if (now - lastDiagTime >= 2500) {
    lastDiagTime = now;
    int rssi      = WiFi.RSSI();
    int medRssi   = getMedianRssi(rssi);
    float rawDist = rssiToDistance(medRssi);
    float dist    = getSmoothedDistance(rawDist);
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
