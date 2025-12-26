#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "../../lib/WebSockets/WebSocketsServer.h"
#include "../../lib/PersWiFiManager/PersWiFiManager.h"
#include <SSD1306Wire.h>
#include "../menu/OLEDMenuImplementation.h"
#include "../menu/OSDManager.h"
#include "../../lib/si5351mcu/si5351mcu.h"
#include "options.h"
#include "../gbs/tv5725.h"
#include "../menu/osd.h"
#include "../video/framesync.h"
#include "../config/Config.h"
#include "../web/SerialMirror.h"

// Forward declarations (if needed for other types)

// Type aliases for Menu and FrameSync
// These are defined here so they can be used in other source files
typedef TV5725<GBS_ADDR> GBS;

struct MenuAttrs
{
    static const int8_t shiftDelta = 4;
    static const int8_t scaleDelta = 4;
    static const int16_t vertShiftRange = 300;
    static const int16_t horizShiftRange = 400;
    static const int16_t vertScaleRange = 100;
    static const int16_t horizScaleRange = 130;
    static const int16_t barLength = 100;
};
typedef MenuManager<GBS, MenuAttrs> Menu;

struct FrameSyncAttrs
{
    static const uint8_t debugInPin = DEBUG_IN_PIN;
    static const uint32_t lockInterval = 100 * 16.70; // every 100 frames
    static const int16_t syncCorrection = 2;          // Sync correction in scanlines to apply when phase lags target
    static const int32_t syncTargetPhase = 90;        // Target vsync phase offset (output trails input) in degrees
};
typedef FrameSyncManager<GBS, FrameSyncAttrs> FrameSync;

// Global object instances
extern SSD1306Wire display;
extern AsyncWebServer server;
extern DNSServer dnsServer;
extern WebSocketsServer webSocket;
extern PersWiFiManager persWM;
extern Si5351mcu Si;
extern SerialMirror SerialM;

// OLED Menu (new system)
#if USE_NEW_OLED_MENU
extern OLEDMenuManager oledMenu;
extern OSDManager osdManager;
extern volatile OLEDMenuNav oledNav;
extern volatile uint8_t rotaryIsrID;
#else
extern String oled_menu[4];
extern String oled_Resolutions[7];
extern String oled_Presets[8];
extern String oled_Misc[4];
extern int oled_menuItem;
extern int oled_subsetFrame;
extern int oled_selectOption;
extern int oled_page;
extern int oled_lastCount;
extern volatile int oled_encoder_pos;
extern volatile int oled_main_pointer;
extern volatile int oled_pointer_count;
extern volatile int oled_sub_pointer;
#endif

// State structures
extern struct runTimeOptions rtos;
extern struct runTimeOptions *rto;
extern struct userOptions uopts;
extern struct userOptions *uopt;
extern struct adcOptions adcopts;
extern struct adcOptions *adco;

// Enum PresetID
enum PresetID : uint8_t {
    PresetHdBypass = 0x21,
    PresetBypassRGBHV = 0x22,
};

// Serial commands
extern char serialCommand;
extern String serialCommandBuffer;
extern char userCommand;
extern String userCommandBuffer;

// Other global variables
extern String slotIndexMap;
extern unsigned long lastVsyncLock;
extern uint8_t lastSegment;
extern uint8_t g_si5351_best_xtal_cl;

// WiFi configuration (defined in WiFiConfig.h)
extern const char *ap_ssid;
extern const char *ap_password;
extern const char *device_hostname_full;
extern const char *device_hostname_partial;

// WiFi event handlers
#ifdef ESP8266
#include <ESP8266WiFi.h>
extern WiFiEventHandler disconnectedEventHandler;
#endif

// Pinger library (optional)
#ifdef HAVE_PINGER_LIBRARY
#include <Pinger.h>
extern unsigned long pingLastTime;
extern Pinger pinger;
#endif

#endif // GLOBALS_H

