/**
 * CattleGuard Pro — Live Tracker Engine v7.0
 * Accurate Wi-Fi RSSI Distance + Live Phone GPS Anchor Matching
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
  let hasPhoneGps = false;

  let espIp         = localStorage.getItem('esp_ip') || '';
  let pollTimer     = null;
  let isOnline      = false;
  let lastPacketAt  = 0;
  let geofenceRadius = 15;
  let lastSyncTime   = 0;

  let smoothedDist = 0;
  const ALPHA = 0.15; // Stable EMA smoothing

  let audioCtx = null;
  let lastBeep = 0;
  let soundOn  = true;

  function $(id) { return document.getElementById(id); }

  /* ── 1. MAP INITIALIZATION ── */
  function initMap() {
    if (typeof L === 'undefined') return;

    map = L.map('map', { zoomControl: false, attributionControl: false, maxZoom: 19 })
           .setView([farmerLat, farmerLng], 19);

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
        hasPhoneGps = true;
        updateFarmerAnchor();

        // Immediately align cow to phone anchor if not already locked to real GPS
        if (!isOnline) {
          cowLat = farmerLat; cowLng = farmerLng;
          if (cowMarker) cowMarker.setLatLng([cowLat, cowLng]);
        }

        if (map) map.setView([farmerLat, farmerLng], 19);
        syncAnchorToEsp();
      },
      err => console.warn('Phone GPS:', err.message),
      { enableHighAccuracy: true, timeout: 10000, maximumAge: 0 }
    );

    navigator.geolocation.watchPosition(
      pos => {
        farmerLat = pos.coords.latitude;
        farmerLng = pos.coords.longitude;
        hasPhoneGps = true;
        updateFarmerAnchor();
        syncAnchorToEsp();
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

  function syncAnchorToEsp() {
    if (!espIp || Date.now() - lastSyncTime < 12000) return;
    lastSyncTime = Date.now();
    const url = 'http://' + espIp.trim().replace(/^https?:\/\//, '') + '/api/setcenter?lat=' + farmerLat.toFixed(6) + '&lng=' + farmerLng.toFixed(6);
    fetch(url, { mode: 'cors' }).catch(() => {});
  }

  /* ── 3. PRECISE MOBILE-HOTSPOT CALIBRATED RSSI DISTANCE ── */
  // Tuned for phone hotspots:
  // - 0m - 0.5m (-46 dBm) -> 0.4m
  // - 1m (-52 dBm)        -> 1.0m
  // - 2m (-58 dBm)        -> 2.0m (Fixed 6m bug!)
  // - 5m (-68 dBm)        -> 5.0m
  // - 15m (-82 dBm)       -> 15.0m (Boundary threshold)
  function rssiToDistance(rssi) {
    if (!rssi || rssi === 0 || rssi < -98) return 35.0;
    if (rssi >= -46) return 0.4;
    if (rssi >= -53) return 0.4 + (-46 - rssi) * (0.7 / 7.0);
    if (rssi >= -60) return 1.1 + (-53 - rssi) * (1.3 / 7.0);
    if (rssi >= -68) return 2.4 + (-60 - rssi) * (2.8 / 8.0);
    if (rssi >= -76) return 5.2 + (-68 - rssi) * (5.3 / 8.0);
    if (rssi >= -84) return 10.5 + (-76 - rssi) * (5.5 / 8.0);
    return Math.min(65.0, 16.0 + (-84 - rssi) * 1.3);
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

    // Use server-smoothed distance, or calculate client-side
    const rawDist = parseFloat(data.dist) || rssiToDistance(parseInt(data.rssi, 10) || -60);
    const dist    = smoothDist(rawDist);
    const speed   = parseFloat(data.spd) || 0;
    const hasGps  = !!data.gpsFix && data.lat && parseFloat(data.lat) !== 0;

    // ── Position resolution ──
    if (hasGps) {
      // Real GPS satellite fix from NEO-6M
      cowLat = parseFloat(data.lat);
      cowLng = parseFloat(data.lng);
    } else {
      // Wi-Fi positioning: place cow relative to phone pin (farmerLat, farmerLng)
      // Exactly matches the pasture distance meter reading:
      if (dist <= 0.8) {
        // When device and phone are together (0 - 0.8m), align cow directly with the pin
        cowLat = farmerLat;
        cowLng = farmerLng;
      } else {
        // 1 meter ≈ 0.00000899 degrees lat/lng
        // Offset cow by the exact distance in meters from the phone pin
        const angle = (data.crs !== undefined && data.crs !== null && !isNaN(parseFloat(data.crs)))
                      ? (parseFloat(data.crs) * Math.PI / 180.0)
                      : 0.785;
        cowLat = farmerLat + (dist * 0.00000899) * Math.cos(angle);
        cowLng = farmerLng + (dist * 0.00000899) * Math.sin(angle);
      }
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

    // Update live GPS coordinates and stats in real-time
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

  /* ── 7. HTTP POLLING ENGINE ── */
  function poll() {
    if (!espIp) return;
    const url = 'http://' + espIp.trim().replace(/^https?:\/\//, '') + '/api/gps';

    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 2500);

    fetch(url, { mode: 'cors', cache: 'no-store', signal: controller.signal })
      .then(r => {
        clearTimeout(timeout);
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.json();
      })
      .then(data => {
        onData(data);
        schedulePoll(1000);
      })
      .catch(() => {
        clearTimeout(timeout);
        if (isOnline && Date.now() - lastPacketAt > 8000) {
          isOnline = false;
          setOffline();
        }
        schedulePoll(2000);
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
    poll();
  }

  /* ── 8. IP CONNECT LOGIC ── */
  function connectToIp(ip) {
    if (!ip || !ip.trim()) return;
    espIp = ip.trim().replace(/^https?:\/\//, '');
    localStorage.setItem('esp_ip', espIp);
    startPolling();
    syncAnchorToEsp();
  }

  /* ── 9. LISTENERS ── */
  document.addEventListener('DOMContentLoaded', () => {
    initMap();

    const slider = $('radius-slider');
    if (slider) {
      slider.addEventListener('input', e => {
        const r = parseInt(e.target.value, 10);
        $('radius-display').textContent = r + ' m';
        geofenceRadius = r;
        if (pastureCircle) pastureCircle.setRadius(r);
      });
    }

    const soundBtn = $('btn-sound-toggle');
    if (soundBtn) {
      soundBtn.addEventListener('click', () => {
        soundOn = !soundOn;
        $('sound-icon').textContent = soundOn ? '🔔' : '🔕';
      });
    }

    const syncBtn = $('btn-sync-phone');
    if (syncBtn) syncBtn.addEventListener('click', () => {
      if (map) map.setView([farmerLat, farmerLng], 19);
    });

    const lockBtn = $('btn-lock-cow');
    if (lockBtn) lockBtn.addEventListener('click', () => {
      if (map) map.setView([cowLat, cowLng], 19);
    });

    const gmapsBtn = $('btn-gmaps');
    if (gmapsBtn) gmapsBtn.addEventListener('click', () => {
      window.open('https://www.google.com/maps?q=' + cowLat + ',' + cowLng, '_blank');
    });

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

    setInterval(() => {
      if (isOnline && Date.now() - lastPacketAt > 8000) {
        isOnline = false;
        setOffline();
      }
    }, 1000);

    if (espIp) {
      $('input-esp-ip').value = espIp;
      startPolling();
    } else {
      setOffline();
    }
  });

})();
