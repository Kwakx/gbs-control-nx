#include "WiFiManager.h"
#include "../core/Globals.h"
#include "../web/WebSocketHandler.h"
#include <ArduinoOTA.h>
#include <DNSServer.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#else
#include <WiFi.h>
#include <ESPmDNS.h>
#endif

void handleWiFi(boolean instant)
{
    static unsigned long lastTimePing = millis();
    if (rto->webServerEnabled && rto->webServerStarted) {
#ifdef ESP8266
        MDNS.update();
#endif

#ifdef ESP32
        webSocket.loop();
#endif
        persWM.handleWiFi(); // if connected, returns instantly. otherwise it reconnects or opens AP

        // Only process DNS requests if we are in AP mode (Captive Portal)
        // Attempting to process requests in STA mode (where DNSServer is stopped) causes UDP errors on ESP32
        if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
            dnsServer.processNextRequest();
        }

        if ((millis() - lastTimePing) > 953) { // slightly odd value so not everything happens at once
            // Note: ping/pong now handled by webSocket.enableHeartbeat() in setup()
        }
        if (((millis() - lastTimePing) > 973) || instant) {
            if ((webSocket.connectedClients(false) > 0) || instant) { // true = with compliant ping
                updateWebSocketData();
            }
            lastTimePing = millis();
        }
    }

    if (rto->allowUpdatesOTA) {
        ArduinoOTA.handle();
    }
    yield();
}

