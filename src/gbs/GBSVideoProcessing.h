#ifndef GBS_VIDEO_PROCESSING_H
#define GBS_VIDEO_PROCESSING_H

#include <Arduino.h>
#include "../core/State.h"

// GBS video processing functions
void setAdcGain(uint8_t gain);
void setAdcParametersGainAndOffset();
void applyYuvPatches();
void applyRGBPatches();
void OutputComponentOrVGA();
void applyComponentColorMixing();
void toggleIfAutoOffset();
void runAutoGain();

#endif // GBS_VIDEO_PROCESSING_H

