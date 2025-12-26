#include "VideoInput.h"
#include "../core/Globals.h"
#include "../gbs/GBSRegister.h"
#include "../gbs/GBSSync.h"
#include "../gbs/GBSVideoProcessing.h"
#include "../gbs/GBSController.h"
#include "../gbs/GBSPhase.h"
#include "../hardware/LEDControl.h"
#include "../utils/DebugHelpers.h"
#include "../wifi/WiFiManager.h"
#include "../gbs/tv5725.h"
#include <Arduino.h>

typedef TV5725<GBS_ADDR> GBS;

uint8_t detectAndSwitchToActiveInput()
{ // if any
    uint8_t currentInput = GBS::ADC_INPUT_SEL::read();
    unsigned long timeout = millis();
    while (millis() - timeout < 450) {
        delay(10);
        handleWiFi(0);

        boolean stable = getStatus16SpHsStable();
        if (stable) {
            currentInput = GBS::ADC_INPUT_SEL::read();
            SerialM.print(F("Activity detected, input: "));
            if (currentInput == 1)
                SerialM.println("RGB");
            else
                SerialM.println(F("Component"));

            if (currentInput == 1) { // RGBS or RGBHV
                boolean vsyncActive = 0;
                rto->inputIsYpBpR = false; // declare for MD
                rto->currentLevelSOG = 13; // test startup with MD and MS separately!
                setAndUpdateSogLevel(rto->currentLevelSOG);

                unsigned long timeOutStart = millis();
                // vsync test
                // 360ms good up to 5_34 SP_V_TIMER_VAL = 0x0b
                while (!vsyncActive && ((millis() - timeOutStart) < 360)) {
                    vsyncActive = GBS::STATUS_SYNC_PROC_VSACT::read();
                    handleWiFi(0); // wifi stack
                    delay(1);
                }

                // if VSync is active, it's RGBHV or RGBHV with CSync on HS pin
                if (vsyncActive) {
                    SerialM.println(F("VSync: present"));
                    GBS::MD_SEL_VGA60::write(1); // VGA 640x480 more likely than EDTV
                    boolean hsyncActive = 0;

                    timeOutStart = millis();
                    while (!hsyncActive && millis() - timeOutStart < 400) {
                        hsyncActive = GBS::STATUS_SYNC_PROC_HSACT::read();
                        handleWiFi(0); // wifi stack
                        delay(1);
                    }

                    if (hsyncActive) {
                        SerialM.print(F("HSync: present"));
                        // The HSync and SOG pins are setup to detect CSync, if present
                        // (SOG mode on, coasting setup, debug bus setup, etc)
                        // SP_H_PROTECT is needed for CSync with a VS source present as well
                        GBS::SP_H_PROTECT::write(1);
                        delay(120);

                        short decodeSuccess = 0;
                        for (int i = 0; i < 3; i++) {
                            // no success if: no signal at all (returns 0.0f), no embedded VSync (returns ~18.5f)
                            // todo: this takes a while with no csync present
                            rto->syncTypeCsync = 1; // temporary for test
                            float sfr = getSourceFieldRate(1);
                            rto->syncTypeCsync = 0; // undo
                            if (sfr > 40.0f)
                                decodeSuccess++; // properly decoded vsync from 40 to xx Hz
                        }

                        if (decodeSuccess >= 2) {
                            SerialM.println(F(" (with CSync)"));
                            GBS::SP_PRE_COAST::write(0x10); // increase from 9 to 16 (EGA 364)
                            delay(40);
                            rto->syncTypeCsync = true;
                        } else {
                            SerialM.println();
                            rto->syncTypeCsync = false;
                        }

                        // check for 25khz, all regular SOG modes first // update: only check for mode 8
                        // MD reg for medium res starts at 0x2C and needs 16 loops to ramp to max of 0x3C (vt 360 .. 496)
                        // if source is HS+VS, can't detect via MD unit, need to set 5_11=0x92 and look at vt: counter
                        for (uint8_t i = 0; i < 16; i++) {
                            //printInfo();
                            uint8_t innerVideoMode = getVideoMode();
                            if (innerVideoMode == 8) {
                                setAndUpdateSogLevel(rto->currentLevelSOG);
                                rto->medResLineCount = GBS::MD_HD1250P_CNTRL::read();
                                SerialM.println(F("med res"));

                                return 1;
                            }
                            // update 25khz detection
                            GBS::MD_HD1250P_CNTRL::write(GBS::MD_HD1250P_CNTRL::read() + 1);
                            //Serial.println(GBS::MD_HD1250P_CNTRL::read(), HEX);
                            delay(30);
                        }

                        rto->videoStandardInput = 15;
                        // exception: apply preset here, not later in syncwatcher
                        applyPresets(rto->videoStandardInput);
                        delay(100);

                        return 3;
                    } else {
                        // need to continue looking
                        SerialM.println(F("but no HSync!"));
                    }
                }

                if (!vsyncActive) { // then do RGBS check
                    rto->syncTypeCsync = true;
                    GBS::MD_SEL_VGA60::write(0); // EDTV60 more likely than VGA60
                    uint16_t testCycle = 0;
                    timeOutStart = millis();
                    while ((millis() - timeOutStart) < 6000) {
                        delay(2);
                        if (getVideoMode() > 0) {
                            if (getVideoMode() != 8) { // if it's mode 8, need to set stuff first
                                return 1;
                            }
                        }
                        testCycle++;
                        // post coast 18 can mislead occasionally (SNES 239 mode)
                        // but even then it still detects the video mode pretty well
                        if ((testCycle % 150) == 0) {
                            if (rto->currentLevelSOG == 1) {
                                rto->currentLevelSOG = 2;
                            } else {
                                rto->currentLevelSOG += 2;
                            }
                            if (rto->currentLevelSOG >= 15) {
                                rto->currentLevelSOG = 1;
                            }
                            setAndUpdateSogLevel(rto->currentLevelSOG);
                        }

                        // new: check for 25khz, use regular scaling route for those
                        if (getVideoMode() == 8) {
                            rto->currentLevelSOG = rto->thisSourceMaxLevelSOG = 13;
                            setAndUpdateSogLevel(rto->currentLevelSOG);
                            rto->medResLineCount = GBS::MD_HD1250P_CNTRL::read();
                            SerialM.println(F("med res"));
                            return 1;
                        }

                        uint8_t currentMedResLineCount = GBS::MD_HD1250P_CNTRL::read();
                        if (currentMedResLineCount < 0x3c) {
                            GBS::MD_HD1250P_CNTRL::write(currentMedResLineCount + 1);
                        } else {
                            GBS::MD_HD1250P_CNTRL::write(0x33);
                        }
                        //Serial.println(GBS::MD_HD1250P_CNTRL::read(), HEX);
                    }

                    //rto->currentLevelSOG = rto->thisSourceMaxLevelSOG = 13;
                    //setAndUpdateSogLevel(rto->currentLevelSOG);

                    return 1; //anyway, let later stage deal with it
                }

                GBS::SP_SOG_MODE::write(1);
                resetSyncProcessor();
                resetModeDetect(); // there was some signal but we lost it. MD is stuck anyway, so reset
                delay(40);
            } else if (currentInput == 0) { // YUV
                uint16_t testCycle = 0;
                rto->inputIsYpBpR = true;    // declare for MD
                GBS::MD_SEL_VGA60::write(0); // EDTV more likely than VGA 640x480

                unsigned long timeOutStart = millis();
                while ((millis() - timeOutStart) < 6000) {
                    delay(2);
                    if (getVideoMode() > 0) {
                        return 2;
                    }

                    testCycle++;
                    if ((testCycle % 180) == 0) {
                        if (rto->currentLevelSOG == 1) {
                            rto->currentLevelSOG = 2;
                        } else {
                            rto->currentLevelSOG += 2;
                        }
                        if (rto->currentLevelSOG >= 16) {
                            rto->currentLevelSOG = 1;
                        }
                        setAndUpdateSogLevel(rto->currentLevelSOG);
                        rto->thisSourceMaxLevelSOG = rto->currentLevelSOG;
                    }
                }

                rto->currentLevelSOG = rto->thisSourceMaxLevelSOG = 14;
                setAndUpdateSogLevel(rto->currentLevelSOG);

                return 2; //anyway, let later stage deal with it
            }

            SerialM.println(" lost..");
            rto->currentLevelSOG = 2;
            setAndUpdateSogLevel(rto->currentLevelSOG);
        }

        GBS::ADC_INPUT_SEL::write(!currentInput); // can only be 1 or 0
        delay(200);

        return 0; // don't do the check on the new input here, wait till next run
    }

    return 0;
}

uint8_t inputAndSyncDetect()
{
    uint8_t syncFound = detectAndSwitchToActiveInput();

    if (syncFound == 0) {
        if (!getSyncPresent()) {
            if (rto->isInLowPowerMode == false) {
                rto->sourceDisconnected = true;
                rto->videoStandardInput = 0;
                // reset to base settings, then go to low power
                GBS::SP_SOG_MODE::write(1);
                goLowPowerWithInputDetection();
                rto->isInLowPowerMode = true;
            }
        }
        return 0;
    } else if (syncFound == 1) { // input is RGBS
        rto->inputIsYpBpR = 0;
        rto->sourceDisconnected = false;
        rto->isInLowPowerMode = false;
        resetDebugPort();
        applyRGBPatches();
        LEDON;
        return 1;
    } else if (syncFound == 2) {
        rto->inputIsYpBpR = 1;
        rto->sourceDisconnected = false;
        rto->isInLowPowerMode = false;
        resetDebugPort();
        applyYuvPatches();
        LEDON;
        return 2;
    } else if (syncFound == 3) { // input is RGBHV
        //already applied
        rto->isInLowPowerMode = false;
        rto->inputIsYpBpR = 0;
        rto->sourceDisconnected = false;
        rto->videoStandardInput = 15;
        resetDebugPort();
        LEDON;
        return 3;
    }

    return 0;
}

uint8_t getVideoMode()
{
    uint8_t detectedMode = 0;

    if (rto->videoStandardInput >= 14) { // check RGBHV first // not mode 13 here, else mode 13 can't reliably exit
        detectedMode = GBS::STATUS_16::read();
        if ((detectedMode & 0x0a) > 0) {    // bit 1 or 3 active?
            return rto->videoStandardInput; // still RGBHV bypass, 14 or 15
        } else {
            return 0;
        }
    }

    detectedMode = GBS::STATUS_00::read();

    // note: if stat0 == 0x07, it's supposedly stable. if we then can't find a mode, it must be an MD problem
    if ((detectedMode & 0x07) == 0x07) {
        if ((detectedMode & 0x80) == 0x80) { // bit 7: SD flag (480i, 480P, 576i, 576P)
            if ((detectedMode & 0x08) == 0x08)
                return 1; // ntsc interlace
            if ((detectedMode & 0x20) == 0x20)
                return 2; // pal interlace
            if ((detectedMode & 0x10) == 0x10)
                return 3; // edtv 60 progressive
            if ((detectedMode & 0x40) == 0x40)
                return 4; // edtv 50 progressive
        }

        detectedMode = GBS::STATUS_03::read();
        if ((detectedMode & 0x10) == 0x10) {
            return 5;
        } // hdtv 720p

        if (rto->videoStandardInput == 4) {
            detectedMode = GBS::STATUS_04::read();
            if ((detectedMode & 0xFF) == 0x80) {
                return 4; // still edtv 50 progressive
            }
        }
    }

    detectedMode = GBS::STATUS_04::read();
    if ((detectedMode & 0x20) == 0x20) { // hd mode on
        if ((detectedMode & 0x61) == 0x61) {
            // hdtv 1080i // 576p mode tends to get misdetected as this, even with all the checks
            // real 1080i (PS2): h:199 v:1124
            // misdetected 576p (PS2): h:215 v:1249
            if (GBS::VPERIOD_IF::read() < 1160) {
                return 6;
            }
        }
        if ((detectedMode & 0x10) == 0x10) {
            if ((detectedMode & 0x04) == 0x04) { // normally HD2376_1250P (PAL FHD?), but using this for 24k
                return 8;
            }
            return 7; // hdtv 1080p
        }
    }

    // graphic modes, mostly used for ps2 doing rgb over yuv with sog
    if ((GBS::STATUS_05::read() & 0x0c) == 0x00) // 2: Horizontal unstable AND 3: Vertical unstable are 0?
    {
        if (GBS::STATUS_00::read() == 0x07) {            // the 3 stat0 stable indicators on, none of the SD indicators on
            if ((GBS::STATUS_03::read() & 0x02) == 0x02) // Graphic mode bit on (any of VGA/SVGA/XGA/SXGA at all detected Hz)
            {
                if (rto->inputIsYpBpR)
                    return 13;
                else
                    return 15; // switch to RGBS/HV handling
            } else {
                // this mode looks like it wants to be graphic mode, but the horizontal counter target in MD is very strict
                static uint8_t XGA_60HZ = GBS::MD_XGA_60HZ_CNTRL::read();
                static uint8_t XGA_70HZ = GBS::MD_XGA_70HZ_CNTRL::read();
                static uint8_t XGA_75HZ = GBS::MD_XGA_75HZ_CNTRL::read();
                static uint8_t XGA_85HZ = GBS::MD_XGA_85HZ_CNTRL::read();

                static uint8_t SXGA_60HZ = GBS::MD_SXGA_60HZ_CNTRL::read();
                static uint8_t SXGA_75HZ = GBS::MD_SXGA_75HZ_CNTRL::read();
                static uint8_t SXGA_85HZ = GBS::MD_SXGA_85HZ_CNTRL::read();

                static uint8_t SVGA_60HZ = GBS::MD_SVGA_60HZ_CNTRL::read();
                static uint8_t SVGA_75HZ = GBS::MD_SVGA_75HZ_CNTRL::read();
                static uint8_t SVGA_85HZ = GBS::MD_SVGA_85HZ_CNTRL::read();

                static uint8_t VGA_75HZ = GBS::MD_VGA_75HZ_CNTRL::read();
                static uint8_t VGA_85HZ = GBS::MD_VGA_85HZ_CNTRL::read();

                short hSkew = random(-2, 2); // skew the target a little
                //Serial.println(XGA_60HZ + hSkew, HEX);
                GBS::MD_XGA_60HZ_CNTRL::write(XGA_60HZ + hSkew);
                GBS::MD_XGA_70HZ_CNTRL::write(XGA_70HZ + hSkew);
                GBS::MD_XGA_75HZ_CNTRL::write(XGA_75HZ + hSkew);
                GBS::MD_XGA_85HZ_CNTRL::write(XGA_85HZ + hSkew);
                GBS::MD_SXGA_60HZ_CNTRL::write(SXGA_60HZ + hSkew);
                GBS::MD_SXGA_75HZ_CNTRL::write(SXGA_75HZ + hSkew);
                GBS::MD_SXGA_85HZ_CNTRL::write(SXGA_85HZ + hSkew);
                GBS::MD_SVGA_60HZ_CNTRL::write(SVGA_60HZ + hSkew);
                GBS::MD_SVGA_75HZ_CNTRL::write(SVGA_75HZ + hSkew);
                GBS::MD_SVGA_85HZ_CNTRL::write(SVGA_85HZ + hSkew);
                GBS::MD_VGA_75HZ_CNTRL::write(VGA_75HZ + hSkew);
                GBS::MD_VGA_85HZ_CNTRL::write(VGA_85HZ + hSkew);
            }
        }
    }

    detectedMode = GBS::STATUS_00::read();
    if ((detectedMode & 0x2F) == 0x07) { // 0_00 H+V stable, not NTSCI, not PALI
        detectedMode = GBS::STATUS_16::read();
        if ((detectedMode & 0x02) == 0x02) { // SP H active
            uint16_t lineCount = GBS::STATUS_SYNC_PROC_VTOTAL::read();
            for (uint8_t i = 0; i < 2; i++) {
                delay(2);
                if (GBS::STATUS_SYNC_PROC_VTOTAL::read() < (lineCount - 1) ||
                    GBS::STATUS_SYNC_PROC_VTOTAL::read() > (lineCount + 1)) {
                    lineCount = 0;
                    rto->notRecognizedCounter = 0;
                    break;
                }
                detectedMode = GBS::STATUS_00::read();
                if ((detectedMode & 0x2F) != 0x07) {
                    lineCount = 0;
                    rto->notRecognizedCounter = 0;
                    break;
                }
            }
            if (lineCount != 0 && rto->notRecognizedCounter < 255) {
                rto->notRecognizedCounter++;
            }
        } else {
            rto->notRecognizedCounter = 0;
        }
    } else {
        rto->notRecognizedCounter = 0;
    }

    if (rto->notRecognizedCounter == 255) {
        return 9;
    }

    return 0; // unknown mode
}

// if testbus has 0x05, sync is present and line counting active. if it has 0x04, sync is present but no line counting
boolean getSyncPresent()
{
    uint8_t debug_backup = GBS::TEST_BUS_SEL::read();
    uint8_t debug_backup_SP = GBS::TEST_BUS_SP_SEL::read();
    if (debug_backup != 0xa) {
        GBS::TEST_BUS_SEL::write(0xa);
    }
    if (debug_backup_SP != 0x0f) {
        GBS::TEST_BUS_SP_SEL::write(0x0f);
    }

    uint16_t readout = GBS::TEST_BUS::read();

    if (debug_backup != 0xa) {
        GBS::TEST_BUS_SEL::write(debug_backup);
    }
    if (debug_backup_SP != 0x0f) {
        GBS::TEST_BUS_SP_SEL::write(debug_backup_SP);
    }
    //if (((readout & 0x0500) == 0x0500) || ((readout & 0x0500) == 0x0400)) {
    if (readout > 0x0180) {
        return true;
    }

    return false;
}

// returns 0_00 bit 2 = H+V both stable (for the IF, not SP)
boolean getStatus00IfHsVsStable()
{
    return ((GBS::STATUS_00::read() & 0x04) == 0x04) ? 1 : 0;
}

// used to be a check for the length of the debug bus readout of 5_63 = 0x0f
// now just checks the chip status at 0_16 HS active (and Interrupt bit4 HS active for RGBHV)
boolean getStatus16SpHsStable()
{
    if (rto->videoStandardInput == 15) { // check RGBHV first
        if (GBS::STATUS_INT_INP_NO_SYNC::read() == 0) {
            return true;
        } else {
            resetInterruptNoHsyncBadBit();
            return false;
        }
    }

    // STAT_16 bit 1 is the "hsync active" flag, which appears to be a reliable indicator
    // checking the flag replaces checking the debug bus pulse length manually
    uint8_t status16 = GBS::STATUS_16::read();
    if ((status16 & 0x02) == 0x02) {
        if (rto->videoStandardInput == 1 || rto->videoStandardInput == 2) {
            if ((status16 & 0x01) != 0x01) { // pal / ntsc should be sync active low
                return true;
            }
        } else {
            return true; // not pal / ntsc
        }
    }

    return false;
}

void goLowPowerWithInputDetection()
{
    GBS::OUT_SYNC_CNTRL::write(0); // no H / V sync out to PAD
    GBS::DAC_RGBS_PWDNZ::write(0); // direct disableDAC()
    //zeroAll();
    setResetParameters(); // includes rto->videoStandardInput = 0
    prepareSyncProcessor();
    delay(100);
    rto->isInLowPowerMode = true;
    SerialM.println(F("Scanning inputs for sources ..."));
    LEDOFF;
}

void optimizeSogLevel()
{
    if (rto->boardHasPower == false) // checkBoardPower is too invasive now
    {
        rto->thisSourceMaxLevelSOG = rto->currentLevelSOG = 13;
        return;
    }
    if (rto->videoStandardInput == 15 || GBS::SP_SOG_MODE::read() != 1 || rto->syncTypeCsync == false) {
        rto->thisSourceMaxLevelSOG = rto->currentLevelSOG = 13;
        return;
    }

    if (rto->inputIsYpBpR) {
        rto->thisSourceMaxLevelSOG = rto->currentLevelSOG = 14;
    } else {
        rto->thisSourceMaxLevelSOG = rto->currentLevelSOG = 13; // similar to yuv, allow variations
    }
    setAndUpdateSogLevel(rto->currentLevelSOG);

    uint8_t debug_backup = GBS::TEST_BUS_SEL::read();
    uint8_t debug_backup_SP = GBS::TEST_BUS_SP_SEL::read();
    if (debug_backup != 0xa) {
        GBS::TEST_BUS_SEL::write(0xa);
        delay(1);
    }
    if (debug_backup_SP != 0x0f) {
        GBS::TEST_BUS_SP_SEL::write(0x0f);
        delay(1);
    }

    GBS::TEST_BUS_EN::write(1);

    delay(100);
    while (1) {
        uint16_t syncGoodCounter = 0;
        unsigned long timeout = millis();
        while ((millis() - timeout) < 60) {
            if (GBS::STATUS_SYNC_PROC_HSACT::read() == 1) {
                syncGoodCounter++;
                if (syncGoodCounter >= 60) {
                    break;
                }
            } else if (syncGoodCounter >= 4) {
                syncGoodCounter -= 3;
            }
        }

        if (syncGoodCounter >= 60) {
            syncGoodCounter = 0;
            //if (getVideoMode() != 0) {
            if (GBS::TEST_BUS_2F::read() > 0) {
                delay(20);
                for (int a = 0; a < 50; a++) {
                    syncGoodCounter++;
                    if (GBS::STATUS_SYNC_PROC_HSACT::read() == 0 || GBS::TEST_BUS_2F::read() == 0) {
                        syncGoodCounter = 0;
                        break;
                    }
                }
                if (syncGoodCounter >= 49) {
                    //SerialM.print("OK @SOG "); SerialM.println(rto->currentLevelSOG); printInfo();
                    break; // found, exit
                } else {
                    //Serial.print(" inner test failed syncGoodCounter: "); Serial.println(syncGoodCounter);
                }
            } else { // getVideoMode() == 0
                     //Serial.print("sog-- syncGoodCounter: "); Serial.println(syncGoodCounter);
            }
        } else { // syncGoodCounter < 40
                 //Serial.print("outer test failed syncGoodCounter: "); Serial.println(syncGoodCounter);
        }

        if (rto->currentLevelSOG >= 2) {
            rto->currentLevelSOG -= 1;
            setAndUpdateSogLevel(rto->currentLevelSOG);
            delay(8); // time for sog to settle
        } else {
            rto->currentLevelSOG = 13; // leave at default level
            setAndUpdateSogLevel(rto->currentLevelSOG);
            delay(8);
            break; // break and exit
        }
    }

    rto->thisSourceMaxLevelSOG = rto->currentLevelSOG;
    if (rto->thisSourceMaxLevelSOG == 0) {
        rto->thisSourceMaxLevelSOG = 1; // fail safe
    }

    if (debug_backup != 0xa) {
        GBS::TEST_BUS_SEL::write(debug_backup);
    }
    if (debug_backup_SP != 0x0f) {
        GBS::TEST_BUS_SP_SEL::write(debug_backup_SP);
    }
}

boolean optimizePhaseSP()
{
    uint16_t pixelClock = GBS::PLLAD_MD::read();
    uint8_t badHt = 0, prevBadHt = 0, worstBadHt = 0, worstPhaseSP = 0, prevPrevBadHt = 0, goodHt = 0;
    boolean runTest = 1;

    if (GBS::STATUS_SYNC_PROC_HTOTAL::read() < (pixelClock - 8)) {
        return 0;
    }
    if (GBS::STATUS_SYNC_PROC_HTOTAL::read() > (pixelClock + 8)) {
        return 0;
    }

    if (rto->currentLevelSOG <= 2) {
        // not very stable, use fixed values
        rto->phaseSP = 16;
        rto->phaseADC = 16;
        if (rto->videoStandardInput > 0 && rto->videoStandardInput <= 4) {
            if (rto->osr == 4) {
                rto->phaseADC += 16;
                rto->phaseADC &= 0x1f;
            }
        }
        delay(8);    // a bit longer, to match default run time
        runTest = 0; // skip to end
    }

    //unsigned long startTime = millis();

    if (runTest) {
        // 32 distinct phase settings, 3 average samples (missing 2 phase steps) > 34
        for (uint8_t u = 0; u < 34; u++) {
            rto->phaseSP++;
            rto->phaseSP &= 0x1f;
            setAndLatchPhaseSP();
            badHt = 0;
            delayMicroseconds(256);
            for (uint8_t i = 0; i < 20; i++) {
                if (GBS::STATUS_SYNC_PROC_HTOTAL::read() != pixelClock) {
                    badHt++;
                    delayMicroseconds(384);
                }
            }
            // if average 3 samples has more badHt than seen yet, this phase step is worse
            if ((badHt + prevBadHt + prevPrevBadHt) > worstBadHt) {
                worstBadHt = (badHt + prevBadHt + prevPrevBadHt);
                worstPhaseSP = (rto->phaseSP - 1) & 0x1f; // medium of 3 samples
            }

            if (badHt == 0) {
                // count good readings as well, to know whether the entire run is valid
                goodHt++;
            }

            prevPrevBadHt = prevBadHt;
            prevBadHt = badHt;
            //Serial.print(rto->phaseSP); Serial.print(" badHt: "); Serial.println(badHt);
        }

        //Serial.println(goodHt);

        if (goodHt < 17) {
            //Serial.println("pxClk unstable");
            return 0;
        }

        // adjust global phase values according to test results
        if (worstBadHt != 0) {
            rto->phaseSP = (worstPhaseSP + 16) & 0x1f;
            // assume color signals arrive at same time: phase adc = phase sp
            // test in hdbypass mode shows this is more related to sog.. the assumptions seem fine at sog = 8
            rto->phaseADC = 16; //(rto->phaseSP) & 0x1f;

            // different OSR require different phase angles, also depending on bypass, etc
            // shift ADC phase 180 degrees for the following
            if (rto->videoStandardInput >= 5 && rto->videoStandardInput <= 7) {
                if (rto->osr == 2) {
                    //Serial.println("shift adc phase");
                    rto->phaseADC += 16;
                    rto->phaseADC &= 0x1f;
                }
            } else if (rto->videoStandardInput > 0 && rto->videoStandardInput <= 4) {
                if (rto->osr == 4) {
                    //Serial.println("shift adc phase");
                    rto->phaseADC += 16;
                    rto->phaseADC &= 0x1f;
                }
            }
        } else {
            // test was always good, so choose any reasonable value
            rto->phaseSP = 16;
            rto->phaseADC = 16;
            if (rto->videoStandardInput > 0 && rto->videoStandardInput <= 4) {
                if (rto->osr == 4) {
                    rto->phaseADC += 16;
                    rto->phaseADC &= 0x1f;
                }
            }
        }
    }

    //Serial.println(millis() - startTime);
    //Serial.print("worstPhaseSP: "); Serial.println(worstPhaseSP);

    /*static uint8_t lastLevelSOG = 99;
  if (lastLevelSOG != rto->currentLevelSOG) {
    SerialM.print("Phase: "); SerialM.print(rto->phaseSP);
    SerialM.print(" SOG: ");  SerialM.print(rto->currentLevelSOG);
    SerialM.println();
  }
  lastLevelSOG = rto->currentLevelSOG;*/

    setAndLatchPhaseSP();
    delay(1);
    setAndLatchPhaseADC();

    return 1;
}

