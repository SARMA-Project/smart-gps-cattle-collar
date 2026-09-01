#include "gps_manager.h"

GpsManager::GpsManager()
    : m_serial(2),
      m_state(GPS_STATE_NO_DATA),
      m_lastByteTimestamp(0),
      m_charsReceivedTotal(0),
      m_hadValidFixOnce(false) {
}

void GpsManager::begin() {
    // Initialize HardwareSerial 2 on ESP32 (GPIO16 = RX2, GPIO17 = TX2)
    m_serial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    m_lastByteTimestamp = millis();
    m_state = GPS_STATE_NO_DATA;
}

void GpsManager::update() {
    while (m_serial.available() > 0) {
        char c = (char)m_serial.read();
        m_tinyGps.encode(c);
        m_charsReceivedTotal++;
        m_lastByteTimestamp = millis();
    }

    updateState();
}

void GpsManager::updateState() {
    uint32_t now = millis();
    
    // Check if we haven't received any data over UART for GPS_NO_DATA_TIMEOUT_MS
    if (now - m_lastByteTimestamp > GPS_NO_DATA_TIMEOUT_MS && m_charsReceivedTotal == 0) {
        m_state = GPS_STATE_NO_DATA;
        return;
    }

    bool locationValid = m_tinyGps.location.isValid() && 
                         m_tinyGps.location.age() < 3000 &&
                         (abs(m_tinyGps.location.lat()) > 0.000001) &&
                         (abs(m_tinyGps.location.lng()) > 0.000001) &&
                         m_tinyGps.location.lat() >= -90.0 && m_tinyGps.location.lat() <= 90.0 &&
                         m_tinyGps.location.lng() >= -180.0 && m_tinyGps.location.lng() <= 180.0;

    if (locationValid) {
        m_state = GPS_STATE_FIXED;
        m_hadValidFixOnce = true;
    } else {
        if (m_hadValidFixOnce) {
            m_state = GPS_STATE_FIX_LOST;
        } else if (m_charsReceivedTotal > 0) {
            m_state = GPS_STATE_SEARCHING;
        } else {
            m_state = GPS_STATE_NO_DATA;
        }
    }
}

GpsData GpsManager::getData() {
    GpsData data;
    data.state = m_state;
    data.valid = (m_state == GPS_STATE_FIXED);
    data.charsProcessed = m_tinyGps.charsProcessed();
    data.sentencesWithFix = m_tinyGps.sentencesWithFix();
    data.failedChecksum = m_tinyGps.failedChecksum();

    if (data.valid) {
        data.latitude = m_tinyGps.location.lat();
        data.longitude = m_tinyGps.location.lng();
        data.altitude = m_tinyGps.altitude.isValid() ? m_tinyGps.altitude.meters() : 0.0f;
        
        // 1 knot = 1.852 km/h
        data.speedKmh = m_tinyGps.speed.isValid() ? (m_tinyGps.speed.knots() * 1.852f) : 0.0f;
        data.course = m_tinyGps.course.isValid() ? m_tinyGps.course.deg() : 0.0f;
        data.satellites = m_tinyGps.satellites.isValid() ? m_tinyGps.satellites.value() : 0;
        data.hdop = m_tinyGps.hdop.isValid() ? m_tinyGps.hdop.hdop() : 99.9f;

        if (data.satellites >= 4 && data.hdop < 5.0f && m_tinyGps.altitude.isValid()) {
            data.fixType = "3D";
        } else if (data.satellites >= 3) {
            data.fixType = "2D";
        } else {
            data.fixType = "FIXED";
        }
    } else {
        data.latitude = 0.0;
        data.longitude = 0.0;
        data.altitude = 0.0f;
        data.speedKmh = 0.0f;
        data.course = 0.0f;
        data.satellites = m_tinyGps.satellites.isValid() ? m_tinyGps.satellites.value() : 0;
        data.hdop = m_tinyGps.hdop.isValid() ? m_tinyGps.hdop.hdop() : 99.9f;
        data.fixType = (m_state == GPS_STATE_SEARCHING) ? "SEARCHING" : 
                       (m_state == GPS_STATE_FIX_LOST) ? "FIX LOST" : "NO FIX";
    }

    data.gpsTime = formatTime();
    data.gpsDate = formatDate();

    return data;
}

const char* GpsManager::getStateString() const {
    switch (m_state) {
        case GPS_STATE_NO_DATA:   return "NO GPS DATA";
        case GPS_STATE_SEARCHING: return "SEARCHING FOR SATELLITES";
        case GPS_STATE_FIXED:     return "GPS FIXED";
        case GPS_STATE_FIX_LOST:  return "GPS FIX LOST";
        default:                  return "UNKNOWN";
    }
}

String GpsManager::getSignalQualityString() {
    if (m_state != GPS_STATE_FIXED) {
        return "No Fix";
    }
    
    uint32_t sats = m_tinyGps.satellites.isValid() ? m_tinyGps.satellites.value() : 0;
    float hdop = m_tinyGps.hdop.isValid() ? m_tinyGps.hdop.hdop() : 99.9f;

    if (sats >= 8 && hdop <= 1.5f) {
        return "Excellent";
    } else if (sats >= 6 && hdop <= 2.5f) {
        return "Good";
    } else if (sats >= 4 && hdop <= 4.0f) {
        return "Fair";
    } else if (sats > 0) {
        return "Poor";
    } else {
        return "No Fix";
    }
}

String GpsManager::formatTime() {
    if (!m_tinyGps.time.isValid()) {
        return "--:--:--";
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d UTC",
             m_tinyGps.time.hour(),
             m_tinyGps.time.minute(),
             m_tinyGps.time.second());
    return String(buf);
}

String GpsManager::formatDate() {
    if (!m_tinyGps.date.isValid()) {
        return "----/--/--";
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             m_tinyGps.date.year(),
             m_tinyGps.date.month(),
             m_tinyGps.date.day());
    return String(buf);
}

String GpsManager::toJsonString() {
    GpsData data = getData();
    StaticJsonDocument<512> doc;

    doc["valid"] = data.valid;
    doc["state"] = getStateString();
    doc["quality"] = getSignalQualityString();

    if (data.valid) {
        doc["latitude"] = data.latitude;
        doc["longitude"] = data.longitude;
        doc["altitude"] = data.altitude;
        doc["speedKmh"] = data.speedKmh;
        doc["course"] = data.course;
        doc["satellites"] = data.satellites;
        doc["hdop"] = data.hdop;
        doc["fixType"] = data.fixType;
        doc["gpsTime"] = data.gpsTime;
        doc["gpsDate"] = data.gpsDate;
    } else {
        doc["latitude"] = nullptr;
        doc["longitude"] = nullptr;
        doc["altitude"] = nullptr;
        doc["speedKmh"] = nullptr;
        doc["course"] = nullptr;
        doc["satellites"] = data.satellites;
        doc["hdop"] = (m_tinyGps.hdop.isValid()) ? data.hdop : 99.9;
        doc["fixType"] = data.fixType;
        doc["gpsTime"] = data.gpsTime;
        doc["gpsDate"] = data.gpsDate;
    }

    doc["charsProcessed"] = data.charsProcessed;
    doc["failedChecksum"] = data.failedChecksum;
    doc["uartActive"] = (m_charsReceivedTotal > 0);

    String jsonOutput;
    serializeJson(doc, jsonOutput);
    return jsonOutput;
}
