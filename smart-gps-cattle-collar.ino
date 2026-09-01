/**
 * ============================================================
 *  ESP8266 NEO-6M Smart GPS Live Tracking Dashboard
 *  Board  : ESP8266 NodeMCU (LoLin V3 / ESP-12E)
 *  GPS    : u-blox NEO-6M via SoftwareSerial
 *
 *  WIRING (NodeMCU -> NEO-6M):
 *    3.3V  -> VCC
 *    GND   -> GND
 *    D1 (GPIO5)  -> NEO-6M TX  (ESP receives GPS data here)
 *    D2 (GPIO4)  -> NEO-6M RX  (optional config commands)
 *
 *  ARDUINO IDE LIBRARIES (install via Library Manager):
 *    - TinyGPSPlus          by Mikal Hart
 *    - ESPAsyncWebServer    by lacamera (ESP8266 fork)
 *    - ESPAsyncTCP          by dvarrel
 *    - ArduinoJson          by Benoit Blanchon (v6.x)
 *
 *  BOARD MANAGER:
 *    - Add URL: http://arduino.esp8266.com/stable/package_esp8266com_index.json
 *    - Install: esp8266 by ESP8266 Community
 *    - Select Board: "NodeMCU 1.0 (ESP-12E Module)"
 *
 *  ACCESS DASHBOARD:
 *    Option A (Home Wi-Fi): Update WIFI_SSID & WIFI_PASSWORD below,
 *              then check Serial Monitor for IP after upload.
 *              Open browser: http://<ESP_IP>
 *    Option B (Field Mode): Connect phone to Wi-Fi "GPS-TRACKER"
 *              Password: GPS123456
 *              Open browser: http://192.168.4.1
 * ============================================================
 */

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <TinyGPSPlus.h>

// ============================================================
//  CONFIGURATION — Edit your Wi-Fi credentials here
// ============================================================
static const char* WIFI_SSID     = "YOUR_WIFI_SSID";
static const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
static const char* AP_SSID       = "GPS-TRACKER";
static const char* AP_PASSWORD   = "GPS123456";

// GPS SoftwareSerial Pins (NodeMCU)
#define GPS_RX_PIN  D1   // GPIO5 - receives NMEA data from NEO-6M TX
#define GPS_TX_PIN  D2   // GPIO4 - sends commands to NEO-6M RX (optional)
#define GPS_BAUD    9600

// Intervals
#define WIFI_TIMEOUT_MS        10000
#define BROADCAST_INTERVAL_MS  1000
#define DEBUG_INTERVAL_MS      3000

// ============================================================
//  GLOBALS
// ============================================================
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus    gps;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

bool     isApMode      = false;
uint32_t lastBroadcast = 0;
uint32_t lastDebug     = 0;
uint32_t lastGpsByte   = 0;
uint32_t totalGpsChars = 0;
bool     hadValidFix   = false;

// ============================================================
//  HTML PAGE (stored in PROGMEM flash to save RAM)
// ============================================================
static const char HTML_PAGE[] PROGMEM = R"===(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>GPS Cattle Collar - Live Tracker</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;600&display=swap" rel="stylesheet">
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" crossorigin=""/>
<style>
:root{--bg:#0B0E14;--card:#151B26;--card2:#1C2433;--border:#242D3D;--border2:#2A364F;--text:#F0F4F8;--muted:#94A3B8;--dim:#64748B;--cyan:#06B6D4;--blue:#3B82F6;--green:#10B981;--yellow:#F59E0B;--red:#EF4444;--ff:'Inter',-apple-system,sans-serif;--mono:'JetBrains Mono',monospace;--r:12px;--rm:8px}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:var(--ff);background:var(--bg);color:var(--text);min-height:100vh;display:flex;flex-direction:column;-webkit-font-smoothing:antialiased}
.mono{font-family:var(--mono)}
.wrap{max-width:1400px;margin:0 auto;padding:1.5rem;display:flex;flex-direction:column;gap:1.5rem}
header{display:flex;justify-content:space-between;align-items:center;padding-bottom:1rem;border-bottom:1px solid var(--border);flex-wrap:wrap;gap:1rem}
.brand{display:flex;align-items:center;gap:.75rem}
.logo{width:44px;height:44px;background:linear-gradient(135deg,rgba(6,182,212,.2),rgba(59,130,246,.2));border:1px solid var(--cyan);border-radius:var(--rm);display:flex;align-items:center;justify-content:center;font-size:1.4rem}
h1{font-size:1.3rem;font-weight:700;letter-spacing:.5px}
.sub{font-size:.82rem;color:var(--muted)}
.sbar{display:flex;gap:.75rem;flex-wrap:wrap}
.badge{background:var(--card);border:1px solid var(--border);padding:.4rem .85rem;border-radius:9999px;display:flex;align-items:center;gap:.5rem;font-size:.78rem;font-weight:500}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;flex-shrink:0}
.dg{background:var(--green);box-shadow:0 0 8px var(--green)}
.dy{background:var(--yellow);box-shadow:0 0 8px var(--yellow)}
.dr{background:var(--red);box-shadow:0 0 8px var(--red)}
.db{background:var(--blue);box-shadow:0 0 8px var(--blue)}
.dp{background:var(--cyan);animation:pulse 1.5s infinite}
@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(6,182,212,.7)}70%{box-shadow:0 0 0 6px rgba(6,182,212,0)}100%{box-shadow:0 0 0 0 rgba(6,182,212,0)}}
.bl{color:var(--dim)}.bv{color:var(--text);font-weight:600}
.banner{display:flex;align-items:center;gap:.75rem;padding:.85rem 1.25rem;border-radius:var(--rm);font-size:.88rem;font-weight:500;transition:all .3s}
.bwarn{background:rgba(245,158,11,.12);border:1px solid rgba(245,158,11,.3);color:#FCD34D}
.bok{background:rgba(16,185,129,.12);border:1px solid rgba(16,185,129,.3);color:#6EE7B7}
.berr{background:rgba(239,68,68,.12);border:1px solid rgba(239,68,68,.3);color:#FCA5A5}
.btxt{flex:1}
.grid{display:grid;grid-template-columns:1fr;gap:1.5rem}
@media(min-width:1024px){.grid{grid-template-columns:1.1fr 1fr}}
.sh{display:flex;justify-content:space-between;align-items:center;margin-bottom:1rem}
.sh h2{font-size:1.05rem;font-weight:600}
.cgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(155px,1fr));gap:1rem}
.tcard{background:var(--card);border:1px solid var(--border);border-radius:var(--r);padding:1.1rem;display:flex;flex-direction:column;gap:.5rem;transition:transform .2s,border-color .2s}
.tcard:hover{transform:translateY(-2px);border-color:var(--border2)}
.acc{border-color:rgba(6,182,212,.4);background:linear-gradient(180deg,rgba(6,182,212,.08) 0%,var(--card) 100%)}
.ch{display:flex;justify-content:space-between;align-items:center}
.ct{font-size:.7rem;font-weight:600;color:var(--dim);letter-spacing:.5px}
.ci{font-size:1rem}
.cv{font-size:1.4rem;font-weight:700;color:var(--text);word-break:break-all}
.cf{font-size:.7rem;color:var(--muted)}
.qpill{padding:.2rem .65rem;border-radius:9999px;font-size:.73rem;font-weight:600}
.pn{background:#334155;color:#94A3B8}.pe{background:rgba(16,185,129,.2);color:#10B981;border:1px solid #10B981}
.pg{background:rgba(59,130,246,.2);color:#3B82F6;border:1px solid #3B82F6}
.pf{background:rgba(245,158,11,.2);color:#F59E0B;border:1px solid #F59E0B}
.pp{background:rgba(239,68,68,.2);color:#EF4444;border:1px solid #EF4444}
.msec{display:flex;flex-direction:column;gap:1rem}
.mwrap{position:relative;width:100%;height:420px;border-radius:var(--r);overflow:hidden;border:1px solid var(--border)}
#map{width:100%;height:100%;background:#0D1117}
.leaflet-tile-pane{filter:brightness(.8) invert(1) contrast(1.2) hue-rotate(200deg) saturate(.3)}
.gcard{background:var(--card);border:1px solid var(--border);border-radius:var(--r);padding:1.25rem;display:flex;flex-direction:column;gap:1rem}
.gchf{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:.75rem}
.gchl{display:flex;align-items:center;gap:.5rem}
.gt{font-size:1rem;font-weight:600}
.gtw{display:flex;align-items:center;gap:.5rem}
.slab{font-size:.8rem;font-weight:600;color:var(--cyan)}
.sw{position:relative;display:inline-block;width:44px;height:24px}
.sw input{opacity:0;width:0;height:0}
.sl{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#334155;transition:.3s;border-radius:24px}
.sl:before{position:absolute;content:"";height:18px;width:18px;left:3px;bottom:3px;background:#fff;transition:.3s;border-radius:50%}
input:checked+.sl{background:var(--cyan)}
input:checked+.sl:before{transform:translateX(20px)}
.ginp{display:flex;align-items:flex-end;justify-content:space-between;flex-wrap:wrap;gap:1rem}
.ifg{display:flex;flex-direction:column;gap:.35rem;flex:1;min-width:220px}
.ifg label{font-size:.8rem;color:var(--muted);font-weight:500}
.iwu{display:flex;align-items:center;gap:.5rem}
.iwu input{background:#0D1117;border:1px solid var(--border);border-radius:var(--rm);color:var(--text);padding:.5rem .75rem;font-family:var(--mono);font-size:.95rem;width:110px}
.iwu input:focus{outline:none;border-color:var(--cyan)}
.utag{font-size:.76rem;color:var(--dim)}
.gact{display:flex;gap:.5rem;flex-wrap:wrap}
.abox{display:flex;align-items:center;gap:1rem;padding:.85rem 1.15rem;border-radius:var(--rm);transition:all .3s;flex-wrap:wrap}
.safe{background:rgba(16,185,129,.1);border:1px solid rgba(16,185,129,.3);color:#6EE7B7}
.breach{background:rgba(239,68,68,.2);border:2px solid #EF4444;color:#FCA5A5;animation:flash 1s infinite alternate}
@keyframes flash{0%{background:rgba(239,68,68,.2);box-shadow:0 0 10px rgba(239,68,68,.3)}100%{background:rgba(239,68,68,.45);box-shadow:0 0 25px rgba(239,68,68,.8)}}
.ai{font-size:1.5rem}.ac{display:flex;flex-direction:column;flex:1}
.at{font-size:.88rem;font-weight:700;letter-spacing:.5px}
.ad{font-size:.8rem;font-family:var(--mono)}
.auw{font-size:.8rem;color:var(--muted)}
.cbar{display:flex;justify-content:space-between;gap:1rem;flex-wrap:wrap}
.bgrp{display:flex;gap:.5rem;flex-wrap:wrap}
.btn{display:inline-flex;align-items:center;gap:.4rem;padding:.55rem 1rem;border-radius:var(--rm);font-size:.84rem;font-weight:500;cursor:pointer;border:1px solid transparent;transition:all .2s;text-decoration:none;font-family:var(--ff)}
.bpri{background:var(--cyan);color:#000;font-weight:600}
.bpri:hover:not(:disabled){background:#22D3EE;box-shadow:0 0 12px rgba(6,182,212,.4)}
.bsec{background:var(--card);color:var(--text);border-color:var(--border)}
.bsec:hover:not(.dis){background:var(--card2)}
.bsuc{background:rgba(16,185,129,.2);color:#10B981;border-color:rgba(16,185,129,.4)}
.bsuc:hover:not(:disabled){background:rgba(16,185,129,.35)}
.bwrn{background:rgba(245,158,11,.2);color:#F59E0B;border-color:rgba(245,158,11,.4)}
.bwrn:hover:not(:disabled){background:rgba(245,158,11,.35)}
.bdng{background:rgba(239,68,68,.2);color:#EF4444;border-color:rgba(239,68,68,.4)}
.bdng:hover:not(:disabled){background:rgba(239,68,68,.35)}
.bgst{background:transparent;color:var(--muted);border-color:var(--border)}
.bgst:hover{color:var(--text)}
.btn:disabled,.btn.dis{opacity:.5;cursor:not-allowed;pointer-events:none}
footer{display:flex;justify-content:space-between;align-items:center;padding-top:1rem;border-top:1px solid var(--border);font-size:.78rem;color:var(--dim);flex-wrap:wrap;gap:1rem}
.fst{display:flex;gap:.4rem}
.fl{color:var(--dim)}.fv{color:var(--muted);font-weight:500}
.msi{font-size:.8rem;color:var(--muted)}
</style>
</head>
<body>
<div class="wrap">
<header>
  <div class="brand">
    <div class="logo">&#128752;</div>
    <div><h1>LIVE GPS TRACKER</h1><p class="sub">ESP8266 + u-blox NEO-6M Hardware Telemetry</p></div>
  </div>
  <div class="sbar">
    <div class="badge"><span class="dot dy" id="d-gps"></span><span class="bl">GPS:</span><span class="bv" id="v-gps">SEARCHING</span></div>
    <div class="badge"><span class="dot dg" id="d-esp"></span><span class="bl">ESP8266:</span><span class="bv" id="v-esp">ONLINE</span></div>
    <div class="badge"><span class="dot db" id="d-wifi"></span><span class="bl">Wi-Fi:</span><span class="bv" id="v-wifi">CONNECTING</span></div>
    <div class="badge"><span class="dot dp"></span><span class="bl">UPDATE:</span><span class="bv" id="v-upd">0s ago</span></div>
  </div>
</header>
<div class="banner bwarn" id="banner">
  <div class="btxt" id="btxt">&#128752; Waiting for GPS satellite lock... Place antenna facing open sky.</div>
  <button class="btn bgst" id="btn-demo">Demo: OFF</button>
</div>
<main class="grid">
  <section>
    <div class="sh">
      <h2>Live Telemetry</h2>
      <div style="display:flex;align-items:center;gap:.5rem">
        <span style="font-size:.8rem;color:var(--dim)">Signal:</span>
        <span class="qpill pn" id="qpill">Searching...</span>
      </div>
    </div>
    <div class="cgrid">
      <div class="tcard"><div class="ch"><span class="ct">LATITUDE</span><span class="ci">&#127760;</span></div><div class="cv mono" id="c-lat">--</div><div class="cf">Degrees (WGS84)</div></div>
      <div class="tcard"><div class="ch"><span class="ct">LONGITUDE</span><span class="ci">&#128506;</span></div><div class="cv mono" id="c-lng">--</div><div class="cf">Degrees (WGS84)</div></div>
      <div class="tcard acc"><div class="ch"><span class="ct">SPEED</span><span class="ci">&#9889;</span></div><div class="cv mono" id="c-spd">--</div><div class="cf">Kilometers / Hour</div></div>
      <div class="tcard"><div class="ch"><span class="ct">ALTITUDE</span><span class="ci">&#127956;</span></div><div class="cv mono" id="c-alt">--</div><div class="cf">Meters above MSL</div></div>
      <div class="tcard"><div class="ch"><span class="ct">SATELLITES</span><span class="ci">&#128752;</span></div><div class="cv mono" id="c-sat">0</div><div class="cf">Tracked</div></div>
      <div class="tcard"><div class="ch"><span class="ct">HDOP</span><span class="ci">&#127919;</span></div><div class="cv mono" id="c-hdop">--</div><div class="cf">Horiz. Precision</div></div>
      <div class="tcard"><div class="ch"><span class="ct">COURSE</span><span class="ci">&#129517;</span></div><div class="cv mono" id="c-crs">--</div><div class="cf">True Heading deg</div></div>
      <div class="tcard"><div class="ch"><span class="ct">FIX TYPE</span><span class="ci">&#128274;</span></div><div class="cv" id="c-fix">NO FIX</div><div class="cf">NMEA Solution</div></div>
      <div class="tcard"><div class="ch"><span class="ct">GPS UTC TIME</span><span class="ci">&#128336;</span></div><div class="cv mono" id="c-time">--:--:--</div><div class="cf" id="c-date">YYYY-MM-DD</div></div>
    </div>
  </section>
  <section class="msec">
    <div class="sh"><h2>Map &amp; Range Monitor</h2><span class="msi" id="msi">Waiting for GPS...</span></div>
    <div class="gcard">
      <div class="gchf">
        <div class="gchl"><span>&#128737;</span><h3 class="gt">Geofence / Range Alert</h3></div>
        <div class="gtw">
          <label class="sw"><input type="checkbox" id="chk-geo" checked><span class="sl"></span></label>
          <span class="slab" id="geo-lbl">ACTIVE</span>
        </div>
      </div>
      <div class="ginp">
        <div class="ifg">
          <label for="geo-r">Safe Range Radius:</label>
          <div class="iwu">
            <input type="number" id="geo-r" value="15" min="5" max="5000" step="1">
            <span class="utag">Meters (15m radius = 30m diameter)</span>
          </div>
        </div>
        <div class="gact">
          <button class="btn bsec" id="btn-setctr" disabled>&#128205; Set Center at GPS</button>
          <button class="btn bgst" id="btn-resetgeo">&#128260; Reset</button>
        </div>
      </div>
      <div class="abox safe" id="abox">
        <div class="ai" id="aico">&#9989;</div>
        <div class="ac">
          <span class="at" id="atit">SAFE ZONE: WITHIN RANGE</span>
          <span class="ad" id="adsc">Distance: 0.0 m | Limit: 15.0 m</span>
        </div>
        <div class="auw"><label><input type="checkbox" id="chk-aud" checked> &#128266; Audio</label></div>
      </div>
    </div>
    <div class="mwrap"><div id="map"></div></div>
    <div class="cbar">
      <div class="bgrp">
        <button class="btn bpri" id="btn-ctr" disabled>&#9711; Center</button>
        <a href="#" target="_blank" class="btn bsec dis" id="btn-gmap">&#8599; Google Maps</a>
      </div>
      <div class="bgrp">
        <button class="btn bsuc" id="btn-trk">&#9654; Track</button>
        <button class="btn bwrn" id="btn-stp" disabled>&#9646;&#9646; Stop</button>
        <button class="btn bdng" id="btn-clr">&#128465; Clear</button>
      </div>
    </div>
  </section>
</main>
<footer>
  <div class="fst"><span class="fl">IP:</span><span class="fv mono" id="f-ip">--</span></div>
  <div class="fst"><span class="fl">Packets:</span><span class="fv mono" id="f-pkt">0</span></div>
  <div class="fst"><span class="fl">UART:</span><span class="fv mono" id="f-uart">CHECKING...</span></div>
  <div class="fst"><span class="fl">Track Points:</span><span class="fv mono" id="f-pts">0/500</span></div>
</footer>
</div>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js" crossorigin=""></script>
<script>
(function(){
'use strict';
var ws=null,wsTimer=null,pktCount=0,lastPkt=Date.now();
var map=null,marker=null,accCircle=null,poly=null,gfCircle=null;
var trackPts=[],tracking=false,userPanned=false;
var gfEnabled=true,gfRadius=15,gfCenter=null;
var audioCtx=null,lastBeep=0;
var demo=false,demoTimer=null,dLat=11.0168,dLng=76.9558;
function $(i){return document.getElementById(i);}
function initMap(){
  if(typeof L==='undefined') return;
  map=L.map('map',{zoomControl:true,attributionControl:false}).setView([11.0168,76.9558],16);
  var dark=L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png',{maxZoom:19,subdomains:'abcd'});
  var osm=L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19});
  dark.addTo(map);
  dark.on('tileerror',function(){map.removeLayer(dark);osm.addTo(map);});
  var icon=L.divIcon({className:'',html:'<div style="width:20px;height:20px;background:#06B6D4;border:3px solid #fff;border-radius:50%;box-shadow:0 0 15px #06B6D4"></div>',iconSize:[20,20],iconAnchor:[10,10]});
  marker=L.marker([0,0],{icon:icon});
  accCircle=L.circle([0,0],{radius:10,color:'#06B6D4',fillColor:'#06B6D4',fillOpacity:0.1,weight:1});
  poly=L.polyline([],{color:'#22D3EE',weight:4,opacity:0.85,lineCap:'round'}).addTo(map);
  gfCircle=L.circle([0,0],{radius:15,color:'#10B981',fillColor:'#10B981',fillOpacity:0.15,weight:2,dashArray:'6,6'});
  map.on('dragstart zoomstart',function(){userPanned=true;});
}
function connectWS(){
  if(demo) return;
  var host=window.location.host||'192.168.4.1';
  var url=(window.location.protocol==='https:'?'wss://':'ws://')+host+'/ws';
  try{
    ws=new WebSocket(url);
    ws.onopen=function(){setDot('d-esp','dg','v-esp','ONLINE');};
    ws.onmessage=function(e){try{processData(JSON.parse(e.data));}catch(x){}};
    ws.onerror=function(){setDot('d-esp','dr','v-esp','ERROR');};
    ws.onclose=function(){setDot('d-esp','dy','v-esp','RECONNECTING');clearTimeout(wsTimer);wsTimer=setTimeout(connectWS,3000);};
  }catch(e){setTimeout(connectWS,3000);}
}
function processData(d){
  lastPkt=Date.now();pktCount++;
  $('f-pkt').textContent=pktCount;
  if(d.ip)$('f-ip').textContent=d.ip;
  if(d.uartActive!==undefined){
    var uel=$('f-uart');
    uel.textContent=d.uartActive?'STREAMING ('+d.charsProcessed+' bytes)':'NO DATA RECEIVED';
    uel.style.color=d.uartActive?'#10B981':'#EF4444';
  }
  updateBanner(d);
  if(d.valid){
    $('c-lat').textContent=d.latitude.toFixed(6)+'deg';
    $('c-lng').textContent=d.longitude.toFixed(6)+'deg';
    $('c-spd').textContent=d.speedKmh.toFixed(1)+' km/h';
    $('c-alt').textContent=d.altitude.toFixed(1)+' m';
    $('c-sat').textContent=d.satellites;
    $('c-hdop').textContent=d.hdop?d.hdop.toFixed(1):'--';
    $('c-crs').textContent=(d.course?d.course.toFixed(1):'0.0')+'deg';
    $('c-fix').textContent=d.fixType||'FIXED';
    $('c-time').textContent=d.gpsTime||'--:--:--';
    $('c-date').textContent=d.gpsDate||'YYYY-MM-DD';
    $('btn-ctr').removeAttribute('disabled');
    $('btn-setctr').removeAttribute('disabled');
    var gm=$('btn-gmap');
    gm.classList.remove('dis');
    gm.href='https://www.google.com/maps?q='+d.latitude+','+d.longitude;
    updateMap(d.latitude,d.longitude,d.hdop);
    evalGeo(d.latitude,d.longitude);
    if(tracking)addPt(d.latitude,d.longitude);
  }else{
    ['c-lat','c-lng','c-spd','c-alt','c-crs'].forEach(function(i){$(i).textContent='--';});
    $('c-sat').textContent=d.satellites||0;
    $('c-hdop').textContent=(d.hdop&&d.hdop<90)?d.hdop.toFixed(1):'--';
    $('c-fix').textContent=d.fixType||'NO FIX';
    $('c-time').textContent=d.gpsTime||'--:--:--';
    $('c-date').textContent=d.gpsDate||'YYYY-MM-DD';
    $('btn-ctr').setAttribute('disabled','');
    $('btn-setctr').setAttribute('disabled','');
    $('btn-gmap').classList.add('dis');
    if(marker&&map&&map.hasLayer(marker))map.removeLayer(marker);
    if(accCircle&&map&&map.hasLayer(accCircle))map.removeLayer(accCircle);
  }
  updateQPill(d.quality||'No Fix');
}
function updateBanner(d){
  var bn=$('banner'),bt=$('btxt');
  if(d.valid){
    bn.className='banner bok';bt.textContent='GPS Fixed - Live telemetry broadcasting';
    setDot('d-gps','dg','v-gps','FIXED');
    $('msi').textContent='Fixed: '+d.latitude.toFixed(4)+', '+d.longitude.toFixed(4);
  }else if(!d.uartActive){
    bn.className='banner berr';bt.textContent='No data from NEO-6M. Check wiring: NEO-6M TX -> D1 (GPIO5)';
    setDot('d-gps','dr','v-gps','NO DATA');
    $('msi').textContent='Hardware error: no NMEA data';
  }else{
    bn.className='banner bwarn';bt.textContent='Searching for satellites... Point antenna to open sky.';
    setDot('d-gps','dy','v-gps','SEARCHING');
    $('msi').textContent='Acquiring satellites...';
  }
}
function setDot(di,cls,vi,txt){
  var d=$(di);if(d)d.className='dot '+cls;
  var v=$(vi);if(v)v.textContent=txt;
}
function updateQPill(q){
  var p=$('qpill');p.textContent=q;
  var m={excellent:'pe',good:'pg',fair:'pf',poor:'pp'};
  p.className='qpill '+(m[q.toLowerCase()]||'pn');
}
function updateMap(lat,lng,hdop){
  if(!map)return;
  var pos=[lat,lng];
  if(!map.hasLayer(marker))marker.addTo(map);
  marker.setLatLng(pos);
  if(!map.hasLayer(accCircle))accCircle.addTo(map);
  accCircle.setLatLng(pos);
  accCircle.setRadius(Math.max((hdop||2)*5,5));
  if(!userPanned)map.panTo(pos);
}
function evalGeo(lat,lng){
  if(!map||!gfEnabled){if(gfCircle&&map&&map.hasLayer(gfCircle))map.removeLayer(gfCircle);return;}
  var cur=L.latLng(lat,lng);
  if(!gfCenter)gfCenter=cur;
  var dist=cur.distanceTo(gfCenter);
  var rad=parseFloat($('geo-r').value)||15;
  gfRadius=rad;
  if(!map.hasLayer(gfCircle))gfCircle.addTo(map);
  gfCircle.setLatLng(gfCenter);gfCircle.setRadius(gfRadius);
  var abox=$('abox');
  if(dist>gfRadius){
    gfCircle.setStyle({color:'#EF4444',fillColor:'#EF4444',fillOpacity:0.35,weight:3,dashArray:'4,4'});
    abox.className='abox breach';
    $('aico').textContent='!!';
    $('atit').textContent='OUT OF RANGE - BREACH ALERT!';
    $('adsc').textContent='Distance: '+dist.toFixed(1)+'m | Limit: '+rad.toFixed(1)+'m (+'+((dist-rad).toFixed(1))+'m over)';
    beep();
  }else{
    gfCircle.setStyle({color:'#10B981',fillColor:'#10B981',fillOpacity:0.15,weight:2,dashArray:'6,6'});
    abox.className='abox safe';
    $('aico').textContent='OK';
    $('atit').textContent='SAFE ZONE: WITHIN RANGE';
    $('adsc').textContent='Distance: '+dist.toFixed(1)+'m | Limit: '+rad.toFixed(1)+'m ('+((rad-dist).toFixed(1))+'m buffer)';
  }
}
function beep(){
  if(!$('chk-aud').checked)return;
  var now=Date.now();if(now-lastBeep<1200)return;lastBeep=now;
  try{
    if(!audioCtx)audioCtx=new(window.AudioContext||window.webkitAudioContext)();
    if(audioCtx.state==='suspended')audioCtx.resume();
    var o=audioCtx.createOscillator(),g=audioCtx.createGain();
    o.type='sawtooth';o.frequency.setValueAtTime(880,audioCtx.currentTime);
    o.frequency.exponentialRampToValueAtTime(440,audioCtx.currentTime+0.3);
    g.gain.setValueAtTime(0.18,audioCtx.currentTime);
    g.gain.exponentialRampToValueAtTime(0.01,audioCtx.currentTime+0.3);
    o.connect(g);g.connect(audioCtx.destination);
    o.start();o.stop(audioCtx.currentTime+0.3);
  }catch(e){}
}
function addPt(lat,lng){
  trackPts.push([lat,lng]);
  if(trackPts.length>500)trackPts.shift();
  if(poly)poly.setLatLngs(trackPts);
  $('f-pts').textContent=trackPts.length+'/500';
}
function initControls(){
  $('chk-geo').onchange=function(){
    gfEnabled=$('chk-geo').checked;
    $('geo-lbl').textContent=gfEnabled?'ACTIVE':'DISABLED';
    $('geo-lbl').style.color=gfEnabled?'#06B6D4':'#64748B';
    if(!gfEnabled&&gfCircle&&map&&map.hasLayer(gfCircle))map.removeLayer(gfCircle);
  };
  $('geo-r').oninput=function(){gfRadius=parseFloat($('geo-r').value)||15;if(gfCircle)gfCircle.setRadius(gfRadius);};
  $('btn-setctr').onclick=function(){if(marker){var p=marker.getLatLng();if(p.lat!==0&&p.lng!==0){gfCenter=p;if(gfCircle)gfCircle.setLatLng(p);}}};
  $('btn-resetgeo').onclick=function(){gfCenter=null;if(gfCircle&&map&&map.hasLayer(gfCircle))map.removeLayer(gfCircle);};
  $('btn-ctr').onclick=function(){if(marker&&map){userPanned=false;var p=marker.getLatLng();if(p.lat!==0)map.setView(p,18);}};
  $('btn-trk').onclick=function(){tracking=true;$('btn-trk').setAttribute('disabled','');$('btn-stp').removeAttribute('disabled');};
  $('btn-stp').onclick=function(){tracking=false;$('btn-stp').setAttribute('disabled','');$('btn-trk').removeAttribute('disabled');};
  $('btn-clr').onclick=function(){trackPts=[];if(poly)poly.setLatLngs([]);$('f-pts').textContent='0/500';};
  $('btn-demo').onclick=toggleDemo;
}
function toggleDemo(){
  demo=!demo;
  $('btn-demo').textContent=demo?'Demo: ON':'Demo: OFF';
  $('btn-demo').style.color=demo?'#10B981':'';
  if(demo){
    if(ws)ws.close();
    setDot('d-esp','db','v-esp','DEMO');
    var step=0;
    demoTimer=setInterval(function(){
      step++;
      var dr=(step>8)?0.0003:0.00005;
      dLat+=(Math.random()-0.3)*dr;
      dLng+=(Math.random()-0.3)*dr;
      processData({valid:true,state:'GPS FIXED',quality:'Excellent',latitude:dLat,longitude:dLng,
        altitude:412+Math.random()*2,speedKmh:4.5+Math.random()*2,course:127+Math.random()*4,
        satellites:9,hdop:1.1,fixType:'3D',gpsTime:'12:00:00 UTC',
        gpsDate:new Date().toISOString().split('T')[0],charsProcessed:14520,failedChecksum:0,
        uartActive:true,ip:'192.168.4.1 (Demo)'});
    },1000);
  }else{
    if(demoTimer)clearInterval(demoTimer);
    connectWS();
  }
}
setInterval(function(){$('v-upd').textContent=((Date.now()-lastPkt)/1000).toFixed(1)+'s ago';},500);
document.addEventListener('DOMContentLoaded',function(){
  initMap();initControls();connectWS();
});
})();
</script>
</body>
</html>
)===";

// ============================================================
//  GPS STATE
// ============================================================
enum GpsState { STATE_NO_DATA, STATE_SEARCHING, STATE_FIXED, STATE_FIX_LOST };
GpsState gpsState = STATE_NO_DATA;

void updateGpsState() {
    bool locValid = gps.location.isValid()
                 && gps.location.age() < 3000
                 && fabs(gps.location.lat()) > 1e-6
                 && fabs(gps.location.lng()) > 1e-6;

    if (locValid) {
        gpsState = STATE_FIXED;
        hadValidFix = true;
    } else {
        if (hadValidFix)        gpsState = STATE_FIX_LOST;
        else if (totalGpsChars) gpsState = STATE_SEARCHING;
        else                    gpsState = STATE_NO_DATA;
    }
}

const char* stateStr() {
    switch (gpsState) {
        case STATE_NO_DATA:   return "NO GPS DATA";
        case STATE_SEARCHING: return "SEARCHING FOR SATELLITES";
        case STATE_FIXED:     return "GPS FIXED";
        case STATE_FIX_LOST:  return "GPS FIX LOST";
        default:              return "UNKNOWN";
    }
}

String qualStr() {
    if (gpsState != STATE_FIXED) return "No Fix";
    uint32_t s = gps.satellites.isValid() ? gps.satellites.value() : 0;
    float    h = gps.hdop.isValid()       ? gps.hdop.hdop()        : 99.9f;
    if (s >= 8 && h <= 1.5f) return "Excellent";
    if (s >= 6 && h <= 2.5f) return "Good";
    if (s >= 4 && h <= 4.0f) return "Fair";
    if (s > 0)               return "Poor";
    return "No Fix";
}

String buildJson(const String& ip) {
    bool valid = (gpsState == STATE_FIXED);
    StaticJsonDocument<512> doc;
    doc["valid"]   = valid;
    doc["state"]   = stateStr();
    doc["quality"] = qualStr();
    doc["ip"]      = ip;

    if (valid) {
        doc["latitude"]  = gps.location.lat();
        doc["longitude"] = gps.location.lng();
        doc["altitude"]  = gps.altitude.isValid()   ? gps.altitude.meters()     : 0.0;
        doc["speedKmh"]  = gps.speed.isValid()      ? gps.speed.knots()*1.852f  : 0.0;
        doc["course"]    = gps.course.isValid()      ? gps.course.deg()          : 0.0;
        doc["satellites"]= gps.satellites.isValid()  ? (int)gps.satellites.value(): 0;
        doc["hdop"]      = gps.hdop.isValid()        ? gps.hdop.hdop()           : 99.9;
        uint32_t sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
        float    hdop = gps.hdop.isValid()       ? gps.hdop.hdop()        : 99.9f;
        if      (sats >= 4 && hdop < 5.0f && gps.altitude.isValid()) doc["fixType"] = "3D";
        else if (sats >= 3)                                            doc["fixType"] = "2D";
        else                                                           doc["fixType"] = "FIXED";
    } else {
        doc["latitude"]  = nullptr;
        doc["longitude"] = nullptr;
        doc["altitude"]  = nullptr;
        doc["speedKmh"]  = nullptr;
        doc["course"]    = nullptr;
        doc["satellites"]= gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
        doc["hdop"]      = gps.hdop.isValid()       ? gps.hdop.hdop()              : 99.9;
        doc["fixType"]   = (gpsState == STATE_SEARCHING) ? "SEARCHING" :
                           (gpsState == STATE_FIX_LOST)  ? "FIX LOST" : "NO FIX";
    }

    if (gps.time.isValid()) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d UTC",
                 gps.time.hour(), gps.time.minute(), gps.time.second());
        doc["gpsTime"] = buf;
    } else { doc["gpsTime"] = "--:--:--"; }

    if (gps.date.isValid()) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                 gps.date.year(), gps.date.month(), gps.date.day());
        doc["gpsDate"] = buf;
    } else { doc["gpsDate"] = "----/--/--"; }

    doc["charsProcessed"] = gps.charsProcessed();
    doc["failedChecksum"] = gps.failedChecksum();
    doc["uartActive"]     = (totalGpsChars > 0);

    String out;
    serializeJson(doc, out);
    return out;
}

// ============================================================
//  WEBSOCKET EVENTS
// ============================================================
void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
               AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WS Client #%u connected\n", client->id());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WS Client #%u disconnected\n", client->id());
    }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=================================");
    Serial.println("  ESP8266 NEO-6M GPS TRACKER");
    Serial.println("=================================");

    // Start GPS SoftwareSerial
    gpsSerial.begin(GPS_BAUD);
    Serial.printf("GPS SoftwareSerial: RX=D1(GPIO5), TX=D2(GPIO4), Baud=%d\n", GPS_BAUD);

    // Connect to Wi-Fi (Station mode)
    Serial.printf("Connecting to Wi-Fi: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
        delay(300);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        isApMode = false;
        Serial.print("Wi-Fi STA connected! IP: ");
        Serial.println(WiFi.localIP());
    } else {
        // Fallback: start Access Point
        isApMode = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        Serial.print("STA failed. Started AP: ");
        Serial.print(AP_SSID);
        Serial.print(" | IP: ");
        Serial.println(WiFi.softAPIP());
    }

    // Setup WebSocket
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // HTTP Routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", HTML_PAGE);
    });

    server.on("/api/gps", HTTP_GET, [](AsyncWebServerRequest* req) {
        String ip = isApMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
        req->send(200, "application/json", buildJson(ip));
    });

    server.begin();

    Serial.println("=================================");
    Serial.print("OPEN DASHBOARD AT: http://");
    Serial.println(isApMode ? WiFi.softAPIP() : WiFi.localIP());
    Serial.println("=================================\n");

    lastGpsByte = millis();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
    // Read GPS serial data
    while (gpsSerial.available() > 0) {
        char c = (char)gpsSerial.read();
        gps.encode(c);
        totalGpsChars++;
        lastGpsByte = millis();
    }
    updateGpsState();

    uint32_t now = millis();

    // Broadcast GPS telemetry via WebSocket every 1 second
    if (now - lastBroadcast >= BROADCAST_INTERVAL_MS) {
        lastBroadcast = now;
        ws.cleanupClients();
        if (ws.count() > 0) {
            String ip = isApMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
            ws.textAll(buildJson(ip));
        }
    }

    // Serial debug output every 3 seconds
    if (now - lastDebug >= DEBUG_INTERVAL_MS) {
        lastDebug = now;
        Serial.printf("[DEBUG] Uptime:%lus | Mode:%s | WS Clients:%u | GPS:%s | Chars:%lu\n",
                      now / 1000,
                      isApMode ? "AP" : "STA",
                      ws.count(),
                      stateStr(),
                      totalGpsChars);
        if (gpsState == STATE_FIXED) {
            Serial.printf("  Lat:%.6f  Lng:%.6f  Sats:%u  Speed:%.1f km/h\n",
                          gps.location.lat(), gps.location.lng(),
                          gps.satellites.isValid() ? gps.satellites.value() : 0,
                          gps.speed.isValid() ? gps.speed.knots()*1.852f : 0.0f);
        }
    }
}
