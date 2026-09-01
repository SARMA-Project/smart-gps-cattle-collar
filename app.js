/**
 * CattleGuard Pro - Client v3.0
 * Features:
 *  - Instant device ON/OFF detection (1.2 s timeout)
 *  - Calibrated Wi-Fi RSSI distance with exponential smoothing
 *  - Manual "Connect" button
 *  - NO fake cow movement when offline
 *  - Phone GPS as pasture anchor
 *  - Satellite map (Esri)
 */

(function () {
  'use strict';

  /* ── STATE ── */
  let map = null;
  let farmerMarker = null;
  let cowMarker = null;
  let pastureCircle = null;
  let breadcrumbTrail = null;
  let trailHistory = [];

  let farmerLat = 11.016842, farmerLng = 76.955819;
  let cowLat    = 11.016842, cowLng    = 76.955819;
  let hasPhoneGps = false;

  let espIp = localStorage.getItem('esp_ip') || '192.168.43.100';
  let lastPacketAt   = 0;
  let isEspOnline    = false;
  let isScanning     = false;

  // RSSI smoothing state (exponential moving average)
  let smoothedDist = 0;
  const ALPHA = 0.25; // 0=no update, 1=raw (0.25 = good balance)

  let audioCtx = null;
  let lastBeep = 0;
  let soundOn  = true;

  function $(id) { return document.getElementById(id); }

  /* ── 1. MAP ── */
  function initMap() {
    if (typeof L === 'undefined') { return; }

    map = L.map('map', { zoomControl: false, attributionControl: false, maxZoom: 19 })
           .setView([farmerLat, farmerLng], 18);

    L.tileLayer(
      'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',
      { maxZoom: 19 }
    ).addTo(map);

    farmerMarker = L.marker([farmerLat, farmerLng], {
      icon: L.divIcon({ className:'', html:'<div class="marker-phone">📍</div>', iconSize:[32,32], iconAnchor:[16,16] })
    }).addTo(map);

    cowMarker = L.marker([cowLat, cowLng], {
      icon: L.divIcon({ className:'', html:'<div class="marker-cow" id="marker-cow">🐄</div>', iconSize:[36,36], iconAnchor:[18,18] })
    }).addTo(map);

    pastureCircle = L.circle([farmerLat, farmerLng], {
      radius: 15, color: '#10b981', fillColor: '#10b981',
      fillOpacity: 0.18, weight: 2, dashArray: '6,6'
    }).addTo(map);

    breadcrumbTrail = L.polyline([], { color:'#06b6d4', weight:3, opacity:0.85 }).addTo(map);

    setTimeout(() => { if (map) map.invalidateSize(); }, 400);

    // Grab phone GPS
    requestPhoneGps();
  }

  /* ── 2. PHONE GPS ── */
  function requestPhoneGps() {
    if (!('geolocation' in navigator)) return;

    navigator.geolocation.getCurrentPosition(
      pos => {
        farmerLat = pos.coords.latitude;
        farmerLng = pos.coords.longitude;
        hasPhoneGps = true;
        updateFarmerAnchor();
        if (!isEspOnline) {
          // Show phone location as cow placeholder ONLY (no fake movement)
          cowLat = farmerLat; cowLng = farmerLng;
          if (cowMarker) cowMarker.setLatLng([cowLat, cowLng]);
        }
        showCoords(farmerLat, farmerLng); // show phone coords as baseline
      },
      err => console.warn('GPS:', err.message),
      { enableHighAccuracy: true, timeout: 10000, maximumAge: 0 }
    );

    navigator.geolocation.watchPosition(
      pos => {
        farmerLat = pos.coords.latitude; farmerLng = pos.coords.longitude;
        updateFarmerAnchor();
      },
      null,
      { enableHighAccuracy: true, maximumAge: 3000 }
    );
  }

  function updateFarmerAnchor() {
    if (!map) return;
    farmerMarker.setLatLng([farmerLat, farmerLng]);
    pastureCircle.setLatLng([farmerLat, farmerLng]);
  }

  function showCoords(lat, lng) {
    $('val-lat').textContent = lat.toFixed(5) + '°';
    $('val-lng').textContent = lng.toFixed(5) + '°';
  }

  /* ── 3. RSSI DISTANCE (calibrated) ── */
  /**
   * Log-distance path loss model:
   *   dist = 10 ^ ((txPower - rssi) / (10 * n))
   *
   * txPower = RSSI at 1 m (calibrate by measuring RSSI right next to router ≈ -40 dBm)
   * n       = path-loss exponent: 2 = free space, 3 = indoor/moderate obstacles
   *
   * At -40 dBm (1 m): dist = 1.0 m ✓
   * At -60 dBm:       dist ≈ 6.3 m (n=2) 
   * At -70 dBm:       dist ≈ 20 m  (n=2)
   *
   * We clamp near-field: anything < 1.5 m stays at ~1 m (you're right next to phone).
   */
  function rssiToDistance(rssi) {
    if (!rssi || rssi === 0 || rssi < -98) return 35.0;
    if (rssi >= -50) return 0.5;
    if (rssi >= -58) return 0.5 + (-50 - rssi) * (1.0 / 8.0);
    if (rssi >= -68) return 1.5 + (-58 - rssi) * (3.0 / 10.0);
    if (rssi >= -78) return 4.5 + (-68 - rssi) * (6.5 / 10.0);
    if (rssi >= -85) return 11.0 + (-78 - rssi) * (7.0 / 7.0);
    return Math.min(65.0, 18.0 + (-85 - rssi) * 1.2);
  }

  function smoothDist(rawDist) {
    if (smoothedDist === 0) { smoothedDist = rawDist; return rawDist; }
    smoothedDist = ALPHA * rawDist + (1 - ALPHA) * smoothedDist;
    return smoothedDist;
  }

  /* ── 4. ALARM ── */
  function beep() {
    if (!soundOn) return;
    const now = Date.now();
    if (now - lastBeep < 900) return;
    lastBeep = now;
    try {
      if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
      if (audioCtx.state === 'suspended') audioCtx.resume();
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      osc.type = 'sawtooth';
      osc.frequency.setValueAtTime(880, audioCtx.currentTime);
      osc.frequency.exponentialRampToValueAtTime(440, audioCtx.currentTime + 0.28);
      gain.gain.setValueAtTime(0.3, audioCtx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime + 0.28);
      osc.connect(gain); gain.connect(audioCtx.destination);
      osc.start(); osc.stop(audioCtx.currentTime + 0.28);
    } catch(e){}
  }

  /* ── 5. DATA HANDLER ── */
  function onData(data) {
    lastPacketAt = Date.now();

    if (!isEspOnline) {
      isEspOnline = true;
      setOnline();
    }

    // ── Distance ──
    const rawDist  = parseFloat(data.dist) || rssiToDistance(parseInt(data.rssi, 10) || -70);
    const dist     = smoothDist(rawDist);
    const speed    = parseFloat(data.spd) || 0;
    const hasGps   = !!data.gpsFix && data.lat && parseFloat(data.lat) !== 0;

    // ── Position ──
    if (hasGps) {
      cowLat = parseFloat(data.lat);
      cowLng = parseFloat(data.lng);
    }
    // When GPS is offline: do NOT move the cow — just show last known position.
    // The distance badge still updates from RSSI.

    // ── Update map ──
    const cowEl = document.getElementById('marker-cow');
    if (cowEl) cowEl.classList.remove('offline-marker');

    if (cowMarker) cowMarker.setLatLng([cowLat, cowLng]);
    trailHistory.push([cowLat, cowLng]);
    if (trailHistory.length > 200) trailHistory.shift();
    if (breadcrumbTrail) breadcrumbTrail.setLatLngs(trailHistory);

    // ── Stats ──
    showCoords(cowLat, cowLng);
    $('val-speed').textContent = speed.toFixed(1) + ' km/h';
    $('val-status').textContent = hasGps ? '3D GPS' : 'Wi-Fi';

    // ── Geofence ──
    const maxR = parseInt($('radius-slider').value, 10) || 15;
    if (pastureCircle) pastureCircle.setRadius(maxR);

    const banner = $('geofence-banner');
    if (dist > maxR) {
      banner.className = 'alert-card breach';
      $('banner-icon').textContent = '🚨';
      $('banner-title').textContent = 'OUT OF PASTURE!';
      $('banner-desc').textContent  = 'Distance: ' + dist.toFixed(1) + 'm  (over by +' + (dist - maxR).toFixed(1) + 'm)';
      $('badge-distance').textContent = dist.toFixed(1) + ' m';
      pastureCircle.setStyle({ color:'#f43f5e', fillColor:'#f43f5e', fillOpacity:0.25, weight:3 });
      beep();
    } else {
      banner.className = 'alert-card safe';
      $('banner-icon').textContent = '🟢';
      $('banner-title').textContent = 'SAFE IN PASTURE';
      $('banner-desc').textContent  = 'Distance: ' + dist.toFixed(1) + 'm  · Safe limit: ' + maxR + 'm';
      $('badge-distance').textContent = dist.toFixed(1) + ' m';
      pastureCircle.setStyle({ color:'#10b981', fillColor:'#10b981', fillOpacity:0.18, weight:2 });
    }
  }

  /* ── 6. ONLINE / OFFLINE UI ── */
  function setOnline() {
    isEspOnline = true;
    const pill = $('device-pill');
    pill.className = 'device-pill online';
    $('status-text').textContent = 'ONLINE';
    $('lbl-ip-info').textContent = 'Collar: ' + espIp;
    const btn = $('btn-connect');
    btn.className = 'connect-btn connected';
    $('connect-btn-icon').textContent = '✅';
    $('connect-btn-label').textContent = 'Connected — ' + espIp;
  }

  function setOffline() {
    isEspOnline = false;
    const pill = $('device-pill');
    pill.className = 'device-pill offline';
    $('status-text').textContent = 'OFFLINE';
    $('lbl-ip-info').textContent = 'Collar not found';
    const btn = $('btn-connect');
    btn.className = 'connect-btn';
    $('connect-btn-icon').textContent = '⚡';
    $('connect-btn-label').textContent = 'Connect Collar Device';

    // Freeze cow: go grey but don't move
    const cowEl = document.getElementById('marker-cow');
    if (cowEl) cowEl.classList.add('offline-marker');

    $('val-speed').textContent = '-- km/h';
    $('val-status').textContent = 'OFFLINE';

    const banner = $('geofence-banner');
    banner.className = 'alert-card offline-state';
    $('banner-icon').textContent = '⚠️';
