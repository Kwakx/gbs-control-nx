#ifndef ROTARY_ENCODER_H
#define ROTARY_ENCODER_H

#include <Arduino.h>
#include "../config/Config.h"

// Rotary encoder ISR functions
#if USE_NEW_OLED_MENU
void IRAM_ATTR isrRotaryEncoderRotateForNewMenu();
void IRAM_ATTR isrRotaryEncoderPushForNewMenu();
#else
void IRAM_ATTR isrRotaryEncoder();
#endif

#endif // ROTARY_ENCODER_H

