/**
 * CattleGuard Pro - Client v3.1 (Auto-Connect Edition)
 * Features:
 *  - Automatic endpoint polling & background IP discovery
 *  - No manual IP input or connect button required
 *  - Calibrated Wi-Fi RSSI distance engine
 *  - Geofence monitoring & sound alert
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

  let lastPacketAt = 0;
  let isEspOnline  = false;
  let activeHost   = location.hostname || 'cowcollar.local';

  // Candidate hosts to try automatically
  const candidateHosts = [
    location.hostname,
    'cowcollar.local',
    '192.168.4.1',
    '192.168.43.100',
    '192.168.1.100',
    '192.168.137.100'
  ].filter(h => h && h !== 'localhost' && h !== '127.0.0.1');

  let smoothedDist = 0;
  const ALPHA = 0.25;

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

    requestPhoneGps();
    initAutoPoller();
  }

  /* ── 2. PHONE GPS ── */
  function requestPhoneGps() {
    if (!('geolocation' in navigator)) return;

    navigator.geolocation.getCurrentPosition(
      pos => {
        farmerLat = pos.coords.latitude;
        farmerLng = pos.coords.longitude;
        updateFarmerAnchor();
        if (!isEspOnline) {
          cowLat = farmerLat; cowLng = farmerLng;
          if (cowMarker) cowMarker.setLatLng([cowLat, cowLng]);
        }
        showCoords(farmerLat, farmerLng);
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
    farmerMarker.setLatLng([farmerLat, farmerLng]);
    pastureCircle.setLatLng([farmerLat, farmerLng]);
  }

  function showCoords(lat, lng) {
    $('val-lat').textContent = lat.toFixed(5) + '°';
    $('val-lng').textContent = lng.toFixed(5) + '°';
  }

  /* ── 3. RSSI DISTANCE CALIBRATION ── */
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

  /* ── 4. ALARM SOUND ── */
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
      setOnline(data.ip || activeHost);
    }

    const rawDist  = parseFloat(data.dist) || rssiToDistance(parseInt(data.rssi, 10) || -70);
    const dist     = smoothDist(rawDist);
    const speed    = parseFloat(data.spd) || 0;
    const hasGps   = !!data.gpsFix && data.lat && parseFloat(data.lat) !== 0;

    if (hasGps) {
      cowLat = parseFloat(data.lat);
      cowLng = parseFloat(data.lng);
    }

    const cowEl = document.getElementById('marker-cow');
    if (cowEl) cowEl.classList.remove('offline-marker');

    if (cowMarker) cowMarker.setLatLng([cowLat, cowLng]);
    trailHistory.push([cowLat, cowLng]);
    if (trailHistory.length > 200) trailHistory.shift();
    if (breadcrumbTrail) breadcrumbTrail.setLatLngs(trailHistory);

    showCoords(cowLat, cowLng);
    $('val-speed').textContent = speed.toFixed(1) + ' km/h';
    $('val-status').textContent = hasGps ? '3D GPS' : 'Wi-Fi';

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

  /* ── 6. UI STATES ── */
  function setOnline(host) {
    isEspOnline = true;
    const pill = $('device-pill');
    if (pill) {
      pill.className = 'device-pill online';
      $('status-text').textContent = 'ONLINE';
    }
    $('lbl-ip-info').textContent = 'Collar connected (' + (host || 'Active') + ')';
  }

  function setOffline() {
    isEspOnline = false;
    const pill = $('device-pill');
    if (pill) {
      pill.className = 'device-pill offline';
      $('status-text').textContent = 'SEARCHING';
    }
    $('lbl-ip-info').textContent = 'Searching for collar Wi-Fi…';

    const cowEl = document.getElementById('marker-cow');
    if (cowEl) cowEl.classList.add('offline-marker');

    $('val-speed').textContent = '-- km/h';
    $('val-status').textContent = '--';

    const banner = $('geofence-banner');
    banner.className = 'alert-card safe';
    $('banner-icon').textContent = '🟡';
    $('banner-title').textContent = 'SEARCHING FOR COLLAR';
    $('banner-desc').textContent  = 'Ensure collar is powered ON or open http://192.168.4.1';
  }

  /* ── 7. AUTO-POLLING TELEMETRY ENGINE ── */
  function fetchGpsFrom(host) {
    const protocol = location.protocol === 'https:' ? 'http:' : location.protocol;
    const url = (host === location.hostname) ? '/api/gps' : protocol + '//' + host + '/api/gps';

    return fetch(url, { mode: 'cors', cache: 'no-store' })
      .then(res => {
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return res.json();
      })
      .then(data => {
        activeHost = host;
        onData(data);
        return true;
      });
  }

  function initAutoPoller() {
    setInterval(() => {
      // 1. Try primary active host
      fetchGpsFrom(activeHost).catch(() => {
        // 2. If primary fails, scan candidate hosts silently
        let found = false;
        candidateHosts.reduce((promiseChain, candidate) => {
          return promiseChain.then(success => {
            if (success) return true;
            return fetchGpsFrom(candidate).then(() => true).catch(() => false);
          });
        }, Promise.resolve(false)).then(hasConnected => {
          if (!hasConnected && Date.now() - lastPacketAt > 2500) {
            setOffline();
          }
        });
      });
    }, 1000);
  }

  /* ── 8. LISTENERS ── */
  document.addEventListener('DOMContentLoaded', () => {
    initMap();

    const slider = $('radius-slider');
    if (slider) {
      slider.addEventListener('input', e => {
        $('radius-display').textContent = e.target.value + ' m';
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
    if (syncBtn) {
      syncBtn.addEventListener('click', () => {
        if (map) map.setView([farmerLat, farmerLng], 18);
      });
    }

    const lockBtn = $('btn-lock-cow');
    if (lockBtn) {
      lockBtn.addEventListener('click', () => {
        if (map) map.setView([cowLat, cowLng], 18);
      });
    }

    const gmapsBtn = $('btn-gmaps');
    if (gmapsBtn) {
      gmapsBtn.addEventListener('click', () => {
        window.open(`https://www.google.com/maps?q=${cowLat},${cowLng}`, '_blank');
      });
    }
  });

})();
