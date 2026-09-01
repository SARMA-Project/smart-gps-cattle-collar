#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include "config.h"
#include "gps_manager.h"
#include "wifi_manager.h"

class WebServerManager {
public:
    WebServerManager(GpsManager& gpsMgr, WifiManager& wifiMgr);
    void begin();
    void update();
    void broadcastGpsData();
    
    size_t getConnectedClientsCount() const { return m_ws.count(); }
    uint32_t getPacketsSent() const { return m_packetsSent; }

private:
    AsyncWebServer m_server;
    AsyncWebSocket m_ws;
    GpsManager& m_gpsManager;
    WifiManager& m_wifiManager;

    bool m_littleFsMounted;
    uint32_t m_lastBroadcastTime;
    uint32_t m_packetsSent;

    void setupRoutes();
    void setupWebSocket();
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);
};

#endif // WEB_SERVER_H
