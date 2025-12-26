#ifndef SERIAL_COMMAND_HANDLER_H
#define SERIAL_COMMAND_HANDLER_H

#include <Arduino.h>
#include "../core/State.h"

// Serial command processing
void discardSerialRxData();
void processSerialCommand(char command);

#endif // SERIAL_COMMAND_HANDLER_H

