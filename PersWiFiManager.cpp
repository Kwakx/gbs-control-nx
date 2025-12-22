/* PersWiFiManager
   version 3.0.1
   https://r-downing.github.io/PersWiFiManager/

   modified for inclusion in gbs-control
   see /3rdparty/PersWiFiManager/ for original code and license
*/
#include "PersWiFiManager.h"

#ifdef ESP32
#include <esp_task_wdt.h>
static const char wifi_htm[] PROGMEM = "NOT_USED"; // Dummy to satisfy unused variable warning if needed, or just standard headers.
#endif

extern const char *device_hostname_full;
extern const char *device_hostname_partial;
// Handled in header/source already


PersWiFiManager::PersWiFiManager(AsyncWebServer &s, DNSServer &d)
{
    _server = &s;
    _dnsServer = &d;
    _apPass = "";
    _lastConnectionCheck = 0;
    _connectionLostTime = 0;
} //PersWiFiManager

bool PersWiFiManager::attemptConnection(const String &ssid, const String &pass)
{
    //attempt to connect to wifi
    // Configure WiFi settings to avoid ESP8266 Core 3.1.x bug / flash wear.
    // IMPORTANT: On ESP32, WiFi.persistent(false) changes the underlying storage to RAM
    // (affects loading of saved credentials), so we only touch persistence on ESP8266.
#ifdef ESP8266
    // Enable persistence only when explicitly saving new credentials
    if (ssid.length() > 0) {
        WiFi.persistent(true);  // Save new credentials to flash
    } else {
        WiFi.persistent(false); // Use saved credentials without re-writing
    }
#endif
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(true);
    
    // Ensure WiFi hardware is fully awake and ready
#ifdef ESP8266
    WiFi.forceSleepWake();
#endif
    delay(100);
    
    // Properly disconnect before mode change
    WiFi.disconnect(false); // false = don't erase credentials
    delay(200); // Increased delay for stability
    WiFi.mode(WIFI_STA);
    delay(200); // Increased delay for stability
#ifdef ESP8266
    WiFi.hostname(device_hostname_partial); // _full // before WiFi.begin();
#else
    WiFi.setHostname(device_hostname_partial);
#endif
    delay(100); // Give time for hostname to be set
    
    if (ssid.length()) {
        if (pass.length())
            WiFi.begin(ssid.c_str(), pass.c_str());
        else
            WiFi.begin(ssid.c_str());
        // Disable persistence after saving (ESP8266 only)
#ifdef ESP8266
        WiFi.persistent(false);
#endif
    } else {
        WiFi.begin();
    }

    //if in nonblock mode, skip this loop
    _connectStartTime = millis(); // + 1;
    while (!_connectNonBlock && _connectStartTime) {
        handleWiFi();
        delay(10);
#ifdef ESP8266
        // Keep wifi active
        // delay(0); // already done by delay(10)
#endif
    }

    return (WiFi.status() == WL_CONNECTED);

} //attemptConnection

void PersWiFiManager::handleWiFi()
{
    if (!_connectStartTime) {
        // Routine checks when not in initial connection phase
        if (WiFi.getMode() == WIFI_STA) {
            if (WiFi.status() == WL_CONNECTED) {
                _connectionLostTime = 0; // Reset counter
            } else {
                // Not connected
                if (_connectionLostTime == 0) {
                    _connectionLostTime = millis();
                }
                
                // If disconnected for more than 30 seconds, try to restart WiFi
                if ((millis() - _connectionLostTime) > 30000) {
                     _connectionLostTime = 0;
                     WiFi.reconnect();
                }
            }
        }
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        _connectStartTime = 0;
        if (_connectHandler)
            _connectHandler();
        return;
    }

    //if failed or not connected and time is up
    if ((WiFi.status() == WL_CONNECT_FAILED) || ((WiFi.status() != WL_CONNECTED) && ((millis() - _connectStartTime) > (1000 * WIFI_CONNECT_TIMEOUT)))) {
        startApMode();
        _connectStartTime = 0; //reset connect start time
    }

} //handleWiFi

void PersWiFiManager::startApMode()
{
    //start AP mode
    IPAddress apIP(192, 168, 4, 1);
#ifdef ESP8266
    WiFi.disconnect(true); // true = erase STA credentials from this session
#else
    WiFi.disconnect(true, true); // true = erase STA credentials
#endif
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
#ifdef ESP8266
    _apPass.length() ? WiFi.softAP(getApSsid().c_str(), _apPass.c_str(), 11) : WiFi.softAP(getApSsid().c_str());
#else
    _apPass.length() ? WiFi.softAP(getApSsid().c_str(), _apPass.c_str(), 11, 0, 4) : WiFi.softAP(getApSsid().c_str());
#endif

    _dnsServer->stop();
    delay(50);
    // set which return code will be used for all other domains (e.g. sending
    // ServerFailure instead of NonExistentDomain will reduce number of queries
    // sent by clients)
    // default is DNSReplyCode::NonExistentDomain
    //_dnsServer->setErrorReplyCode(DNSReplyCode::ServerFailure);
    // modify TTL associated  with the domain name (in seconds) // default is 60 seconds
    _dnsServer->setTTL(300); // (in seconds) as per example
    //_dnsServer->start((byte)53, device_hostname_full, apIP);
    _dnsServer->start(53, "*", apIP);

    if (_apHandler)
        _apHandler();
} //startApMode

void PersWiFiManager::setConnectNonBlock(bool b)
{
    _connectNonBlock = b;
} //setConnectNonBlock

void PersWiFiManager::setupWiFiHandlers()
{
    // note: removed DNS server setup here

    _server->on("/wifi/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        //  -2: no scan started, -1: scan running, >=0: scan finished with N networks
        int n = WiFi.scanComplete();
        String s = "";

        if (n == -2) {
            // Start scan asynchronously; web UI polls this endpoint.
            WiFi.scanNetworks(true);
        } else if (n > 0) {
            //build array of indices
            int ix[n];
            for (int i = 0; i < n; i++)
                ix[i] = i;

            //sort by signal strength
            for (int i = 0; i < n; i++)
                for (int j = 1; j < n - i; j++)
                    if (WiFi.RSSI(ix[j]) > WiFi.RSSI(ix[j - 1]))
                        std::swap(ix[j], ix[j - 1]);
            //remove duplicates
            for (int i = 0; i < n; i++)
                for (int j = i + 1; j < n; j++)
                    if (WiFi.SSID(ix[i]).equals(WiFi.SSID(ix[j])) && WiFi.encryptionType(ix[i]) == WiFi.encryptionType(ix[j]))
                        ix[j] = -1;

            s.reserve(2050);
            for (int i = 0; i < n && s.length() < 2000; i++) { //check s.length to limit memory usage
                if (ix[i] != -1) {
#ifdef ESP32
                    s += String(i ? "\n" : "") + ((constrain(WiFi.RSSI(ix[i]), -100, -50) + 100) * 2) + "," + ((WiFi.encryptionType(ix[i]) == WIFI_AUTH_OPEN) ? 0 : 1) + "," + WiFi.SSID(ix[i]);
#else
                    s += String(i ? "\n" : "") + ((constrain(WiFi.RSSI(ix[i]), -100, -50) + 100) * 2) + "," + ((WiFi.encryptionType(ix[i]) == ENC_TYPE_NONE) ? 0 : 1) + "," + WiFi.SSID(ix[i]);
#endif
                }
            }
            // don't cache found ssid's
            WiFi.scanDelete();
        } else if (n < -1) {
            // Unexpected error code; keep response empty to allow next poll
            // (older code treated -2 as "start scan").
        } else {
            // n == -1: scan still running; keep response empty
        }
        //send string to client
        request->send(200, "text/plain", s);
    }); //_server->on /wifi/list

    // #ifdef WIFI_HTM_PROGMEM
    //   _server->on("/wifi.htm", HTTP_GET, [](AsyncWebServerRequest* request) {
    //     request->send(200, "text/html", FPSTR(wifi_htm));
    //     WiFi.scanNetworks(true); // early scan to have results ready on /wifi/list
    //   });
    // #endif

} //setupWiFiHandlers

bool PersWiFiManager::begin(const String &ssid, const String &pass)
{
    setupWiFiHandlers();
    return attemptConnection(ssid, pass); //switched order of these two for return
} //begin

String PersWiFiManager::getApSsid()
{
    return _apSsid.length() ? _apSsid : "gbscontrol";
} //getApSsid

void PersWiFiManager::setApCredentials(const String &apSsid, const String &apPass)
{
    if (apSsid.length())
        _apSsid = apSsid;
    if (apPass.length() >= 8)
        _apPass = apPass;
} //setApCredentials

void PersWiFiManager::onConnect(WiFiChangeHandlerFunction fn)
{
    _connectHandler = fn;
}

void PersWiFiManager::onAp(WiFiChangeHandlerFunction fn)
{
    _apHandler = fn;
}
