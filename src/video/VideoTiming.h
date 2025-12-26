#ifndef VIDEO_TIMING_H
#define VIDEO_TIMING_H

#include <Arduino.h>
#include "../core/State.h"

// Video timing functions
void set_htotal(uint16_t htotal);
void set_vtotal(uint16_t vtotal);
void printVideoTimings();
void setHSyncStartPosition(uint16_t value);
void setHSyncStopPosition(uint16_t value);
void setVSyncStartPosition(uint16_t value);
void setVSyncStopPosition(uint16_t value);
void setMemoryHblankStartPosition(uint16_t value);
void setMemoryHblankStopPosition(uint16_t value);
void setDisplayHblankStartPosition(uint16_t value);
void setDisplayHblankStopPosition(uint16_t value);
void setMemoryVblankStartPosition(uint16_t value);
void setMemoryVblankStopPosition(uint16_t value);
void setDisplayVblankStartPosition(uint16_t value);
void setDisplayVblankStopPosition(uint16_t value);
void setCsVsStart(uint16_t start);
void setCsVsStop(uint16_t stop);
uint16_t getCsVsStart();
uint16_t getCsVsStop();
boolean runAutoBestHTotal();
boolean applyBestHTotal(uint16_t bestHTotal);
void fastGetBestHtotal();

#endif // VIDEO_TIMING_H

