#include "GBSRegister.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "tv5725.h"

// Global variable for current segment
uint8_t lastSegment = 0xFF; // GBS segment for direct access

void writeOneByte(uint8_t slaveRegister, uint8_t value)
{
    writeBytes(slaveRegister, &value, 1);
}

void writeBytes(uint8_t slaveRegister, uint8_t *values, uint8_t numValues)
{
    typedef TV5725<GBS_ADDR> GBS;
    if (slaveRegister == 0xF0 && numValues == 1) {
        lastSegment = *values;
    } else
        GBS::write(lastSegment, slaveRegister, values, numValues);
}

void readFromRegister(uint8_t reg, int bytesToRead, uint8_t *output)
{
    typedef TV5725<GBS_ADDR> GBS;
    return GBS::read(lastSegment, reg, output, bytesToRead);
}

void printReg(uint8_t seg, uint8_t reg)
{
    uint8_t readout;
    readFromRegister(reg, 1, &readout);
    // didn't think this HEX trick would work, but it does! (?)
    SerialM.print("0x");
    SerialM.print(readout, HEX);
    SerialM.print(", // s");
    SerialM.print(seg);
    SerialM.print("_");
    SerialM.println(reg, HEX);
}

// dumps the current chip configuration in a format that's ready to use as new preset :)
void dumpRegisters(byte segment)
{
    if (segment > 5)
        return;
    writeOneByte(0xF0, segment);

    switch (segment) {
        case 0:
            for (int x = 0x40; x <= 0x5F; x++) {
                printReg(0, x);
            }
            for (int x = 0x90; x <= 0x9F; x++) {
                printReg(0, x);
            }
            break;
        case 1:
            for (int x = 0x0; x <= 0x2F; x++) {
                printReg(1, x);
            }
            break;
        case 2:
            for (int x = 0x0; x <= 0x3F; x++) {
                printReg(2, x);
            }
            break;
        case 3:
            for (int x = 0x0; x <= 0x7F; x++) {
                printReg(3, x);
            }
            break;
        case 4:
            for (int x = 0x0; x <= 0x5F; x++) {
                printReg(4, x);
            }
            break;
        case 5:
            for (int x = 0x0; x <= 0x6F; x++) {
                printReg(5, x);
            }
            break;
    }
}
