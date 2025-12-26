#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <Arduino.h>

// LED control macros for ESP8266 and ESP32
#ifdef ESP8266
#ifdef LEDON
#undef LEDON
#endif
#ifdef LEDOFF
#undef LEDOFF
#endif
#define LEDON                     \
    pinMode(LED_BUILTIN, OUTPUT); \
    digitalWrite(LED_BUILTIN, LOW)
#define LEDOFF                       \
    digitalWrite(LED_BUILTIN, HIGH); \
    pinMode(LED_BUILTIN, INPUT)
#elif defined(ESP32)
// ESP32: LED on D2 (GPIO 2) is active HIGH (inverted logic compared to ESP8266)
#ifdef LED_BUILTIN
#ifdef LEDON
#undef LEDON
#endif
#ifdef LEDOFF
#undef LEDOFF
#endif
#define LEDON                     \
    pinMode(LED_BUILTIN, OUTPUT); \
    digitalWrite(LED_BUILTIN, HIGH)
#define LEDOFF                       \
    pinMode(LED_BUILTIN, OUTPUT); \
    digitalWrite(LED_BUILTIN, LOW)
#else
#ifdef LEDON
#undef LEDON
#endif
#ifdef LEDOFF
#undef LEDOFF
#endif
#define LEDON
#define LEDOFF
#endif
#else
// Other platforms (not ESP8266, not ESP32)
#ifdef LED_BUILTIN
#ifdef LEDON
#undef LEDON
#endif
#ifdef LEDOFF
#undef LEDOFF
#endif
#define LEDON                     \
    pinMode(LED_BUILTIN, OUTPUT); \
    digitalWrite(LED_BUILTIN, LOW)
#define LEDOFF                       \
    digitalWrite(LED_BUILTIN, HIGH); \
    pinMode(LED_BUILTIN, INPUT)
#else
#ifdef LEDON
#undef LEDON
#endif
#ifdef LEDOFF
#undef LEDOFF
#endif
#define LEDON
#define LEDOFF
#endif
#endif

#endif // LED_CONTROL_H

