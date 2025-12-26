#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Pin definitions for ESP8266 and ESP32
#ifdef ESP32
const int SDA_PIN = 21;
const int SCL_PIN = 22;
const int pin_clk = 33;
const int pin_data = 32;
const int pin_switch = 14;
#else
const int SDA_PIN = D2;
const int SCL_PIN = D1;
const int pin_clk = 14;            //D5 = GPIO14 (input of one direction for encoder)
const int pin_data = 13;           //D7 = GPIO13	(input of one direction for encoder)
const int pin_switch = 0;          //D3 = GPIO0 pulled HIGH, else boot fail (middle push button for encoder)
#endif

// Feature flags
#define HAVE_BUTTONS 0
#define USE_NEW_OLED_MENU 1

// Debug pin
#ifdef ESP32
#ifdef DEBUG_IN_PIN
#undef DEBUG_IN_PIN
#endif
#define DEBUG_IN_PIN 27
#ifndef LED_BUILTIN
#define LED_BUILTIN 2  // D2 pin on ESP32 (GPIO 2)
#endif
#else
#ifdef DEBUG_IN_PIN
#undef DEBUG_IN_PIN
#endif
#define DEBUG_IN_PIN D6 // marked "D12/MISO/D6" (Wemos D1) or D6 (Lolin NodeMCU)
#endif

#endif // CONFIG_H

