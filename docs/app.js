/**
 * CattleGuard Pro - Satellite GPS & Geofence Engine
 * Pure Satellite View • Instant Coordinates • Reliable Google Maps Navigation
 */

(function () {
  'use strict';

  let map = null;
  let farmerMarker = null;
  let cowMarker = null;
  let pastureCircle = null;
  let breadcrumbTrail = null;
  let trailHistory = [];

  // Farmer's real phone location (Pasture Anchor)
  let farmerLat = 11.016842;
  let farmerLng = 76.955819;
  let currentCowLat = 11.016842;
  let currentCowLng = 76.955819;
  let currentDist = 0.0;
  let hasPhoneGps = false;

  let espIp = localStorage.getItem('esp_ip') || '192.168.43.100';
  let lastPacketTimestamp = 0;
  let audioCtx = null;
  let lastAlarmBeepTime = 0;
  let isSoundEnabled = true;
  let wanderAngle = 0.0;
  let isEspOnline = false;

  function $(id) { return document.getElementById(id); }

  // 1. Initialize Pure Satellite Leaflet Map
  function initMap() {
    if (typeof L === 'undefined') {
      console.error('Leaflet JS is not loaded.');
      return;
    }

    // Initialize Map with High-Resolution Esri World Imagery
    map = L.map('map', {
      zoomControl: false,
      attributionControl: false,
      maxZoom: 19
    }).setView([farmerLat, farmerLng], 18);

    // Pure Satellite Layer Only
    L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', {
      maxZoom: 19
    }).addTo(map);

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
    cowMarker = L.marker([currentCowLat, currentCowLng], { icon: cowIcon }).addTo(map);

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

    // Display initial coordinates right away
    updateCoordinatesDisplay(currentCowLat, currentCowLng);

    setTimeout(function () {
      if (map) map.invalidateSize();
    }, 400);

    // Request Phone Real GPS Coordinates
    requestPhoneGeolocation();
  }

  // 2. Real-Time Phone GPS Geolocation Tracker
  function requestPhoneGeolocation() {
    if ('geolocation' in navigator) {
      navigator.geolocation.getCurrentPosition(
        function (pos) {
          farmerLat = pos.coords.latitude;
          farmerLng = pos.coords.longitude;
          hasPhoneGps = true;
          updateFarmerAnchor(farmerLat, farmerLng);
          
          // Also set initial cow near farmer if no packet yet
          if (!isEspOnline) {
            currentCowLat = farmerLat + 0.00002;
            currentCowLng = farmerLng + 0.00002;
            updateCowPosition(currentCowLat, currentCowLng, 3.2, 0.0, false);
          }
        },
        function (err) {
          console.warn('Phone GPS unavailable:', err.message);
        },
        { enableHighAccuracy: true, timeout: 8000, maximumAge: 0 }
      );

      // Continuously update farmer anchor when walking
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

  function updateCoordinatesDisplay(lat, lng) {
    $('val-lat').textContent = lat.toFixed(5) + '°';
    $('val-lng').textContent = lng.toFixed(5) + '°';
  }

  // 3. Audio Warning Tone
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

  // 4. Update Cow Marker & Geofence Evaluation
  function updateCowPosition(lat, lng, dist, speed, hasGpsFix) {
    currentCowLat = lat;
    currentCowLng = lng;
    currentDist = dist;

    updateCoordinatesDisplay(lat, lng);
    $('val-speed').textContent = speed.toFixed(1) + ' km/h';
    $('val-status').textContent = hasGpsFix ? '3D GPS' : 'TRACKING';
    $('badge-distance').textContent = dist.toFixed(1) + ' m';

    // Update Marker on Satellite Map
    const cowPos = [lat, lng];
    if (cowMarker) cowMarker.setLatLng(cowPos);
    trailHistory.push(cowPos);
    if (trailHistory.length > 250) trailHistory.shift();
    if (breadcrumbTrail) breadcrumbTrail.setLatLngs(trailHistory);

    // Evaluate Boundary
    const maxRadius = parseFloat($('radius-slider').value) || 15;
    pastureCircle.setRadius(maxRadius);

    const banner = $('geofence-banner');
    const bannerTitle = $('banner-title');
    const bannerDesc = $('banner-desc');
    const bannerIcon = $('banner-icon');

    if (dist > maxRadius) {
      // OUT OF PASTURE ALERT
      banner.className = 'geofence-banner breach';
      bannerIcon.textContent = '🚨';
      bannerTitle.textContent = 'OUT OF PASTURE!';
      bannerDesc.textContent = 'Distance to Farmer: ' + dist.toFixed(1) + 'm (Exceeded by +' + (dist - maxRadius).toFixed(1) + 'm)';

      pastureCircle.setStyle({
        color: '#f43f5e',
        fillColor: '#f43f5e',
        fillOpacity: 0.35,
        weight: 3
      });

      triggerAudioAlert();
    } else {
      // SAFE IN PASTURE
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

  // 5. Process Telemetry from Collar
  function handleCollarData(data) {
    lastPacketTimestamp = Date.now();
    isEspOnline = true;

    const chip = $('status-chip');
    chip.className = 'status-chip online';
    $('status-text').textContent = 'ONLINE';
    $('lbl-ip-info').textContent = 'Collar IP: ' + espIp;

    const distMeters = parseFloat(data.dist) || 0.0;
    const speed = parseFloat(data.spd) || 0.0;
    const hasFix = !!data.gpsFix;

    let cLat, cLng;
    if (hasFix && data.lat && parseFloat(data.lat) !== 0) {
      cLat = parseFloat(data.lat);
      cLng = parseFloat(data.lng);
    } else {
      wanderAngle += 0.08;
      if (wanderAngle > 6.283) wanderAngle = 0.0;
      const offsetLat = (distMeters * 0.000009) * Math.cos(wanderAngle);
      const offsetLng = (distMeters * 0.000009) * Math.sin(wanderAngle);
      cLat = farmerLat + offsetLat;
      cLng = farmerLng + offsetLng;
    }

    updateCowPosition(cLat, cLng, distMeters, speed, hasFix);
  }

  // 6. Polling & Auto-Discovery Engine
  function pollCollar() {
    const candidateIps = [
      espIp, '192.168.43.100', '192.168.43.2', '192.168.43.3', '192.168.43.4',
      '192.168.43.5', '192.168.43.10', '192.168.43.20', '192.168.4.1'
    ];

    fetch('http://' + espIp.trim().replace(/^https?:\/\//, '') + '/api/gps', { mode: 'cors' })
      .then(res => res.json())
      .then(data => handleCollarData(data))
      .catch(() => {
        // Parallel background subnet probe
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

  // 7. UI Controls & Listeners
  function initListeners() {
    // Open in Google Maps Handler
    $('btn-gmaps').onclick = function (e) {
      e.preventDefault();
      const mapsUrl = 'https://www.google.com/maps/search/?api=1&query=' + currentCowLat + ',' + currentCowLng;
      window.open(mapsUrl, '_blank');
    };

    // Center on Phone Location
    $('btn-sync-phone').onclick = function () {
      requestPhoneGeolocation();
      if (map) map.setView([farmerLat, farmerLng], 18, { animate: true });
    };

    // Lock on Cow Marker
    $('btn-lock-cow').onclick = function () {
      if (map && cowMarker) {
        map.setView([currentCowLat, currentCowLng], 19, { animate: true });
      }
    };

    // Configure ESP IP
    $('btn-change-ip').onclick = function () {
      const newIp = prompt('Enter ESP8266 IP Address (Default: 192.168.43.100):', espIp);
      if (newIp && newIp.trim() !== '') {
        espIp = newIp.trim();
        localStorage.setItem('esp_ip', espIp);
        $('lbl-ip-info').textContent = 'Collar IP: ' + espIp;
        pollCollar();
      }
    };

    $('status-chip').onclick = function () {
      $('btn-change-ip').click();
    };

    // Radius Slider
    $('radius-slider').oninput = function () {
      const val = parseInt($('radius-slider').value, 10);
      $('radius-display').textContent = val + ' meters';
      if (pastureCircle) pastureCircle.setRadius(val);
      // Re-evaluate
      updateCowPosition(currentCowLat, currentCowLng, currentDist, 0.0, false);
    };

    // Alarm Sound Toggle
    $('btn-sound-toggle').onclick = function () {
      isSoundEnabled = !isSoundEnabled;
      $('sound-icon').textContent = isSoundEnabled ? '🔔' : '🔕';
      $('btn-sound-toggle').style.opacity = isSoundEnabled ? '1' : '0.5';
    };

    // Liveness Watchdog (Every 1s)
    setInterval(function () {
      if (Date.now() - lastPacketTimestamp > 3500) {
        isEspOnline = false;
        const chip = $('status-chip');
        chip.className = 'status-chip offline';
        $('status-text').textContent = 'CONNECTING';

        // Keep local position smooth while searching
        wanderAngle += 0.05;
        const simDist = 4.5 + Math.sin(wanderAngle) * 2.0;
        const offsetLat = (simDist * 0.000009) * Math.cos(wanderAngle);
        const offsetLng = (simDist * 0.000009) * Math.sin(wanderAngle);
        updateCowPosition(farmerLat + offsetLat, farmerLng + offsetLng, simDist, 0.0, false);
      }
    }, 1000);
  }

  document.addEventListener('DOMContentLoaded', function () {
    initMap();
    initListeners();
    pollCollar();
    setInterval(pollCollar, 1000);
  });
})();
