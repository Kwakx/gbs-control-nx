#include "WiFiConfig.h"
#include <Arduino.h>

// WiFi configuration definitions
#ifdef THIS_DEVICE_MASTER
const char *ap_ssid = "gbscontrol";
const char *ap_password = "qqqqqqqq";
const char *device_hostname_full = "gbscontrol.local";
const char *device_hostname_partial = "gbscontrol"; // for MDNS

const char ap_info_string[] PROGMEM =
    "(WiFi): AP mode (SSID: gbscontrol, pass 'qqqqqqqq'): Access 'gbscontrol.local' in your browser";
const char st_info_string[] PROGMEM =
    "(WiFi): Access 'http://gbscontrol:80' or 'http://gbscontrol.local' (or device IP) in your browser";
#else
const char *ap_ssid = "gbsslave";
const char *ap_password = "qqqqqqqq";
const char *device_hostname_full = "gbsslave.local";
const char *device_hostname_partial = "gbsslave"; // for MDNS

const char ap_info_string[] PROGMEM =
    "(WiFi): AP mode (SSID: gbsslave, pass 'qqqqqqqq'): Access 'gbsslave.local' in your browser";
const char st_info_string[] PROGMEM =
    "(WiFi): Access 'http://gbsslave:80' or 'http://gbsslave.local' (or device IP) in your browser";
#endif

