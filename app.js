/**
 * CattleGuard Pro - Mobile GPS & Hybrid Geofence Engine
 * Features: Satellite Maps, Phone GPS Geolocation, Web Audio Alarm, Auto-Discovery
 */

(function () {
  'use strict';

  let map = null;
  let satelliteLayer = null;
  let streetLayer = null;
  let isSatellite = true;

  let farmerMarker = null;
  let cowMarker = null;
  let pastureCircle = null;
  let breadcrumbTrail = null;
  let trailHistory = [];

  // Farmer's real phone location (Center/Anchor)
  let farmerLat = 11.016842;
  let farmerLng = 76.955819;
  let hasPhoneGps = false;

  let espIp = localStorage.getItem('esp_ip') || '192.168.43.100';
  let lastPacketTimestamp = Date.now();
  let audioCtx = null;
  let lastAlarmBeepTime = 0;
  let isSoundEnabled = true;
  let wanderAngle = 0.0;

  function $(id) { return document.getElementById(id); }

  // 1. Initialize Dual-Layer Leaflet Map
  function initMap() {
    if (typeof L === 'undefined') {
      console.error('Leaflet JS is not loaded.');
      return;
    }

    map = L.map('map', {
      zoomControl: false,
      attributionControl: false,
      maxZoom: 19
    }).setView([farmerLat, farmerLng], 18);

    // High resolution Satellite Imagery (Esri World Imagery)
    satelliteLayer = L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', {
      maxZoom: 19
    }).addTo(map);

    // Dark Street View (CartoDB Dark Matter)
    streetLayer = L.tileLayer('https://{s}.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}{r}.png', {
      maxZoom: 19,
      subdomains: 'abcd'
    });

    // Farmer Phone Pin (Center)
    const farmerIcon = L.divIcon({
      className: '',
      html: '<div class="marker-radar-ring">📍</div>',
      iconSize: [32, 32],
      iconAnchor: [16, 16]
    });
    farmerMarker = L.marker([farmerLat, farmerLng], { icon: farmerIcon }).addTo(map);

    // Cow Collar Marker (Live Moving Target)
    const cowIcon = L.divIcon({
      className: '',
      html: '<div class="marker-cow-glow">🐄</div>',
      iconSize: [36, 36],
      iconAnchor: [18, 18]
    });
    cowMarker = L.marker([farmerLat, farmerLng], { icon: cowIcon }).addTo(map);

    // Safe Pasture Boundary Circle
    pastureCircle = L.circle([farmerLat, farmerLng], {
      radius: 15,
      color: '#10b981',
      fillColor: '#10b981',
      fillOpacity: 0.18,
      weight: 2,
      dashArray: '6, 6'
    }).addTo(map);

    // Breadcrumb Trail
    breadcrumbTrail = L.polyline([], {
      color: '#06b6d4',
      weight: 3,
      opacity: 0.85
    }).addTo(map);

    // Auto-fit bounds
    setTimeout(function () {
      if (map) map.invalidateSize();
    }, 400);

    // Prompt and bind Phone GPS
    requestPhoneGeolocation();
  }

  // 2. Real-Time Phone GPS Tracker
  function requestPhoneGeolocation() {
    if ('geolocation' in navigator) {
      navigator.geolocation.getCurrentPosition(
        function (pos) {
          farmerLat = pos.coords.latitude;
          farmerLng = pos.coords.longitude;
          hasPhoneGps = true;
          updateFarmerAnchor(farmerLat, farmerLng);
        },
        function (err) {
          console.warn('Phone GPS prompt dismissed/unavailable:', err.message);
        },
        { enableHighAccuracy: true, timeout: 8000, maximumAge: 0 }
      );

      // Continuously update farmer anchor if walking
      navigator.geolocation.watchPosition(
        function (pos) {
          farmerLat = pos.coords.latitude;
          farmerLng = pos.coords.longitude;
          updateFarmerAnchor(farmerLat, farmerLng);
        },
        null,
        { enableHighAccuracy: true, maximumAge: 2000 }
      );
    }
  }

  function updateFarmerAnchor(lat, lng) {
    if (!map) return;
    const pos = [lat, lng];
    farmerMarker.setLatLng(pos);
    pastureCircle.setLatLng(pos);
  }

  // 3. Audio Alarm Tone
  function triggerAudioAlert() {
    if (!isSoundEnabled) return;
    const now = Date.now();
    if (now - lastAlarmBeepTime < 900) return;
    lastAlarmBeepTime = now;

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
      osc.connect(gain);
      gain.connect(audioCtx.destination);
      osc.start();
      osc.stop(audioCtx.currentTime + 0.28);
    } catch (e) {}
  }

  // 4. Process Incoming Collar Telemetry
  function handleCollarData(data) {
    lastPacketTimestamp = Date.now();

    // Top Status Indicator
    const chip = $('status-chip');
    chip.className = 'status-chip online';
    $('status-text').textContent = 'Live';

    const distMeters = parseFloat(data.dist) || 0.0;
    const maxRadius = parseFloat($('radius-slider').value) || 15;
    pastureCircle.setRadius(maxRadius);

    // Calculate Cow Coordinates
    let cowLat, cowLng;
    if (data.gpsFix && data.lat && parseFloat(data.lat) !== 0) {
      cowLat = parseFloat(data.lat);
      cowLng = parseFloat(data.lng);
    } else {
      wanderAngle += 0.08;
      if (wanderAngle > 6.283) wanderAngle = 0.0;
      // 1 meter ≈ 0.000009 degrees
      const offsetLat = (distMeters * 0.000009) * Math.cos(wanderAngle);
      const offsetLng = (distMeters * 0.000009) * Math.sin(wanderAngle);
      cowLat = farmerLat + offsetLat;
      cowLng = farmerLng + offsetLng;
    }

    // Update Telemetry Display (Pure GPS data)
    $('val-lat').textContent = cowLat.toFixed(5) + '°';
    $('val-lng').textContent = cowLng.toFixed(5) + '°';
    $('val-speed').textContent = (parseFloat(data.spd) || 0.0).toFixed(1) + ' km/h';
    $('val-status').textContent = data.gpsFix ? '3D GPS' : 'TRACKING';
    $('badge-distance').textContent = distMeters.toFixed(1) + ' m';
    $('link-gmaps').href = 'https://www.google.com/maps?q=' + cowLat + ',' + cowLng;

    // Update Map Marker & Breadcrumb
    const cowPos = [cowLat, cowLng];
    cowMarker.setLatLng(cowPos);
    trailHistory.push(cowPos);
    if (trailHistory.length > 300) trailHistory.shift();
    breadcrumbTrail.setLatLngs(trailHistory);

    // Geofence Evaluation
    const banner = $('geofence-banner');
    const bannerTitle = $('banner-title');
    const bannerDesc = $('banner-desc');
    const bannerIcon = $('banner-icon');

    if (distMeters > maxRadius) {
      // BREACH STATE
      banner.className = 'geofence-banner breach';
      bannerIcon.textContent = '🚨';
      bannerTitle.textContent = 'OUT OF PASTURE!';
      bannerDesc.textContent = 'Distance to Farmer: ' + distMeters.toFixed(1) + 'm (Exceeded by +' + (distMeters - maxRadius).toFixed(1) + 'm)';

      pastureCircle.setStyle({
        color: '#f43f5e',
        fillColor: '#f43f5e',
        fillOpacity: 0.35,
        weight: 3
      });

      triggerAudioAlert();
    } else {
      // SAFE STATE
      banner.className = 'geofence-banner safe';
      bannerIcon.textContent = '🟢';
      bannerTitle.textContent = 'SAFE IN PASTURE';
      bannerDesc.textContent = 'Collar is inside your ' + maxRadius + 'm safe perimeter';

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
      .then(data => handleCollarData(data))
      .catch(() => {
        // Scan other subnet candidates in parallel
        candidateIps.forEach(function (ip) {
          fetch('http://' + ip + '/api/gps', { mode: 'cors' })
            .then(res => res.json())
            .then(data => {
              espIp = ip;
              localStorage.setItem('esp_ip', ip);
              handleCollarData(data);
            })
            .catch(() => {});
        });
      });
  }

  // 6. Setup Interactive UI Listeners
  function initListeners() {
    // Satellite / Street Map Toggle
    $('btn-layer-toggle').onclick = function () {
      isSatellite = !isSatellite;
      if (isSatellite) {
        map.removeLayer(streetLayer);
        map.addLayer(satelliteLayer);
        $('btn-layer-toggle').className = 'map-btn map-btn-active';
        $('btn-layer-toggle').innerHTML = '🛰️ <span>Satellite</span>';
      } else {
        map.removeLayer(satelliteLayer);
        map.addLayer(streetLayer);
        $('btn-layer-toggle').className = 'map-btn';
        $('btn-layer-toggle').innerHTML = '🗺️ <span>Streets</span>';
      }
    };

    // Center on Phone Location
    $('btn-sync-phone').onclick = function () {
      requestPhoneGeolocation();
      if (map) map.setView([farmerLat, farmerLng], 18, { animate: true });
    };

    // Lock on Cow Marker
    $('btn-lock-cow').onclick = function () {
      if (map && cowMarker) {
        map.setView(cowMarker.getLatLng(), 19, { animate: true });
      }
    };

    // Radius Slider
    $('radius-slider').oninput = function () {
      const val = parseInt($('radius-slider').value, 10);
      $('radius-display').textContent = val + ' meters';
      if (pastureCircle) pastureCircle.setRadius(val);
    };

    // Alarm Sound Toggle
    $('btn-sound-toggle').onclick = function () {
      isSoundEnabled = !isSoundEnabled;
      $('sound-icon').textContent = isSoundEnabled ? '🔔' : '🔕';
      $('btn-sound-toggle').style.opacity = isSoundEnabled ? '1' : '0.5';
    };

    // Liveness Watchdog
    setInterval(function () {
      if (Date.now() - lastPacketTimestamp > 4000) {
        const chip = $('status-chip');
        chip.className = 'status-chip offline';
        $('status-text').textContent = 'Searching';
      }
    }, 1000);
  }

  document.addEventListener('DOMContentLoaded', function () {
    initMap();
    initListeners();
    autoPollCollar();
    setInterval(autoPollCollar, 1000);
  });
})();
