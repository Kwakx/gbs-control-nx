#ifndef SLOT_MANAGER_H
#define SLOT_MANAGER_H

#include <Arduino.h>
#include "../core/State.h"
#include "../core/slot.h"

// Slot management functions
const uint8_t *loadPresetFromLittleFS(byte forVideoMode);
void savePresetToLittleFS();

#endif // SLOT_MANAGER_H

