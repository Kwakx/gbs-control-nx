#include "GBSPhase.h"
#include "../core/Globals.h"
#include "../gbs/GBSController.h"
#include "../gbs/GBSPresets.h"
#include "tv5725.h"

void setAndUpdateSogLevel(uint8_t level)
{
    typedef TV5725<GBS_ADDR> GBS;
    rto->currentLevelSOG = level & 0x1f;
    GBS::ADC_SOGCTRL::write(level);
    setAndLatchPhaseSP();
    setAndLatchPhaseADC();
    latchPLLAD();
    GBS::INTERRUPT_CONTROL_00::write(0xff); // reset irq status
    GBS::INTERRUPT_CONTROL_00::write(0x00);
}

void advancePhase()
{
    rto->phaseADC = (rto->phaseADC + 1) & 0x1f;
    setAndLatchPhaseADC();
}

void movePhaseThroughRange()
{
    for (uint8_t i = 0; i < 128; i++) { // 4x for 4x oversampling?
        advancePhase();
    }
}

void setAndLatchPhaseSP()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::PA_SP_LAT::write(0); // latch off
    GBS::PA_SP_S::write(rto->phaseSP);
    GBS::PA_SP_LAT::write(1); // latch on
}

void setAndLatchPhaseADC()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::PA_ADC_LAT::write(0);
    GBS::PA_ADC_S::write(rto->phaseADC);
    GBS::PA_ADC_LAT::write(1);
}

void nudgeMD()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::MD_VS_FLIP::write(!GBS::MD_VS_FLIP::read());
}

void updateSpDynamic(boolean withCurrentVideoModeCheck)
{
    typedef TV5725<GBS_ADDR> GBS;
    if (!rto->boardHasPower || rto->sourceDisconnected) {
        return;
    }

    uint8_t vidModeReadout = getVideoMode();
    if (vidModeReadout == 0) {
        vidModeReadout = getVideoMode();
    }

    if (rto->videoStandardInput == 0 && vidModeReadout == 0) {
        if (GBS::SP_DLT_REG::read() > 0x30)
            GBS::SP_DLT_REG::write(0x30); // 5_35
        else
            GBS::SP_DLT_REG::write(0xC0);
        return;
    }
    // reset condition, allow most formats to detect
    if (vidModeReadout == 0 && withCurrentVideoModeCheck) {
        if ((rto->noSyncCounter % 16) <= 8 && rto->noSyncCounter != 0) {
            GBS::SP_DLT_REG::write(0x30); // 5_35
        } else if ((rto->noSyncCounter % 16) > 8 && rto->noSyncCounter != 0) {
            GBS::SP_DLT_REG::write(0xC0); // may want to use lower, around 0x70
        } else {
            GBS::SP_DLT_REG::write(0x30);
        }
        GBS::SP_H_PULSE_IGNOR::write(0x02);
        GBS::SP_H_CST_ST::write(0x10);
        GBS::SP_H_CST_SP::write(0x100);
        GBS::SP_H_COAST::write(0);        // 5_3e 2 (just in case)
        GBS::SP_H_TIMER_VAL::write(0x3a); // new: 5_33 default 0x3a, set shorter for better hsync drop detect
        if (rto->syncTypeCsync) {
            GBS::SP_COAST_INV_REG::write(1); // new, allows SP to see otherwise potentially skipped vlines
        }
        rto->coastPositionIsSet = false;
        return;
    }

    if (rto->syncTypeCsync) {
        GBS::SP_COAST_INV_REG::write(0);
    }

    if (rto->videoStandardInput != 0) {
        if (rto->videoStandardInput <= 2) { // SD interlaced
            GBS::SP_PRE_COAST::write(7);
            GBS::SP_POST_COAST::write(3);
            GBS::SP_DLT_REG::write(0xC0);     // old: 0x140 works better than 0x130 with psx
            GBS::SP_H_TIMER_VAL::write(0x28); // 5_33

            if (rto->syncTypeCsync) {
                uint16_t hPeriod = GBS::HPERIOD_IF::read();
                for (int i = 0; i < 16; i++) {
                    if (hPeriod == 511 || hPeriod < 200) {
                        hPeriod = GBS::HPERIOD_IF::read(); // retry / overflow
                        if (i == 15) {
                            hPeriod = 300;
                            break; // give up, use short base to get low ignore value later
                        }
                    } else {
                        break;
                    }
                    delayMicroseconds(100);
                }

                uint16_t ignoreLength = hPeriod * 0.081f; // hPeriod is base length
                if (hPeriod <= 200) {                     // mode is NTSC / PAL, very likely overflow
                    ignoreLength = 0x18;                  // use neutral but low value
                }

                // get hpw to ht ratio. stability depends on hpll lock
                double ratioHs, ratioHsAverage = 0.0;
                uint8_t testOk = 0;
                for (int i = 0; i < 30; i++) {
                    ratioHs = (double)GBS::STATUS_SYNC_PROC_HLOW_LEN::read() / (double)(GBS::STATUS_SYNC_PROC_HTOTAL::read() + 1);
                    if (ratioHs > 0.041 && ratioHs < 0.152) { // 0.152 : (354 / 2345) is 9.5uS on NTSC (crtemudriver)
                        testOk++;
                        ratioHsAverage += ratioHs;
                        if (testOk == 12) {
                            ratioHs = ratioHsAverage / testOk;
                            break;
                        }
                        delayMicroseconds(30);
                    }
                }
                if (testOk != 12) {
                    ratioHs = 0.032; // 0.032: (~100 / 2560) is ~2.5uS on NTSC (find with crtemudriver)
                }

                uint16_t pllDiv = GBS::PLLAD_MD::read();
                ignoreLength = ignoreLength + (pllDiv * (ratioHs * 0.38)); // for factor: crtemudriver tests

                // > check relies on sync instability (potentially from too large ign. length) getting cought earlier
                if (ignoreLength > GBS::SP_H_PULSE_IGNOR::read() || GBS::SP_H_PULSE_IGNOR::read() >= 0x90) {
                    if (ignoreLength > 0x90) { // if higher, HPERIOD_IF probably was 511 / limit
                        ignoreLength = 0x90;
                    }
                    if (ignoreLength >= 0x1A && ignoreLength <= 0x42) {
                        ignoreLength = 0x1A; // at the low end should stick to 0x1A
                    }
                    if (ignoreLength != GBS::SP_H_PULSE_IGNOR::read()) {
                        GBS::SP_H_PULSE_IGNOR::write(ignoreLength);
                        rto->coastPositionIsSet = 0; // mustn't be skipped, needed when input changes dotclock / Hz
                        SerialM.print(F(" (debug) ign. length: 0x"));
                        SerialM.println(ignoreLength, HEX);
                    }
                }
            }
        } else if (rto->videoStandardInput <= 4) {
            GBS::SP_PRE_COAST::write(7);  // these two were 7 and 6
            GBS::SP_POST_COAST::write(6); // and last 11 and 11
            GBS::SP_DLT_REG::write(0xA0);
            GBS::SP_H_PULSE_IGNOR::write(0x0E);    // ps3: up to 0x3e, ps2: < 0x14
        } else if (rto->videoStandardInput == 5) { // 720p
            GBS::SP_PRE_COAST::write(7);           // down to 4 ok with ps2
            GBS::SP_POST_COAST::write(7);          // down to 6 ok with ps2 // ps3: 8 too much
            GBS::SP_DLT_REG::write(0x30);
            GBS::SP_H_PULSE_IGNOR::write(0x08);    // ps3: 0xd too much
        } else if (rto->videoStandardInput <= 7) { // 1080i,p
            GBS::SP_PRE_COAST::write(9);
            GBS::SP_POST_COAST::write(18); // of 1124 input lines
            GBS::SP_DLT_REG::write(0x70);
            GBS::SP_H_PULSE_IGNOR::write(0x06);
        } else if (rto->videoStandardInput >= 13) { // 13, 14 and 15 (was just 13 and 15)
            if (rto->syncTypeCsync == false) {
                GBS::SP_PRE_COAST::write(0x00);
                GBS::SP_POST_COAST::write(0x00);
                GBS::SP_H_PULSE_IGNOR::write(0xff); // required this because 5_02 0 is on
                GBS::SP_DLT_REG::write(0x00);       // sometimes enough on it's own, but not always
            } else {                                // csync
                GBS::SP_PRE_COAST::write(0x04);     // as in bypass mode set function
                GBS::SP_POST_COAST::write(0x07);    // as in bypass mode set function
                GBS::SP_DLT_REG::write(0x70);
                GBS::SP_H_PULSE_IGNOR::write(0x02);
            }
        }
    }
}

void updateCoastPosition(boolean autoCoast)
{
    typedef TV5725<GBS_ADDR> GBS;
    if (((rto->videoStandardInput == 0) || (rto->videoStandardInput > 14)) ||
        !rto->boardHasPower || rto->sourceDisconnected) {
        return;
    }

    uint32_t accInHlength = 0;
    uint16_t prevInHlength = GBS::HPERIOD_IF::read();
    for (uint8_t i = 0; i < 8; i++) {
        // psx jitters between 427, 428
        uint16_t thisInHlength = GBS::HPERIOD_IF::read();
        if ((thisInHlength > (prevInHlength - 3)) && (thisInHlength < (prevInHlength + 3))) {
            accInHlength += thisInHlength;
        } else {
            return;
        }
        if (!getStatus16SpHsStable()) {
            return;
        }

        prevInHlength = thisInHlength;
    }
    accInHlength = (accInHlength * 4) / 8;

    // 30.09.19 new: especially in low res VGA input modes, it can clip at "511 * 4 = 2044"
    // limit to more likely actual value of 430
    if (accInHlength >= 2040) {
        accInHlength = 1716;
    }

    if (accInHlength <= 240) {
        // check for low res, low Hz > can overflow HPERIOD_IF
        if (GBS::STATUS_SYNC_PROC_VTOTAL::read() <= 322) {
            delay(4);
            if (GBS::STATUS_SYNC_PROC_VTOTAL::read() <= 322) {
                SerialM.println(F(" (debug) updateCoastPosition: low res, low hz"));
                accInHlength = 2000;
                // usually need to lower charge pump. todo: write better check
                if (rto->syncTypeCsync && rto->videoStandardInput > 0 && rto->videoStandardInput <= 4) {
                    if (GBS::PLLAD_ICP::read() >= 5 && GBS::PLLAD_FS::read() == 1) {
                        GBS::PLLAD_ICP::write(5);
                        GBS::PLLAD_FS::write(0);
                        latchPLLAD();
                        rto->phaseIsSet = 0;
                    }
                }
            }
        }
    }

    // accInHlength around 1732 here / NTSC
    // scope on sync-in, enable 5_3e 2 > shows coast borders
    if (accInHlength > 32) {
        if (autoCoast) {
            // autoCoast (5_55 7 = on)
            GBS::SP_H_CST_ST::write((uint16_t)(accInHlength * 0.0562f)); // ~0x61, right after color burst
            GBS::SP_H_CST_SP::write((uint16_t)(accInHlength * 0.1550f)); // ~0x10C, shortly before sync falling edge
            GBS::SP_HCST_AUTO_EN::write(1);
        } else {
            // new: with SP_H_PROTECT disabled, even SNES can be a small value. Small value greatly improves Mega Drive
            GBS::SP_H_CST_ST::write(0x10);

            GBS::SP_H_CST_SP::write((uint16_t)(accInHlength * 0.968f)); // ~0x678
            GBS::SP_HCST_AUTO_EN::write(0);
        }
        rto->coastPositionIsSet = 1;
    }
}

void updateClampPosition()
{
    typedef TV5725<GBS_ADDR> GBS;
    if ((rto->videoStandardInput == 0) || !rto->boardHasPower || rto->sourceDisconnected) {
        return;
    }
    // this is required especially on mode changes with ypbpr
    if (getVideoMode() == 0) {
        return;
    }

    if (rto->inputIsYpBpR) {
        GBS::SP_CLAMP_MANUAL::write(0);
    } else {
        GBS::SP_CLAMP_MANUAL::write(1); // no auto clamp for RGB
    }

    // STATUS_SYNC_PROC_HTOTAL is "ht: " value; use with SP_CLP_SRC_SEL = 1 pixel clock
    // GBS::HPERIOD_IF::read()  is "h: " value; use with SP_CLP_SRC_SEL = 0 osc clock
    uint32_t accInHlength = 0;
    uint16_t prevInHlength = 0;
    uint16_t thisInHlength = 0;
    if (rto->syncTypeCsync)
        prevInHlength = GBS::HPERIOD_IF::read();
    else
        prevInHlength = GBS::STATUS_SYNC_PROC_HTOTAL::read();
    for (uint8_t i = 0; i < 16; i++) {
        if (rto->syncTypeCsync)
            thisInHlength = GBS::HPERIOD_IF::read();
        else
            thisInHlength = GBS::STATUS_SYNC_PROC_HTOTAL::read();
        if ((thisInHlength > (prevInHlength - 3)) && (thisInHlength < (prevInHlength + 3))) {
            accInHlength += thisInHlength;
        } else {
            return;
        }
        if (!getStatus16SpHsStable()) {
            return;
        }

        prevInHlength = thisInHlength;
        delayMicroseconds(100);
    }
    accInHlength = accInHlength / 16; // for the 16x loop

    // HPERIOD_IF: 9 bits (0-511, represents actual scanline time / 4, can overflow to low values)
    // if it overflows, the calculated clamp positions are likely around 1 to 4. good enough
    // STATUS_SYNC_PROC_HTOTAL: 12 bits (0-4095)
    if (accInHlength > 4095) {
        return;
    }

    uint16_t oldClampST = GBS::SP_CS_CLP_ST::read();
    uint16_t oldClampSP = GBS::SP_CS_CLP_SP::read();
    float multiSt = rto->syncTypeCsync == 1 ? 0.032f : 0.010f;
    float multiSp = rto->syncTypeCsync == 1 ? 0.174f : 0.058f;
    uint16_t start = 1 + (accInHlength * multiSt); // HPERIOD_IF: *0.04 seems good
    uint16_t stop = 2 + (accInHlength * multiSp);  // HPERIOD_IF: *0.178 starts to creep into some EDTV modes

    if (rto->inputIsYpBpR) {
        // YUV: // ST shift forward to pass blacker than black HSync, sog: min * 0.08
        multiSt = rto->syncTypeCsync == 1 ? 0.089f : 0.032f;
        start = 1 + (accInHlength * multiSt);

        // new: HDBypass rewrite to sync to falling HS edge: move clamp position forward
        // RGB can stay the same for now (clamp will start on sync pulse, a benefit in RGB
        if (rto->outModeHdBypass) {
            if (videoStandardInputIsPalNtscSd()) {
                start += 0x60;
                stop += 0x60;
            }
            // raise blank level a bit that's not handled 100% by clamping
            GBS::HD_BLK_GY_DATA::write(0x05);
            GBS::HD_BLK_BU_DATA::write(0x00);
            GBS::HD_BLK_RV_DATA::write(0x00);
        }
    }

    if ((start < (oldClampST - 1) || start > (oldClampST + 1)) ||
        (stop < (oldClampSP - 1) || stop > (oldClampSP + 1))) {
        GBS::SP_CS_CLP_ST::write(start);
        GBS::SP_CS_CLP_SP::write(stop);
    }

    rto->clampPositionIsSet = true;
}

void togglePhaseAdjustUnits()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::PA_SP_BYPSZ::write(0); // yes, 0 means bypass on here
    GBS::PA_SP_BYPSZ::write(1);
    delay(2);
    GBS::PA_ADC_BYPSZ::write(0);
    GBS::PA_ADC_BYPSZ::write(1);
    delay(2);
}

