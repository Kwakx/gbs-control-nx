#include "SerialCommandHandler.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"

void discardSerialRxData()
{
    uint16_t maxThrowAway = 0x1fff;
    while (Serial.available() && maxThrowAway > 0) {
        Serial.read();
        maxThrowAway--;
    }
}

// Note: processSerialCommand will contain the large switch statement from loop()
// This will be extracted when refactoring the main loop() function

