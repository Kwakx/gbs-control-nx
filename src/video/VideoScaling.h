#ifndef VIDEO_SCALING_H
#define VIDEO_SCALING_H

#include <Arduino.h>
#include "../core/State.h"

// Video scaling functions
void scaleHorizontal(uint16_t amountToScale, bool subtracting);
void scaleVertical(uint16_t amountToScale, bool subtracting);

#endif // VIDEO_SCALING_H

