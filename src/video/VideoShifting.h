#ifndef VIDEO_SHIFTING_H
#define VIDEO_SHIFTING_H

#include <Arduino.h>
#include "../core/State.h"

// Video shifting functions
void shiftHorizontal(uint16_t amountToShift, bool subtracting);
void shiftHorizontalLeft();
void shiftHorizontalRight();
void shiftHorizontalLeftIF(uint8_t amount);
void shiftHorizontalRightIF(uint8_t amount);
void shiftVertical(uint16_t amountToAdd, bool subtracting);
void shiftVerticalUp();
void shiftVerticalDown();
void shiftVerticalUpIF();
void shiftVerticalDownIF();
void moveHS(uint16_t amountToAdd, bool subtracting);
void moveVS(uint16_t amountToAdd, bool subtracting);
void invertHS();
void invertVS();

#endif // VIDEO_SHIFTING_H

