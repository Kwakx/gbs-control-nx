#include "Globals.h"
#include "../config/Config.h"
#include "../config/WiFiConfig.h"

// Global object instances
SSD1306Wire display(0x3c, SDA_PIN, SCL_PIN); //inits I2C address & pins for OLED

#if USE_NEW_OLED_MENU
OLEDMenuManager oledMenu(&display);
OSDManager osdManager;
volatile OLEDMenuNav oledNav = OLEDMenuNav::IDLE;
volatile uint8_t rotaryIsrID = 0;
#else
String oled_menu[4] = {"Resolutions", "Presets", "Misc.", "Current Settings"};
String oled_Resolutions[7] = {"1280x960", "1280x1024", "1280x720", "1920x1080", "480/576", "Downscale", "Pass-Through"};
String oled_Presets[8] = {"1", "2", "3", "4", "5", "6", "7", "Back"};
String oled_Misc[4] = {"Reset GBS", "Restore Factory", "-----Back"};

int oled_menuItem = 1;
int oled_subsetFrame = 0;
int oled_selectOption = 0;
int oled_page = 0;

int oled_lastCount = 0;
volatile int oled_encoder_pos = 0;
volatile int oled_main_pointer = 0; // volatile vars change done with ISR
volatile int oled_pointer_count = 0;
volatile int oled_sub_pointer = 0;
#endif

unsigned long lastVsyncLock = millis();

// Si5351mcu instance
Si5351mcu Si;

// WiFi configuration is in WiFiConfig.h

AsyncWebServer server(80);
DNSServer dnsServer;
WebSocketsServer webSocket(81);
PersWiFiManager persWM(server, dnsServer);

// State structures
struct runTimeOptions rtos;
struct runTimeOptions *rto = &rtos;
struct userOptions uopts;
struct userOptions *uopt = &uopts;
struct adcOptions adcopts;
struct adcOptions *adco = &adcopts;

String slotIndexMap = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~()!*:,";

char serialCommand;               // Serial / Web Server commands
String serialCommandBuffer = "";  // Buffering for Web Server commands
char userCommand;               // Serial / Web Server commands
String userCommandBuffer = "";  // Buffering for Web Server commands
// lastSegment is defined in src/gbs/GBSRegister.cpp

// Best observed Si5351 XTAL load capacitance setting from detection/init.
uint8_t g_si5351_best_xtal_cl = 0xD2; // 10pF default

// SerialMirror instance (will be moved to SerialMirror.cpp)
// extern SerialMirror SerialM; - will be defined in SerialMirror.cpp

// WiFi event handlers
#ifdef ESP8266
WiFiEventHandler disconnectedEventHandler;
#endif

// Pinger library (optional)
#ifdef HAVE_PINGER_LIBRARY
unsigned long pingLastTime;
Pinger pinger;
#endif

