#include "I2CManager.h"
#include "../config/Config.h"
#include <Wire.h>

void stopWire()
{
    pinMode(SCL_PIN, INPUT);
    pinMode(SDA_PIN, INPUT);
    delayMicroseconds(80);
}

void startWire()
{
    Wire.begin(SDA_PIN, SCL_PIN);
    // On ESP8266 we can override pin modes to disable internal pullups.
    // On ESP32, forcing pinMode after Wire.begin() can interfere with the HW I2C peripheral
    // and lead to flaky transactions / partially-written Si5351 registers.
#ifndef ESP32
    // The i2c wire library sets pullup resistors on by default.
    // Disable these to detect/work with GBS onboard pullups
    pinMode(SCL_PIN, OUTPUT_OPEN_DRAIN);
    pinMode(SDA_PIN, OUTPUT_OPEN_DRAIN);
#endif
    // no issues even at 700k, requires ESP8266 160Mhz CPU clock, else (80Mhz) uses 400k in library
    // no problem with Si5351 at 700k either
#ifdef ESP8266
    Wire.setClock(400000);
#else
    // ESP32: 700kHz for faster I2C communication with Si5351
    Wire.setClock(700000);
#endif
}

