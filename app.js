/**
 * Smart GPS Live Tracking Dashboard Client
 * Real-time Hardware Telemetry & Geofence Monitor
 */

(function () {

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
