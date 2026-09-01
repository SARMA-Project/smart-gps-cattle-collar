#include "web_server.h"

WebServerManager::WebServerManager(GpsManager& gpsMgr, WifiManager& wifiMgr)
    : m_server(HTTP_PORT),
      m_ws(WEBSOCKET_PATH),
      m_gpsManager(gpsMgr),
      m_wifiManager(wifiMgr),
      m_littleFsMounted(false),
      m_lastBroadcastTime(0),
      m_packetsSent(0) {
}

void WebServerManager::begin() {
    // Mount LittleFS
    if (LittleFS.begin(true)) {
        m_littleFsMounted = true;
        Serial.println("LittleFS: Mounted successfully");
    } else {
        m_littleFsMounted = false;
        Serial.println("LittleFS: Failed to mount. Embedded fallback assets will be used.");
    }

    setupWebSocket();
    setupRoutes();

    m_server.begin();
    Serial.println("WEB SERVER: STARTED on port 80");
}

void WebServerManager::setupWebSocket() {
    m_ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWsEvent(server, client, type, arg, data, len);
    });
    m_server.addHandler(&m_ws);
}

void WebServerManager::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WebSocket: Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        // Immediately send current GPS state on connect
        String jsonPayload = m_gpsManager.toJsonString();
        client->text(jsonPayload);
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WebSocket: Client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            // Received ping or command from client
            if (len >= 4 && memcmp(data, "ping", 4) == 0) {
                client->text("{\"type\":\"pong\"}");
            }
        }
    }
}

void WebServerManager::setupRoutes() {
    // REST API /api/gps
    m_server.on("/api/gps", HTTP_GET, [this](AsyncWebServerRequest* request) {
        String jsonPayload = m_gpsManager.toJsonString();
        request->send(200, "application/json", jsonPayload);
    });

    // REST API /api/status
    m_server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        StaticJsonDocument<256> doc;
        doc["esp32"] = "ONLINE";
        doc["wifiMode"] = m_wifiManager.getModeString();
        doc["ip"] = m_wifiManager.getIpString();
        doc["uptimeSec"] = millis() / 1000;
        doc["wsClients"] = m_ws.count();
        doc["packetsSent"] = m_packetsSent;
        doc["gpsState"] = m_gpsManager.getStateString();

        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    // Serve index.html
    m_server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (m_littleFsMounted && LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send(LittleFS, "/index.html", "text/html");
        }
    });

    // Serve LittleFS static files directly
    if (m_littleFsMounted) {
        m_server.serveStatic("/", LittleFS, "/");
    }

    // CORS headers for local development testing
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // 404 handler
    m_server.onNotFound([](AsyncWebServerRequest* request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404, "text/plain", "404: Not Found");
        }
    });
}

void WebServerManager::update() {
    uint32_t now = millis();
    if (now - m_lastBroadcastTime >= BROADCAST_INTERVAL_MS) {
        m_lastBroadcastTime = now;
        broadcastGpsData();
    }
    
    // Clean up websocket clients
    m_ws.cleanupClients();
}

void WebServerManager::broadcastGpsData() {
    if (m_ws.count() == 0) {
        return;
    }
    
    String jsonPayload = m_gpsManager.toJsonString();
    m_ws.textAll(jsonPayload);
    m_packetsSent++;
}
