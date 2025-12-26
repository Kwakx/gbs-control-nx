#ifndef VIDEO_INPUT_H
#define VIDEO_INPUT_H

#include <Arduino.h>
#include "../core/State.h"

// Video input detection and switching
uint8_t inputAndSyncDetect();
uint8_t detectAndSwitchToActiveInput();
void optimizeSogLevel();
void goLowPowerWithInputDetection();
boolean optimizePhaseSP();
uint8_t getVideoMode();
boolean getSyncPresent();
boolean getStatus16SpHsStable();
boolean getStatus00IfHsVsStable();

#endif // VIDEO_INPUT_H

