#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <Arduino.h>

// WiFi configuration
#define THIS_DEVICE_MASTER

// External declarations - definitions are in WiFiConfig.cpp
extern const char *ap_ssid;
extern const char *ap_password;
extern const char *device_hostname_full;
extern const char *device_hostname_partial;

extern const char ap_info_string[] PROGMEM;
extern const char st_info_string[] PROGMEM;

#endif // WIFI_CONFIG_H

