/**
 * CattleGuard Pro - Strict Hardware GPS & Geofence Engine
 * Zero Fake Movement • Instant Offline Detection • Real Hardware Telemetry
 */

(function () {
  'use strict';

  let map = null;
  let farmerMarker = null;
  let cowMarker = null;
  let pastureCircle = null;
  let breadcrumbTrail = null;
  let trailHistory = [];

  // Farmer's real phone location (Pasture Center Anchor)
  let farmerLat = 11.016842;
  let farmerLng = 76.955819;
  let cowLat = 11.016842;
  let cowLng = 76.955819;
  let currentDist = 0.0;
  let hasPhoneGps = false;

  let espIp = localStorage.getItem('esp_ip') || '192.168.43.100';
  let lastPacketTimestamp = 0;
  let audioCtx = null;
  let lastAlarmBeepTime = 0;
  let isSoundEnabled = true;
  let isEspOnline = false;

  function $(id) { return document.getElementById(id); }

  // 1. Initialize Pure Satellite Leaflet Map
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

    // Cow Collar Marker
    const cowIcon = L.divIcon({
      className: '',
      html: '<div class="marker-cow-glow" id="marker-cow">🐄</div>',
      iconSize: [36, 36],
      iconAnchor: [18, 18]
    });
    cowMarker = L.marker([cowLat, cowLng], { icon: cowIcon }).addTo(map);

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

    // Display initial coordinates
    updateCoordinatesDisplay(cowLat, cowLng);

    setTimeout(function () {
      if (map) map.invalidateSize();
    }, 400);

    // Acquire Real Phone GPS Coordinates
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
          
          if (!isEspOnline) {
            cowLat = farmerLat;
            cowLng = farmerLng;
            if (cowMarker) cowMarker.setLatLng([cowLat, cowLng]);
            updateCoordinatesDisplay(cowLat, cowLng);
          }
        },
        function (err) {
          console.warn('Phone GPS prompt dismissed/denied:', err.message);
        },
        { enableHighAccuracy: true, timeout: 8000, maximumAge: 0 }
      );

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

  // 3. Audio Warning Alarm
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

  // 4. Process Real Telemetry from ESP Collar
  function handleCollarData(data) {
    lastPacketTimestamp = Date.now();
    isEspOnline = true;

    // Status: Online
    const chip = $('status-chip');
    chip.className = 'status-chip online';
    $('status-text').textContent = 'ONLINE';
    $('lbl-ip-info').textContent = 'Collar IP: ' + espIp;

    const distMeters = parseFloat(data.dist) || 0.0;
    const speed = parseFloat(data.spd) || 0.0;
    const hasGpsFix = !!data.gpsFix;

    // Set real position: If GPS fix exists, use GPS; otherwise place at measured distance
    if (hasGpsFix && data.lat && parseFloat(data.lat) !== 0) {
      cowLat = parseFloat(data.lat);
      cowLng = parseFloat(data.lng);
    } else {
      // Offset relative to phone by distance (Static angle until real movement)
      const offset = distMeters * 0.000009;
      cowLat = farmerLat + offset;
      cowLng = farmerLng;
    }

    currentDist = distMeters;
    updateCoordinatesDisplay(cowLat, cowLng);
    $('val-speed').textContent = speed.toFixed(1) + ' km/h';
    $('val-status').textContent = hasGpsFix ? '3D GPS' : 'SIGNAL TRACK';
    $('badge-distance').textContent = distMeters.toFixed(1) + ' m';

    // Move marker to real position
    const cowPos = [cowLat, cowLng];
    if (cowMarker) cowMarker.setLatLng(cowPos);
    trailHistory.push(cowPos);
    if (trailHistory.length > 200) trailHistory.shift();
    if (breadcrumbTrail) breadcrumbTrail.setLatLngs(trailHistory);

    // Geofence Evaluation
    const maxRadius = parseFloat($('radius-slider').value) || 15;
    pastureCircle.setRadius(maxRadius);

    const banner = $('geofence-banner');
    const bannerTitle = $('banner-title');
    const bannerDesc = $('banner-desc');
    const bannerIcon = $('banner-icon');

    if (distMeters > maxRadius) {
      // BREACH STATE
      banner.className = 'geofence-banner breach';
      bannerIcon.textContent = '🚨';
      bannerTitle.textContent = 'OUT OF PASTURE!';
      bannerDesc.textContent = 'Collar Distance: ' + distMeters.toFixed(1) + 'm (Exceeded by +' + (distMeters - maxRadius).toFixed(1) + 'm)';

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
      bannerDesc.textContent = 'Collar Distance: ' + distMeters.toFixed(1) + 'm &bull; Safe Limit: ' + maxRadius + 'm';

      pastureCircle.setStyle({
        color: '#10b981',
        fillColor: '#10b981',
        fillOpacity: 0.18,
        weight: 2
      });
    }
  }

  // 5. Handle Device Switched Off / Offline State
  function setCollarOffline() {
    isEspOnline = false;

    const chip = $('status-chip');
    chip.className = 'status-chip offline';
    $('status-text').textContent = 'OFFLINE';
    $('lbl-ip-info').textContent = 'Collar: Not Responding (' + espIp + ')';

    $('val-speed').textContent = '0.0 km/h';
    $('val-status').textContent = 'OFFLINE';
    $('badge-distance').textContent = '-- m';

    const banner = $('geofence-banner');
    banner.className = 'geofence-banner';
    banner.style.background = 'rgba(255, 255, 255, 0.05)';
    banner.style.border = '1px solid rgba(255, 255, 255, 0.15)';
    banner.style.boxShadow = 'none';
    $('banner-icon').textContent = '⚠️';
    $('banner-title').textContent = 'COLLAR OFFLINE';
    $('banner-title').style.color = '#94a3b8';
    $('banner-desc').textContent = 'Device switched off. Power on ESP8266 & Hotspot.';

    pastureCircle.setStyle({
      color: '#64748b',
      fillColor: '#64748b',
      fillOpacity: 0.1,
      weight: 1
    });

    // FREEZE POSITION: Do NOT move or simulate any random wander!
  }

  // 6. Polling & Auto-Discovery
  function pollCollar() {
    const candidateIps = [
      espIp, '192.168.43.100', '192.168.43.2', '192.168.43.3', '192.168.43.4',
      '192.168.43.5', '192.168.43.10', '192.168.43.20', '192.168.4.1'
    ];

    fetch('http://' + espIp.trim().replace(/^https?:\/\//, '') + '/api/gps', { mode: 'cors' })
      .then(res => res.json())
      .then(data => handleCollarData(data))
      .catch(() => {
        // Probe other candidate IPs
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
    // Open in Google Maps
    $('btn-gmaps').onclick = function (e) {
      e.preventDefault();
      const mapsUrl = 'https://www.google.com/maps/search/?api=1&query=' + cowLat + ',' + cowLng;
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
        map.setView(cowMarker.getLatLng(), 19, { animate: true });
      }
    };

    // Configure ESP IP
    $('btn-change-ip').onclick = function () {
      const newIp = prompt('Enter ESP8266 Collar IP Address (Default: 192.168.43.100):', espIp);
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
    };

    // Alarm Sound Toggle
    $('btn-sound-toggle').onclick = function () {
      isSoundEnabled = !isSoundEnabled;
      $('sound-icon').textContent = isSoundEnabled ? '🔔' : '🔕';
      $('btn-sound-toggle').style.opacity = isSoundEnabled ? '1' : '0.5';
    };

    // Watchdog: If no packet received for > 2.5s, declare OFFLINE and FREEZE position
    setInterval(function () {
      if (Date.now() - lastPacketTimestamp > 2500) {
        setCollarOffline();
      }
    }, 1000);
  }

  document.addEventListener('DOMContentLoaded', function () {
    initMap();
    initListeners();
    setCollarOffline(); // Start in clean offline state
    pollCollar();
    setInterval(pollCollar, 1000);
  });
})();
