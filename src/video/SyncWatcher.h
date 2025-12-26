#ifndef SYNC_WATCHER_H
#define SYNC_WATCHER_H

#include <Arduino.h>
#include "../core/State.h"

// Sync watcher - main synchronization loop
void runSyncWatcher();
void fastSogAdjust();
boolean snapToIntegralFrameRate(void);
boolean checkBoardPower();
void calibrateAdcOffset();

#endif // SYNC_WATCHER_H

