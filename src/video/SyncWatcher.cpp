#include "SyncWatcher.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../hardware/LEDControl.h"
#include "../gbs/GBSRegister.h"
#include "../gbs/GBSPresets.h"
#include "../gbs/GBSSync.h"
#include "../gbs/GBSPhase.h"
#include "../gbs/GBSController.h"
#include "../gbs/GBSVideoProcessing.h"
#include "../video/VideoInput.h"
#include "../video/VideoTiming.h"
#include "../video/VideoScaling.h"
#include "../video/Scanlines.h"
#include "../video/Deinterlacing.h"
#include "../clock/Si5351Manager.h"
#include "../wifi/WiFiManager.h"
#include "../utils/DebugHelpers.h"
#include "../gbs/tv5725.h"
#include "framesync.h"
#include "../../lib/si5351mcu/si5351mcu.h"
#include <Arduino.h>
#include <Wire.h>

typedef TV5725<GBS_ADDR> GBS;

void fastSogAdjust()
{
    if (rto->noSyncCounter <= 5) {
        uint8_t debug_backup = GBS::TEST_BUS_SEL::read();
        uint8_t debug_backup_SP = GBS::TEST_BUS_SP_SEL::read();
        if (debug_backup != 0xa) {
            GBS::TEST_BUS_SEL::write(0xa);
        }
        if (debug_backup_SP != 0x0f) {
            GBS::TEST_BUS_SP_SEL::write(0x0f);
        }

        if ((GBS::TEST_BUS_2F::read() & 0x05) != 0x05) {
            while ((GBS::TEST_BUS_2F::read() & 0x05) != 0x05) {
                if (rto->currentLevelSOG >= 4) {
                    rto->currentLevelSOG -= 2;
                } else {
                    rto->currentLevelSOG = 13;
                    setAndUpdateSogLevel(rto->currentLevelSOG);
                    delay(40);
                    break; // abort / restart next round
                }
                setAndUpdateSogLevel(rto->currentLevelSOG);
                delay(28); // 4
            }
            delay(10);
        }

        if (debug_backup != 0xa) {
            GBS::TEST_BUS_SEL::write(debug_backup);
        }
        if (debug_backup_SP != 0x0f) {
            GBS::TEST_BUS_SP_SEL::write(debug_backup_SP);
        }
    }
}

boolean snapToIntegralFrameRate(void)
{
    // Fetch the current output frame rate
    float ofr = getOutputFrameRate();

    if (ofr < 1.0f) {
        delay(1);
        ofr = getOutputFrameRate();
    }

    float target;
    if (ofr > 56.5f && ofr < 64.5f) {
        target = 60.0f; // NTSC like
    } else if (ofr > 46.5f && ofr < 54.5f) {
        target = 50.0f; // PAL like
    } else {
        // too far out of spec for an auto adjust
        SerialM.println(F("out of bounds"));
        return false;
    }

    SerialM.print(F("Snap to "));
    SerialM.print(target, 1); // precission 1
    SerialM.println("Hz");

    // We'll be adjusting the htotal incrementally, so store current and best match.
    uint16_t currentHTotal = GBS::VDS_HSYNC_RST::read();
    uint16_t closestHTotal = currentHTotal;

    // What's the closest we've been to the frame rate?
    float closestDifference = fabs(target - ofr);

    // Repeatedly adjust htotals until we find the closest match.
    for (;;) {
        delay(0);

        // Try to move closer to the desired framerate.
        if (target > ofr) {
            if (currentHTotal > 0 && applyBestHTotal(currentHTotal - 1)) {
                --currentHTotal;
            } else {
                return false;
            }
        } else if (target < ofr) {
            if (currentHTotal < 4095 && applyBestHTotal(currentHTotal + 1)) {
                ++currentHTotal;
            } else {
                return false;
            }
        } else {
            return true;
        }

        // Are we closer?
        ofr = getOutputFrameRate();

        if (ofr < 1.0f) {
            delay(1);
            ofr = getOutputFrameRate();
        }
        if (ofr < 1.0f) {
            return false;
        }

        // If we're getting closer, continue trying, otherwise break out of the test loop.
        float newDifference = fabs(target - ofr);
        if (newDifference < closestDifference) {
            closestDifference = newDifference;
            closestHTotal = currentHTotal;
        } else {
            break;
        }
    }

    // Reapply the closest htotal if need be.
    if (closestHTotal != currentHTotal) {
        applyBestHTotal(closestHTotal);
    }

    return true;
}

boolean checkBoardPower()
{
    GBS::ADC_UNUSED_69::write(0x6a); // 0110 1010
    if (GBS::ADC_UNUSED_69::read() == 0x6a) {
        GBS::ADC_UNUSED_69::write(0);
        return 1;
    }

    GBS::ADC_UNUSED_69::write(0); // attempt to clear
    if (rto->boardHasPower == true) {
        Serial.println(F("! power / i2c lost !"));
    }

    return 0;
}

void calibrateAdcOffset()
{
    GBS::PAD_BOUT_EN::write(0);          // disable output to pin for test
    GBS::PLL648_CONTROL_01::write(0xA5); // display clock to adc = 162mhz
    GBS::ADC_INPUT_SEL::write(2);        // 10 > R2/G2/B2 as input (not connected, so to isolate ADC)
    GBS::DEC_MATRIX_BYPS::write(1);
    GBS::DEC_TEST_ENABLE::write(1);
    GBS::ADC_5_03::write(0x31);    // bottom clamps, filter max (40mhz)
    GBS::ADC_TEST_04::write(0x00); // disable bit 1
    GBS::SP_CS_CLP_ST::write(0x00);
    GBS::SP_CS_CLP_SP::write(0x00);
    GBS::SP_5_56::write(0x05); // SP_SOG_MODE needs to be 1
    GBS::SP_5_57::write(0x80);
    GBS::ADC_5_00::write(0x02);
    GBS::TEST_BUS_SEL::write(0x0b); // 0x2b
    GBS::TEST_BUS_EN::write(1);
    resetDigital();

    uint16_t hitTargetCounter = 0;
    uint16_t readout16 = 0;
    uint8_t missTargetCounter = 0;
    uint8_t readout = 0;

    GBS::ADC_RGCTRL::write(0x7F);
    GBS::ADC_GGCTRL::write(0x7F);
    GBS::ADC_BGCTRL::write(0x7F);
    GBS::ADC_ROFCTRL::write(0x7F);
    GBS::ADC_GOFCTRL::write(0x3D); // start
    GBS::ADC_BOFCTRL::write(0x7F);
    GBS::DEC_TEST_SEL::write(1); // 5_1f = 0x1c

    //unsigned long overallTimer = millis();
    unsigned long startTimer = 0;
    for (uint8_t i = 0; i < 3; i++) {
        missTargetCounter = 0;
        hitTargetCounter = 0;
        delay(20);
        startTimer = millis();

        // loop breaks either when the timer runs out, or hitTargetCounter reaches target
        while ((millis() - startTimer) < 800) {
            readout16 = GBS::TEST_BUS::read() & 0x7fff;
            //Serial.println(readout16, HEX);
            // readout16 is unsigned, always >= 0
            if (readout16 < 7) {
                hitTargetCounter++;
                missTargetCounter = 0;
            } else if (missTargetCounter++ > 2) {
                if (i == 0) {
                    GBS::ADC_GOFCTRL::write(GBS::ADC_GOFCTRL::read() + 1); // incr. offset
                    readout = GBS::ADC_GOFCTRL::read();
                    Serial.print(" G: ");
                } else if (i == 1) {
                    GBS::ADC_ROFCTRL::write(GBS::ADC_ROFCTRL::read() + 1);
                    readout = GBS::ADC_ROFCTRL::read();
                    Serial.print(" R: ");
                } else if (i == 2) {
                    GBS::ADC_BOFCTRL::write(GBS::ADC_BOFCTRL::read() + 1);
                    readout = GBS::ADC_BOFCTRL::read();
                    Serial.print(" B: ");
                }
                Serial.print(readout, HEX);

                if (readout >= 0x52) {
                    // some kind of failure
                    break;
                }

                delay(10);
                hitTargetCounter = 0;
                missTargetCounter = 0;
                startTimer = millis(); // extend timer
            }
            if (hitTargetCounter > 1500) {
                break;
            }
        }
        if (i == 0) {
            // G done, prep R
            adco->g_off = GBS::ADC_GOFCTRL::read();
            GBS::ADC_GOFCTRL::write(0x7F);
            GBS::ADC_ROFCTRL::write(0x3D);
            GBS::DEC_TEST_SEL::write(2); // 5_1f = 0x2c
        }
        if (i == 1) {
            adco->r_off = GBS::ADC_ROFCTRL::read();
            GBS::ADC_ROFCTRL::write(0x7F);
            GBS::ADC_BOFCTRL::write(0x3D);
            GBS::DEC_TEST_SEL::write(3); // 5_1f = 0x3c
        }
        if (i == 2) {
            adco->b_off = GBS::ADC_BOFCTRL::read();
        }
        Serial.println("");
    }

    if (readout >= 0x52) {
        // there was a problem; revert
        adco->r_off = adco->g_off = adco->b_off = 0x40;
    }

    GBS::ADC_GOFCTRL::write(adco->g_off);
    GBS::ADC_ROFCTRL::write(adco->r_off);
    GBS::ADC_BOFCTRL::write(adco->b_off);
}

void runSyncWatcher()
{
    if (!rto->boardHasPower) {
        return;
    }

    static uint8_t newVideoModeCounter = 0;
    static uint16_t activeStableLineCount = 0;
    static unsigned long lastSyncDrop = millis();
    static unsigned long lastLineCountMeasure = millis();

    uint16_t thisStableLineCount = 0;
    uint8_t detectedVideoMode = getVideoMode();
    boolean status16SpHsStable = getStatus16SpHsStable();

    if (rto->outModeHdBypass && status16SpHsStable) {
        if (videoStandardInputIsPalNtscSd()) {
            if (millis() - lastLineCountMeasure > 765) {
                thisStableLineCount = GBS::STATUS_SYNC_PROC_VTOTAL::read();
                for (uint8_t i = 0; i < 3; i++) {
                    delay(2);
                    if (GBS::STATUS_SYNC_PROC_VTOTAL::read() < (thisStableLineCount - 3) ||
                        GBS::STATUS_SYNC_PROC_VTOTAL::read() > (thisStableLineCount + 3)) {
                        thisStableLineCount = 0;
                        break;
                    }
                }

                if (thisStableLineCount != 0) {
                    if (thisStableLineCount < (activeStableLineCount - 3) ||
                        thisStableLineCount > (activeStableLineCount + 3)) {
                        activeStableLineCount = thisStableLineCount;
                        if (activeStableLineCount < 230 || activeStableLineCount > 340) {
                            // only doing NTSC/PAL currently, an unusual line count probably means a format change
                            setCsVsStart(1);
                            if (getCsVsStop() == 1) {
                                setCsVsStop(2);
                            }
                            // MD likes to get stuck as usual
                            nudgeMD();
                        } else {
                            setCsVsStart(thisStableLineCount - 9);
                        }

                        Serial.printf("HDBypass CsVsSt: %d\n", getCsVsStart());
                        delay(150);
                    }
                }

                lastLineCountMeasure = millis();
            }
        }
    }

    if (rto->videoStandardInput == 13) { // using flaky graphic modes
        if (detectedVideoMode == 0) {
            if (GBS::STATUS_INT_SOG_BAD::read() == 0) {
                detectedVideoMode = 13; // then keep it
            }
        }
    }

    static unsigned long preemptiveSogWindowStart = millis();
    static const uint16_t sogWindowLen = 3000; // ms
    static uint16_t badHsActive = 0;
    static boolean lastAdjustWasInActiveWindow = 0;

    if (rto->syncTypeCsync && !rto->inputIsYpBpR && (newVideoModeCounter == 0)) {
        // look for SOG instability
        if (GBS::STATUS_INT_SOG_BAD::read() == 1 || GBS::STATUS_INT_SOG_SW::read() == 1) {
            resetInterruptSogSwitchBit();
            if ((millis() - preemptiveSogWindowStart) > sogWindowLen) {
                // start new window
                preemptiveSogWindowStart = millis();
                badHsActive = 0;
            }
            lastVsyncLock = millis(); // best reset this
        }

        if ((millis() - preemptiveSogWindowStart) < sogWindowLen) {
            for (uint8_t i = 0; i < 16; i++) {
                if (GBS::STATUS_INT_SOG_BAD::read() == 1 || GBS::STATUS_SYNC_PROC_HSACT::read() == 0) {
                    resetInterruptSogBadBit();
                    uint16_t hlowStart = GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
                    if (rto->videoStandardInput == 0)
                        hlowStart = 777; // fix initial state no HLOW_LEN
                    for (int a = 0; a < 20; a++) {
                        if (GBS::STATUS_SYNC_PROC_HLOW_LEN::read() != hlowStart) {
                            // okay, source still active so count this one, break back to outer for loop
                            badHsActive++;
                            lastVsyncLock = millis(); // delay this
                            //Serial.print(badHsActive); Serial.print(" ");
                            break;
                        }
                    }
                }
                if ((i % 3) == 0) {
                    delay(1);
                } else {
                    delay(0);
                }
            }

            if (badHsActive >= 17) {
                if (rto->currentLevelSOG >= 2) {
                    rto->currentLevelSOG -= 1;
                    setAndUpdateSogLevel(rto->currentLevelSOG);
                    delay(30);
                    updateSpDynamic(0);
                    badHsActive = 0;
                    lastAdjustWasInActiveWindow = 1;
                } else if (badHsActive > 40) {
                    optimizeSogLevel();
                    badHsActive = 0;
                    lastAdjustWasInActiveWindow = 1;
                }
                preemptiveSogWindowStart = millis(); // restart window
            }
        } else if (lastAdjustWasInActiveWindow) {
            lastAdjustWasInActiveWindow = 0;
            if (rto->currentLevelSOG >= 8) {
                rto->currentLevelSOG -= 1;
                setAndUpdateSogLevel(rto->currentLevelSOG);
                delay(30);
                updateSpDynamic(0);
                badHsActive = 0;
                rto->phaseIsSet = 0;
            }
        }
    }

    if ((detectedVideoMode == 0 || !status16SpHsStable) && rto->videoStandardInput != 15) {
        rto->noSyncCounter++;
        rto->continousStableCounter = 0;
        lastVsyncLock = millis(); // best reset this
        if (rto->noSyncCounter == 1) {
            freezeVideo();
            return; // do nothing else
        }

        rto->phaseIsSet = 0;

        if (rto->noSyncCounter <= 3 || GBS::STATUS_SYNC_PROC_HSACT::read() == 0) {
            freezeVideo();
        }

        if (newVideoModeCounter == 0) {
            LEDOFF; // LEDOFF on sync loss

            if (rto->noSyncCounter == 2) { // this usually repeats
                //printInfo(); printInfo(); SerialM.println();
                //rto->printInfos = 0;
                if ((millis() - lastSyncDrop) > 1500) { // minimum space between runs
                    if (rto->printInfos == false) {
                        SerialM.print("\n.");
                    }
                } else {
                    if (rto->printInfos == false) {
                        SerialM.print(".");
                    }
                }

                // if sog is lowest, adjust up
                if (rto->currentLevelSOG <= 1 && videoStandardInputIsPalNtscSd()) {
                    rto->currentLevelSOG += 1;
                    setAndUpdateSogLevel(rto->currentLevelSOG);
                    delay(30);
                }
                lastSyncDrop = millis(); // restart timer
            }
        }

        if (rto->noSyncCounter == 8) {
            GBS::SP_H_CST_ST::write(0x10);
            GBS::SP_H_CST_SP::write(0x100);
            //GBS::SP_H_PROTECT::write(1);  // at noSyncCounter = 32 will alternate on / off
            if (videoStandardInputIsPalNtscSd()) {
                // this can early detect mode changes (before updateSpDynamic resets all)
                GBS::SP_PRE_COAST::write(9);
                GBS::SP_POST_COAST::write(9);
                // new: test SD<>EDTV changes
                uint8_t ignore = GBS::SP_H_PULSE_IGNOR::read();
                if (ignore >= 0x33) {
                    GBS::SP_H_PULSE_IGNOR::write(ignore / 2);
                }
            }
            rto->coastPositionIsSet = 0;
        }

        if (rto->noSyncCounter % 27 == 0) {
            // the * check needs to be first (go before auto sog level) to support SD > HDTV detection
            SerialM.print("*");
            updateSpDynamic(1);
        }

        if (rto->noSyncCounter % 32 == 0) {
            if (GBS::STATUS_SYNC_PROC_HSACT::read() == 1) {
                unfreezeVideo();
            } else {
                freezeVideo();
            }
        }

        if (rto->inputIsYpBpR && (rto->noSyncCounter == 34)) {
            GBS::SP_NO_CLAMP_REG::write(1); // unlock clamp
            rto->clampPositionIsSet = false;
        }

        if (rto->noSyncCounter == 38) {
            nudgeMD();
        }

        if (rto->syncTypeCsync) {
            if (rto->noSyncCounter > 47) {
                if (rto->noSyncCounter % 16 == 0) {
                    GBS::SP_H_PROTECT::write(!GBS::SP_H_PROTECT::read());
                }
            }
        }

        if (rto->noSyncCounter % 150 == 0) {
            if (rto->noSyncCounter == 150 || rto->noSyncCounter % 900 == 0) {
                SerialM.print("\nno signal\n");
                // check whether discrete VSync is present. if so, need to go to input detect
                uint8_t extSyncBackup = GBS::SP_EXT_SYNC_SEL::read();
                GBS::SP_EXT_SYNC_SEL::write(0);
                delay(240);
                printInfo();
                if (GBS::STATUS_SYNC_PROC_VSACT::read() == 1) {
                    delay(10);
                    if (GBS::STATUS_SYNC_PROC_VSACT::read() == 1) {
                        rto->noSyncCounter = 0x07fe;
                    }
                }
                GBS::SP_EXT_SYNC_SEL::write(extSyncBackup);
            }
            GBS::SP_H_COAST::write(0);   // 5_3e 2
            GBS::SP_H_PROTECT::write(0); // 5_3e 4
            GBS::SP_H_CST_ST::write(0x10);
            GBS::SP_H_CST_SP::write(0x100); // instead of disabling 5_3e 5 coast
            GBS::SP_CS_CLP_ST::write(32);   // neutral clamp values
            GBS::SP_CS_CLP_SP::write(48);   //
            updateSpDynamic(1);
            nudgeMD(); // can fix MD not noticing a line count update
            delay(80);

            // prepare optimizeSogLevel
            // use STATUS_SYNC_PROC_HLOW_LEN changes to determine whether source is still active
            uint16_t hlowStart = GBS::STATUS_SYNC_PROC_HLOW_LEN::read();
            if (GBS::PLLAD_VCORST::read() == 1) {
                // exception: we're in startup and pllad isn't locked yet > HLOW_LEN always 0
                hlowStart = 777; // now it'll run optimizeSogLevel if needed
            }
            for (int a = 0; a < 128; a++) {
                if (GBS::STATUS_SYNC_PROC_HLOW_LEN::read() != hlowStart) {
                    // source still there
                    if (rto->noSyncCounter % 450 == 0) {
                        rto->currentLevelSOG = 0; // worst case, sometimes necessary, will be unstable but at least detect
                        setAndUpdateSogLevel(rto->currentLevelSOG);
                    } else {
                        optimizeSogLevel();
                    }
                    break;
                } else if (a == 127) {
                    // set sog to be able to see something
                    rto->currentLevelSOG = 5;
                    setAndUpdateSogLevel(rto->currentLevelSOG);
                }
                delay(0);
            }

            resetSyncProcessor();
            delay(8);
            resetModeDetect();
            delay(8);
        }

        // long no signal time, check other input
        if (rto->noSyncCounter % 413 == 0) {
            if (GBS::ADC_INPUT_SEL::read() == 1) {
                GBS::ADC_INPUT_SEL::write(0);
            } else {
                GBS::ADC_INPUT_SEL::write(1);
            }
            delay(40);
            unsigned long timeout = millis();
            while (millis() - timeout <= 210) {
                if (getStatus16SpHsStable()) {
                    rto->noSyncCounter = 0x07fe; // will cause a return
                    break;
                }
                handleWiFi(0);
                delay(1);
            }

            if (millis() - timeout > 210) {
                if (GBS::ADC_INPUT_SEL::read() == 1) {
                    GBS::ADC_INPUT_SEL::write(0);
                } else {
                    GBS::ADC_INPUT_SEL::write(1);
                }
            }
        }

        newVideoModeCounter = 0;
        // sog unstable check end
    }

    // if format changed to valid, potentially new video mode
    if (((detectedVideoMode != 0 && detectedVideoMode != rto->videoStandardInput) ||
         (detectedVideoMode != 0 && rto->videoStandardInput == 0)) &&
        rto->videoStandardInput != 15) {
        // before thoroughly checking for a mode change, watch format via newVideoModeCounter
        if (newVideoModeCounter < 255) {
            newVideoModeCounter++;
            rto->continousStableCounter = 0; // usually already 0, but occasionally not
            if (newVideoModeCounter > 1) {   // help debug a few commits worth
                if (newVideoModeCounter == 2) {
                    SerialM.println();
                }
                SerialM.print(newVideoModeCounter);
            }
            if (newVideoModeCounter == 3) {
                freezeVideo();
                GBS::SP_H_CST_ST::write(0x10);
                GBS::SP_H_CST_SP::write(0x100);
                rto->coastPositionIsSet = 0;
                delay(10);
                if (getVideoMode() == 0) {
                    updateSpDynamic(1); // check ntsc to 480p and back
                    delay(40);
                }
            }
        }

        if (newVideoModeCounter >= 8) {
            uint8_t vidModeReadout = 0;
            SerialM.print(F("\nFormat change:"));
            for (int a = 0; a < 30; a++) {
                vidModeReadout = getVideoMode();
                if (vidModeReadout == 13) {
                    newVideoModeCounter = 5;
                } // treat ps2 quasi rgb as stable
                if (vidModeReadout != detectedVideoMode) {
                    newVideoModeCounter = 0;
                }
            }
            if (newVideoModeCounter != 0) {
                // apply new mode
                SerialM.print(" ");
                SerialM.print(vidModeReadout);
                SerialM.println(F(" <stable>"));
                //Serial.print("Old: "); Serial.print(rto->videoStandardInput);
                //Serial.print(" New: "); Serial.println(detectedVideoMode);
                rto->videoIsFrozen = false;

                if (GBS::SP_SOG_MODE::read() == 1) {
                    rto->syncTypeCsync = true;
                } else {
                    rto->syncTypeCsync = false;
                }
                boolean wantPassThroughMode = uopt->presetPreference == 10;

                if (((rto->videoStandardInput == 1 || rto->videoStandardInput == 3) && (detectedVideoMode == 2 || detectedVideoMode == 4)) ||
                    rto->videoStandardInput == 0 ||
                    ((rto->videoStandardInput == 2 || rto->videoStandardInput == 4) && (detectedVideoMode == 1 || detectedVideoMode == 3))) {
                    rto->useHdmiSyncFix = 1;
                    //SerialM.println("hdmi sync fix: yes");
                } else {
                    rto->useHdmiSyncFix = 0;
                    //SerialM.println("hdmi sync fix: no");
                }

                if (!wantPassThroughMode) {
                    // needs to know the sync type for early updateclamp (set above)
                    applyPresets(detectedVideoMode);
                } else {
                    rto->videoStandardInput = detectedVideoMode;
                    setOutModeHdBypass(false);
                }
                rto->videoStandardInput = detectedVideoMode;
                rto->noSyncCounter = 0;
                rto->continousStableCounter = 0; // also in postloadsteps
                newVideoModeCounter = 0;
                activeStableLineCount = 0;
                delay(20); // post delay
                badHsActive = 0;
                preemptiveSogWindowStart = millis();
            } else {
                unfreezeVideo(); // (whops)
                SerialM.print(" ");
                SerialM.print(vidModeReadout);
                SerialM.println(F(" <not stable>"));
                printInfo();
                newVideoModeCounter = 0;
                if (rto->videoStandardInput == 0) {
                    // if we got here from standby mode, return there soon
                    // but occasionally, this is a regular new mode that needs a SP parameter change to work
                    // ie: 1080p needs longer post coast, which the syncwatcher loop applies at some point
                    rto->noSyncCounter = 0x05ff; // give some time in normal loop
                }
            }
        }
    } else if (getStatus16SpHsStable() && detectedVideoMode != 0 && rto->videoStandardInput != 15 && (rto->videoStandardInput == detectedVideoMode)) {
        // last used mode reappeared / stable again
        if (rto->continousStableCounter < 255) {
            rto->continousStableCounter++;
        }

        static boolean doFullRestore = 0;
        if (rto->noSyncCounter >= 150) {
            // source was gone for longer // clamp will be updated at continousStableCounter 50
            rto->coastPositionIsSet = false;
            rto->phaseIsSet = false;
            FrameSync::reset(uopt->frameTimeLockMethod);
            doFullRestore = 1;
            SerialM.println();
        }

        rto->noSyncCounter = 0;
        newVideoModeCounter = 0;

        if (rto->continousStableCounter == 1 && !doFullRestore) {
            rto->videoIsFrozen = true; // ensures unfreeze
            unfreezeVideo();
        }

        if (rto->continousStableCounter == 2) {
            updateSpDynamic(0);
            if (doFullRestore) {
                delay(20);
                optimizeSogLevel();
                doFullRestore = 0;
            }
            rto->videoIsFrozen = true; // ensures unfreeze
            unfreezeVideo();           // called 2nd time here to make sure
        }

        if (rto->continousStableCounter == 4) {
            LEDON;
        }

        if (!rto->phaseIsSet) {
            if (rto->continousStableCounter >= 10 && rto->continousStableCounter < 61) {
                // added < 61 to make a window, else sources with little pll lock hammer this
                if ((rto->continousStableCounter % 10) == 0) {
                    rto->phaseIsSet = optimizePhaseSP();
                }
            }
        }

        // 5_3e 2 SP_H_COAST test
        //if (rto->continousStableCounter == 11) {
        //  if (rto->coastPositionIsSet) {
        //    GBS::SP_H_COAST::write(1);
        //  }
        //}

        if (rto->continousStableCounter == 160) {
            resetInterruptSogBadBit();
        }

        if (rto->continousStableCounter == 45) {
            GBS::ADC_UNUSED_67::write(0); // clear sync fix temp registers (67/68)
            //rto->coastPositionIsSet = 0; // leads to a flicker
            rto->clampPositionIsSet = 0; // run updateClampPosition occasionally
        }

        if (rto->continousStableCounter % 31 == 0) {
            // new: 8 regular interval checks up until 255
            updateSpDynamic(0);
        }

        if (rto->continousStableCounter >= 3) {
            if ((rto->videoStandardInput == 1 || rto->videoStandardInput == 2) &&
                !rto->outModeHdBypass && rto->noSyncCounter == 0) {
                // deinterlacer and scanline code
                static uint8_t timingAdjustDelay = 0;
                static uint8_t oddEvenWhenArmed = 0;
                boolean preventScanlines = 0;

                if (rto->deinterlaceAutoEnabled) {
                    uint16_t VPERIOD_IF = GBS::VPERIOD_IF::read();
                    static uint8_t filteredLineCountMotionAdaptiveOn = 0, filteredLineCountMotionAdaptiveOff = 0;
                    static uint16_t VPERIOD_IF_OLD = VPERIOD_IF; // for glitch filter

                    if (VPERIOD_IF_OLD != VPERIOD_IF) {
                        //freezeVideo(); // glitch filter
                        preventScanlines = 1;
                        filteredLineCountMotionAdaptiveOn = 0;
                        filteredLineCountMotionAdaptiveOff = 0;
                        if (uopt->enableFrameTimeLock || rto->extClockGenDetected) {
                            if (uopt->deintMode == 1) { // using bob
                                timingAdjustDelay = 11; // arm timer (always)
                                oddEvenWhenArmed = VPERIOD_IF % 2;
                            }
                        }
                    }

                    if (VPERIOD_IF == 522 || VPERIOD_IF == 524 || VPERIOD_IF == 526 ||
                        VPERIOD_IF == 622 || VPERIOD_IF == 624 || VPERIOD_IF == 626) { // ie v:524, even counts > enable
                        filteredLineCountMotionAdaptiveOn++;
                        filteredLineCountMotionAdaptiveOff = 0;
                        if (filteredLineCountMotionAdaptiveOn >= 2) // at least >= 2
                        {
                            if (uopt->deintMode == 0 && !rto->motionAdaptiveDeinterlaceActive) {
                                if (GBS::GBS_OPTION_SCANLINES_ENABLED::read() == 1) { // don't rely on rto->scanlinesEnabled
                                    disableScanlines();
                                }
                                enableMotionAdaptDeinterlace();
                                if (timingAdjustDelay == 0) {
                                    timingAdjustDelay = 11; // arm timer only if it's not already armed
                                    oddEvenWhenArmed = VPERIOD_IF % 2;
                                } else {
                                    timingAdjustDelay = 0; // cancel timer
                                }
                                preventScanlines = 1;
                            }
                            filteredLineCountMotionAdaptiveOn = 0;
                        }
                    } else if (VPERIOD_IF == 521 || VPERIOD_IF == 523 || VPERIOD_IF == 525 ||
                               VPERIOD_IF == 623 || VPERIOD_IF == 625 || VPERIOD_IF == 627) { // ie v:523, uneven counts > disable
                        filteredLineCountMotionAdaptiveOff++;
                        filteredLineCountMotionAdaptiveOn = 0;
                        if (filteredLineCountMotionAdaptiveOff >= 2) // at least >= 2
                        {
                            if (uopt->deintMode == 0 && rto->motionAdaptiveDeinterlaceActive) {
                                disableMotionAdaptDeinterlace();
                                if (timingAdjustDelay == 0) {
                                    timingAdjustDelay = 11; // arm timer only if it's not already armed
                                    oddEvenWhenArmed = VPERIOD_IF % 2;
                                } else {
                                    timingAdjustDelay = 0; // cancel timer
                                }
                            }
                            filteredLineCountMotionAdaptiveOff = 0;
                        }
                    } else {
                        filteredLineCountMotionAdaptiveOn = filteredLineCountMotionAdaptiveOff = 0;
                    }

                    VPERIOD_IF_OLD = VPERIOD_IF; // part of glitch filter

                    if (uopt->deintMode == 1) { // using bob
                        if (rto->motionAdaptiveDeinterlaceActive) {
                            disableMotionAdaptDeinterlace();
                            FrameSync::reset(uopt->frameTimeLockMethod);
                            GBS::GBS_RUNTIME_FTL_ADJUSTED::write(1);
                            lastVsyncLock = millis();
                        }
                        if (uopt->wantScanlines && !rto->scanlinesEnabled) {
                            enableScanlines();
                        } else if (!uopt->wantScanlines && rto->scanlinesEnabled) {
                            disableScanlines();
                        }
                    }

                    // timing adjust after a few stable cycles
                    // should arrive here with either odd or even VPERIOD_IF
                    /*if (timingAdjustDelay != 0) {
            Serial.print(timingAdjustDelay); Serial.print(" ");
          }*/
                    if (timingAdjustDelay != 0) {
                        if ((VPERIOD_IF % 2) == oddEvenWhenArmed) {
                            timingAdjustDelay--;
                            if (timingAdjustDelay == 0) {
                                if (uopt->enableFrameTimeLock) {
                                    FrameSync::reset(uopt->frameTimeLockMethod);
                                    GBS::GBS_RUNTIME_FTL_ADJUSTED::write(1);
                                    delay(10);
                                    lastVsyncLock = millis();
                                }
                                externalClockGenSyncInOutRate();
                            }
                        }
                        /*else {
              Serial.println("!!!");
            }*/
                    }
                }

                // scanlines
                if (uopt->wantScanlines) {
                    if (!rto->scanlinesEnabled && !rto->motionAdaptiveDeinterlaceActive && !preventScanlines) {
                        enableScanlines();
                    } else if (!uopt->wantScanlines && rto->scanlinesEnabled) {
                        disableScanlines();
                    }
                }
            }
        }
    }

    if (rto->videoStandardInput >= 14) { // RGBHV checks
        static uint16_t RGBHVNoSyncCounter = 0;

        if (uopt->preferScalingRgbhv && rto->continousStableCounter >= 2) {
            boolean needPostAdjust = 0;
            static uint16_t activePresetLineCount = 0;
            // is the source in range for scaling RGBHV and is it currently in mode 15?
            uint16_t sourceLines = GBS::STATUS_SYNC_PROC_VTOTAL::read(); // if sourceLines = 0, might be in some reset state
            if ((sourceLines <= 535 && sourceLines != 0) && rto->videoStandardInput == 15) {
                uint16_t firstDetectedSourceLines = sourceLines;
                boolean moveOn = 1;
                for (int i = 0; i < 30; i++) { // not the best check, but we don't want to try if this is not stable (usually is though)
                    sourceLines = GBS::STATUS_SYNC_PROC_VTOTAL::read();
                    // range needed for interlace
                    if ((sourceLines < firstDetectedSourceLines - 3) || (sourceLines > firstDetectedSourceLines + 3)) {
                        moveOn = 0;
                        break;
                    }
                    delay(10);
                }
                if (moveOn) {
                    SerialM.println(F(" RGB/HV upscale mode"));
                    rto->isValidForScalingRGBHV = true;
                    GBS::GBS_OPTION_SCALING_RGBHV::write(1);
                    rto->autoBestHtotalEnabled = 1;

                    if (rto->syncTypeCsync == false) {
                        GBS::SP_SOG_MODE::write(0);
                        GBS::SP_NO_COAST_REG::write(1);
                        GBS::ADC_5_00::write(0x10); // 5_00 might be required
                        GBS::PLL_IS::write(0);      // 0_40 2: this provides a clock for IF and test bus readings
                        GBS::PLL_VCORST::write(1);  // 0_43 5: also required for clock
                        delay(320);                 // min 250
                    } else {
                        GBS::SP_SOG_MODE::write(1);
                        GBS::SP_H_CST_ST::write(0x10); // 5_4d  // set some default values
                        GBS::SP_H_CST_SP::write(0x80); // will be updated later
                        GBS::SP_H_PROTECT::write(1);   // some modes require this (or invert SOG)
                    }
                    delay(4);

                    float sourceRate = getSourceFieldRate(1);
                    Serial.println(sourceRate);

                    // todo: this hack is hard to understand when looking at applypreset and mode is suddenly 1,2 or 3
                    if (uopt->presetPreference == 2) {
                        // custom preset defined, try to load (set mode = 14 here early)
                        rto->videoStandardInput = 14;
                    } else {
                        if (sourceLines < 280) {
                            // this is "NTSC like?" check, seen 277 lines in "512x512 interlaced (emucrt)"
                            rto->videoStandardInput = 1;
                        } else if (sourceLines < 380) {
                            // this is "PAL like?" check, seen vt:369 (MDA mode)
                            rto->videoStandardInput = 2;
                        } else if (sourceRate > 44.0f && sourceRate < 53.8f) {
                            // not low res but PAL = "EDTV"
                            rto->videoStandardInput = 4;
                            needPostAdjust = 1;
                        } else { // sourceRate > 53.8f
                            // "60Hz EDTV"
                            rto->videoStandardInput = 3;
                            needPostAdjust = 1;
                        }
                    }

                    if (uopt->presetPreference == 10) {
                        uopt->presetPreference = Output960P; // fix presetPreference which can be "bypass"
                    }

                    activePresetLineCount = sourceLines;
                    applyPresets(rto->videoStandardInput);

                    GBS::GBS_OPTION_SCALING_RGBHV::write(1);
                    GBS::IF_INI_ST::write(16);   // fixes pal(at least) interlace
                    GBS::SP_SOG_P_ATO::write(1); // 5_20 1 auto SOG polarity (now "hpw" should never be close to "ht")

                    GBS::SP_SDCS_VSST_REG_L::write(2); // 5_3f
                    GBS::SP_SDCS_VSSP_REG_L::write(0); // 5_40

                    rto->coastPositionIsSet = rto->clampPositionIsSet = 0;
                    rto->videoStandardInput = 14;

                    if (GBS::PLLAD_ICP::read() >= 6) {
                        GBS::PLLAD_ICP::write(5); // reduce charge pump current for more general use
                        latchPLLAD();
                        delay(40);
                    }

                    updateSpDynamic(1);
                    if (rto->syncTypeCsync == false) {
                        GBS::SP_SOG_MODE::write(0);
                        GBS::SP_CLAMP_MANUAL::write(1);
                        GBS::SP_NO_COAST_REG::write(1);
                    } else {
                        GBS::SP_SOG_MODE::write(1);
                        GBS::SP_H_CST_ST::write(0x10); // 5_4d  // set some default values
                        GBS::SP_H_CST_SP::write(0x80); // will be updated later
                        GBS::SP_H_PROTECT::write(1);   // some modes require this (or invert SOG)
                    }
                    delay(300);

                        if (rto->extClockGenDetected && !uopt->disableExternalClockGenerator) {
                            // switch to ext clock
                            if (!rto->outModeHdBypass) {
                                if (GBS::PLL648_CONTROL_01::read() != 0x75) {
                                    // Store current clock if it's not our target or internal default
                                    if (GBS::PLL648_CONTROL_01::read() != 0x35) {
                                        GBS::GBS_PRESET_DISPLAY_CLOCK::write(GBS::PLL648_CONTROL_01::read());
                                    }
                                    
                                    // Switch to external clock (0x75)
                                    Si.enable(0);
                                    delayMicroseconds(800);
                                    GBS::PLL648_CONTROL_01::write(0x75);
                                    GBS::PAD_CKIN_ENZ::write(0); // Ensure CKIN is enabled
                                }
                            }
                            // sync clocks now
                            externalClockGenSyncInOutRate();
                        }

                    // note: this is all duplicated below. unify!
                    if (needPostAdjust) {
                        // base preset was "3" / no line doubling
                        // info: actually the position needs to be adjusted based on hor. freq or "h:" value (todo!)
                        GBS::IF_HB_ST2::write(0x08);  // patches
                        GBS::IF_HB_SP2::write(0x68);  // image
                        GBS::IF_HBIN_SP::write(0x50); // position
                        if (rto->presetID == 0x05) {
                            GBS::IF_HB_ST2::write(0x480);
                            GBS::IF_HB_SP2::write(0x8E);
                        }

                        float sfr = getSourceFieldRate(0);
                        if (sfr >= 69.0) {
                            SerialM.println("source >= 70Hz");
                            // increase vscale; vscale -= 57 seems to hit magic factor often
                            // 512 + 57 = 569 + 57 = 626 + 57 = 683
                            GBS::VDS_VSCALE::write(GBS::VDS_VSCALE::read() - 57);

                        } else {
                            // 50/60Hz, presumably
                            // adjust vposition
                            GBS::IF_VB_SP::write(8);
                            GBS::IF_VB_ST::write(6);
                        }
                    }
                }
            }
            // if currently in scaling RGB/HV, check for "SD" < > "EDTV" style source changes
            else if ((sourceLines <= 535 && sourceLines != 0) && rto->videoStandardInput == 14) {
                // todo: custom presets?
                if (sourceLines < 280 && activePresetLineCount > 280) {
                    rto->videoStandardInput = 1;
                } else if (sourceLines < 380 && activePresetLineCount > 380) {
                    rto->videoStandardInput = 2;
                } else if (sourceLines > 380 && activePresetLineCount < 380) {
                    rto->videoStandardInput = 3;
                    needPostAdjust = 1;
                }

                if (rto->videoStandardInput != 14) {
                    // check thoroughly first
                    uint16_t firstDetectedSourceLines = sourceLines;
                    boolean moveOn = 1;
                    for (int i = 0; i < 30; i++) {
                        sourceLines = GBS::STATUS_SYNC_PROC_VTOTAL::read();
                        if ((sourceLines < firstDetectedSourceLines - 3) || (sourceLines > firstDetectedSourceLines + 3)) {
                            moveOn = 0;
                            break;
                        }
                        delay(10);
                    }

                    if (moveOn) {
                        // need to change presets
                        if (rto->videoStandardInput <= 2) {
                            SerialM.println(F(" RGB/HV upscale mode base 15kHz"));
                        } else {
                            SerialM.println(F(" RGB/HV upscale mode base 31kHz"));
                        }

                        if (uopt->presetPreference == 10) {
                            uopt->presetPreference = Output960P; // fix presetPreference which can be "bypass"
                        }

                        activePresetLineCount = sourceLines;
                        applyPresets(rto->videoStandardInput);

                        GBS::GBS_OPTION_SCALING_RGBHV::write(1);
                        GBS::IF_INI_ST::write(16);   // fixes pal(at least) interlace
                        GBS::SP_SOG_P_ATO::write(1); // 5_20 1 auto SOG polarity

                        // adjust vposition
                        GBS::SP_SDCS_VSST_REG_L::write(2); // 5_3f
                        GBS::SP_SDCS_VSSP_REG_L::write(0); // 5_40

                        rto->coastPositionIsSet = rto->clampPositionIsSet = 0;
                        rto->videoStandardInput = 14;

                        if (GBS::PLLAD_ICP::read() >= 6) {
                            GBS::PLLAD_ICP::write(5); // reduce charge pump current for more general use
                            latchPLLAD();
                        }

                        updateSpDynamic(1);
                        if (rto->syncTypeCsync == false) {
                            GBS::SP_SOG_MODE::write(0);
                            GBS::SP_CLAMP_MANUAL::write(1);
                            GBS::SP_NO_COAST_REG::write(1);
                        } else {
                            GBS::SP_SOG_MODE::write(1);
                            GBS::SP_H_CST_ST::write(0x10); // 5_4d  // set some default values
                            GBS::SP_H_CST_SP::write(0x80); // will be updated later
                            GBS::SP_H_PROTECT::write(1);   // some modes require this (or invert SOG)
                        }
                        delay(300);

                        if (rto->extClockGenDetected && !uopt->disableExternalClockGenerator && rto->videoStandardInput != 14) {
                            // switch to ext clock
                            if (!rto->outModeHdBypass) {
                                if (GBS::PLL648_CONTROL_01::read() != 0x75) {
                                    // Store current clock if it's not our target or internal default
                                    if (GBS::PLL648_CONTROL_01::read() != 0x35) {
                                        GBS::GBS_PRESET_DISPLAY_CLOCK::write(GBS::PLL648_CONTROL_01::read());
                                    }
                                    
                                    // Switch to external clock (0x75)
                                    Si.enable(0);
                                    delayMicroseconds(800);
                                    GBS::PLL648_CONTROL_01::write(0x75);
                                    GBS::PAD_CKIN_ENZ::write(0); // Ensure CKIN is enabled
                                }
                            }
                            // sync clocks now
                            externalClockGenSyncInOutRate();
                        }

                        // note: this is all duplicated above. unify!
                        if (needPostAdjust) {
                            // base preset was "3" / no line doubling
                            // info: actually the position needs to be adjusted based on hor. freq or "h:" value (todo!)
                            GBS::IF_HB_ST2::write(0x08);  // patches
                            GBS::IF_HB_SP2::write(0x68);  // image
                            GBS::IF_HBIN_SP::write(0x50); // position
                            if (rto->presetID == 0x05) {
                                GBS::IF_HB_ST2::write(0x480);
                                GBS::IF_HB_SP2::write(0x8E);
                            }

                            float sfr = getSourceFieldRate(0);
                            if (sfr >= 69.0) {
                                SerialM.println("source >= 70Hz");
                                // increase vscale; vscale -= 57 seems to hit magic factor often
                                // 512 + 57 = 569 + 57 = 626 + 57 = 683
                                GBS::VDS_VSCALE::write(GBS::VDS_VSCALE::read() - 57);

                            } else {
                                // 50/60Hz, presumably
                                // adjust vposition
                                GBS::IF_VB_SP::write(8);
                                GBS::IF_VB_ST::write(6);
                            }
                        }
                    } else {
                        // was unstable, undo videoStandardInput change
                        rto->videoStandardInput = 14;
                    }
                }
            }
            // check whether to revert back to full bypass
            else if ((sourceLines > 535) && rto->videoStandardInput == 14) {
                uint16_t firstDetectedSourceLines = sourceLines;
                boolean moveOn = 1;
                for (int i = 0; i < 30; i++) {
                    sourceLines = GBS::STATUS_SYNC_PROC_VTOTAL::read();
                    // range needed for interlace
                    if ((sourceLines < firstDetectedSourceLines - 3) || (sourceLines > firstDetectedSourceLines + 3)) {
                        moveOn = 0;
                        break;
                    }
                    delay(10);
                }
                if (moveOn) {
                    SerialM.println(F(" RGB/HV upscale mode disabled"));
                    rto->videoStandardInput = 15;
                    rto->isValidForScalingRGBHV = false;

                    activePresetLineCount = 0;
                    applyPresets(rto->videoStandardInput); // exception: apply preset here, not later in syncwatcher

                    delay(300);
                }
            }
        } // done preferScalingRgbhv

        if (!uopt->preferScalingRgbhv && rto->videoStandardInput == 14) {
            // user toggled the web ui button / revert scaling rgbhv
            rto->videoStandardInput = 15;
            rto->isValidForScalingRGBHV = false;
            applyPresets(rto->videoStandardInput);
            delay(300);
        }

        // stability check, for CSync and HV separately
        uint16_t limitNoSync = 0;
        uint8_t VSHSStatus = 0;
        boolean stable = 0;
        if (rto->syncTypeCsync == true) {
            if (GBS::STATUS_INT_SOG_BAD::read() == 1) {
                // STATUS_INT_SOG_BAD = 0x0f bit 0, interrupt reg
                resetModeDetect();
                stable = 0;
                SerialM.print("`");
                delay(10);
                resetInterruptSogBadBit();
            } else {
                stable = 1;
                VSHSStatus = GBS::STATUS_00::read();
                // this status can get stuck (regularly does)
                stable = ((VSHSStatus & 0x04) == 0x04); // RGBS > check h+v from 0_00
            }
            limitNoSync = 200; // 100
        } else {
            VSHSStatus = GBS::STATUS_16::read();
            // this status usually updates when a source goes off
            stable = ((VSHSStatus & 0x0a) == 0x0a); // RGBHV > check h+v from 0_16
            limitNoSync = 300;
        }

        if (!stable) {
            LEDOFF;
            RGBHVNoSyncCounter++;
            rto->continousStableCounter = 0;
            if (RGBHVNoSyncCounter % 20 == 0) {
                SerialM.print("`");
            }
        } else {
            RGBHVNoSyncCounter = 0;
            LEDON;
            if (rto->continousStableCounter < 255) {
                rto->continousStableCounter++;
                if (rto->continousStableCounter == 6) {
                    updateSpDynamic(1);
                }
            }
        }

        if (RGBHVNoSyncCounter > limitNoSync) {
            RGBHVNoSyncCounter = 0;
            setResetParameters();
            prepareSyncProcessor();
            resetSyncProcessor(); // todo: fix MD being stuck in last mode when sync disappears
            //resetModeDetect();
            rto->noSyncCounter = 0;
            //Serial.println("RGBHV limit no sync");
        }

        static unsigned long lastTimeSogAndPllRateCheck = millis();
        if ((millis() - lastTimeSogAndPllRateCheck) > 900) {
            if (rto->videoStandardInput == 15) {
                // start out by adjusting sync polarity, may reset sog unstable interrupt flag
                updateHVSyncEdge();
                delay(100);
            }

            static uint8_t runsWithSogBadStatus = 0;
            static uint8_t oldHPLLState = 0;
            if (rto->syncTypeCsync == false) {
                if (GBS::STATUS_INT_SOG_BAD::read()) { // SOG source unstable indicator
                    runsWithSogBadStatus++;
                    //SerialM.print("test: "); SerialM.println(runsWithSogBadStatus);
                    if (runsWithSogBadStatus >= 4) {
                        SerialM.println(F("RGB/HV < > SOG"));
                        rto->syncTypeCsync = true;
                        rto->HPLLState = runsWithSogBadStatus = RGBHVNoSyncCounter = 0;
                        rto->noSyncCounter = 0x07fe; // will cause a return
                    }
                } else {
                    runsWithSogBadStatus = 0;
                }
            }

            uint32_t currentPllRate = 0;
            static uint32_t oldPllRate = 10;

            // how fast is the PLL running? needed to set charge pump and gain
            // typical: currentPllRate: 1560, currentPllRate: 3999 max seen the pll reach: 5008 for 1280x1024@75
            if (GBS::STATUS_INT_SOG_BAD::read() == 0) {
                currentPllRate = getPllRate();
                //Serial.println(currentPllRate);
                if (currentPllRate > 100 && currentPllRate < 7500) {
                    if ((currentPllRate < (oldPllRate - 3)) || (currentPllRate > (oldPllRate + 3))) {
                        delay(40);
                        if (GBS::STATUS_INT_SOG_BAD::read() == 1)
                            delay(100);
                        currentPllRate = getPllRate(); // test again, guards against random spurs
                        // but don't force currentPllRate to = 0 if these inner checks fail,
                        // prevents csync <> hvsync changes
                        if ((currentPllRate < (oldPllRate - 3)) || (currentPllRate > (oldPllRate + 3))) {
                            oldPllRate = currentPllRate; // okay, it changed
                        }
                    }
                } else {
                    currentPllRate = 0;
                }
            }

            resetInterruptSogBadBit();

            //short activeChargePumpLevel = GBS::PLLAD_ICP::read();
            //short activeGainBoost = GBS::PLLAD_FS::read();
            //SerialM.print(" rto->HPLLState: "); SerialM.println(rto->HPLLState);
            //SerialM.print(" currentPllRate: "); SerialM.println(currentPllRate);
            //SerialM.print(" CPL: "); SerialM.print(activeChargePumpLevel);
            //SerialM.print(" Gain: "); SerialM.print(activeGainBoost);
            //SerialM.print(" KS: "); SerialM.print(GBS::PLLAD_KS::read());

            oldHPLLState = rto->HPLLState; // do this first, else it can miss events
            if (currentPllRate != 0) {
                if (currentPllRate < 1030) {
                    rto->HPLLState = 1;
                } else if (currentPllRate < 2300) {
                    rto->HPLLState = 2;
                } else if (currentPllRate < 3200) {
                    rto->HPLLState = 3;
                } else if (currentPllRate < 3800) {
                    rto->HPLLState = 4;
                } else {
                    rto->HPLLState = 5;
                }
            }

            if (rto->videoStandardInput == 15) {
                if (oldHPLLState != rto->HPLLState) {
                    if (rto->HPLLState == 1) {
                        GBS::PLLAD_KS::write(2); // KS = 2 okay
                        GBS::PLLAD_FS::write(0);
                        GBS::PLLAD_ICP::write(6);
                    } else if (rto->HPLLState == 2) {
                        GBS::PLLAD_KS::write(1);
                        GBS::PLLAD_FS::write(0);
                        GBS::PLLAD_ICP::write(6);
                    } else if (rto->HPLLState == 3) { // KS = 1 okay
                        GBS::PLLAD_KS::write(1);
                        GBS::PLLAD_FS::write(1);
                        GBS::PLLAD_ICP::write(6); // would need 7 but this is risky
                    } else if (rto->HPLLState == 4) {
                        GBS::PLLAD_KS::write(0); // KS = 0 from here on
                        GBS::PLLAD_FS::write(0);
                        GBS::PLLAD_ICP::write(6);
                    } else if (rto->HPLLState == 5) {
                        GBS::PLLAD_KS::write(0); // KS = 0
                        GBS::PLLAD_FS::write(1);
                        GBS::PLLAD_ICP::write(6);
                    }

                    latchPLLAD();
                    delay(2);
                    setOverSampleRatio(4, false); // false = do apply // will auto decrease to max possible factor
                    SerialM.print(F("(H-PLL) rate: "));
                    SerialM.print(currentPllRate);
                    SerialM.print(F(" state: "));
                    SerialM.println(rto->HPLLState);
                    delay(100);
                }
            } else if (rto->videoStandardInput == 14) {
                if (oldHPLLState != rto->HPLLState) {
                    SerialM.print(F("(H-PLL) rate: "));
                    SerialM.print(currentPllRate);
                    SerialM.print(F(" state (no change): "));
                    SerialM.println(rto->HPLLState);
                    // need to manage HPLL state change somehow

                    //FrameSync::reset(uopt->frameTimeLockMethod);
                }
            }

            if (rto->videoStandardInput == 14) {
                // scanlines
                if (uopt->wantScanlines) {
                    if (!rto->scanlinesEnabled && !rto->motionAdaptiveDeinterlaceActive) {
                        if (GBS::IF_LD_RAM_BYPS::read() == 0) { // line doubler on?
                            enableScanlines();
                        }
                    } else if (!uopt->wantScanlines && rto->scanlinesEnabled) {
                        disableScanlines();
                    }
                }
            }

            rto->clampPositionIsSet = false; // RGBHV should regularly check clamp position
            lastTimeSogAndPllRateCheck = millis();
        }
    }

    if (rto->noSyncCounter >= 0x07fe) {
        // couldn't recover, source is lost
        // restore initial conditions and move to input detect
        GBS::DAC_RGBS_PWDNZ::write(0); // 0 = disable DAC
        rto->noSyncCounter = 0;
        SerialM.println();
        goLowPowerWithInputDetection(); // does not further nest, so it can be called here // sets reset parameters
    }

    // Monitor Si5351 lock status if generator is detected and enabled
    if (rto->extClockGenDetected && !uopt->disableExternalClockGenerator && !rto->sourceDisconnected) {
        static uint32_t lastSiLockCheck = 0;
        if (millis() - lastSiLockCheck > 2000) {
            lastSiLockCheck = millis();
            
            int st = -999;
            Wire.beginTransmission(SIADDR);
            Wire.write(0x00);
            if (Wire.endTransmission() == 0) {
                size_t n = Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
                if (n == 1) st = Wire.read();
            }
            
            // Treat LOLA as lock indicator; ignore LOLB (PLLB). bit5=LOLA, bit6=LOLB, bit4=LOS, bit7=SYS_INIT
            // Relaxed check: ignore SYS_INIT (bit7) for monitoring as it can be transient/sticky while PLLA is locked.
            bool siLocked = (st >= 0) && ((st & 0x20) == 0);
            if (!siLocked) {
                static uint32_t lastSiLockFailLog = 0;
                if (millis() - lastSiLockFailLog > 1000) {
                    SerialM.printf("Si5351 lock lost (st:0x%02X). Re-syncing...\n", st);
                    lastSiLockFailLog = millis();
                }
                // Try to restore sync if we have a stable source
                if (rto->continousStableCounter > 20 && rto->noSyncCounter == 0) {
                    externalClockGenSyncInOutRate();
                }
            }
        }
    }
}

