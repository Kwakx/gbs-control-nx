#include "PreferencesManager.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include <LittleFS.h>
#include "../core/options.h"

void loadDefaultUserOptions()
{
    uopt->presetPreference = Output960P;    // #1
    uopt->enableFrameTimeLock = 0; // permanently adjust frame timing to avoid glitch vertical bar. does not work on all displays!
    uopt->presetSlot = 'A';          //
    uopt->frameTimeLockMethod = 0; // compatibility with more displays
    uopt->enableAutoGain = 0;
    uopt->wantScanlines = 0;
    uopt->wantOutputComponent = 0;
    uopt->deintMode = 0;
    uopt->wantVdsLineFilter = 0;
    uopt->wantPeaking = 1;
    uopt->preferScalingRgbhv = 1;
    uopt->wantTap6 = 1;
    uopt->PalForce60 = 0;
    uopt->matchPresetSource = 1;             // #14
    uopt->wantStepResponse = 1;              // #15
    uopt->wantFullHeight = 1;                // #16
    uopt->enableCalibrationADC = 1;          // #17
    uopt->scanlineStrength = 0x30;           // #18
    uopt->disableExternalClockGenerator = 0; // #19
}

void saveUserPrefs()
{
    File f = LittleFS.open("/preferencesv2.txt", "w");
    if (!f) {
        SerialM.println(F("saveUserPrefs: open file failed"));
        return;
    }
    f.write(uopt->presetPreference + '0'); // #1
    f.write(uopt->enableFrameTimeLock + '0');
    f.write(uopt->presetSlot);
    f.write(uopt->frameTimeLockMethod + '0');
    f.write(uopt->enableAutoGain + '0');
    f.write(uopt->wantScanlines + '0');
    f.write(uopt->wantOutputComponent + '0');
    f.write(uopt->deintMode + '0');
    f.write(uopt->wantVdsLineFilter + '0');
    f.write(uopt->wantPeaking + '0');
    f.write(uopt->preferScalingRgbhv + '0');
    f.write(uopt->wantTap6 + '0');
    f.write(uopt->PalForce60 + '0');
    f.write(uopt->matchPresetSource + '0');             // #14
    f.write(uopt->wantStepResponse + '0');              // #15
    f.write(uopt->wantFullHeight + '0');                // #16
    f.write(uopt->enableCalibrationADC + '0');          // #17
    f.write(uopt->scanlineStrength + '0');              // #18
    f.write(uopt->disableExternalClockGenerator + '0'); // #19

    f.close();
}

