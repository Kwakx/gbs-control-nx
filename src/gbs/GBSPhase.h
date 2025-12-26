#ifndef GBS_PHASE_H
#define GBS_PHASE_H

#include <Arduino.h>
#include "../core/State.h"

// GBS phase adjustment functions
void advancePhase();
void movePhaseThroughRange();
void setAndLatchPhaseSP();
void setAndLatchPhaseADC();
void nudgeMD();
void updateSpDynamic(boolean withCurrentVideoModeCheck);
void updateCoastPosition(boolean autoCoast);
void updateClampPosition();
void togglePhaseAdjustUnits();
void setAndUpdateSogLevel(uint8_t level);

#endif // GBS_PHASE_H

