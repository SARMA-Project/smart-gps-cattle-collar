#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>
#include <ArduinoJson.h>
#include "config.h"

enum GpsState {
    GPS_STATE_NO_DATA = 0,
    GPS_STATE_SEARCHING,
    GPS_STATE_FIXED,
    GPS_STATE_FIX_LOST
};

struct GpsData {
    bool valid;
    double latitude;
    double longitude;
    float altitude;     // meters
    float speedKmh;     // km/h (converted from knots)
    float course;       // degrees
    uint32_t satellites;
    float hdop;
    String fixType;     // "NO FIX", "2D FIX", "3D FIX"
    String gpsTime;     // "HH:MM:SS" UTC
    String gpsDate;     // "YYYY-MM-DD" UTC
    GpsState state;
    uint32_t charsProcessed;
    uint32_t sentencesWithFix;
    uint32_t failedChecksum;
};

class GpsManager {
public:
    GpsManager();
    void begin();
    void update();
    
    GpsData getData();
    GpsState getState() const { return m_state; }
    const char* getStateString() const;
    String getSignalQualityString();
    String toJsonString();

    bool hasHardwareDataReceived() const { return m_charsReceivedTotal > 0; }
    uint32_t getCharsProcessed() const { return m_tinyGps.charsProcessed(); }

private:
    HardwareSerial m_serial;
    TinyGPSPlus m_tinyGps;
    GpsState m_state;
    uint32_t m_lastByteTimestamp;
    uint32_t m_charsReceivedTotal;
    bool m_hadValidFixOnce;

    void updateState();
    String formatTime();
    String formatDate();
};

#endif // GPS_MANAGER_H
