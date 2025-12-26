#ifndef GBS_REGISTER_H
#define GBS_REGISTER_H

#include <Arduino.h>
#include "tv5725.h"

// GBS Register operations
// Functions for reading and writing GBS chip registers

void writeOneByte(uint8_t slaveRegister, uint8_t value);
void writeBytes(uint8_t slaveRegister, uint8_t *values, uint8_t numValues);
void readFromRegister(uint8_t reg, int bytesToRead, uint8_t *output);
void printReg(uint8_t seg, uint8_t reg);
void dumpRegisters(byte segment);

// Global variable for current segment
extern uint8_t lastSegment;

#endif // GBS_REGISTER_H

