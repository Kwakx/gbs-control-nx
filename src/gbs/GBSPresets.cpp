#include "GBSPresets.h"
#include "GBSRegister.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../video/framesync.h"
#include "../../presets/presetHdBypassSection.h"
#include "../../presets/presetDeinterlacerSection.h"
#include "../../presets/presetMdSection.h"
#include "../../presets/ntsc_240p.h"
#include "../../presets/pal_240p.h"
#include "tv5725.h"
#include <LittleFS.h>

void copyBank(uint8_t *bank, const uint8_t *programArray, uint16_t *index)
{
    for (uint8_t x = 0; x < 16; ++x) {
        bank[x] = pgm_read_byte(programArray + *index);
        (*index)++;
    }
}

boolean videoStandardInputIsPalNtscSd()
{
    if (rto->videoStandardInput == 1 || rto->videoStandardInput == 2) {
        return true;
    }
    return false;
}

void zeroAll()
{
    // turn processing units off first
    writeOneByte(0xF0, 0);
    writeOneByte(0x46, 0x00); // reset controls 1
    writeOneByte(0x47, 0x00); // reset controls 2

    // zero out entire register space
    for (int y = 0; y < 6; y++) {
        writeOneByte(0xF0, (uint8_t)y);
        for (int z = 0; z < 16; z++) {
            uint8_t bank[16];
            for (int w = 0; w < 16; w++) {
                bank[w] = 0;
            }
            writeBytes(z * 16, bank, 16);
        }
    }
}

void loadHdBypassSection()
{
    uint16_t index = 0;
    uint8_t bank[16];
    writeOneByte(0xF0, 1);
    for (int j = 3; j <= 5; j++) { // start at 0x30
        copyBank(bank, presetHdBypassSection, &index);
        writeBytes(j * 16, bank, 16);
    }
}

void loadPresetDeinterlacerSection()
{
    uint16_t index = 0;
    uint8_t bank[16];
    writeOneByte(0xF0, 2);
    for (int j = 0; j <= 3; j++) { // start at 0x00
        copyBank(bank, presetDeinterlacerSection, &index);
        writeBytes(j * 16, bank, 16);
    }
}

void loadPresetMdSection()
{
    uint16_t index = 0;
    uint8_t bank[16];
    writeOneByte(0xF0, 1);
    for (int j = 6; j <= 7; j++) { // start at 0x60
        copyBank(bank, presetMdSection, &index);
        writeBytes(j * 16, bank, 16);
    }
    bank[0] = pgm_read_byte(presetMdSection + index);
    bank[1] = pgm_read_byte(presetMdSection + index + 1);
    bank[2] = pgm_read_byte(presetMdSection + index + 2);
    bank[3] = pgm_read_byte(presetMdSection + index + 3);
    writeBytes(8 * 16, bank, 4); // MD section ends at 0x83, not 0x90
}

uint8_t getSingleByteFromPreset(const uint8_t *programArray, unsigned int offset)
{
    return pgm_read_byte(programArray + offset);
}

void writeProgramArrayNew(const uint8_t *programArray, boolean skipMDSection)
{
    uint16_t index = 0;
    uint8_t bank[16];
    uint8_t y = 0;

    typedef TV5725<GBS_ADDR> GBS;

    FrameSync::cleanup();

    // should only be possible if previously was in RGBHV bypass, then hit a manual preset switch
    if (rto->videoStandardInput == 15) {
        rto->videoStandardInput = 0;
    }

    rto->outModeHdBypass = 0; // the default at this stage
    if (GBS::ADC_INPUT_SEL::read() == 0) {
        rto->inputIsYpBpR = 1; // new: update the var here, allow manual preset loads
    } else {
        rto->inputIsYpBpR = 0;
    }

    uint8_t reset46 = GBS::RESET_CONTROL_0x46::read(); // for keeping these as they are now
    uint8_t reset47 = GBS::RESET_CONTROL_0x47::read();

    for (; y < 6; y++) {
        writeOneByte(0xF0, (uint8_t)y);
        switch (y) {
            case 0:
                for (int j = 0; j <= 1; j++) { // 2 times
                    for (int x = 0; x <= 15; x++) {
                        if (j == 0 && x == 4) {
                            // keep DAC off
                            if (rto->useHdmiSyncFix) {
                                bank[x] = pgm_read_byte(programArray + index) & ~(1 << 0);
                            } else {
                                bank[x] = pgm_read_byte(programArray + index);
                            }
                        } else if (j == 0 && x == 6) {
                            bank[x] = reset46;
                        } else if (j == 0 && x == 7) {
                            bank[x] = reset47;
                        } else if (j == 0 && x == 9) {
                            // keep sync output off
                            if (rto->useHdmiSyncFix) {
                                bank[x] = pgm_read_byte(programArray + index) | (1 << 2);
                            } else {
                                bank[x] = pgm_read_byte(programArray + index);
                            }
                        } else {
                            // use preset values
                            bank[x] = pgm_read_byte(programArray + index);
                        }

                        index++;
                    }
                    writeBytes(0x40 + (j * 16), bank, 16);
                }
                copyBank(bank, programArray, &index);
                writeBytes(0x90, bank, 16);
                break;
            case 1:
                for (int j = 0; j <= 2; j++) { // 3 times
                    copyBank(bank, programArray, &index);
                    if (j == 0) {
                        bank[0] = bank[0] & ~(1 << 5); // clear 1_00 5
                        bank[1] = bank[1] | (1 << 0);  // set 1_01 0
                        bank[12] = bank[12] & 0x0f;    // clear 1_0c upper bits
                        bank[13] = 0;                  // clear 1_0d
                    }
                    writeBytes(j * 16, bank, 16);
                }
                if (!skipMDSection) {
                    loadPresetMdSection();
                    if (rto->syncTypeCsync)
                        GBS::MD_SEL_VGA60::write(0); // EDTV possible
                    else
                        GBS::MD_SEL_VGA60::write(1); // VGA 640x480 more likely

                    GBS::MD_HD1250P_CNTRL::write(rto->medResLineCount); // patch med res support
                }
                break;
            case 2:
                loadPresetDeinterlacerSection();
                break;
            case 3:
                for (int j = 0; j <= 7; j++) { // 8 times
                    copyBank(bank, programArray, &index);
                    writeBytes(j * 16, bank, 16);
                }
                // blank out VDS PIP registers, otherwise they can end up uninitialized
                for (int x = 0; x <= 15; x++) {
                    writeOneByte(0x80 + x, 0x00);
                }
                break;
            case 4:
                for (int j = 0; j <= 5; j++) { // 6 times
                    copyBank(bank, programArray, &index);
                    writeBytes(j * 16, bank, 16);
                }
                break;
            case 5:
                for (int j = 0; j <= 6; j++) { // 7 times
                    for (int x = 0; x <= 15; x++) {
                        bank[x] = pgm_read_byte(programArray + index);
                        if (index == 322) { // s5_02 bit 6+7 = input selector (only bit 6 is relevant)
                            if (rto->inputIsYpBpR)
                                bitClear(bank[x], 6);
                            else
                                bitSet(bank[x], 6);
                        }
                        if (index == 323) { // s5_03 set clamps according to input channel
                            if (rto->inputIsYpBpR) {
                                bitClear(bank[x], 2); // G bottom clamp
                                bitSet(bank[x], 1);   // R mid clamp
                                bitSet(bank[x], 3);   // B mid clamp
                            } else {
                                bitClear(bank[x], 2); // G bottom clamp
                                bitClear(bank[x], 1); // R bottom clamp
                                bitClear(bank[x], 3); // B bottom clamp
                            }
                        }
                        if (index == 352) { // s5_20 always force to 0x02 (only SP_SOG_P_ATO)
                            bank[x] = 0x02;
                        }
                        if (index == 375) { // s5_37
                            if (videoStandardInputIsPalNtscSd()) {
                                bank[x] = 0x6b;
                            } else {
                                bank[x] = 0x02;
                            }
                        }
                        if (index == 382) {     // s5_3e
                            bitSet(bank[x], 5); // SP_DIS_SUB_COAST = 1
                        }
                        if (index == 407) {     // s5_57
                            bitSet(bank[x], 0); // SP_NO_CLAMP_REG = 1
                        }
                        index++;
                    }
                    writeBytes(j * 16, bank, 16);
                }
                break;
        }
    }

    // scaling RGBHV mode
    if (uopt->preferScalingRgbhv && rto->isValidForScalingRGBHV) {
        GBS::GBS_OPTION_SCALING_RGBHV::write(1);
        rto->videoStandardInput = 3;
    }
}
