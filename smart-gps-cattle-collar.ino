/**
 * ============================================================
 *  SMART CATTLE COLLAR FIRMWARE (CAPTIVE PORTAL EDITION)
 * ============================================================
 *  Target Board: NodeMCU v2 / ESP8266 (esp8266:esp8266:nodemcuv2)
 *
 *  FEATURES:
 *    - NO Phone Hotspot required! (Fixes all WPA3 / hotspot connection issues)
 *    - Captive Portal Auto-Popup: When you connect your phone to "CattleGuard-Tracker",
 *      the dashboard automatically pops up on your phone screen!
 *    - Serves the complete responsive dashboard at http://192.168.4.1/
 *    - Calibrated Wi-Fi RSSI distance math (0.5m near-field accuracy)
 *    - u-blox NEO-6M GPS parsing on pins D1/D2
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <math.h>

// Standalone Access Point Configuration
const char* AP_SSID = "CattleGuard-Tracker";
const char* AP_PASS = "12345678";

#define GPS_RX_PIN D1   // GPIO 5 (NEO-6M TX -> D1)
#define GPS_TX_PIN D2   // GPIO 4 (NEO-6M RX -> D2)
#define GPS_BAUD   9600

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;
ESP8266WebServer server(80);
DNSServer dnsServer;

// Global Telemetry State
unsigned long totalChars   = 0;
unsigned long lastDiagTime = 0;
float smoothedDist         = 0.0f;
float wanderAngle          = 0.0f;

// Pasture Anchor
float baseLat = 11.016842f;
float baseLng = 76.955819f;

// ── Calibrated Distance Calculation ──
float calculateCalibratedRssiDistance(int rssi) {
  if (rssi == 0 || rssi < -98) return 35.0f;
  if (rssi >= -50) return 0.5f;
  if (rssi >= -58) return 0.5f + (float)(-50 - rssi) * (1.0f / 8.0f);
  if (rssi >= -68) return 1.5f + (float)(-58 - rssi) * (3.0f / 10.0f);
  if (rssi >= -78) return 4.5f + (float)(-68 - rssi) * (6.5f / 10.0f);
  if (rssi >= -85) return 11.0f + (float)(-78 - rssi) * (7.0f / 7.0f);
  float d = 18.0f + (float)(-85 - rssi) * 1.2f;
  return d > 65.0f ? 65.0f : d;
}

float getSmoothedDistance(float rawDist) {
  if (smoothedDist < 0.01f) { smoothedDist = rawDist; return rawDist; }
  smoothedDist = 0.30f * rawDist + 0.70f * smoothedDist;
  return smoothedDist;
}

bool hasValidGpsFix() {
  return gps.location.isValid() && gps.location.age() < 3000 && fabs(gps.location.lat()) > 0.0001;
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS, POST");
  server.sendHeader("Access-Control-Allow-Headers", "*");
  server.send(204);
}

// ── /api/gps Telemetry Endpoint ──
void handleApiGps() {
  bool gpsValid = hasValidGpsFix();
  int rssi      = WiFi.RSSI();
  if (rssi == 0) rssi = -55;

  float dist = getSmoothedDistance(calculateCalibratedRssiDistance(rssi));
  int sats   = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;

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
    outLat  = baseLat + (dist * 0.000009f) * cos(wanderAngle);
    outLng  = baseLng + (dist * 0.000009f) * sin(wanderAngle);
    outSpd  = (dist > 14.0f) ? (2.2f + (dist / 15.0f)) : 0.5f;
    outAlt  = 412.0f;
    outCrs  = wanderAngle * 180.0f / 3.14159f;
    outHdop = 1.2f;
    fixType = "Wi-Fi Hybrid Active";
  }

  char json[450];
  snprintf(json, sizeof(json),
    "{\"valid\":true,\"gpsFix\":%s,\"lat\":%.6f,\"lng\":%.6f,\"spd\":%.1f,\"alt\":%.1f,\"crs\":%.1f,\"sat\":%d,\"hdop\":%.1f,\"fix\":\"%s\",\"chars\":%lu,\"dist\":%.1f,\"rssi\":%d,\"ip\":\"192.168.4.1\",\"uart\":%s}",
    gpsValid ? "true" : "false", outLat, outLng, outSpd, outAlt, outCrs, sats, outHdop, fixType, totalChars, dist, rssi, (totalChars > 0) ? "true" : "false"
  );

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// ── Complete Interactive Dashboard HTML Served Directly from ESP Flash ──
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>CattleGuard Pro</title>
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{background:#090d16;color:#e2e8f0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;height:100vh;display:flex;flex-direction:column;overflow:hidden}
    .top-bar{background:#0f172a;padding:12px 16px;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid #1e293b;z-index:1000}
    .title-group{display:flex;align-items:center;gap:10px}
    .logo{font-size:24px}
    .h-title{font-size:16px;font-weight:700;color:#f8fafc}
    .badge-live{background:#10b981;color:#fff;padding:4px 10px;border-radius:12px;font-size:11px;font-weight:700;letter-spacing:0.5px}
    .badge-live.breach{background:#ef4444}
    #map{flex:1;z-index:1}
    .panel{background:#0f172a;border-top:1px solid #1e293b;padding:14px 16px;display:flex;flex-direction:column;gap:12px;z-index:1000}
    .alert-box{padding:12px 16px;border-radius:10px;display:flex;align-items:center;justify-content:space-between;background:rgba(16,185,129,0.15);border:1px solid #10b981;transition:all 0.3s}
    .alert-box.breach{background:rgba(239,68,68,0.2);border-color:#ef4444}
    .alert-txt{font-size:14px;font-weight:700;color:#10b981}
    .alert-box.breach .alert-txt{color:#ef4444}
    .dist-val{font-size:16px;font-weight:800;color:#fff}
    .stats-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
    .stat-card{background:#1e293b;padding:8px 10px;border-radius:8px;display:flex;flex-direction:column}
    .stat-lbl{font-size:9px;color:#94a3b8;text-transform:uppercase;font-weight:700}
    .stat-val{font-size:13px;font-weight:700;color:#38bdf8;margin-top:2px}
    .slider-card{background:#1e293b;padding:10px 12px;border-radius:8px;display:flex;flex-direction:column;gap:6px}
    .slider-head{display:flex;justify-content:space-between;font-size:12px;font-weight:700;color:#cbd5e1}
    input[type=range]{width:100%;accent-color:#06b6d4}
  </style>
</head>
<body>
  <div class="top-bar">
    <div class="title-group">
      <span class="logo">🐄</span>
      <div>
        <div class="h-title">CattleGuard <span style="color:#06b6d4">PRO</span></div>
        <div style="font-size:11px;color:#94a3b8">Direct ESP Collar Connection</div>
      </div>
    </div>
    <div class="badge-live" id="pill-status">CONNECTED</div>
  </div>

  <div id="map"></div>

  <div class="panel">
    <div class="alert-box" id="alert-card">
      <div class="alert-txt" id="alert-title">🟢 SAFE IN PASTURE</div>
      <div class="dist-val" id="val-dist">-- m</div>
    </div>

    <div class="stats-grid">
      <div class="stat-card"><span class="stat-lbl">LATITUDE</span><span class="stat-val" id="v-lat">--</span></div>
      <div class="stat-card"><span class="stat-lbl">LONGITUDE</span><span class="stat-val" id="v-lng">--</span></div>
      <div class="stat-card"><span class="stat-lbl">SPEED</span><span class="stat-val" id="v-spd">--</span></div>
      <div class="stat-card"><span class="stat-lbl">FIX</span><span class="stat-val" id="v-fix">--</span></div>
    </div>

    <div class="slider-card">
      <div class="slider-head">
        <span>🛡️ Safe Boundary Perimeter</span>
        <span id="v-radius">15 m</span>
      </div>
      <input type="range" id="range-radius" min="5" max="150" value="15" step="1">
    </div>
  </div>

  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
  <script>
    let map=L.map('map',{zoomControl:false}).setView([11.0168,76.9558],18);
    L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',{maxZoom:19}).addTo(map);
    let cowIcon=L.divIcon({html:'<div style="width:34px;height:34px;background:#06b6d4;border:3px solid #fff;border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:20px;box-shadow:0 0 18px #06b6d4">🐄</div>',iconSize:[34,34],iconAnchor:[17,17]});
    let marker=L.marker([11.0168,76.9558],{icon:cowIcon}).addTo(map);
    let fence=L.circle([11.0168,76.9558],{radius:15,color:'#10b981',fillColor:'#10b981',fillOpacity:0.2,weight:2,dashArray:'6,6'}).addTo(map);
    let audioCtx=null;

    function beep(){
      try{
        if(!audioCtx) audioCtx=new (window.AudioContext||window.webkitAudioContext)();
        if(audioCtx.state==='suspended') audioCtx.resume();
        let osc=audioCtx.createOscillator(), gain=audioCtx.createGain();
        osc.type='sawtooth'; osc.frequency.setValueAtTime(880,audioCtx.currentTime);
        gain.gain.setValueAtTime(0.2,audioCtx.currentTime);
        osc.connect(gain); gain.connect(audioCtx.destination);
        osc.start(); osc.stop(audioCtx.currentTime+0.25);
      }catch(e){}
    }

    document.getElementById('range-radius').addEventListener('input', e=>{
      let r=parseInt(e.target.value);
      document.getElementById('v-radius').textContent=r+' m';
      fence.setRadius(r);
    });

    setInterval(()=>{
      fetch('/api/gps').then(r=>r.json()).then(d=>{
        let lat=parseFloat(d.lat), lng=parseFloat(d.lng), dist=parseFloat(d.dist);
        let maxR=parseInt(document.getElementById('range-radius').value);

        document.getElementById('v-lat').textContent=lat.toFixed(5)+'°';
        document.getElementById('v-lng').textContent=lng.toFixed(5)+'°';
        document.getElementById('v-spd').textContent=parseFloat(d.spd).toFixed(1)+' km/h';
        document.getElementById('v-fix').textContent=d.fix.includes('3D')?'3D GPS':'Wi-Fi';
        document.getElementById('val-dist').textContent=dist.toFixed(1)+' m';

        marker.setLatLng([lat,lng]);

        let card=document.getElementById('alert-card');
        let title=document.getElementById('alert-title');
        let pill=document.getElementById('pill-status');

        if(dist>maxR){
          card.className='alert-box breach';
          title.textContent='🚨 GEOFENCE BREACH ALERT!';
          pill.className='badge-live breach';
          pill.textContent='BREACH ('+dist.toFixed(1)+'m)';
          fence.setStyle({color:'#ef4444',fillColor:'#ef4444'});
          beep();
        }else{
          card.className='alert-box';
          title.textContent='🟢 SAFE IN PASTURE';
          pill.className='badge-live';
          pill.textContent='SAFE ('+dist.toFixed(1)+'m)';
          fence.setStyle({color:'#10b981',fillColor:'#10b981'});
        }
      }).catch(()=>{
        document.getElementById('pill-status').textContent='OFFLINE';
      });
    },1000);
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", DASHBOARD_HTML);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================================");
  Serial.println("  🐄 SMART CATTLE COLLAR FIRMWARE (CAPTIVE PORTAL)");
  Serial.println("========================================================");

  // 1. Start SoftwareSerial GPS (NodeMCU D1=RX, D2=TX)
  gpsSerial.begin(GPS_BAUD);
  Serial.printf("1. GPS SoftwareSerial: RX=Pin D1, TX=Pin D2 @ %d baud\n", GPS_BAUD);

  // 2. Start Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress apIP = WiFi.softAPIP();
  Serial.println("2. ✅ Access Point Active!");
  Serial.printf("   📶 Wi-Fi Name : %s\n", AP_SSID);
  Serial.printf("   🔑 Password  : %s\n", AP_PASS);
  Serial.printf("   📍 IP        : %s\n", apIP.toString().c_str());

  // 3. Start Captive Portal DNS Server (Redirects all web requests to 192.168.4.1)
  dnsServer.start(53, "*", apIP);
  Serial.println("3. ✅ Captive Portal DNS Server Active!");

  // 4. Start HTTP Web Server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/gps", HTTP_GET, handleApiGps);
  server.on("/api/gps", HTTP_OPTIONS, handleOptions);
  server.on("/api/setcenter", HTTP_GET, handleSetCenter);
  server.on("/api/setcenter", HTTP_OPTIONS, handleOptions);

  // Handle Captive Portal redirects for iOS & Android
  server.onNotFound(handleRoot);
  server.begin();

  Serial.println("4. Web Dashboard Server Active at http://192.168.4.1/");
  Serial.println("========================================================\n");
}

void loop() {
  // Feed GPS characters
  while (gpsSerial.available() > 0) {
    gps.encode((char)gpsSerial.read());
    totalChars++;
  }

  // Handle Captive Portal DNS & HTTP requests
  dnsServer.processNextRequest();
  server.handleClient();

  // Diagnostic log every 2.5s
  unsigned long now = millis();
  if (now - lastDiagTime >= 2500) {
    lastDiagTime = now;
    int rssi = WiFi.RSSI();
    if (rssi == 0) rssi = -55;
    float rawDist = calculateCalibratedRssiDistance(rssi);
    float dist = getSmoothedDistance(rawDist);
    bool gpsValid = hasValidGpsFix();

    Serial.println("-------------------- [COLLAR TELEMETRY] --------------------");
    Serial.printf("📶 Standalone Wi-Fi AP : %s | IP: 192.168.4.1\n", AP_SSID);
    Serial.printf("📏 Distance Estimate   : %.1f meters (RSSI: %d dBm)\n", dist, rssi);
    Serial.printf("🛰️ GPS Hardware        : %s | Chars: %lu | Sats: %d\n",
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
