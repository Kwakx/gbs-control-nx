#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#include <Arduino.h>
#include "../core/State.h"

// Preferences management functions
void saveUserPrefs();
void loadDefaultUserOptions();
void syncReverseRotaryEncoderIsrMirror();

#endif // PREFERENCES_MANAGER_H

