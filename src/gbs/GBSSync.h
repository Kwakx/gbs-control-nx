#ifndef GBS_SYNC_H
#define GBS_SYNC_H

#include <Arduino.h>
#include "../core/State.h"

// GBS sync processor functions
void prepareSyncProcessor();
void updateHVSyncEdge();
void setResetParameters();
void resetInterruptSogSwitchBit();
void resetInterruptSogBadBit();
void resetInterruptNoHsyncBadBit();
void activeFrameTimeLockInitialSteps();

#endif // GBS_SYNC_H

