#include "Logging.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"

void myLog(char const* type, char command) {
    SerialM.printf("%s command %c at settings source %d, custom slot %d, status %x\n",
        type, command, uopt->presetPreference, uopt->presetSlot, rto->presetID);
}

