/**
 * Smart GPS & Wi-Fi RSSI Hybrid Live Tracking Dashboard Client
 * Real-time Hardware Telemetry & Geofence Monitor
 */

(function () {
  'use strict';

  let map = null;
  let marker = null;
  let accCircle = null;
  let gfCircle = null;
  let trail = null;
  let trailPoints = [];
  let anchorPos = null;

  let espIp = localStorage.getItem('esp_ip') || '192.168.43.100';
  let lastPacketTime = Date.now();
  let packetCount = 0;
  let audioCtx = null;
  let lastAlarmBeep = 0;

  function $(id) { return document.getElementById(id); }

  // 1. Initialize Leaflet Map
  function initMap() {
    if (typeof L === 'undefined') return;

    // Default centered on farm location
    const defaultCenter = [11.016842, 76.955819];
    anchorPos = L.latLng(defaultCenter[0], defaultCenter[1]);

    map = L.map('map', { zoomControl: true }).setView(defaultCenter, 18);
    
    // Modern dark tile layer
    const tileLayer = L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
      maxZoom: 19,
      subdomains: 'abcd'
    });
    tileLayer.addTo(map);

    tileLayer.on('tileerror', function() {
      L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 19 }).addTo(map);
    });

    const cowIcon = L.divIcon({
      className: '',
      html: '<div style="position:relative;width:28px;height:28px;display:flex;align-items:center;justify-content:center;background:#06B6D4;border:3px solid #fff;border-radius:50%;box-shadow:0 0 16px #06B6D4;font-size:14px;">🐄</div>',
      iconSize: [28, 28],
      iconAnchor: [14, 14]
    });

    marker = L.marker(defaultCenter, { icon: cowIcon }).addTo(map);
    accCircle = L.circle(defaultCenter, { radius: 10, color: '#06B6D4', fillColor: '#06B6D4', fillOpacity: 0.15, weight: 1 }).addTo(map);
    gfCircle = L.circle(defaultCenter, { radius: 15, color: '#10B981', fillColor: '#10B981', fillOpacity: 0.15, weight: 2, dashArray: '6,6' }).addTo(map);
    trail = L.polyline([], { color: '#38bdf8', weight: 4, opacity: 0.8 }).addTo(map);

    // Allow user to click on map to set their Farm Anchor Center
    map.on('click', function (e) {
      setCustomAnchor(e.latlng.lat, e.latlng.lng);
    });
  }

  // 2. Set Custom Anchor & Sync to ESP
  function setCustomAnchor(lat, lng) {
    anchorPos = L.latLng(lat, lng);
    gfCircle.setLatLng(anchorPos);
    $('map-info').textContent = '📍 Farm Anchor Set: ' + lat.toFixed(5) + ', ' + lng.toFixed(5);
    
    // Notify ESP of custom anchor
    if (espIp) {
      const cleanIp = espIp.trim().replace(/^https?:\/\//, '');
      fetch('http://' + cleanIp + '/api/setcenter?lat=' + lat + '&lng=' + lng, { mode: 'no-cors' }).catch(()=>{});
    }
  }

  // 3. Audio Alarm Tone
  function playBeep() {
    if (!$('chk-sound').checked) return;
    const now = Date.now();
    if (now - lastAlarmBeep < 1200) return;
    lastAlarmBeep = now;

    try {
      if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
      if (audioCtx.state === 'suspended') audioCtx.resume();
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      osc.type = 'sawtooth';
      osc.frequency.setValueAtTime(880, audioCtx.currentTime);
      osc.frequency.exponentialRampToValueAtTime(440, audioCtx.currentTime + 0.3);
      gain.gain.setValueAtTime(0.25, audioCtx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime + 0.3);
      osc.connect(gain);
      gain.connect(audioCtx.destination);
      osc.start();
      osc.stop(audioCtx.currentTime + 0.3);
    } catch (e) {}
  }

  // 4. Process Telemetry Data from ESP
  function processTelemetry(d, pingMs) {
    lastPacketTime = Date.now();
    packetCount++;
    $('f-pkts').textContent = packetCount;
    $('v-ping').textContent = pingMs + ' ms';

    // Status Badges
    $('b-esp').innerHTML = '<span class="dot dot-green"></span><span class="lbl">ESP:</span> <span class="val">ONLINE</span>';
    
    const lat = parseFloat(d.lat);
    const lng = parseFloat(d.lng);
    const dist = parseFloat(d.dist) || 0.0;
    const allowedRadius = parseFloat($('geo-radius').value) || 15;

    // Update Telemetry Cards
    $('c-lat').textContent = lat.toFixed(6) + '°';
    $('c-lng').textContent = lng.toFixed(6) + '°';
    $('c-spd').textContent = d.spd;
    $('c-alt').textContent = d.alt;
    $('c-sat').textContent = d.sat;
    $('c-hdop').textContent = d.hdop;
    $('c-crs').textContent = (d.crs !== undefined ? d.crs : '0.0') + '°';
    $('c-fix').textContent = d.fix || 'ACTIVE';
    $('c-time').textContent = d.time;
    $('c-date').textContent = d.date;

    $('f-uart').textContent = d.uart ? ('STREAMING (' + d.chars + ' bytes)') : 'STANDBY';
    $('f-uart').style.color = d.uart ? '#10B981' : '#F59E0B';
    $('f-dist').textContent = dist.toFixed(1) + ' m';

    $('btn-gmaps').href = 'https://www.google.com/maps?q=' + lat + ',' + lng;

    // Update Map Marker & Trail
    const curPos = [lat, lng];
    marker.setLatLng(curPos);
    accCircle.setLatLng(curPos);
    accCircle.setRadius(Math.max((parseFloat(d.hdop) || 1.5) * 5, 6));

    trailPoints.push(curPos);
    if (trailPoints.length > 500) trailPoints.shift();
    trail.setLatLngs(trailPoints);

    // Geofence Evaluation
    const rBox = $('range-box');
    const rTitle = $('range-title');
    const rDesc = $('range-desc');
    const banner = $('alert-banner');
    const bannerText = $('alert-text');

    if (dist > allowedRadius) {
      // BREACH STATE
      $('b-gps').innerHTML = '<span class="dot dot-red"></span><span class="lbl">STATUS:</span> <span class="val" style="color:#ef4444">OUT OF RANGE</span>';
      $('pill-signal').className = 'pill pill-gray';
      $('pill-signal').style.background = 'rgba(239,68,68,0.2)';
      $('pill-signal').style.color = '#ef4444';
      $('pill-signal').textContent = 'BREACH DETECTED';

      rBox.className = 'range-status breached';
      $('range-icon').textContent = '🚨';
      rTitle.textContent = 'BREACH DETECTED: OUT OF BOUNDS!';
      rDesc.textContent = 'Distance to Farmer: ' + dist.toFixed(1) + 'm (Exceeded by ' + (dist - allowedRadius).toFixed(1) + 'm)';

      banner.className = 'alert-banner alert-danger';
      bannerText.innerHTML = '🚨 <b>OUT OF BOUNDS ALERT:</b> Cattle collar moved beyond ' + allowedRadius + 'm safe pasture radius!';

      gfCircle.setStyle({ color: '#EF4444', fillColor: '#EF4444', fillOpacity: 0.35, weight: 3 });
      playBeep();
    } else {
      // SAFE STATE
      $('b-gps').innerHTML = '<span class="dot dot-green"></span><span class="lbl">STATUS:</span> <span class="val">SAFE (INSIDE)</span>';
      $('pill-signal').className = 'pill pill-green';
      $('pill-signal').textContent = 'WITHIN PERIMETER';

      rBox.className = 'range-status safe';
      $('range-icon').textContent = '🟢';
      rTitle.textContent = 'SAFE ZONE: WITHIN PASTURE';
      rDesc.textContent = 'Distance to Farmer: ' + dist.toFixed(1) + 'm &bull; Safe Range: ' + allowedRadius + 'm';

      banner.className = 'alert-banner alert-success';
      bannerText.innerHTML = '🟢 <b>LIVE TELEMETRY ACTIVE:</b> Collar is inside the ' + allowedRadius + 'm safe perimeter.';

      gfCircle.setStyle({ color: '#10B981', fillColor: '#10B981', fillOpacity: 0.15, weight: 2 });
    }
  }

  // 5. Auto-Discovery & Polling Engine
  function autoDiscoverAndPoll() {
    // 1. Try current/cached IP first
    pollEsp(espIp, function(success) {
      if (!success) {
        // 2. Fast background subnet scan (192.168.43.x)
        scanHotspotSubnet();
      }
    });
  }

  function scanHotspotSubnet() {
    const candidateIps = [
      '192.168.43.100', '192.168.43.2', '192.168.43.3', '192.168.43.4', '192.168.43.5',
      '192.168.43.10', '192.168.43.20', '192.168.43.50', '192.168.43.150', '192.168.4.1'
    ];

    $('alert-text').textContent = '🔍 Auto-discovering Collar on Hotspot network...';

    candidateIps.forEach(function(ip) {
      fetch('http://' + ip + '/api/gps', { mode: 'cors' })
        .then(res => res.json())
        .then(data => {
          espIp = ip;
          localStorage.setItem('esp_ip', espIp);
          $('esp-ip').value = espIp;
          processTelemetry(data, 25);
        })
        .catch(()=>{});
    });
  }

  function pollEsp(targetIp, cb) {
    if (!targetIp) return;
    const t0 = performance.now();
    const cleanIp = targetIp.trim().replace(/^https?:\/\//, '');
    const url = 'http://' + cleanIp + '/api/gps';

    fetch(url, { mode: 'cors' })
      .then(res => res.json())
      .then(data => {
        const pingMs = Math.round(performance.now() - t0);
        processTelemetry(data, pingMs);
        if (cb) cb(true);
      })
      .catch(err => {
        $('b-esp').innerHTML = '<span class="dot dot-red"></span><span class="lbl">ESP:</span> <span class="val">SEARCHING</span>';
        $('v-ping').textContent = '-- ms';
        if (cb) cb(false);
      });
  }

  // 6. Setup Controls
  function initControls() {
    $('esp-ip').value = espIp;

    $('btn-connect').onclick = function () {
      espIp = $('esp-ip').value.trim();
      localStorage.setItem('esp_ip', espIp);
      $('alert-text').textContent = '🔄 Connecting to ESP8266 at ' + espIp + '...';
      pollEsp(espIp);
    };

    $('btn-set-anchor').onclick = function () {
      if (marker && map.hasLayer(marker)) {
        const p = marker.getLatLng();
        setCustomAnchor(p.lat, p.lng);
        alert('📍 Farm Anchor set to Current Location (' + p.lat.toFixed(5) + ', ' + p.lng.toFixed(5) + ')');
      }
    };

    $('btn-recenter').onclick = function () {
      if (marker && map.hasLayer(marker)) {
        map.setView(marker.getLatLng(), 18, { animate: true });
      }
    };

    $('geo-radius').oninput = function () {
      const r = parseFloat($('geo-radius').value) || 15;
      if (gfCircle) gfCircle.setRadius(r);
    };

    setInterval(function () {
      const sec = ((Date.now() - lastPacketTime) / 1000).toFixed(0);
      $('v-ago').textContent = sec + 's ago';
    }, 1000);
  }

  document.addEventListener('DOMContentLoaded', function () {
    initMap();
    initControls();
    autoDiscoverAndPoll();
    setInterval(autoDiscoverAndPoll, 1000);
  });
})();



            // Styling for Safe State (Green)
            geofenceCircle.setStyle({
                color: '#10B981',
                fillColor: '#10B981',
                fillOpacity: 0.15,
                weight: 2,
                dashArray: '6, 6'
            });

            elGeofenceAlertBox.className = 'geofence-alert-box safe-state';
            elGeofenceAlertIcon.textContent = '✅';
            elGeofenceAlertTitle.textContent = 'SAFE ZONE: WITHIN RANGE';
            elGeofenceAlertDesc.textContent = `Position Safe: Distance ${distanceMeters.toFixed(1)} m | Allowed Radius Limit: ${geofenceRadius.toFixed(1)} m (${(geofenceRadius - distanceMeters).toFixed(1)} m buffer remaining)`;
        }
    }

    // --- NON-BLOCKING WEB AUDIO ALARM TONE ---
    function triggerAudioAlarm() {
        if (!chkAudioAlert || !chkAudioAlert.checked) return;

        const now = Date.now();
        if (now - lastAudioAlarmTime < 1200) return; // Limit beep frequency to once per 1.2s
        lastAudioAlarmTime = now;

        try {
            if (!audioCtx) {
                audioCtx = new (window.AudioContext || window.webkitAudioContext)();
            }

            if (audioCtx.state === 'suspended') {
                audioCtx.resume();
            }

            const osc = audioCtx.createOscillator();
            const gain = audioCtx.createGain();

            osc.type = 'sawtooth';
            osc.frequency.setValueAtTime(880, audioCtx.currentTime); // A5 note
            osc.frequency.exponentialRampToValueAtTime(440, audioCtx.currentTime + 0.3); // Pitch bend down

            gain.gain.setValueAtTime(0.18, audioCtx.currentTime);
            gain.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime + 0.3);

            osc.connect(gain);
            gain.connect(audioCtx.destination);

            osc.start();
            osc.stop(audioCtx.currentTime + 0.3);
        } catch (e) {
            console.warn('Audio alarm exception:', e);
        }
    }

    // --- UI UPDATERS ---
    function updateGpsStateUI(data) {
        const stateStr = data.state || (data.valid ? 'GPS FIXED' : 'SEARCHING FOR SATELLITES');

        if (data.valid) {
            updateBadge(elGpsStatus, 'FIXED', 'dot-green');
            setBanner('banner-success', '✅ GPS Signal Fixed - Broadcasting real-time telemetry');
            elMapStatusInfo.textContent = `Fixed: ${data.latitude.toFixed(4)}°, ${data.longitude.toFixed(4)}°`;
        } else if (stateStr.includes('NO GPS DATA') || (data.uartActive === false)) {
            updateBadge(elGpsStatus, 'NO DATA', 'dot-red');
            setBanner('banner-error', '❌ GPS UART: No serial data received from NEO-6M. Verify RX/TX wiring.');
            elMapStatusInfo.textContent = 'Hardware Error: No NMEA serial data';
        } else if (stateStr.includes('FIX LOST')) {
            updateBadge(elGpsStatus, 'FIX LOST', 'dot-yellow');
            setBanner('banner-warning', '⚠️ GPS Fix Lost - Re-acquiring satellite signals...');
            elMapStatusInfo.textContent = 'Satellite lock lost';
        } else {
            updateBadge(elGpsStatus, 'SEARCHING', 'dot-yellow');
            setBanner('banner-warning', '🛰️ Searching for GPS satellites... Place antenna facing open sky.');
            elMapStatusInfo.textContent = 'Acquiring satellite constellation...';
        }
    }

    function updateBadge(badgeElement, text, dotClass) {
        if (!badgeElement) return;
        badgeElement.textContent = text;
        const parent = badgeElement.closest('.status-badge');
        if (parent) {
            const dot = parent.querySelector('.status-dot');
            if (dot) {
                dot.className = `status-dot ${dotClass}`;
            }
        }
    }

    function setBanner(className, message) {
        elBanner.className = `diagnostic-banner ${className}`;
        elBannerText.textContent = message;
    }

    function updateSignalQualityPill(quality) {
        elQualityPill.textContent = quality;
        elQualityPill.className = 'quality-pill';

        switch (quality.toLowerCase()) {
            case 'excellent': elQualityPill.classList.add('pill-excellent'); break;
            case 'good':      elQualityPill.classList.add('pill-good'); break;
            case 'fair':      elQualityPill.classList.add('pill-fair'); break;
            case 'poor':      elQualityPill.classList.add('pill-poor'); break;
        if (!map) return;
        if (mapMarker && map.hasLayer(mapMarker)) map.removeLayer(mapMarker);
        if (accuracyCircle && map.hasLayer(accuracyCircle)) map.removeLayer(accuracyCircle);
    }

    // --- ROUTE TRACKING ---
    function addTrackPoint(lat, lng) {
        trackPoints.push([lat, lng]);

        // Cap at 500 points
        if (trackPoints.length > 500) {
            trackPoints.shift();
        }

        if (trackPolyline) {
            trackPolyline.setLatLngs(trackPoints);
        }

        elFootPoints.textContent = `${trackPoints.length} / 500`;
    }

    // --- CONTROLS INITIALIZATION ---
    function initControls() {
        // Geofence Controls
        chkGeofenceEnable.addEventListener('change', () => {
            geofenceEnabled = chkGeofenceEnable.checked;
            lblGeofenceStatus.textContent = geofenceEnabled ? 'ACTIVE' : 'DISABLED';
            lblGeofenceStatus.style.color = geofenceEnabled ? '#06B6D4' : '#64748B';

            if (!geofenceEnabled && geofenceCircle && map.hasLayer(geofenceCircle)) {
                map.removeLayer(geofenceCircle);
                elGeofenceAlertBox.className = 'geofence-alert-box safe-state';
                elGeofenceAlertIcon.textContent = '⏸️';
                elGeofenceAlertTitle.textContent = 'GEOFENCE MONITORING DISABLED';
                elGeofenceAlertDesc.textContent = 'Enable switch above to reactivate safe range alert monitoring.';
            }
        });

        numGeofenceRadius.addEventListener('input', () => {
            let val = parseFloat(numGeofenceRadius.value);
            if (isNaN(val) || val < 1) val = 15;
            geofenceRadius = val;
            if (geofenceCircle) geofenceCircle.setRadius(geofenceRadius);
        });

        btnSetGeofenceCenter.addEventListener('click', () => {
            if (mapMarker) {
                const pos = mapMarker.getLatLng();
                if (pos.lat !== 0 && pos.lng !== 0) {
                    geofenceCenter = pos;
                    if (geofenceCircle) geofenceCircle.setLatLng(geofenceCenter);
                    console.log('Geofence center updated to:', pos);
                }
            }
        });

        btnResetGeofence.addEventListener('click', () => {
            geofenceCenter = null;
            if (geofenceCircle && map.hasLayer(geofenceCircle)) {
                map.removeLayer(geofenceCircle);
            }
            elGeofenceAlertDesc.textContent = 'Geofence center reset. Next GPS position will set new anchor.';
        });

        // Map Controls
        btnCenterMap.addEventListener('click', () => {
            if (mapMarker && map) {
                userHasPannedMap = false;
                const pos = mapMarker.getLatLng();
                if (pos.lat !== 0 && pos.lng !== 0) {
                    map.setView(pos, 18);
                }
            }
        });

        btnStartTrack.addEventListener('click', () => {
            isTracking = true;
            btnStartTrack.setAttribute('disabled', 'true');
            btnStopTrack.removeAttribute('disabled');
        });

        btnStopTrack.addEventListener('click', () => {
            isTracking = false;
            btnStartTrack.removeAttribute('disabled');
            btnStopTrack.setAttribute('disabled', 'true');
        });

        btnClearTrack.addEventListener('click', () => {
            trackPoints = [];
            if (trackPolyline) trackPolyline.setLatLngs([]);
            elFootPoints.textContent = '0 / 500';
        });

        btnToggleDemo.addEventListener('click', () => {
            toggleDemoMode();
        });
    }

    // --- DEMO / MOCK MODE FOR LOCAL TESTING ---
    function toggleDemoMode() {
        isDemoMode = !isDemoMode;

        if (isDemoMode) {
            btnToggleDemo.textContent = 'Demo Mode: ON';
            btnToggleDemo.style.color = '#10B981';
            
            if (websocket) websocket.close();
            if (restPollTimer) clearInterval(restPollTimer);

            console.log('DEMO MODE ACTIVATED - Simulating Movement');
            updateBadge(elEspStatus, 'DEMO MODE', 'dot-blue');

            let step = 0;
            demoInterval = setInterval(() => {
                step++;
                // Simulate vehicle/person walking outwards to breach geofence range
                const driftRadius = (step > 8) ? 0.0003 : 0.00005; // Walks out of 15m radius after ~8s
                demoLat += (Math.random() - 0.3) * driftRadius;
                demoLng += (Math.random() - 0.3) * driftRadius;

                const mockData = {
                    valid: true,
                    state: 'GPS FIXED',
                    quality: 'Excellent',
                    latitude: demoLat,
                    longitude: demoLng,
                    altitude: 412.5 + Math.random() * 2,
                    speedKmh: 4.5 + (Math.random() * 2),
                    course: 127.4 + (Math.random() * 4 - 2),
                    satellites: 9,
                    hdop: 1.1,
                    fixType: '3D',
                    gpsTime: new Date().toUTCString().split(' ')[4] + ' UTC',
                    gpsDate: new Date().toISOString().split('T')[0],
                    charsProcessed: 14520,
                    failedChecksum: 0,
                    uartActive: true,
                    ip: '192.168.4.1 (Demo)'
                };

                processGpsData(mockData);
            }, 1000);
        } else {
            btnToggleDemo.textContent = 'Demo Mode: OFF';
            btnToggleDemo.style.color = '';
            if (demoInterval) clearInterval(demoInterval);
            initWebSocket();
        }
    }

    // --- LAST UPDATE TIMER ---
    function startUpdateTimer() {
        setInterval(() => {
            const elapsedSec = ((Date.now() - lastPacketTimestamp) / 1000).toFixed(1);
            elLastUpdate.textContent = `${elapsedSec}s ago`;
        }, 500);
    }

    document.addEventListener('DOMContentLoaded', function () {
        initMap();
        initControls();
        pollEsp();
        setInterval(pollEsp, 1000);
    });
})();
