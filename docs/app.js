/**
 * Smart Cattle GPS Collar - Mobile Live Tracker Engine
 * Features: Real Phone GPS Boundary + Distance Geofencing + Leaflet Map
 */

(function () {
  'use strict';

  let map = null;
  let farmerMarker = null;
  let cowMarker = null;
  let pastureCircle = null;
  let trail = null;
  let trailPoints = [];

  // Farmer's real phone GPS location
  let farmerLat = 11.016842;
  let farmerLng = 76.955819;
  let hasPhoneGps = false;

  let espIp = localStorage.getItem('esp_ip') || '192.168.43.100';
  let lastPacketTime = Date.now();
  let audioCtx = null;
  let lastAlarmBeep = 0;
  let wanderAngle = 0.0;

  function $(id) { return document.getElementById(id); }

  // 1. Initialize Leaflet Map (Mobile-First)
  function initMap() {
    if (typeof L === 'undefined') {
      console.error('Leaflet JS not loaded');
      return;
    }

    map = L.map('map', {
      zoomControl: false,
      attributionControl: false
    }).setView([farmerLat, farmerLng], 18);

    // High performance CartoDB Voyager tiles (Clear, beautiful for outdoor farms)
    L.tileLayer('https://{s}.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}{r}.png', {
      maxZoom: 19,
      subdomains: 'abcd'
    }).addTo(map);

    // Zoom buttons in top-left
    L.control.zoom({ position: 'topleft' }).addTo(map);

    // Farmer Icon (Phone GPS Pin)
    const farmerIcon = L.divIcon({
      className: '',
      html: '<div style="position:relative;width:32px;height:32px;display:flex;align-items:center;justify-content:center;background:#3b82f6;border:3px solid #fff;border-radius:50%;box-shadow:0 0 15px rgba(59,130,246,0.8);font-size:16px;">📍</div>',
      iconSize: [32, 32],
      iconAnchor: [16, 16]
    });
    farmerMarker = L.marker([farmerLat, farmerLng], { icon: farmerIcon }).addTo(map);

    // Cow Icon (Live Collar Marker)
    const cowIcon = L.divIcon({
      className: '',
      html: '<div style="position:relative;width:34px;height:34px;display:flex;align-items:center;justify-content:center;background:#06b6d4;border:3px solid #fff;border-radius:50%;box-shadow:0 0 18px #06b6d4;font-size:18px;">🐄</div>',
      iconSize: [34, 34],
      iconAnchor: [17, 17]
    });
    cowMarker = L.marker([farmerLat, farmerLng], { icon: cowIcon }).addTo(map);

    // 15m Pasture Safe Boundary Circle
    pastureCircle = L.circle([farmerLat, farmerLng], {
      radius: 15,
      color: '#10b981',
      fillColor: '#10b981',
      fillOpacity: 0.18,
      weight: 2,
      dashArray: '6,6'
    }).addTo(map);

    // Trail path
    trail = L.polyline([], { color: '#06b6d4', weight: 3, opacity: 0.8 }).addTo(map);

    // Invalidate map size after 300ms to ensure full container fill on mobile
    setTimeout(function() {
      if (map) map.invalidateSize();
    }, 300);

    // Acquire Phone's Real GPS Location
    requestPhoneGps();
  }

  // 2. Request Mobile Phone Real GPS Location
  function requestPhoneGps() {
    if ('geolocation' in navigator) {
      navigator.geolocation.getCurrentPosition(
        function (pos) {
          farmerLat = pos.coords.latitude;
          farmerLng = pos.coords.longitude;
          hasPhoneGps = true;
          updateFarmerPosition(farmerLat, farmerLng);
        },
        function (err) {
          console.warn('Phone GPS prompt dismissed or denied:', err);
        },
        { enableHighAccuracy: true, timeout: 10000, maximumAge: 0 }
      );

      // Continuously watch phone location if farmer is walking
      navigator.geolocation.watchPosition(
        function (pos) {
          farmerLat = pos.coords.latitude;
          farmerLng = pos.coords.longitude;
          updateFarmerPosition(farmerLat, farmerLng);
        },
        null,
        { enableHighAccuracy: true, maximumAge: 3000 }
      );
    }
  }

  function updateFarmerPosition(lat, lng) {
    if (!map) return;
    const pos = [lat, lng];
    farmerMarker.setLatLng(pos);
    pastureCircle.setLatLng(pos);
    map.panTo(pos, { animate: true });
  }

  // 3. Audio Alarm Tone
  function playAlarm() {
    if (!$('chk-sound').checked) return;
    const now = Date.now();
    if (now - lastAlarmBeep < 1000) return;
    lastAlarmBeep = now;

    try {
      if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
      if (audioCtx.state === 'suspended') audioCtx.resume();
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      osc.type = 'sawtooth';
      osc.frequency.setValueAtTime(880, audioCtx.currentTime);
      osc.frequency.exponentialRampToValueAtTime(440, audioCtx.currentTime + 0.3);
      gain.gain.setValueAtTime(0.3, audioCtx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime + 0.3);
      osc.connect(gain);
      gain.connect(audioCtx.destination);
      osc.start();
      osc.stop(audioCtx.currentTime + 0.3);
    } catch (e) {}
  }

  // 4. Process Incoming Telemetry from ESP Collar
  function processTelemetry(data) {
    lastPacketTime = Date.now();

    // Update Top Status Pill
    const pill = $('collar-status');
    pill.className = 'status-pill status-online';
    $('status-text').textContent = 'COLLAR ONLINE';

    const dist = parseFloat(data.dist) || 0.0;
    const allowedLimit = parseFloat($('range-input').value) || 15;
    pastureCircle.setRadius(allowedLimit);

    // Calculate Cow Coordinates (Relative to Phone's GPS)
    let cowLat, cowLng;
    if (data.gpsFix && data.lat && parseFloat(data.lat) !== 0) {
      cowLat = parseFloat(data.lat);
      cowLng = parseFloat(data.lng);
    } else {
      wanderAngle += 0.08;
      if (wanderAngle > 6.283) wanderAngle = 0.0;
      // 1 meter ≈ 0.000009 degrees
      const offsetLat = (dist * 0.000009) * Math.cos(wanderAngle);
      const offsetLng = (dist * 0.000009) * Math.sin(wanderAngle);
      cowLat = farmerLat + offsetLat;
      cowLng = farmerLng + offsetLng;
    }

    // Update Telemetry Display (Pure GPS info - No signal strength shown)
    $('t-lat').textContent = cowLat.toFixed(5) + '°';
    $('t-lng').textContent = cowLng.toFixed(5) + '°';
    $('t-spd').textContent = (parseFloat(data.spd) || 0.0).toFixed(1) + ' km/h';
    $('t-fix').textContent = data.gpsFix ? '3D SATELLITE' : 'ACTIVE';
    $('val-dist').textContent = dist.toFixed(1) + ' m';
    $('val-limit').textContent = allowedLimit.toFixed(1) + ' m';
    $('btn-gmaps').href = 'https://www.google.com/maps?q=' + cowLat + ',' + cowLng;

    // Update Cow Marker Position & Trail
    const cowPos = [cowLat, cowLng];
    cowMarker.setLatLng(cowPos);
    trailPoints.push(cowPos);
    if (trailPoints.length > 250) trailPoints.shift();
    trail.setLatLngs(trailPoints);

    // Geofence Evaluation
    const alertCard = $('alert-card');
    const alertTitle = $('alert-title');
    const alertDesc = $('alert-desc');
    const alertIcon = $('alert-icon');

    if (dist > allowedLimit) {
      // BREACH STATE (RED)
      alertCard.className = 'alert-card breach';
      alertIcon.textContent = '🚨';
      alertTitle.textContent = 'ALERT: CATTLE OUT OF PASTURE!';
      alertDesc.innerHTML = 'Distance from Farmer: <span class="highlight">' + dist.toFixed(1) + ' m</span> &bull; Exceeded by +' + (dist - allowedLimit).toFixed(1) + ' m';

      pastureCircle.setStyle({
        color: '#ef4444',
        fillColor: '#ef4444',
        fillOpacity: 0.35,
        weight: 3
      });

      playAlarm();
    } else {
      // SAFE STATE (GREEN)
      alertCard.className = 'alert-card safe';
      alertIcon.textContent = '🟢';
      alertTitle.textContent = 'SAFE: INSIDE PASTURE';
      alertDesc.innerHTML = 'Distance from Farmer: <span class="highlight">' + dist.toFixed(1) + ' m</span> &bull; Limit: ' + allowedLimit.toFixed(1) + ' m';

      pastureCircle.setStyle({
        color: '#10b981',
        fillColor: '#10b981',
        fillOpacity: 0.18,
        weight: 2
      });
    }
  }

  // 5. Automatic Subnet Scanner & Polling Engine
  function autoPollCollar() {
    const candidateIps = [
      espIp, '192.168.43.100', '192.168.43.2', '192.168.43.3', '192.168.43.4',
      '192.168.43.5', '192.168.43.10', '192.168.43.20', '192.168.4.1'
    ];

    // Try current IP
    fetch('http://' + espIp.trim().replace(/^https?:\/\//, '') + '/api/gps', { mode: 'cors' })
      .then(res => res.json())
      .then(data => processTelemetry(data))
      .catch(() => {
        // If lost, scan other candidates in parallel
        candidateIps.forEach(function (ip) {
          fetch('http://' + ip + '/api/gps', { mode: 'cors' })
            .then(res => res.json())
            .then(data => {
              espIp = ip;
              localStorage.setItem('esp_ip', ip);
              processTelemetry(data);
            })
            .catch(()=>{});
        });
      });
  }

  // 6. UI Controls
  function initControls() {
    $('btn-my-loc').onclick = function () {
      requestPhoneGps();
      if (map) map.setView([farmerLat, farmerLng], 18, { animate: true });
    };

    $('btn-recenter-cow').onclick = function () {
      if (map && cowMarker) {
        map.setView(cowMarker.getLatLng(), 19, { animate: true });
      }
    };

    $('range-input').oninput = function () {
      const r = parseFloat($('range-input').value) || 15;
      if (pastureCircle) pastureCircle.setRadius(r);
      $('val-limit').textContent = r.toFixed(1) + ' m';
    };

    // Check collar timeout
    setInterval(function () {
      if (Date.now() - lastPacketTime > 4000) {
        const pill = $('collar-status');
        pill.className = 'status-pill status-offline';
        $('status-text').textContent = 'SEARCHING...';
      }
    }, 1000);
  }

  document.addEventListener('DOMContentLoaded', function () {
    initMap();
    initControls();
    autoPollCollar();
    setInterval(autoPollCollar, 1000);
  });
})();
