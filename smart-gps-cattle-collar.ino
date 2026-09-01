/**
 * ============================================================
 *  ESP8266 NEO-6M SMART GPS LIVE TRACKER (LIGHTWEIGHT & STABLE)
 * ============================================================
 *  Board  : ESP8266 NodeMCU (ESP-12E / LoLin V3)
 *  GPS    : u-blox NEO-6M via SoftwareSerial (D1 RX, D2 TX)
 *
 *  REQUIRED LIBRARIES (Only 1 library to install!):
 *    - TinyGPSPlus by Mikal Hart
 *    (Uses built-in ESP8266WiFi and ESP8266WebServer - no heavy Async libs!)
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
 *  HOW TO VIEW:
 *    1. Turn ON Phone Hotspot (CowTracker / cow12345).
 *    2. Turn ON NodeMCU.
 *    3. Open Serial Monitor (115200 baud) or check Hotspot devices for IP.
 *    4. Open your phone browser: http://<ESP_IP>
 *       (If hotspot is not available, connect to Wi-Fi "GPS-TRACKER" pass "GPS123456"
 *        and open http://192.168.4.1)
 * ============================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>

// ---------------- CONFIGURATION ----------------
const char* HOTSPOT_SSID = "CowTracker";
const char* HOTSPOT_PASS = "cow12345";

const char* AP_SSID      = "GPS-TRACKER";
const char* AP_PASS      = "GPS123456";

#define GPS_RX_PIN D1   // GPIO 5 (Connected to NEO-6M TX)
#define GPS_TX_PIN D2   // GPIO 4 (Connected to NEO-6M RX)
#define GPS_BAUD   9600

// ---------------- OBJECTS ----------------
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;
ESP8266WebServer server(80);

// ---------------- STATE ----------------
bool isApMode = false;
unsigned long totalChars = 0;
unsigned long lastSerialDebug = 0;

// ---------------- EMBEDDED DASHBOARD HTML ----------------
static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Live GPS Cattle Tracker</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:#0f172a;color:#f8fafc;padding:12px}
.container{max-width:1100px;margin:0 auto;display:flex;flex-direction:column;gap:12px}
header{display:flex;justify-content:space-between;align-items:center;background:#1e293b;padding:12px 16px;border-radius:10px;border:1px solid #334155;flex-wrap:wrap;gap:8px}
.title{font-size:1.15rem;font-weight:700;color:#38bdf8;display:flex;align-items:center;gap:6px}
.badge{padding:4px 10px;border-radius:20px;font-size:0.75rem;font-weight:600;display:inline-flex;align-items:center;gap:5px}
.bg-ok{background:#065f46;color:#34d399}
.bg-warn{background:#78350f;color:#fbbf24}
.bg-err{background:#881337;color:#f43f5e}
.dot{width:7px;height:7px;border-radius:50%;background:currentColor;display:inline-block}

.alert-banner{padding:10px 14px;border-radius:8px;font-size:0.85rem;font-weight:600;display:flex;align-items:center;justify-content:space-between;transition:0.3s}
.alert-safe{background:rgba(16,185,129,0.15);border:1px solid #10b981;color:#6ee7b7}
.alert-breach{background:rgba(239,68,68,0.25);border:2px solid #ef4444;color:#fca5a5;animation:pulse-red 1s infinite alternate}
@keyframes pulse-red{0%{background:rgba(239,68,68,0.2)}100%{background:rgba(239,68,68,0.5);box-shadow:0 0 15px #ef4444}}

.geo-controls{background:#1e293b;padding:10px 14px;border-radius:8px;border:1px solid #334155;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px;font-size:0.85rem}
.geo-inputs{display:flex;align-items:center;gap:8px}
.geo-inputs input{background:#0f172a;border:1px solid #475569;color:#f8fafc;padding:5px 8px;border-radius:6px;width:75px;font-weight:700}
button{background:#0284c7;color:#fff;border:none;padding:6px 12px;border-radius:6px;font-weight:600;cursor:pointer;font-size:0.8rem;transition:0.2s}
button:hover{background:#0369a1}
button:disabled{opacity:0.5;cursor:not-allowed}

.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px}
.card{background:#1e293b;border:1px solid #334155;border-radius:8px;padding:10px;display:flex;flex-direction:column;gap:3px}
.card-label{font-size:0.68rem;color:#94a3b8;font-weight:600;letter-spacing:0.5px}
.card-val{font-size:1.15rem;font-weight:700;font-family:monospace;color:#f8fafc}
.card-unit{font-size:0.65rem;color:#64748b}

#map-wrap{height:420px;border-radius:10px;overflow:hidden;border:1px solid #334155}
#map{width:100%;height:100%}

.footer{display:flex;justify-content:space-between;font-size:0.75rem;color:#64748b;padding:4px 0}
</style>
</head>
<body>
<div class="container">
  <header>
    <div class="title">🛰️ LIVE GPS TRACKER</div>
    <div style="display:flex;gap:6px;flex-wrap:wrap">
      <span class="badge bg-warn" id="b-gps"><span class="dot"></span>GPS: SEARCHING</span>
      <span class="badge bg-ok" id="b-esp"><span class="dot"></span>ESP: ONLINE</span>
      <span class="badge bg-ok" id="b-time">UPDATED: 0s ago</span>
    </div>
  </header>

  <div class="alert-banner alert-safe" id="alert-box">
    <span id="alert-text">🟢 GPS Initializing... waiting for satellite fix</span>
    <label style="font-size:0.75rem;cursor:pointer"><input type="checkbox" id="chk-sound" checked> 🔊 Alarm</label>
  </div>

  <div class="geo-controls">
    <div class="geo-inputs">
      <span>🛡️ Geofence Safe Radius:</span>
      <input type="number" id="geo-radius" value="15" min="5" max="5000">
      <span>meters</span>
    </div>
    <div style="display:flex;gap:6px">
      <button id="btn-set-center">📍 Set Safe Anchor</button>
      <button id="btn-center-map" style="background:#334155">◎ Center Map</button>
      <a id="btn-gmap" href="#" target="_blank" style="text-decoration:none"><button style="background:#059669">↗ Google Maps</button></a>
    </div>
  </div>

  <div class="grid">
    <div class="card"><div class="card-label">LATITUDE</div><div class="card-val" id="v-lat">--</div><div class="card-unit">WGS84 Deg</div></div>
    <div class="card"><div class="card-label">LONGITUDE</div><div class="card-val" id="v-lng">--</div><div class="card-unit">WGS84 Deg</div></div>
    <div class="card"><div class="card-label">SPEED</div><div class="card-val" id="v-spd">--</div><div class="card-unit">km / h</div></div>
    <div class="card"><div class="card-label">ALTITUDE</div><div class="card-val" id="v-alt">--</div><div class="card-unit">meters</div></div>
    <div class="card"><div class="card-label">SATELLITES</div><div class="card-val" id="v-sat">0</div><div class="card-unit">visible</div></div>
    <div class="card"><div class="card-label">PRECISION (HDOP)</div><div class="card-val" id="v-hdop">--</div><div class="card-unit">HDOP</div></div>
    <div class="card"><div class="card-label">FIX TYPE</div><div class="card-val" id="v-fix">NO FIX</div><div class="card-unit">solution</div></div>
    <div class="card"><div class="card-label">GPS TIME (UTC)</div><div class="card-val" id="v-time">--:--:--</div><div class="card-unit" id="v-date">YYYY-MM-DD</div></div>
  </div>

  <div id="map-wrap"><div id="map"></div></div>

  <div class="footer">
    <span id="f-uart">UART: Checking...</span>
    <span id="f-dist">Distance to Anchor: 0 m</span>
  </div>
</div>

<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script>
let map, marker, accCircle, gfCircle, anchorPos = null;
let lastUpdate = Date.now(), audioCtx = null, lastBeep = 0;

function initMap() {
  map = L.map('map', {zoomControl: true}).setView([11.0168, 76.9558], 16);
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {maxZoom: 19}).addTo(map);

  const customIcon = L.divIcon({
    html: '<div style="width:18px;height:18px;background:#38bdf8;border:3px solid #fff;border-radius:50%;box-shadow:0 0 10px #0284c7"></div>',
    iconSize: [18, 18], iconAnchor: [9, 9]
  });

  marker = L.marker([0, 0], {icon: customIcon});
  accCircle = L.circle([0, 0], {radius: 10, color: '#38bdf8', fillColor: '#38bdf8', fillOpacity: 0.15, weight: 1});
  gfCircle = L.circle([0, 0], {radius: 15, color: '#10b981', fillColor: '#10b981', fillOpacity: 0.15, weight: 2, dashArray: '6,6'});
}

function playAlarm() {
  if (!document.getElementById('chk-sound').checked) return;
  const now = Date.now();
  if (now - lastBeep < 1000) return;
  lastBeep = now;
  try {
    if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    if (audioCtx.state === 'suspended') audioCtx.resume();
    const osc = audioCtx.createOscillator();
    const gain = audioCtx.createGain();
    osc.type = 'sawtooth';
    osc.frequency.setValueAtTime(880, audioCtx.currentTime);
    osc.frequency.exponentialRampToValueAtTime(440, audioCtx.currentTime + 0.25);
    gain.gain.setValueAtTime(0.2, audioCtx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime + 0.25);
    osc.connect(gain); gain.connect(audioCtx.destination);
    osc.start(); osc.stop(audioCtx.currentTime + 0.25);
  } catch(e){}
}

function updateUI(d) {
  lastUpdate = Date.now();
  document.getElementById('b-esp').className = 'badge bg-ok';
  document.getElementById('b-esp').innerHTML = '<span class="dot"></span>ESP: ONLINE';

  document.getElementById('f-uart').textContent = d.uart ? ('UART: Streaming (' + d.chars + ' bytes)') : 'UART: No Data from NEO-6M';
  document.getElementById('f-uart').style.color = d.uart ? '#34d399' : '#f43f5e';

  if (d.valid) {
    document.getElementById('b-gps').className = 'badge bg-ok';
    document.getElementById('b-gps').innerHTML = '<span class="dot"></span>GPS: FIXED (' + d.sat + ' Sats)';

    document.getElementById('v-lat').textContent = d.lat.toFixed(6) + '°';
    document.getElementById('v-lng').textContent = d.lng.toFixed(6) + '°';
    document.getElementById('v-spd').textContent = d.spd.toFixed(1);
    document.getElementById('v-alt').textContent = d.alt.toFixed(1);
    document.getElementById('v-sat').textContent = d.sat;
    document.getElementById('v-hdop').textContent = d.hdop.toFixed(1);
    document.getElementById('v-fix').textContent = d.fix;
    document.getElementById('v-time').textContent = d.time;
    document.getElementById('v-date').textContent = d.date;
    document.getElementById('btn-gmap').href = 'https://www.google.com/maps?q=' + d.lat + ',' + d.lng;

    const pos = [d.lat, d.lng];
    if (!map.hasLayer(marker)) marker.addTo(map);
    marker.setLatLng(pos);

    if (!map.hasLayer(accCircle)) accCircle.addTo(map);
    accCircle.setLatLng(pos);
    accCircle.setRadius(Math.max(d.hdop * 4, 5));

    // Geofence check
    if (!anchorPos) anchorPos = L.latLng(d.lat, d.lng);
    const curPos = L.latLng(d.lat, d.lng);
    const dist = curPos.distanceTo(anchorPos);
    const allowedRadius = parseFloat(document.getElementById('geo-radius').value) || 15;

    if (!map.hasLayer(gfCircle)) gfCircle.addTo(map);
    gfCircle.setLatLng(anchorPos);
    gfCircle.setRadius(allowedRadius);

    document.getElementById('f-dist').textContent = 'Distance to Anchor: ' + dist.toFixed(1) + ' m (Limit: ' + allowedRadius + ' m)';

    const alertBox = document.getElementById('alert-box');
    const alertText = document.getElementById('alert-text');
    if (dist > allowedRadius) {
      alertBox.className = 'alert-banner alert-breach';
      alertText.innerHTML = '🚨 OUT OF RANGE BREACH! Exceeded safe zone by ' + (dist - allowedRadius).toFixed(1) + ' m!';
      gfCircle.setStyle({color: '#ef4444', fillColor: '#ef4444', fillOpacity: 0.3});
      playAlarm();
    } else {
      alertBox.className = 'alert-banner alert-safe';
      alertText.innerHTML = '🟢 POSITION SAFE: Within ' + allowedRadius + 'm range (' + (allowedRadius - dist).toFixed(1) + 'm margin)';
      gfCircle.setStyle({color: '#10b981', fillColor: '#10b981', fillOpacity: 0.15});
    }
  } else {
    document.getElementById('b-gps').className = 'badge bg-warn';
    document.getElementById('b-gps').innerHTML = '<span class="dot"></span>GPS: SEARCHING';
    document.getElementById('v-lat').textContent = '--';
    document.getElementById('v-lng').textContent = '--';
    document.getElementById('v-spd').textContent = '--';
    document.getElementById('v-alt').textContent = '--';
    document.getElementById('v-sat').textContent = d.sat || 0;
    document.getElementById('v-hdop').textContent = '--';
    document.getElementById('v-fix').textContent = d.fix || 'NO FIX';
    document.getElementById('v-time').textContent = d.time || '--:--:--';
    document.getElementById('v-date').textContent = d.date || 'YYYY-MM-DD';

    const alertBox = document.getElementById('alert-box');
    alertBox.className = 'alert-banner alert-safe';
    alertBox.innerHTML = '🛰️ Searching for GPS satellites... Ensure antenna points to open sky.';
  }
}

function pollGps() {
  fetch('/api/gps')
    .then(r => r.json())
    .then(d => updateUI(d))
    .catch(e => {
      document.getElementById('b-esp').className = 'badge bg-err';
      document.getElementById('b-esp').innerHTML = '<span class="dot"></span>ESP: RECONNECTING';
    });
}

document.getElementById('btn-set-center').onclick = () => {
  if (marker && map.hasLayer(marker)) {
    anchorPos = marker.getLatLng();
    gfCircle.setLatLng(anchorPos);
    alert('✅ Safe zone center set to current GPS location!');
  } else {
    alert('⚠️ Waiting for valid GPS fix before setting anchor.');
  }
};

document.getElementById('btn-center-map').onclick = () => {
  if (marker && map.hasLayer(marker)) map.panTo(marker.getLatLng());
};

document.getElementById('geo-radius').oninput = () => {
  const r = parseFloat(document.getElementById('geo-radius').value) || 15;
  if (gfCircle) gfCircle.setRadius(r);
};

setInterval(() => {
  const sec = ((Date.now() - lastUpdate) / 1000).toFixed(0);
  document.getElementById('b-time').textContent = 'UPDATED: ' + sec + 's ago';
}, 1000);

initMap();
pollGps();
setInterval(pollGps, 1000);
</script>
</body>
</html>
)rawliteral";

// ---------------- SEND JSON TELEMETRY ----------------
void handleApiGps() {
  bool valid = gps.location.isValid() && (gps.location.age() < 3000) && (fabs(gps.location.lat()) > 0.0001);

  char latStr[16] = "0.000000";
  char lngStr[16] = "0.000000";
  char spdStr[10] = "0.0";
  char altStr[10] = "0.0";
  char hdopStr[10] = "99.9";

  if (valid) {
    dtostrf(gps.location.lat(), 1, 6, latStr);
    dtostrf(gps.location.lng(), 1, 6, lngStr);
    if (gps.speed.isValid())    dtostrf(gps.speed.knots() * 1.852f, 1, 1, spdStr);
    if (gps.altitude.isValid()) dtostrf(gps.altitude.meters(), 1, 1, altStr);
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
    "{\"valid\":%s,\"lat\":%s,\"lng\":%s,\"spd\":%s,\"alt\":%s,\"sat\":%d,\"hdop\":%s,\"fix\":\"%s\",\"time\":\"%s\",\"date\":\"%s\",\"chars\":%lu,\"uart\":%s}",
    valid ? "true" : "false",
    latStr, lngStr, spdStr, altStr, sats, hdopStr,
    fixStr, timeStr, dateStr,
    totalChars, (totalChars > 0) ? "true" : "false"
  );

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", jsonBuf);
}

void handleRoot() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n\n========================================");
  Serial.println(" ESP8266 GPS TRACKER (SUPER LIGHTWEIGHT)");
  Serial.println("========================================");

  // Initialize SoftwareSerial for NEO-6M GPS
  gpsSerial.begin(GPS_BAUD);
  Serial.printf("GPS SoftwareSerial initialized on D1(RX) / D2(TX) @ %d baud\n", GPS_BAUD);

  // Connect to Phone Hotspot
  Serial.printf("Connecting to Phone Hotspot [%s]...\n", HOTSPOT_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    isApMode = false;
    Serial.print("✅ CONNECTED TO PHONE HOTSPOT! IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    // If phone hotspot is not found, launch Access Point
    isApMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.println("⚠️ Hotspot not found. Started fallback Access Point.");
    Serial.printf("SSID: %s | Pass: %s | IP: ", AP_SSID, AP_PASS);
    Serial.println(WiFi.softAPIP());
  }

  // Setup Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/gps", HTTP_GET, handleApiGps);
  server.begin();

  Serial.println("\n----------------------------------------");
  Serial.print("OPEN DASHBOARD AT: http://");
  Serial.println(isApMode ? WiFi.softAPIP() : WiFi.localIP());
  Serial.println("----------------------------------------\n");
}

// ---------------- MAIN LOOP ----------------
void loop() {
  // 1. Process GPS Data continuously
  while (gpsSerial.available() > 0) {
    char c = (char)gpsSerial.read();
    gps.encode(c);
    totalChars++;
  }

  // 2. Handle incoming HTTP requests
  server.handleClient();

  // 3. Periodic debug to Serial Monitor
  unsigned long now = millis();
  if (now - lastSerialDebug >= 3000) {
    lastSerialDebug = now;
    bool valid = gps.location.isValid() && (gps.location.age() < 3000) && (fabs(gps.location.lat()) > 0.0001);
    Serial.printf("[STATUS] GPS: %s | Chars: %lu | Sats: %d",
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

