/**
 * ESP32 NEO-6M Smart GPS Live Tracking Dashboard Client
 * Production JavaScript Engine with Geofence & Safe Range Monitoring
 */

(function () {
    'use strict';

    // State Variables
    let websocket = null;
    let wsReconnectTimer = null;
    let restPollTimer = null;
    let lastPacketTimestamp = Date.now();
    let packetCounter = 0;
    
    let isDemoMode = false;
    let demoInterval = null;
    let demoLat = 11.0168;
    let demoLng = 76.9558;

    // Map & Tracking State
    let map = null;
    let mapMarker = null;
    let accuracyCircle = null;
    let trackPolyline = null;
    let trackPoints = []; // Capped at 500 points
    let isTracking = false;
    let userHasPannedMap = false;

    // Geofence / Safe Range Alert State
    let geofenceEnabled = true;
    let geofenceRadius = 15; // default 15 meters radius (30m diameter)
    let geofenceCenter = null; // L.latLng
    let geofenceCircle = null;
    let isGeofenceBreached = false;
    let audioCtx = null;
    let lastAudioAlarmTime = 0;

    // UI Elements Cache
    const elGpsStatus = document.getElementById('val-gps-status');
    const elEspStatus = document.getElementById('val-esp32-status');
    const elWifiMode = document.getElementById('val-wifi-mode');
    const elLastUpdate = document.getElementById('val-last-update');
    
    const elLat = document.getElementById('card-lat');
    const elLng = document.getElementById('card-lng');
    const elSpeed = document.getElementById('card-speed');
    const elAlt = document.getElementById('card-alt');
    const elSats = document.getElementById('card-sats');
    const elHdop = document.getElementById('card-hdop');
    const elCourse = document.getElementById('card-course');
    const elFixType = document.getElementById('card-fixtype');
    const elTime = document.getElementById('card-time');
    const elDate = document.getElementById('card-date');
    const elQualityPill = document.getElementById('signal-quality-pill');

    const elBanner = document.getElementById('diagnostic-banner');
    const elBannerText = document.getElementById('banner-text');
    const elMapStatusInfo = document.getElementById('map-status-info');

    // Geofence UI Cache
    const chkGeofenceEnable = document.getElementById('chk-geofence-enable');
    const lblGeofenceStatus = document.getElementById('lbl-geofence-status');
    const numGeofenceRadius = document.getElementById('num-geofence-radius');
    const btnSetGeofenceCenter = document.getElementById('btn-set-geofence-center');
    const btnResetGeofence = document.getElementById('btn-reset-geofence');
    const elGeofenceAlertBox = document.getElementById('geofence-alert-box');
    const elGeofenceAlertIcon = document.getElementById('geofence-alert-icon');
    const elGeofenceAlertTitle = document.getElementById('geofence-alert-title');
    const elGeofenceAlertDesc = document.getElementById('geofence-alert-desc');
    const chkAudioAlert = document.getElementById('chk-audio-alert');

    const elFootIp = document.getElementById('foot-ip');
    const elFootPackets = document.getElementById('foot-packets');
    const elFootUart = document.getElementById('foot-uart');
    const elFootPoints = document.getElementById('foot-points');

    const btnCenterMap = document.getElementById('btn-center-map');
    const btnGmaps = document.getElementById('btn-gmaps');
    const btnStartTrack = document.getElementById('btn-start-track');
    const btnStopTrack = document.getElementById('btn-stop-track');
    const btnClearTrack = document.getElementById('btn-clear-track');
    const btnToggleDemo = document.getElementById('btn-toggle-demo');

    // Initialize Application
    document.addEventListener('DOMContentLoaded', () => {
        initMap();
        initControls();
        initWebSocket();
        startUpdateTimer();
    });

    // --- MAP INITIALIZATION ---
    function initMap() {
        if (typeof L === 'undefined') {
            console.warn('Leaflet library failed to load. Map features disabled.');
            document.getElementById('map-offline-overlay').style.display = 'block';
            return;
        }

        try {
            // Default view centered on initial coordinates
            map = L.map('map', {
                zoomControl: true,
                attributionControl: false
            }).setView([11.0168, 76.9558], 18);

            // Dark CartoDB Map Tiles
            const darkTiles = L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
                maxZoom: 19,
                subdomains: 'abcd'
            });

            // Standard OpenStreetMap Tile Fallback
            const osmTiles = L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
                maxZoom: 19
            });

            darkTiles.addTo(map);

            darkTiles.on('tileerror', () => {
                console.warn('CartoDB dark tiles unreachable, attempting OpenStreetMap fallback...');
                map.removeLayer(darkTiles);
                osmTiles.addTo(map);
            });

            // Custom Neon Marker Icon
            const neonIcon = L.divIcon({
                className: 'custom-map-marker',
                html: `<div style="
                    width: 20px;
                    height: 20px;
                    background: #06B6D4;
                    border: 3px solid #FFFFFF;
                    border-radius: 50%;
                    box-shadow: 0 0 15px #06B6D4, 0 0 25px #06B6D4;
                "></div>`,
                iconSize: [20, 20],
                iconAnchor: [10, 10]
            });

            mapMarker = L.marker([0, 0], { icon: neonIcon });
            accuracyCircle = L.circle([0, 0], {
                radius: 10,
                color: '#06B6D4',
                fillColor: '#06B6D4',
                fillOpacity: 0.1,
                weight: 1
            });

            trackPolyline = L.polyline([], {
                color: '#22D3EE',
                weight: 4,
                opacity: 0.85,
                lineCap: 'round',
                lineJoin: 'round'
            }).addTo(map);

            // Geofence Circle on Map
            geofenceCircle = L.circle([0, 0], {
                radius: geofenceRadius,
                color: '#10B981',
                fillColor: '#10B981',
                fillOpacity: 0.15,
                weight: 2,
                dashArray: '6, 6'
            });

            // Detect manual panning
            map.on('dragstart zoomstart', () => {
                userHasPannedMap = true;
            });

        } catch (e) {
            console.error('Error initializing map:', e);
            document.getElementById('map-offline-overlay').style.display = 'block';
        }
    }

    // --- WEBSOCKET ENGINE ---
    function initWebSocket() {
        if (isDemoMode) return;

        const host = window.location.host || '192.168.4.1';
        const protocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
        const wsUrl = protocol + host + '/ws';

        console.log('Connecting to WebSocket:', wsUrl);
        updateBadge(elEspStatus, 'ONLINE', 'dot-green');

        try {
            websocket = new WebSocket(wsUrl);

            websocket.onopen = () => {
                console.log('WebSocket Connection Established');
                updateBadge(elEspStatus, 'ONLINE', 'dot-green');
                if (restPollTimer) clearInterval(restPollTimer);
            };

            websocket.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    processGpsData(data);
                } catch (err) {
                    console.error('Failed to parse WebSocket JSON payload:', err);
                }
            };

            websocket.onerror = (err) => {
                console.warn('WebSocket Error:', err);
                updateBadge(elEspStatus, 'OFFLINE', 'dot-red');
            };

            websocket.onclose = () => {
                console.warn('WebSocket Closed. Attempting reconnect in 3s...');
                updateBadge(elEspStatus, 'RECONNECTING', 'dot-yellow');
                
                // Fallback to REST polling while WebSocket reconnects
                startRestPolling();

                clearTimeout(wsReconnectTimer);
                wsReconnectTimer = setTimeout(() => {
                    initWebSocket();
                }, 3000);
            };
        } catch (e) {
            console.error('WebSocket Exception:', e);
            startRestPolling();
        }
    }

    // --- REST POLLING FALLBACK ---
    function startRestPolling() {
        if (restPollTimer || isDemoMode) return;
        console.log('Starting REST API polling fallback (/api/gps)...');

        restPollTimer = setInterval(() => {
            fetch('/api/gps')
                .then(res => res.json())
                .then(data => {
                    updateBadge(elEspStatus, 'ONLINE (REST)', 'dot-yellow');
                    processGpsData(data);
                })
                .catch(err => {
                    console.warn('REST API fetch error:', err);
                    updateBadge(elEspStatus, 'OFFLINE', 'dot-red');
                });
        }, 1500);
    }

    // --- GPS DATA PROCESSOR ---
    function processGpsData(data) {
        lastPacketTimestamp = Date.now();
        packetCounter++;
        elFootPackets.textContent = packetCounter;

        if (data.ip) elFootIp.textContent = data.ip;

        // Diagnostic Stream Info
        if (data.uartActive !== undefined) {
            if (data.uartActive) {
                elFootUart.textContent = `STREAMING (${data.charsProcessed || 0} bytes)`;
                elFootUart.style.color = '#10B981';
            } else {
                elFootUart.textContent = 'NO DATA RECEIVED';
                elFootUart.style.color = '#EF4444';
            }
        }

        // Update GPS Status Badge & Banner
        updateGpsStateUI(data);

        // Update Telemetry Cards
        if (data.valid) {
            elLat.textContent = `${data.latitude.toFixed(6)}°`;
            elLng.textContent = `${data.longitude.toFixed(6)}°`;
            elSpeed.textContent = `${data.speedKmh.toFixed(1)} km/h`;
            elAlt.textContent = `${data.altitude.toFixed(1)} m`;
            elSats.textContent = data.satellites;
            elHdop.textContent = data.hdop ? data.hdop.toFixed(1) : '--';
            elCourse.textContent = `${data.course ? data.course.toFixed(1) : '0.0'}°`;
            elFixType.textContent = data.fixType || 'FIXED';
            elTime.textContent = data.gpsTime || '--:--:--';
            elDate.textContent = data.gpsDate || 'YYYY-MM-DD';

            // Enable Map & Geofence Buttons
            btnCenterMap.removeAttribute('disabled');
            btnSetGeofenceCenter.removeAttribute('disabled');
            btnGmaps.classList.remove('disabled');
            btnGmaps.href = `https://www.google.com/maps?q=${data.latitude},${data.longitude}`;

            // Update Leaflet Map Position
            updateMapPosition(data.latitude, data.longitude, data.hdop);

            // Evaluate Geofence Range Alert
            evaluateGeofence(data.latitude, data.longitude);

            // Update Path Tracking
            if (isTracking) {
                addTrackPoint(data.latitude, data.longitude);
            }
        } else {
            // Invalid / No Fix State - Show '--' for numeric values (NO FAKE ZEROES)
            elLat.textContent = '--';
            elLng.textContent = '--';
            elSpeed.textContent = '--';
            elAlt.textContent = '--';
            elSats.textContent = data.satellites !== undefined ? data.satellites : 0;
            elHdop.textContent = data.hdop && data.hdop < 90 ? data.hdop.toFixed(1) : '--';
            elCourse.textContent = '--';
            elFixType.textContent = data.fixType || 'NO FIX';
            elTime.textContent = data.gpsTime || '--:--:--';
            elDate.textContent = data.gpsDate || 'YYYY-MM-DD';

            btnCenterMap.setAttribute('disabled', 'true');
            btnSetGeofenceCenter.setAttribute('disabled', 'true');
            btnGmaps.classList.add('disabled');
            btnGmaps.removeAttribute('href');

            removeMapMarker();
        }

        // Signal Quality Pill
        updateSignalQualityPill(data.quality || 'No Fix');
    }

    // --- GEOFENCE / RANGE MONITORING ENGINE ---
    function evaluateGeofence(lat, lng) {
        if (!map || !geofenceEnabled) {
            if (geofenceCircle && map && map.hasLayer(geofenceCircle)) {
                map.removeLayer(geofenceCircle);
            }
            return;
        }

        const currentPos = L.latLng(lat, lng);

        // Auto-initialize geofence center to initial GPS location if not set yet
        if (!geofenceCenter) {
            geofenceCenter = currentPos;
        }

        // Calculate exact geodesic distance in meters
        const distanceMeters = currentPos.distanceTo(geofenceCenter);
        const radiusMeters = parseFloat(numGeofenceRadius.value) || 15;
        geofenceRadius = radiusMeters;

        // Render Geofence Circle on Map
        if (!map.hasLayer(geofenceCircle)) {
            geofenceCircle.addTo(map);
        }
        geofenceCircle.setLatLng(geofenceCenter);
        geofenceCircle.setRadius(geofenceRadius);

        // Check Range Breach (Distance > Radius)
        if (distanceMeters > geofenceRadius) {
            isGeofenceBreached = true;
            const breachAmount = (distanceMeters - geofenceRadius).toFixed(1);

            // Styling for Breach State (Red)
            geofenceCircle.setStyle({
                color: '#EF4444',
                fillColor: '#EF4444',
                fillOpacity: 0.35,
                weight: 3,
                dashArray: '4, 4'
            });

            elGeofenceAlertBox.className = 'geofence-alert-box breach-state';
            elGeofenceAlertIcon.textContent = '🚨';
            elGeofenceAlertTitle.textContent = 'OUT OF RANGE BREACH ALERT!';
            elGeofenceAlertDesc.textContent = `BREACH DETECTED! Current Distance: ${distanceMeters.toFixed(1)} m | Allowed Radius: ${geofenceRadius.toFixed(1)} m (Exceeded by +${breachAmount} m)`;

            // Trigger Audio Alarm Tone
            triggerAudioAlarm();

        } else {
            isGeofenceBreached = false;

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
            default:          elQualityPill.classList.add('pill-neutral'); break;
        }
    }

    // --- MAP POSITION UPDATER ---
    function updateMapPosition(lat, lng, hdop) {
        if (!map) return;

        const pos = [lat, lng];

        if (!map.hasLayer(mapMarker)) {
            mapMarker.addTo(map);
        }
        mapMarker.setLatLng(pos);

        if (!map.hasLayer(accuracyCircle)) {
            accuracyCircle.addTo(map);
        }
        
        // Estimate accuracy radius from HDOP (HDOP * 5m approx)
        const radius = Math.max((hdop || 2) * 5, 5);
        accuracyCircle.setLatLng(pos);
        accuracyCircle.setRadius(radius);

        // Auto center map if user hasn't manually panned away
        if (!userHasPannedMap) {
            map.panTo(pos);
        }
    }

    function removeMapMarker() {
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

})();
