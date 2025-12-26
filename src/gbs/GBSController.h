#ifndef GBS_CONTROLLER_H
#define GBS_CONTROLLER_H

#include <Arduino.h>
#include "../core/State.h"
#include "../video/VideoInput.h"

#define AUTO_GAIN_INIT 0x48

// Main GBS controller functions
void applyPresets(uint8_t result);
void doPostPresetLoadSteps();
void resetPLLAD();
void latchPLLAD();
void resetPLL();
void ResetSDRAM();
void resetDigital();
void resetSyncProcessor();
void resetModeDetect();
void unfreezeVideo();
void freezeVideo();
// getVideoMode(), getSyncPresent(), getStatus16SpHsStable(), getStatus00IfHsVsStable() are declared in VideoInput.h
void setOverSampleRatio(uint8_t newRatio, boolean prepareOnly);
void setOutModeHdBypass(bool regsInitialized);
void bypassModeSwitch_RGBHV();
float getSourceFieldRate(boolean useSPBus);
float getOutputFrameRate();
void setIfHblankParameters();

#endif // GBS_CONTROLLER_H

