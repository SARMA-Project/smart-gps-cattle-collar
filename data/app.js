/**
 * CattleGuard Pro - Bluetooth 5.0 Edition (v5.0)
 * Features:
 *  - 100% Bluetooth operation (No SSIDs, No Wi-Fi hotspots needed)
 *  - Web Bluetooth / Serial API integration for pairing with "CowCollar-BT"
 *  - Calibrated Bluetooth RSSI distance & location estimation
 *  - Real-time Leaflet map tracking & geofence audio alarms
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
  let isBtConnected = false;
  let bluetoothDevice = null;
  let bluetoothPort = null;

  let smoothedDist = 0;
  const ALPHA = 0.30;

  let audioCtx = null;
  let lastBeep = 0;
  let soundOn  = true;

  function $(id) { return document.getElementById(id); }

  /* ── 1. MAP INITIALIZATION ── */
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
  }

  /* ── 2. PHONE GPS ANCHOR ── */
  function requestPhoneGps() {
    if (!('geolocation' in navigator)) return;

    navigator.geolocation.getCurrentPosition(
      pos => {
        farmerLat = pos.coords.latitude;
        farmerLng = pos.coords.longitude;
        updateFarmerAnchor();
        if (!isBtConnected) {
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

  /* ── 3. BLUETOOTH RSSI DISTANCE CALIBRATION ── */
  function rssiToDistance(rssi) {
    if (!rssi || rssi === 0 || rssi < -98) return 30.0;
    if (rssi >= -48) return 0.5;
    if (rssi >= -58) return 0.5 + (-48 - rssi) * (1.0 / 10.0);
    if (rssi >= -68) return 1.5 + (-58 - rssi) * (3.5 / 10.0);
    if (rssi >= -78) return 5.0 + (-68 - rssi) * (6.0 / 10.0);
    if (rssi >= -85) return 11.0 + (-78 - rssi) * (7.0 / 7.0);
    return Math.min(60.0, 18.0 + (-85 - rssi) * 1.2);
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

  /* ── 5. TELEMETRY PACKET PROCESSOR ── */
  function onData(data) {
    lastPacketAt = Date.now();

    if (!isBtConnected) {
      setBtOnline();
    }

    const rawDist = parseFloat(data.dist) || rssiToDistance(parseInt(data.rssi, 10) || -60);
    const dist    = smoothDist(rawDist);
    const speed   = parseFloat(data.spd) || 0;
    const hasGps  = !!data.gpsFix && data.lat && parseFloat(data.lat) !== 0;

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
    $('val-status').textContent = hasGps ? '3D GPS' : 'BT Signal';

    const maxR = parseInt($('radius-slider').value, 10) || 15;
    if (pastureCircle) pastureCircle.setRadius(maxR);

    const banner = $('geofence-banner');
    if (dist > maxR) {
      banner.className = 'alert-card breach';
      $('banner-icon').textContent = '🚨';
      $('banner-title').textContent = 'OUT OF PASTURE!';
      $('banner-desc').textContent  = 'Bluetooth Range Distance: ' + dist.toFixed(1) + 'm (Over by +' + (dist - maxR).toFixed(1) + 'm)';
      $('badge-distance').textContent = dist.toFixed(1) + ' m';
      pastureCircle.setStyle({ color:'#f43f5e', fillColor:'#f43f5e', fillOpacity:0.25, weight:3 });
      beep();
    } else {
      banner.className = 'alert-card safe';
      $('banner-icon').textContent = '🟢';
      $('banner-title').textContent = 'SAFE IN PASTURE';
      $('banner-desc').textContent  = 'Bluetooth Distance: ' + dist.toFixed(1) + 'm · Safe limit: ' + maxR + 'm';
      $('badge-distance').textContent = dist.toFixed(1) + ' m';
      pastureCircle.setStyle({ color:'#10b981', fillColor:'#10b981', fillOpacity:0.18, weight:2 });
    }
  }

  /* ── 6. UI STATES ── */
  function setBtOnline() {
    isBtConnected = true;
    const pill = $('device-pill');
    if (pill) {
      pill.className = 'device-pill online';
      $('status-text').textContent = 'CONNECTED';
    }
    $('lbl-ip-info').textContent = 'Bluetooth Device: Connected (CowCollar-BT)';
  }

  function setBtOffline() {
    isBtConnected = false;
    const pill = $('device-pill');
    if (pill) {
      pill.className = 'device-pill offline';
      $('status-text').textContent = 'DISCONNECTED';
    }
    $('lbl-ip-info').textContent = 'Bluetooth Device: Disconnected';

    const cowEl = document.getElementById('marker-cow');
    if (cowEl) cowEl.classList.add('offline-marker');

    $('val-speed').textContent = '-- km/h';
    $('val-status').textContent = '--';

    const banner = $('geofence-banner');
    banner.className = 'alert-card safe';
    $('banner-icon').textContent = '🔵';
    $('banner-title').textContent = 'BLUETOOTH READY';
    $('banner-desc').textContent  = 'Click 🔵 top icon to pair with CowCollar-BT';
  }

  /* ── 7. WEB BLUETOOTH & SERIAL CONNECTION ENGINE ── */
  async function connectBluetooth() {
    try {
      // 1. Try Web Serial API (Chrome/Android Bluetooth Serial RFCOMM / USB)
      if ('serial' in navigator) {
        bluetoothPort = await navigator.serial.requestPort();
        await bluetoothPort.open({ baudRate: 115200 });

        setBtOnline();
        const textDecoder = new TextDecoderStream();
        const readableStreamClosed = bluetoothPort.readable.pipeTo(textDecoder.writable);
        const reader = textDecoder.readable.getReader();

        let buffer = '';
        while (true) {
          const { value, done } = await reader.read();
          if (done) break;
          buffer += value;
          let lines = buffer.split('\n');
          buffer = lines.pop(); // keep last incomplete line
          for (let line of lines) {
            line = line.trim();
            if (line.startsWith('{') && line.endsWith('}')) {
              try {
                const json = JSON.parse(line);
                onData(json);
              } catch (err) {}
            }
          }
        }
      }
      // 2. Try Web Bluetooth API Fallback
      else if ('bluetooth' in navigator) {
        bluetoothDevice = await navigator.bluetooth.requestDevice({
          acceptAllDevices: true,
          optionalServices: ['00001101-0000-1000-8000-00805f9b34fb'] // SPP UUID
        });
        setBtOnline();
      } else {
        alert('Web Bluetooth/Serial API is supported in Chrome on Android & Desktop. Please use Chrome browser.');
      }
    } catch (e) {
      console.warn('Bluetooth Pairing:', e.message);
    }
  }

  /* ── 8. LISTENERS ── */
  document.addEventListener('DOMContentLoaded', () => {
    initMap();

    const btBtn = $('btn-bt-connect');
    if (btBtn) {
      btBtn.addEventListener('click', connectBluetooth);
    }

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

    // Check packet liveness
    setInterval(() => {
      if (isBtConnected && Date.now() - lastPacketAt > 3500) {
        setBtOffline();
      }
    }, 1000);
  });

})();
