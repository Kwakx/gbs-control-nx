#ifndef DEBUG_HELPERS_H
#define DEBUG_HELPERS_H

#include <Arduino.h>

// Debug helper functions
void dumpRegisters(byte segment);
void printReg(uint8_t seg, uint8_t reg);
void printInfo();
void readEeprom();
void resetDebugPort();

#endif // DEBUG_HELPERS_H

