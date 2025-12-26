#ifndef SI5351_MANAGER_H
#define SI5351_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include "../core/State.h"

// Si5351 External Clock Generator Management
void externalClockGenDetectAndInitialize();
void externalClockGenResetClock();
void externalClockGenSyncInOutRate();
uint32_t getPllRate();

// Global variable for best XTAL load capacitance
extern uint8_t g_si5351_best_xtal_cl;

#endif // SI5351_MANAGER_H

