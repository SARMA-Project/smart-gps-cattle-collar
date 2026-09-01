#include "wifi_manager.h"

WifiManager::WifiManager()
    : m_status(WIFI_MODE_DISCONNECTED),
      m_lastReconnectCheck(0) {
}

void WifiManager::begin() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    delay(100);

    bool tryStaMode = (WIFI_SSID != nullptr && 
                       strlen(WIFI_SSID) > 0 && 
                       strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0);

    if (tryStaMode) {
        Serial.print("WiFi: CONNECTING to SSID '");
        Serial.print(WIFI_SSID);
        Serial.println("'...");

        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        uint32_t startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt < WIFI_STA_TIMEOUT_MS)) {
            delay(250);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            m_status = WIFI_MODE_STA_CONNECTED;
            Serial.println("WiFi: CONNECTED (Station Mode)");
            Serial.print("WiFi: IP ADDRESS: ");
            Serial.println(WiFi.localIP());
            return;
        } else {
            Serial.println("WiFi: Station Mode connection failed / timed out.");
        }
    } else {
        Serial.println("WiFi: No Station SSID configured. Starting Access Point mode directly.");
    }

    startAccessPoint();
}

void WifiManager::startAccessPoint() {
    Serial.println("WiFi: STARTING ACCESS POINT FALLBACK...");
    WiFi.mode(WIFI_AP);
    
    WiFi.softAPConfig(AP_LOCAL_IP, AP_GATEWAY, AP_SUBNET);
    bool apCreated = WiFi.softAP(AP_SSID, AP_PASSWORD);

    if (apCreated) {
        m_status = WIFI_MODE_AP_FALLBACK;
        Serial.println("WiFi: AP MODE ACTIVATED");
        Serial.print("WiFi: AP SSID: ");
        Serial.println(AP_SSID);
        Serial.print("WiFi: AP PASSWORD: ");
        Serial.println(AP_PASSWORD);
        Serial.print("WiFi: AP IP ADDRESS: ");
        Serial.println(WiFi.softAPIP());
    } else {
        m_status = WIFI_MODE_DISCONNECTED;
        Serial.println("WiFi: ERROR - Failed to initialize Access Point!");
    }
}

void WifiManager::update() {
    uint32_t now = millis();
    if (now - m_lastReconnectCheck < 5000) {
        return;
    }
    m_lastReconnectCheck = now;

    if (m_status == WIFI_MODE_STA_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi: Station connection lost! Switching to AP fallback mode...");
            startAccessPoint();
        }
    }
}

IPAddress WifiManager::getIpAddress() const {
    if (m_status == WIFI_MODE_STA_CONNECTED) {
        return WiFi.localIP();
    } else if (m_status == WIFI_MODE_AP_FALLBACK) {
        return WiFi.softAPIP();
    }
    return IPAddress(0, 0, 0, 0);
}

String WifiManager::getIpString() const {
    return getIpAddress().toString();
}

const char* WifiManager::getModeString() const {
    switch (m_status) {
        case WIFI_MODE_STA_CONNECTED: return "CONNECTED";
        case WIFI_MODE_AP_FALLBACK:    return "AP MODE";
        case WIFI_MODE_DISCONNECTED:  return "DISCONNECTED";
        default:                      return "UNKNOWN";
    }
}
