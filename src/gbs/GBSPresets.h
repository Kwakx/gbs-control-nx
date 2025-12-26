#ifndef GBS_PRESETS_H
#define GBS_PRESETS_H

#include <Arduino.h>
#include "tv5725.h"
#include "../core/State.h"

// GBS Preset loading functions
void copyBank(uint8_t *bank, const uint8_t *programArray, uint16_t *index);
void zeroAll();
void loadHdBypassSection();
void loadPresetDeinterlacerSection();
void loadPresetMdSection();
void writeProgramArrayNew(const uint8_t *programArray, boolean skipMDSection);
const uint8_t *loadPresetFromLittleFS(byte forVideoMode);
void savePresetToLittleFS();
uint8_t getSingleByteFromPreset(const uint8_t *programArray, unsigned int offset);
boolean videoStandardInputIsPalNtscSd();

#endif // GBS_PRESETS_H

