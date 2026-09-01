/**
 * CattleGuard Pro — Wi-Fi HTTP Edition v6.0
 * Connects to ESP32/ESP8266 via HTTP polling (/api/gps)
 * Works from GitHub Pages (CORS-enabled)
 */

(function () {
  'use strict';

  /* ── STATE ── */
  let map = null;
  let farmerMarker = null;
  let cowMarker    = null;
  let pastureCircle = null;
  let breadcrumbTrail = null;
  let trailHistory = [];

  let farmerLat = 11.016842, farmerLng = 76.955819;
  let cowLat    = 11.016842, cowLng    = 76.955819;

  let espIp         = localStorage.getItem('esp_ip') || '';
  let pollTimer     = null;
  let isOnline      = false;
  let lastPacketAt  = 0;
  let geofenceRadius = 15;

  let smoothedDist = 0;
  const ALPHA = 0.15; // Match firmware (lower = more stable)

  let audioCtx = null;
  let lastBeep = 0;
  let soundOn  = true;

  function $(id) { return document.getElementById(id); }

  /* ── 1. MAP ── */
  function initMap() {
    if (typeof L === 'undefined') return;

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
      radius: geofenceRadius,
      color: '#10b981', fillColor: '#10b981',
      fillOpacity: 0.18, weight: 2, dashArray: '6,6'
    }).addTo(map);

    breadcrumbTrail = L.polyline([], { color:'#06b6d4', weight:3, opacity:0.85 }).addTo(map);

    setTimeout(() => { if (map) map.invalidateSize(); }, 400);
    requestPhoneGps();
  }

  /* ── 2. PHONE GPS ANCHOR ── */
  function requestPhoneGps() {
    if (!('geolocation' in navigator)) return;
    navigator.geolocation.getCurrentPosition(
      pos => {
        farmerLat = pos.coords.latitude;
        farmerLng = pos.coords.longitude;
        updateFarmerAnchor();
        if (!isOnline) {
          cowLat = farmerLat; cowLng = farmerLng;
          if (cowMarker) cowMarker.setLatLng([cowLat, cowLng]);
        }
      },
      err => console.warn('Phone GPS:', err.message),
      { enableHighAccuracy: true, timeout: 10000, maximumAge: 0 }
    );
    navigator.geolocation.watchPosition(
      pos => {
        farmerLat = pos.coords.latitude;
        farmerLng = pos.coords.longitude;
        updateFarmerAnchor();
      },
      null,
      { enableHighAccuracy: true, maximumAge: 3000 }
    );
  }

  function updateFarmerAnchor() {
    if (!map) return;
    if (farmerMarker) farmerMarker.setLatLng([farmerLat, farmerLng]);
    if (pastureCircle) pastureCircle.setLatLng([farmerLat, farmerLng]);
  }

  /* ── 3. RSSI DISTANCE (log-distance model, matches firmware exactly) ── */
  function rssiToDistance(rssi) {
    if (!rssi || rssi === 0 || rssi < -98) return 35.0;
    const TX_POWER = -40.0; // RSSI at 1m from phone hotspot
    const N        =   2.2; // Open-field path-loss exponent
    let d = Math.pow(10, (TX_POWER - rssi) / (10 * N));
    if (d < 0.3) d = 0.3;
    if (d > 80)  d = 80;
    return d;
  }

  function smoothDist(rawDist) {
    if (smoothedDist === 0) { smoothedDist = rawDist; return rawDist; }
    smoothedDist = ALPHA * rawDist + (1 - ALPHA) * smoothedDist;
    return smoothedDist;
  }

  /* ── 4. AUDIO ALARM ── */
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
    if (!isOnline) setOnline();

    // Distance from ESP (already smoothed server-side via RSSI) or recalculate
    const rawDist = parseFloat(data.dist) || rssiToDistance(parseInt(data.rssi, 10) || -70);
    const dist    = smoothDist(rawDist);
    const speed   = parseFloat(data.spd) || 0;
    const hasGps  = !!data.gpsFix && data.lat && parseFloat(data.lat) !== 0;

    // Update cow position
    if (hasGps) {
      cowLat = parseFloat(data.lat);
      cowLng = parseFloat(data.lng);
    }
    if (cowMarker) {
      cowMarker.setLatLng([cowLat, cowLng]);
      const el = document.getElementById('marker-cow');
      if (el) el.classList.remove('offline-marker');
    }

    // Update trail
    trailHistory.push([cowLat, cowLng]);
    if (trailHistory.length > 200) trailHistory.shift();
    if (breadcrumbTrail) breadcrumbTrail.setLatLngs(trailHistory);

    // Update stats
    $('val-lat').textContent   = cowLat.toFixed(5) + '°';
    $('val-lng').textContent   = cowLng.toFixed(5) + '°';
    $('val-speed').textContent = speed.toFixed(1) + ' km/h';
    $('val-status').textContent = hasGps ? '3D GPS' : 'Wi-Fi Est.';

    // ── Geofence check ──
    const maxR = parseInt($('radius-slider').value, 10) || 15;
    geofenceRadius = maxR;
    if (pastureCircle) pastureCircle.setRadius(maxR);

    const banner = $('geofence-banner');
    if (dist > maxR) {
      banner.className = 'alert-card breach';
      $('banner-icon').textContent  = '🚨';
      $('banner-title').textContent = 'OUT OF PASTURE!';
      $('banner-desc').textContent  = 'Over by +' + (dist - maxR).toFixed(1) + 'm';
      $('badge-distance').textContent = dist.toFixed(1) + ' m';
      if (pastureCircle) pastureCircle.setStyle({ color:'#f43f5e', fillColor:'#f43f5e', fillOpacity:0.25, weight:3 });
      beep();
    } else {
      banner.className = 'alert-card safe';
      $('banner-icon').textContent  = '🟢';
      $('banner-title').textContent = 'SAFE IN PASTURE';
      $('banner-desc').textContent  = 'Distance: ' + dist.toFixed(1) + 'm · Limit: ' + maxR + 'm';
      $('badge-distance').textContent = dist.toFixed(1) + ' m';
      if (pastureCircle) pastureCircle.setStyle({ color:'#10b981', fillColor:'#10b981', fillOpacity:0.18, weight:2 });
    }
  }

  /* ── 6. ONLINE / OFFLINE UI ── */
  function setOnline() {
    isOnline = true;
    $('device-pill').className = 'device-pill online';
    $('status-text').textContent = 'ONLINE';
    $('lbl-ip-info').textContent = 'Collar: ' + espIp;
    const ipInput = $('ip-input-wrap');
    if (ipInput) ipInput.style.display = 'none';
  }

  function setOffline() {
    isOnline = false;
    $('device-pill').className = 'device-pill offline';
    $('status-text').textContent = 'SEARCHING';
    const el = document.getElementById('marker-cow');
    if (el) el.classList.add('offline-marker');
    $('val-speed').textContent = '-- km/h';
    $('val-status').textContent = '--';
    $('badge-distance').textContent = '-- m';
    const banner = $('geofence-banner');
    banner.className = 'alert-card safe';
    $('banner-icon').textContent  = '🔌';
    $('banner-title').textContent = 'COLLAR OFFLINE';
    $('banner-desc').textContent  = 'Enter ESP IP from Serial Monitor below';
  }

  /* ── 7. HTTP POLLING ENGINE (sequential, no overlap) ── */
  let _pollActive = false;

  function poll() {
    if (!espIp) return;
    const url = 'http://' + espIp.trim().replace(/^https?:\/\//, '') + '/api/gps';

    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 2500); // 2.5s hard timeout

    fetch(url, { mode: 'cors', cache: 'no-store', signal: controller.signal })
      .then(r => {
        clearTimeout(timeout);
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.json();
      })
      .then(data => {
        onData(data);
        schedulePoll(1000); // Next poll 1s after successful response
      })
      .catch(() => {
        clearTimeout(timeout);
        // Declare offline only after 8 seconds without any packet
        if (isOnline && Date.now() - lastPacketAt > 8000) {
          isOnline = false;
          setOffline();
        }
        schedulePoll(2000); // Retry in 2s on error
      });
  }

  function schedulePoll(delay) {
    if (pollTimer) clearTimeout(pollTimer);
    pollTimer = setTimeout(poll, delay);
  }

  function startPolling() {
    if (pollTimer) clearTimeout(pollTimer);
    if (!espIp) return;
    $('lbl-ip-info').textContent = 'Connecting to ' + espIp + '...';
    poll(); // Kick off first request immediately
  }

  /* ── 8. IP CONNECT LOGIC ── */
  function connectToIp(ip) {
    if (!ip || !ip.trim()) return;
    espIp = ip.trim().replace(/^https?:\/\//, '');
    localStorage.setItem('esp_ip', espIp);
    startPolling();
  }

  /* ── 9. LISTENERS ── */
  document.addEventListener('DOMContentLoaded', () => {
    initMap();

    // Radius slider — updates circle in real-time
    const slider = $('radius-slider');
    if (slider) {
      slider.addEventListener('input', e => {
        const r = parseInt(e.target.value, 10);
        $('radius-display').textContent = r + ' m';
        geofenceRadius = r;
        if (pastureCircle) pastureCircle.setRadius(r);
      });
    }

    // Sound toggle
    const soundBtn = $('btn-sound-toggle');
    if (soundBtn) {
      soundBtn.addEventListener('click', () => {
        soundOn = !soundOn;
        $('sound-icon').textContent = soundOn ? '🔔' : '🔕';
      });
    }

    // Map FAB buttons
    const syncBtn = $('btn-sync-phone');
    if (syncBtn) syncBtn.addEventListener('click', () => {
      if (map) map.setView([farmerLat, farmerLng], 18);
    });

    const lockBtn = $('btn-lock-cow');
    if (lockBtn) lockBtn.addEventListener('click', () => {
      if (map) map.setView([cowLat, cowLng], 18);
    });

    const gmapsBtn = $('btn-gmaps');
    if (gmapsBtn) gmapsBtn.addEventListener('click', () => {
      window.open('https://www.google.com/maps?q=' + cowLat + ',' + cowLng, '_blank');
    });

    // IP Connect button
    const connectBtn = $('btn-ip-connect');
    if (connectBtn) {
      connectBtn.addEventListener('click', () => {
        const val = $('input-esp-ip').value.trim();
        if (val) connectToIp(val);
      });
    }

    const ipInput = $('input-esp-ip');
    if (ipInput) {
      ipInput.addEventListener('keydown', e => {
        if (e.key === 'Enter') connectToIp(ipInput.value);
      });
    }

    // Liveness check — 8 seconds grace period
    setInterval(() => {
      if (isOnline && Date.now() - lastPacketAt > 8000) {
        isOnline = false;
        setOffline();
      }
    }, 1000);

    // Auto-start polling if saved IP exists
    if (espIp) {
      $('input-esp-ip').value = espIp;
      startPolling();
    } else {
      setOffline();
    }
  });

})();
