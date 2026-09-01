#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

enum WifiModeStatus {
    WIFI_MODE_DISCONNECTED = 0,
    WIFI_MODE_STA_CONNECTED,
    WIFI_MODE_AP_FALLBACK
};

class WifiManager {
public:
    WifiManager();
    void begin();
    void update();

    bool isConnectedToStation() const { return m_status == WIFI_MODE_STA_CONNECTED; }
    bool isApMode() const { return m_status == WIFI_MODE_AP_FALLBACK; }
    IPAddress getIpAddress() const;
    String getIpString() const;
    const char* getModeString() const;
    WifiModeStatus getStatus() const { return m_status; }

private:
    WifiModeStatus m_status;
    uint32_t m_lastReconnectCheck;
    void startAccessPoint();
};

#endif // WIFI_MANAGER_H
