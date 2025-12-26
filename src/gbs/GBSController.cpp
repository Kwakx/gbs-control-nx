#include "GBSController.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../gbs/GBSPresets.h"
#include "../gbs/GBSSync.h"
#include "../gbs/GBSVideoProcessing.h"
#include "../gbs/GBSPhase.h"
#include "../clock/Si5351Manager.h"
#include "../video/VideoInput.h"
#include "../video/VideoTiming.h"
#include "../storage/SlotManager.h"
#include "../wifi/WiFiManager.h"
#include "../menu/OLEDMenu.h"
#include "../utils/DebugHelpers.h"
#include "tv5725.h"
#include "../video/framesync.h"
#include "../../presets/ntsc_240p.h"
#include "../../presets/pal_240p.h"
#include "../../presets/ntsc_720x480.h"
#include "../../presets/pal_768x576.h"
#include "../../presets/ntsc_1280x720.h"
#include "../../presets/ntsc_1280x1024.h"
#include "../../presets/ntsc_1920x1080.h"
#include "../../presets/ntsc_downscale.h"
#include "../../presets/pal_1280x720.h"
#include "../../presets/pal_1280x1024.h"
#include "../../presets/pal_1920x1080.h"
#include "../../presets/pal_downscale.h"
#include <Arduino.h>

void resetPLLAD()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::PLLAD_VCORST::write(1);
    GBS::PLLAD_PDZ::write(1); // in case it was off
    latchPLLAD();
    GBS::PLLAD_VCORST::write(0);
    delay(1);
    latchPLLAD();
    rto->clampPositionIsSet = 0; // test, but should be good
    rto->continousStableCounter = 1;
}

void latchPLLAD()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::PLLAD_LAT::write(0);
    delayMicroseconds(128);
    GBS::PLLAD_LAT::write(1);
}

void resetPLL()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::PLL_VCORST::write(1);
    delay(1);
    GBS::PLL_VCORST::write(0);
    delay(1);
    rto->clampPositionIsSet = 0; // test, but should be good
    rto->continousStableCounter = 1;
}

void ResetSDRAM()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::SDRAM_RESET_CONTROL::write(0x02);
    GBS::SDRAM_RESET_SIGNAL::write(1);
    GBS::SDRAM_RESET_SIGNAL::write(0);
    GBS::SDRAM_RESET_CONTROL::write(0x82);
}

void resetDigital()
{
    typedef TV5725<GBS_ADDR> GBS;
    boolean keepBypassActive = 0;
    if (GBS::SFTRST_HDBYPS_RSTZ::read() == 1) { // if HDBypass enabled
        keepBypassActive = 1;
    }

    GBS::RESET_CONTROL_0x47::write(0x17); // new, keep 0,1,2,4 on (DEC,MODE,SYNC,INT) //MODE okay?

    if (rto->outModeHdBypass) { // if currently in bypass
        GBS::RESET_CONTROL_0x46::write(0x00);
        GBS::RESET_CONTROL_0x47::write(0x1F);
        return; // 0x46 stays all 0
    }

    GBS::RESET_CONTROL_0x46::write(0x41); // keep VDS (6) + IF (0) enabled, reset rest
    if (keepBypassActive == 1) {          // if HDBypass enabled
        GBS::RESET_CONTROL_0x47::write(0x1F);
    }
    GBS::RESET_CONTROL_0x46::write(0x7f);
}

void resetSyncProcessor()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::SFTRST_SYNC_RSTZ::write(0);
    delayMicroseconds(10);
    GBS::SFTRST_SYNC_RSTZ::write(1);
}

void resetModeDetect()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::SFTRST_MODE_RSTZ::write(0);
    delay(1); // needed
    GBS::SFTRST_MODE_RSTZ::write(1);
}

void unfreezeVideo()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::CAPTURE_ENABLE::write(1);
}

void freezeVideo()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::CAPTURE_ENABLE::write(0);
    rto->videoIsFrozen = true;
}

float getSourceFieldRate(boolean useSPBus)
{
    typedef TV5725<GBS_ADDR> GBS;
    uint32_t cpuFreqMHz_raw = ESP.getCpuFreqMHz();
    double tickRateHz =
#ifdef ESP32
        1000000.0; // MeasurePeriod uses micros() on ESP32
#else
        cpuFreqMHz_raw * 1000000.0;
#endif
    uint8_t testBusSelBackup = GBS::TEST_BUS_SEL::read();
    uint8_t spBusSelBackup = GBS::TEST_BUS_SP_SEL::read();
    uint8_t ifBusSelBackup = GBS::IF_TEST_SEL::read();
    uint8_t debugPinBackup = GBS::PAD_BOUT_EN::read();

    if (debugPinBackup != 1)
        GBS::PAD_BOUT_EN::write(1); // enable output to pin for test

    if (ifBusSelBackup != 3)
        GBS::IF_TEST_SEL::write(3); // IF averaged frame time

    if (useSPBus) {
        if (rto->syncTypeCsync) {
            if (testBusSelBackup != 0xa)
                GBS::TEST_BUS_SEL::write(0xa);
        } else {
            if (testBusSelBackup != 0x0)
                GBS::TEST_BUS_SEL::write(0x0); // RGBHV: TB 0x20 has vsync
        }
        if (spBusSelBackup != 0x0f)
            GBS::TEST_BUS_SP_SEL::write(0x0f);
    } else {
        if (testBusSelBackup != 0)
            GBS::TEST_BUS_SEL::write(0); // needs decimation + if
    }

    float retVal = 0;

    uint32_t fieldTimeTicks = FrameSync::getPulseTicks();
    if (fieldTimeTicks == 0) {
        // try again
        fieldTimeTicks = FrameSync::getPulseTicks();
    }

    if (fieldTimeTicks > 0) {
        retVal = tickRateHz / (double)fieldTimeTicks;
        if (retVal < 47.0f || retVal > 86.0f) {
            // try again
            fieldTimeTicks = FrameSync::getPulseTicks();
            if (fieldTimeTicks > 0) {
                retVal = tickRateHz / (double)fieldTimeTicks;
            }
        }
    }

    GBS::TEST_BUS_SEL::write(testBusSelBackup);
    GBS::PAD_BOUT_EN::write(debugPinBackup);
    if (spBusSelBackup != 0x0f)
        GBS::TEST_BUS_SP_SEL::write(spBusSelBackup);
    if (ifBusSelBackup != 3)
        GBS::IF_TEST_SEL::write(ifBusSelBackup);

    return retVal;
}

float getOutputFrameRate()
{
    typedef TV5725<GBS_ADDR> GBS;
    uint32_t cpuFreqMHz_raw = ESP.getCpuFreqMHz();
    double tickRateHz =
#ifdef ESP32
        1000000.0; // MeasurePeriod uses micros() on ESP32
#else
        cpuFreqMHz_raw * 1000000.0;
#endif
    uint8_t testBusSelBackup = GBS::TEST_BUS_SEL::read();
    uint8_t debugPinBackup = GBS::PAD_BOUT_EN::read();

    if (debugPinBackup != 1)
        GBS::PAD_BOUT_EN::write(1); // enable output to pin for test

    if (testBusSelBackup != 2)
        GBS::TEST_BUS_SEL::write(2); // 0x4d = 0x22 VDS test

    float retVal = 0;

    // ESP32 can show significant ISR latency jitter with WiFi/RTOS, which
    // makes a single pulse measurement unreliable. Take multiple samples and
    // reject outliers (similar to FrameSyncManager::runFrequency()).
    uint32_t ticks1 = 0, ticks2 = 0, ticks3 = 0;
    float f1 = 0, f2 = 0, f3 = 0;
    auto calcHz = [&](uint32_t ticks) -> float {
        if (ticks == 0) return 0.0f;
        return (float)(tickRateHz / (double)ticks);
    };

    ticks1 = FrameSync::getPulseTicks();
    f1 = calcHz(ticks1);
    ticks2 = FrameSync::getPulseTicks();
    f2 = calcHz(ticks2);

    // If first two samples disagree too much, take a third and pick the median.
    float diff12 = fabsf(f2 - f1);
    float rel12 = (f1 > 0 && f2 > 0) ? (diff12 / fminf(f1, f2)) : 999.0f;
    if (ticks1 == 0 || ticks2 == 0 || f1 < 47.0f || f1 > 86.0f || f2 < 47.0f || f2 > 86.0f || diff12 > 0.5f || rel12 > 0.00833f) {
        ticks3 = FrameSync::getPulseTicks();
        f3 = calcHz(ticks3);

        // Choose median of valid samples (ignoring zeros/out-of-range as worst).
        auto score = [&](float f) -> float {
            if (f < 47.0f || f > 86.0f) return 9999.0f;
            return fabsf(f - 50.0f); // prefer around 50/60Hz region
        };
        // simplistic: pick the sample closest to the other(s)
        float d13 = fabsf(f3 - f1);
        float d23 = fabsf(f3 - f2);
        if (score(f1) <= score(f2) && score(f1) <= score(f3)) retVal = f1;
        else if (score(f2) <= score(f1) && score(f2) <= score(f3)) retVal = f2;
        else retVal = f3;

        // If we have two close samples among the three, average them for stability.
        if (f1 >= 47.0f && f1 <= 86.0f && f2 >= 47.0f && f2 <= 86.0f && diff12 <= 0.5f && rel12 <= 0.00833f) retVal = 0.5f * (f1 + f2);
        else if (f1 >= 47.0f && f1 <= 86.0f && f3 >= 47.0f && f3 <= 86.0f && d13 <= 0.5f) retVal = 0.5f * (f1 + f3);
        else if (f2 >= 47.0f && f2 <= 86.0f && f3 >= 47.0f && f3 <= 86.0f && d23 <= 0.5f) retVal = 0.5f * (f2 + f3);
    } else {
        // Two consistent samples: average them.
        retVal = 0.5f * (f1 + f2);
    }

    GBS::TEST_BUS_SEL::write(testBusSelBackup);
    GBS::PAD_BOUT_EN::write(debugPinBackup);

    return retVal;
}

// This function sets the oversample ratio (1x, 2x, 4x) for the ADC
// prepareOnly: if true, only prepare registers, don't apply changes
void setOverSampleRatio(uint8_t newRatio, boolean prepareOnly)
{
    typedef TV5725<GBS_ADDR> GBS;
    uint8_t ks = GBS::PLLAD_KS::read();

    bool hi_res = rto->videoStandardInput == 8 || rto->videoStandardInput == 4 || rto->videoStandardInput == 3;
    bool bypass = rto->presetID == PresetHdBypass;

    switch (newRatio) {
        case 1:
            if (ks == 0)
                GBS::PLLAD_CKOS::write(0);
            if (ks == 1)
                GBS::PLLAD_CKOS::write(1);
            if (ks == 2)
                GBS::PLLAD_CKOS::write(2);
            if (ks == 3)
                GBS::PLLAD_CKOS::write(3);
            GBS::ADC_CLK_ICLK2X::write(0);
            GBS::ADC_CLK_ICLK1X::write(0);
            GBS::DEC1_BYPS::write(1); // dec1 couples to ADC_CLK_ICLK2X
            GBS::DEC2_BYPS::write(1);

            // Necessary to avoid a 2x-scaled image for some reason.
            if (hi_res && !bypass) {
                GBS::ADC_CLK_ICLK1X::write(1);
                //GBS::DEC2_BYPS::write(0);
            }

            rto->osr = 1;
            //if (!prepareOnly) SerialM.println("OSR 1x");

            break;
        case 2:
            if (ks == 0) {
                setOverSampleRatio(1, false);
                return;
            } // 2x impossible
            if (ks == 1)
                GBS::PLLAD_CKOS::write(0);
            if (ks == 2)
                GBS::PLLAD_CKOS::write(1);
            if (ks == 3)
                GBS::PLLAD_CKOS::write(2);
            GBS::ADC_CLK_ICLK2X::write(0);
            GBS::ADC_CLK_ICLK1X::write(1);
            GBS::DEC2_BYPS::write(0);
            GBS::DEC1_BYPS::write(1); // dec1 couples to ADC_CLK_ICLK2X

            if (hi_res && !bypass) {
                //GBS::ADC_CLK_ICLK2X::write(1);
                //GBS::DEC1_BYPS::write(0);
                // instead increase CKOS by 1 step
                GBS::PLLAD_CKOS::write(GBS::PLLAD_CKOS::read() + 1);
            }

            rto->osr = 2;
            //if (!prepareOnly) SerialM.println("OSR 2x");

            break;
        case 4:
            if (ks == 0) {
                setOverSampleRatio(1, false);
                return;
            } // 4x impossible
            if (ks == 1) {
                setOverSampleRatio(1, false);
                return;
            } // 4x impossible
            if (ks == 2)
                GBS::PLLAD_CKOS::write(0);
            if (ks == 3)
                GBS::PLLAD_CKOS::write(1);
            GBS::ADC_CLK_ICLK2X::write(1);
            GBS::ADC_CLK_ICLK1X::write(1);
            GBS::DEC1_BYPS::write(0); // dec1 couples to ADC_CLK_ICLK2X
            GBS::DEC2_BYPS::write(0);

            rto->osr = 4;
            //if (!prepareOnly) SerialM.println("OSR 4x");

            break;
        default:
            break;
    }

    if (!prepareOnly)
        latchPLLAD();
}

void applyPresets(uint8_t result)
{
    typedef TV5725<GBS_ADDR> GBS;
    if (!rto->boardHasPower) {
        SerialM.println(F("GBS board not responding!"));
        return;
    }

    // if RGBHV scaling and invoked through web ui or custom preset
    // need to know syncTypeCsync
    if (result == 14) {
        if (GBS::STATUS_SYNC_PROC_HSACT::read() == 1) {
            rto->inputIsYpBpR = 0;
            if (GBS::STATUS_SYNC_PROC_VSACT::read() == 0) {
                rto->syncTypeCsync = 1;
            } else {
                rto->syncTypeCsync = 0;
            }
        }
    }

    boolean waitExtra = 0;
    if (rto->outModeHdBypass || rto->videoStandardInput == 15 || rto->videoStandardInput == 0) {
        waitExtra = 1;
        if (result <= 4 || result == 14 || result == 8 || result == 9) {
            GBS::SFTRST_IF_RSTZ::write(1); // early init
            GBS::SFTRST_VDS_RSTZ::write(1);
            GBS::SFTRST_DEC_RSTZ::write(1);
        }
    }
    rto->presetIsPalForce60 = 0;      // the default
    rto->outModeHdBypass = 0;         // the default at this stage

    // in case it is set; will get set appropriately later in doPostPresetLoadSteps()
    GBS::GBS_PRESET_CUSTOM::write(0);
    rto->isCustomPreset = false;

    // carry over debug view if possible
    if (GBS::ADC_UNUSED_62::read() != 0x00) {
        // only if the preset to load isn't custom
        // (else the command will instantly disable debug view)
        if (uopt->presetPreference != 2) {
            serialCommand = 'D';
        }
    }

    if (result == 0) {
        // Unknown
        SerialM.println(F("Source format not properly recognized, using fallback preset!"));
        result = 3;                   // in case of success: override to 480p60
        GBS::ADC_INPUT_SEL::write(1); // RGB
        delay(100);
        if (GBS::STATUS_SYNC_PROC_HSACT::read() == 1) {
            rto->inputIsYpBpR = 0;
            rto->syncWatcherEnabled = 1;
            if (GBS::STATUS_SYNC_PROC_VSACT::read() == 0) {
                rto->syncTypeCsync = 1;
            } else {
                rto->syncTypeCsync = 0;
            }
        } else {
            GBS::ADC_INPUT_SEL::write(0); // YPbPr
            delay(100);
            if (GBS::STATUS_SYNC_PROC_HSACT::read() == 1) {
                rto->inputIsYpBpR = 1;
                rto->syncTypeCsync = 1;
                rto->syncWatcherEnabled = 1;
            } else {
                // found nothing at all, turn off output

                // If we call setResetParameters(), soon afterwards loop() ->
                // inputAndSyncDetect() -> goLowPowerWithInputDetection() will
                // call setResetParameters() again. But if we don't call
                // setResetParameters() here, the second call will never happen.
                setResetParameters();

                // Deselect the output resolution in the web UI. We cannot call
                // doPostPresetLoadSteps() to select the right resolution, since
                // it *enables* the output (showing a green screen) even if
                // previously disabled, and we want to *disable* it.
                rto->presetID = 0;
                return;
            }
        }
    }

    if (uopt->PalForce60 == 1) {
        if (uopt->presetPreference != 2) { // != custom. custom saved as pal preset has ntsc customization
            if (result == 2 || result == 4) {
                Serial.println(F("PAL@50 to 60Hz"));
                rto->presetIsPalForce60 = 1;
            }
            if (result == 2) {
                result = 1;
            }
            if (result == 4) {
                result = 3;
            }
        }
    }

    /// If uopt->presetPreference == OutputCustomized and we load a custom
    /// preset, check if it's intended to bypass scaling at the current input
    /// resolution. If so, setup bypass and skip the rest of applyPresets().
    auto applySavedBypassPreset = [&result]() -> bool {
        uint8_t rawPresetId = GBS::GBS_PRESET_ID::read();
        if (rawPresetId == PresetHdBypass) {
            // Required for switching from 240p to 480p to work.
            rto->videoStandardInput = result;

            // Setup video mode passthrough.
            setOutModeHdBypass(true);
            return true;
        }
        if (rawPresetId == PresetBypassRGBHV) {
            // TODO implement bypassModeSwitch_RGBHV (I don't have RGBHV inputs to verify)
        }
        return false;
    };

    if (result == 1 || result == 3 || result == 8 || result == 9 || result == 14) {
        // NTSC input
        if (uopt->presetPreference == 0) {
            writeProgramArrayNew(ntsc_240p, false);
        } else if (uopt->presetPreference == 1) {
            writeProgramArrayNew(ntsc_720x480, false);
        } else if (uopt->presetPreference == 3) {
            writeProgramArrayNew(ntsc_1280x720, false);
        }
#if defined(ESP8266) || defined(ESP32)
        else if (uopt->presetPreference == OutputCustomized) {
            const uint8_t *preset = loadPresetFromLittleFS(result);
            writeProgramArrayNew(preset, false);
            if (applySavedBypassPreset()) {
                return;
            }
        } else if (uopt->presetPreference == 4) {
            if (uopt->matchPresetSource && (result != 8) && (GBS::GBS_OPTION_SCALING_RGBHV::read() == 0)) {
                SerialM.println(F("matched preset override > 1280x960"));
                writeProgramArrayNew(ntsc_240p, false); // pref = x1024 override to x960
            } else {
                writeProgramArrayNew(ntsc_1280x1024, false);
            }
        }
#endif
        else if (uopt->presetPreference == 5) {
            writeProgramArrayNew(ntsc_1920x1080, false);
        } else if (uopt->presetPreference == 6) {
            writeProgramArrayNew(ntsc_downscale, false);
        }
    } else if (result == 2 || result == 4) {
        // PAL input
        if (uopt->presetPreference == 0) {
            if (uopt->matchPresetSource) {
                SerialM.println(F("matched preset override > 1280x1024"));
                writeProgramArrayNew(pal_1280x1024, false); // pref = x960 override to x1024
            } else {
                writeProgramArrayNew(pal_240p, false);
            }
        } else if (uopt->presetPreference == 1) {
            writeProgramArrayNew(pal_768x576, false);
        } else if (uopt->presetPreference == 3) {
            writeProgramArrayNew(pal_1280x720, false);
        }
#if defined(ESP8266) || defined(ESP32)
        else if (uopt->presetPreference == OutputCustomized) {
            const uint8_t *preset = loadPresetFromLittleFS(result);
            writeProgramArrayNew(preset, false);
            if (applySavedBypassPreset()) {
                return;
            }
        } else if (uopt->presetPreference == 4) {
            writeProgramArrayNew(pal_1280x1024, false);
        }
#endif
        else if (uopt->presetPreference == 5) {
            writeProgramArrayNew(pal_1920x1080, false);
        } else if (uopt->presetPreference == 6) {
            writeProgramArrayNew(pal_downscale, false);
        }
    } else if (result == 5 || result == 6 || result == 7 || result == 13) {
        // use bypass mode for these HD sources
        rto->videoStandardInput = result;
        setOutModeHdBypass(false);
        return;
    } else if (result == 15) {
        SerialM.print(F("RGB/HV "));
        if (rto->syncTypeCsync) {
            SerialM.print(F("(CSync) "));
        }
        SerialM.println();
        bypassModeSwitch_RGBHV();
        // don't go through doPostPresetLoadSteps
        return;
    }

    rto->videoStandardInput = result;
    if (waitExtra) {
        // extra time needed for digital resets, so that autobesthtotal works first attempt
        delay(400); // min ~ 300
    }
    doPostPresetLoadSteps();
}

void bypassModeSwitch_RGBHV()
{
    typedef TV5725<GBS_ADDR> GBS;
    if (!rto->boardHasPower) {
        SerialM.println(F("GBS board not responding!"));
        return;
    }

    GBS::DAC_RGBS_PWDNZ::write(0);   // disable DAC
    GBS::PAD_SYNC_OUT_ENZ::write(1); // disable sync out

    loadHdBypassSection();
    externalClockGenResetClock();
    FrameSync::cleanup();
    GBS::ADC_UNUSED_62::write(0x00); // clear debug view
    GBS::PA_ADC_BYPSZ::write(1);     // enable phase unit ADC
    GBS::PA_SP_BYPSZ::write(1);      // enable phase unit SP
    applyRGBPatches();
    resetDebugPort();
    rto->videoStandardInput = 15;       // making sure
    rto->autoBestHtotalEnabled = false; // not necessary, since VDS is off / bypassed // todo: mode 14 (works anyway)
    rto->clampPositionIsSet = false;
    rto->HPLLState = 0;

    GBS::PLL_CKIS::write(0);           // 0_40 0 //  0: PLL uses OSC clock | 1: PLL uses input clock
    GBS::PLL_DIVBY2Z::write(0);        // 0_40 1 // 1= no divider (full clock, ie 27Mhz) 0 = halved clock
    GBS::PLL_ADS::write(0);            // 0_40 3 test:  input clock is from PCLKIN (disconnected, not ADC clock)
    GBS::PLL_MS::write(2);             // 0_40 4-6 select feedback clock (but need to enable tri state!)
    GBS::PAD_TRI_ENZ::write(1);        // enable some pad's tri state (they become high-z / inputs), helps noise
    GBS::MEM_PAD_CLK_INVERT::write(0); // helps also
    if (rto->extClockGenDetected) {
        GBS::PLL648_CONTROL_01::write(0x75);
    } else {
        GBS::PLL648_CONTROL_01::write(0x35);
    }
    GBS::PLL648_CONTROL_03::write(0x00); // 0_43
    GBS::PLL_LEN::write(1);              // 0_43

    GBS::DAC_RGBS_ADC2DAC::write(1);
    GBS::OUT_SYNC_SEL::write(1); // 0_4f 1=sync from HDBypass, 2=sync from SP, (00 = from VDS)

    GBS::SFTRST_HDBYPS_RSTZ::write(1); // enable
    GBS::HD_INI_ST::write(0);          // needs to be some small value or apparently 0 works
    GBS::HD_MATRIX_BYPS::write(1);     // bypass since we'll treat source as RGB
    GBS::HD_DYN_BYPS::write(1);        // bypass since we'll treat source as RGB

    GBS::PAD_SYNC1_IN_ENZ::write(0); // filter H/V sync input1 (0 = on)
    GBS::PAD_SYNC2_IN_ENZ::write(0); // filter H/V sync input2 (0 = on)

    GBS::SP_SOG_P_ATO::write(1); // 5_20 1 corrects hpw readout and slightly affects sync
    if (rto->syncTypeCsync == false) {
        GBS::SP_SOG_SRC_SEL::write(0);  // 5_20 0 | 0: from ADC 1: from hs // use ADC and turn it off = no SOG
        GBS::ADC_SOGEN::write(1);       // 5_02 0 ADC SOG // having it 0 drags down the SOG (hsync) input; = 1: need to supress SOG decoding
        GBS::SP_EXT_SYNC_SEL::write(0); // connect HV input ( 5_20 bit 3 )
        GBS::SP_SOG_MODE::write(0);     // 5_56 bit 0 // 0: normal, 1: SOG
        GBS::SP_NO_COAST_REG::write(1); // vblank coasting off
        GBS::SP_PRE_COAST::write(0);
        GBS::SP_POST_COAST::write(0);
        GBS::SP_H_PULSE_IGNOR::write(0xff); // cancel out SOG decoding
        GBS::SP_SYNC_BYPS::write(0);        // external H+V sync for decimator (+ sync out) | 1 to mirror in sync, 0 to output processed sync
        GBS::SP_HS_POL_ATO::write(1);       // 5_55 4 auto polarity for retiming
        GBS::SP_VS_POL_ATO::write(1);       // 5_55 6
        GBS::SP_HS_LOOP_SEL::write(1);      // 5_57_6 | 0 enables retiming on SP | 1 to bypass input to HDBYPASS
        GBS::SP_H_PROTECT::write(0);        // 5_3e 4 disable for H/V
        rto->phaseADC = 16;
        rto->phaseSP = 8;
    } else {
        // todo: SOG SRC can be ADC or HS input pin. HS requires TTL level, ADC can use lower levels
        // HS seems to have issues at low PLL speeds
        // maybe add detection whether ADC Sync is needed
        GBS::SP_SOG_SRC_SEL::write(0);  // 5_20 0 | 0: from ADC 1: hs is sog source
        GBS::SP_EXT_SYNC_SEL::write(1); // disconnect HV input
        GBS::ADC_SOGEN::write(1);       // 5_02 0 ADC SOG
        GBS::SP_SOG_MODE::write(1);     // apparently needs to be off for HS input (on for ADC)
        GBS::SP_NO_COAST_REG::write(0); // vblank coasting on
        GBS::SP_PRE_COAST::write(4);    // 5_38, > 4 can be seen with clamp invert on the lower lines
        GBS::SP_POST_COAST::write(7);
        GBS::SP_SYNC_BYPS::write(0);   // use regular sync for decimator (and sync out) path
        GBS::SP_HS_LOOP_SEL::write(1); // 5_57_6 | 0 enables retiming on SP | 1 to bypass input to HDBYPASS
        GBS::SP_H_PROTECT::write(1);   // 5_3e 4 enable for SOG
        rto->currentLevelSOG = 24;
        rto->phaseADC = 16;
        rto->phaseSP = 8;
    }
    GBS::SP_CLAMP_MANUAL::write(1);  // needs to be 1
    GBS::SP_COAST_INV_REG::write(0); // just in case

    GBS::SP_DIS_SUB_COAST::write(1);   // 5_3e 5
    GBS::SP_HS_PROC_INV_REG::write(0); // 5_56 5
    GBS::SP_VS_PROC_INV_REG::write(0); // 5_56 6
    GBS::PLLAD_KS::write(1);           // 0 - 3
    setOverSampleRatio(2, true);       // prepare only = true
    GBS::DEC_MATRIX_BYPS::write(1);    // 5_1f with adc to dac mode
    GBS::ADC_FLTR::write(0);           // 5_03 4/5 ADC filter 3=40, 2=70, 1=110, 0=150 Mhz

    GBS::PLLAD_ICP::write(4);
    GBS::PLLAD_FS::write(0);    // low gain
    GBS::PLLAD_MD::write(1856); // 1349 perfect for for 1280x+ ; 1856 allows lower res to detect

    GBS::ADC_TA_05_CTRL::write(0x02); // 5_05 1 // minor SOG clamp effect
    GBS::ADC_TEST_04::write(0x02);    // 5_04
    GBS::ADC_TEST_0C::write(0x12);    // 5_0c 1 4
    GBS::DAC_RGBS_R0ENZ::write(1);
    GBS::DAC_RGBS_G0ENZ::write(1);
    GBS::DAC_RGBS_B0ENZ::write(1);
    GBS::OUT_SYNC_CNTRL::write(1);
    resetDigital();       // this will leave 0_46 all 0
    resetSyncProcessor(); // required to initialize SOG status
    delay(2);
    ResetSDRAM();
    delay(2);
    resetPLLAD();
    togglePhaseAdjustUnits();
    delay(20);
    GBS::PLLAD_LEN::write(1);        // 5_11 1
    GBS::DAC_RGBS_PWDNZ::write(1);   // enable DAC
    GBS::PAD_SYNC_OUT_ENZ::write(0); // enable sync out

    setAndLatchPhaseSP(); // different for CSync and pure HV modes
    setAndLatchPhaseADC();
    latchPLLAD();

    if (uopt->enableAutoGain == 1 && adco->r_gain == 0) {
        setAdcGain(AUTO_GAIN_INIT);
        GBS::DEC_TEST_ENABLE::write(1);
    } else if (uopt->enableAutoGain == 1 && adco->r_gain != 0) {
        GBS::ADC_RGCTRL::write(adco->r_gain);
        GBS::ADC_GGCTRL::write(adco->g_gain);
        GBS::ADC_BGCTRL::write(adco->b_gain);
        GBS::DEC_TEST_ENABLE::write(1);
    } else {
        GBS::DEC_TEST_ENABLE::write(0); // no need for decimation test to be enabled
    }
}

void doPostPresetLoadSteps()
{
    typedef TV5725<GBS_ADDR> GBS;
    //unsigned long postLoadTimer = millis();

    // adco->r_gain gets applied if uopt->enableAutoGain is set.
    if (uopt->enableAutoGain) {
        if (uopt->presetPreference == OutputCustomized) {
            // Loaded custom preset, we want to keep newly loaded gain. Save
            // gain written by loadPresetFromLittleFS -> writeProgramArrayNew.
            adco->r_gain = GBS::ADC_RGCTRL::read();
            adco->g_gain = GBS::ADC_GGCTRL::read();
            adco->b_gain = GBS::ADC_BGCTRL::read();
        } else {
            // Loaded fixed preset. Keep adco->r_gain from before overwriting
            // registers.
        }
    }

    //GBS::PAD_SYNC_OUT_ENZ::write(1);  // no sync out
    //GBS::DAC_RGBS_PWDNZ::write(0);    // no DAC
    //GBS::SFTRST_MEM_FF_RSTZ::write(0);  // mem fifos keep in reset

    if (rto->videoStandardInput == 0) {
        uint8_t videoMode = getVideoMode();
        SerialM.print(F("post preset: rto->videoStandardInput 0 > "));
        SerialM.println(videoMode);
        if (videoMode > 0) {
            rto->videoStandardInput = videoMode;
        }
    }
    rto->presetID = GBS::GBS_PRESET_ID::read();
    rto->isCustomPreset = GBS::GBS_PRESET_CUSTOM::read();

    GBS::ADC_UNUSED_64::write(0);
    GBS::ADC_UNUSED_65::write(0); // clear temp storage
    GBS::ADC_UNUSED_66::write(0);
    GBS::ADC_UNUSED_67::write(0); // clear temp storage
    GBS::PAD_CKIN_ENZ::write(0);  // 0 = clock input enable (pin40)

    if (!rto->isCustomPreset) {
        prepareSyncProcessor(); // todo: handle modes 14 and 15 better, now that they support scaling
    }
    if (rto->videoStandardInput == 14) {
        // copy of code in bypassModeSwitch_RGBHV
        if (rto->syncTypeCsync == false) {
            GBS::SP_SOG_SRC_SEL::write(0);  // 5_20 0 | 0: from ADC 1: from hs // use ADC and turn it off = no SOG
            GBS::ADC_SOGEN::write(1);       // 5_02 0 ADC SOG // having it 0 drags down the SOG (hsync) input; = 1: need to supress SOG decoding
            GBS::SP_EXT_SYNC_SEL::write(0); // connect HV input ( 5_20 bit 3 )
            GBS::SP_SOG_MODE::write(0);     // 5_56 bit 0 // 0: normal, 1: SOG
            GBS::SP_NO_COAST_REG::write(1); // vblank coasting off
            GBS::SP_PRE_COAST::write(0);
            GBS::SP_POST_COAST::write(0);
            GBS::SP_H_PULSE_IGNOR::write(0xff); // cancel out SOG decoding
            GBS::SP_SYNC_BYPS::write(0);        // external H+V sync for decimator (+ sync out) | 1 to mirror in sync, 0 to output processed sync
            GBS::SP_HS_POL_ATO::write(1);       // 5_55 4 auto polarity for retiming
            GBS::SP_VS_POL_ATO::write(1);       // 5_55 6
            GBS::SP_HS_LOOP_SEL::write(1);      // 5_57_6 | 0 enables retiming on SP | 1 to bypass input to HDBYPASS
            GBS::SP_H_PROTECT::write(0);        // 5_3e 4 disable for H/V
            rto->phaseADC = 16;
            rto->phaseSP = 8;
        } else {
            // todo: SOG SRC can be ADC or HS input pin. HS requires TTL level, ADC can use lower levels
            // HS seems to have issues at low PLL speeds
            // maybe add detection whether ADC Sync is needed
            GBS::SP_SOG_SRC_SEL::write(0);  // 5_20 0 | 0: from ADC 1: hs is sog source
            GBS::SP_EXT_SYNC_SEL::write(1); // disconnect HV input
            GBS::ADC_SOGEN::write(1);       // 5_02 0 ADC SOG
            GBS::SP_SOG_MODE::write(1);     // apparently needs to be off for HS input (on for ADC)
            GBS::SP_NO_COAST_REG::write(0); // vblank coasting on
            GBS::SP_PRE_COAST::write(4);    // 5_38, > 4 can be seen with clamp invert on the lower lines
            GBS::SP_POST_COAST::write(7);
            GBS::SP_SYNC_BYPS::write(0);   // use regular sync for decimator (and sync out) path
            GBS::SP_HS_LOOP_SEL::write(1); // 5_57_6 | 0 enables retiming on SP | 1 to bypass input to HDBYPASS
            GBS::SP_H_PROTECT::write(1);   // 5_3e 4 enable for SOG
            rto->currentLevelSOG = 24;
            rto->phaseADC = 16;
            rto->phaseSP = 8;
        }
    }

    GBS::SP_H_PROTECT::write(0);
    GBS::SP_COAST_INV_REG::write(0); // just in case
    if (!rto->outModeHdBypass && GBS::GBS_OPTION_SCALING_RGBHV::read() == 0) {
        // setOutModeHdBypass has it's own and needs to update later
        updateSpDynamic(0); // remember: rto->videoStandardInput for RGB(C/HV) in scaling is 1, 2 or 3 here
    }

    GBS::SP_NO_CLAMP_REG::write(1); // (keep) clamp disabled, to be enabled when position determined
    GBS::OUT_SYNC_CNTRL::write(1);  // prepare sync out to PAD

    // auto offset adc prep
    GBS::ADC_AUTO_OFST_PRD::write(1);   // by line (0 = by frame)
    GBS::ADC_AUTO_OFST_DELAY::write(0); // sample delay 0 (1 to 4 pipes)
    GBS::ADC_AUTO_OFST_STEP::write(0);  // 0 = abs diff, then 1 to 3 steps
    GBS::ADC_AUTO_OFST_TEST::write(1);
    GBS::ADC_AUTO_OFST_RANGE_REG::write(0x00); // 5_0f U/V ranges = 0 (full range, 1 to 15)

    if (rto->inputIsYpBpR == true) {
        applyYuvPatches();
    } else {
        applyRGBPatches();
    }

    if (rto->outModeHdBypass) {
        GBS::OUT_SYNC_SEL::write(1); // 0_4f 1=sync from HDBypass, 2=sync from SP
        rto->autoBestHtotalEnabled = false;
    } else {
        rto->autoBestHtotalEnabled = true;
    }

    rto->phaseADC = GBS::PA_ADC_S::read(); // we can't know which is right, get from preset
    rto->phaseSP = 8;                      // get phase into global variables early: before latching

    if (rto->inputIsYpBpR) {
        rto->thisSourceMaxLevelSOG = rto->currentLevelSOG = 14;
    } else {
        rto->thisSourceMaxLevelSOG = rto->currentLevelSOG = 13; // similar to yuv, allow variations
    }

    setAndUpdateSogLevel(rto->currentLevelSOG);

    if (!rto->isCustomPreset) {
        // Writes ADC_RGCTRL. If auto gain is enabled, ADC_RGCTRL will be
        // overwritten further down at `uopt->enableAutoGain == 1`.
        setAdcParametersGainAndOffset();
    }

    GBS::GPIO_CONTROL_00::write(0x67); // most GPIO pins regular GPIO
    GBS::GPIO_CONTROL_01::write(0x00); // all GPIO outputs disabled
    rto->clampPositionIsSet = 0;
    rto->coastPositionIsSet = 0;
    rto->phaseIsSet = 0;
    rto->continousStableCounter = 0;
    rto->noSyncCounter = 0;
    rto->motionAdaptiveDeinterlaceActive = false;
    rto->scanlinesEnabled = false;
    rto->failRetryAttempts = 0;
    rto->videoIsFrozen = true;       // ensures unfreeze
    rto->sourceDisconnected = false; // this must be true if we reached here (no syncwatcher operation)
    rto->boardHasPower = true;       //same

    if (rto->presetID == 0x06 || rto->presetID == 0x16) {
        rto->isCustomPreset = 0; // override so it applies section 2 deinterlacer settings
    }

    if (!rto->isCustomPreset) {
        if (rto->videoStandardInput == 3 || rto->videoStandardInput == 4 ||
            rto->videoStandardInput == 8 || rto->videoStandardInput == 9) {
            GBS::IF_LD_RAM_BYPS::write(1); // 1_0c 0 no LD, do this before setIfHblankParameters
        }

        //setIfHblankParameters();              // 1_0e, 1_18, 1_1a
        GBS::IF_INI_ST::write(0); // 16.08.19: don't calculate, use fixed to 0
        // the following sets a field offset that eliminates 240p content forced to 480i flicker
        //GBS::IF_INI_ST::write(GBS::PLLAD_MD::read() * 0.4261f);  // upper: * 0.4282f  lower: 0.424f

        GBS::IF_HS_INT_LPF_BYPS::write(0); // 1_02 2
        // 0 allows int/lpf for smoother scrolling with non-ideal scaling, also reduces jailbars and even noise
        // interpolation or lpf available, lpf looks better
        GBS::IF_HS_SEL_LPF::write(1);     // 1_02 1
        GBS::IF_HS_PSHIFT_BYPS::write(1); // 1_02 3 nonlinear scale phase shift bypass
        // 1_28 1 1:hbin generated write reset 0:line generated write reset
        GBS::IF_LD_WRST_SEL::write(1); // at 1 fixes output position regardless of 1_24
        //GBS::MADPT_Y_DELAY_UV_DELAY::write(0); // 2_17 default: 0 // don't overwrite

        GBS::SP_RT_HS_ST::write(0); // 5_49 // retiming hs ST, SP
        GBS::SP_RT_HS_SP::write(GBS::PLLAD_MD::read() * 0.93f);

        GBS::VDS_PK_LB_CORE::write(0); // 3_44 0-3 // 1 for anti source jailbars
        GBS::VDS_PK_LH_CORE::write(0); // 3_46 0-3 // 1 for anti source jailbars
        if (rto->presetID == 0x05 || rto->presetID == 0x15) {
            GBS::VDS_PK_LB_GAIN::write(0x16); // 3_45 // peaking HF
            GBS::VDS_PK_LH_GAIN::write(0x0A); // 3_47
        } else {
            GBS::VDS_PK_LB_GAIN::write(0x16); // 3_45
            GBS::VDS_PK_LH_GAIN::write(0x18); // 3_47
        }
        GBS::VDS_PK_VL_HL_SEL::write(0); // 3_43 0 if 1 then 3_45 HF almost no effect (coring 0xf9)
        GBS::VDS_PK_VL_HH_SEL::write(0); // 3_43 1

        GBS::VDS_STEP_GAIN::write(1); // step response, max 15 (VDS_STEP_DLY_CNTRL set in presets)

        // DAC filters / keep in presets for now
        //GBS::VDS_1ST_INT_BYPS::write(1); // disable RGB stage interpolator
        //GBS::VDS_2ND_INT_BYPS::write(1); // disable YUV stage interpolator

        // most cases will use osr 2
        setOverSampleRatio(2, true); // prepare only = true

        // full height option
        if (uopt->wantFullHeight) {
            if (rto->videoStandardInput == 1 || rto->videoStandardInput == 3) {
                //if (rto->presetID == 0x5)
                //{ // out 1080p 60
                //  GBS::VDS_DIS_VB_ST::write(GBS::VDS_VSYNC_RST::read() - 1);
                //  GBS::VDS_DIS_VB_SP::write(42);
                //  GBS::VDS_VB_SP::write(42 - 10); // is VDS_DIS_VB_SP - 10 = 32 // watch for vblank overflow (ps3)
                //  GBS::VDS_VSCALE::write(455);
                //  GBS::IF_VB_SP::write(GBS::IF_VB_SP::read() + 4);
                //  GBS::IF_VB_ST::write(GBS::IF_VB_ST::read() + 4);
                //  SerialM.println(F("full height"));
                //}
            } else if (rto->videoStandardInput == 2 || rto->videoStandardInput == 4) {
                if (rto->presetID == 0x15) { // out 1080p 50
                    GBS::VDS_VSCALE::write(455);
                    GBS::VDS_DIS_VB_ST::write(GBS::VDS_VSYNC_RST::read()); // full = 1125 of 1125
                    GBS::VDS_DIS_VB_SP::write(42);
                    GBS::IF_VB_SP::write(GBS::IF_VB_SP::read() + 22);
                    GBS::IF_VB_ST::write(GBS::IF_VB_ST::read() + 22);
                    SerialM.println(F("full height"));
                }
            }
        }

        if (rto->videoStandardInput == 1 || rto->videoStandardInput == 2) {
            //GBS::PLLAD_ICP::write(5);         // 5 rather than 6 to work well with CVBS sync as well as CSync

            GBS::ADC_FLTR::write(3);             // 5_03 4/5 ADC filter 3=40, 2=70, 1=110, 0=150 Mhz
            GBS::PLLAD_KS::write(2);             // 5_16
            setOverSampleRatio(4, true);         // prepare only = true
            GBS::IF_SEL_WEN::write(0);           // 1_02 0; 0 for SD, 1 for EDTV
            if (rto->inputIsYpBpR) {             // todo: check other videoStandardInput in component vs rgb
                GBS::IF_HS_TAP11_BYPS::write(0); // 1_02 4 Tap11 LPF bypass in YUV444to422
                GBS::IF_HS_Y_PDELAY::write(2);   // 1_02 5+6 delays
                GBS::VDS_V_DELAY::write(0);      // 3_24 2
                GBS::VDS_Y_DELAY::write(3);      // 3_24 4/5 delays
            }

            // downscale preset: source is SD
            if (rto->presetID == 0x06 || rto->presetID == 0x16) {
                setCsVsStart(2);                        // or 3, 0
                setCsVsStop(0);                         // fixes field position
                GBS::IF_VS_SEL::write(1);               // 1_00 5 // turn off VHS sync feature
                GBS::IF_VS_FLIP::write(0);              // 1_01 0
                GBS::IF_LD_RAM_BYPS::write(0);          // 1_0c 0
                GBS::IF_HS_DEC_FACTOR::write(1);        // 1_0b 4
                GBS::IF_LD_SEL_PROV::write(0);          // 1_0b 7
                GBS::IF_HB_ST::write(2);                // 1_10 deinterlace offset
                GBS::MADPT_Y_VSCALE_BYPS::write(0);     // 2_02 6
                GBS::MADPT_UV_VSCALE_BYPS::write(0);    // 2_02 7
                GBS::MADPT_PD_RAM_BYPS::write(0);       // 2_24 2 one line fifo for line phase adjust
                GBS::MADPT_VSCALE_DEC_FACTOR::write(1); // 2_31 0..1
                GBS::MADPT_SEL_PHASE_INI::write(0);     // 2_31 2 disable for SD (check 240p content)
                if (rto->videoStandardInput == 1) {
                    GBS::IF_HB_ST2::write(0x490); // 1_18
                    GBS::IF_HB_SP2::write(0x80);  // 1_1a
                    GBS::IF_HB_SP::write(0x4A);   // 1_12 deinterlace offset, green bar
                    GBS::IF_HBIN_SP::write(0xD0); // 1_26
                } else if (rto->videoStandardInput == 2) {
                    GBS::IF_HB_SP2::write(0x74);  // 1_1a
                    GBS::IF_HB_SP::write(0x50);   // 1_12 deinterlace offset, green bar
                    GBS::IF_HBIN_SP::write(0xD0); // 1_26
                }
            }
        }
        if (rto->videoStandardInput == 3 || rto->videoStandardInput == 4 ||
            rto->videoStandardInput == 8 || rto->videoStandardInput == 9) {
            // EDTV p-scan, need to either double adc data rate and halve vds scaling
            // or disable line doubler (better) (50 / 60Hz shared)

            GBS::ADC_FLTR::write(3); // 5_03 4/5
            GBS::PLLAD_KS::write(1); // 5_16

            if (rto->presetID != 0x06 && rto->presetID != 0x16) {
                setCsVsStart(14);        // pal // hm
                setCsVsStop(11);         // probably setting these for modes 8,9
                GBS::IF_HB_SP::write(0); // 1_12 deinterlace offset, fixes colors (downscale needs diff)
            }
            setOverSampleRatio(2, true);           // with KS = 1 for modes 3, 4, 8
            GBS::IF_HS_DEC_FACTOR::write(0);       // 1_0b 4+5
            GBS::IF_LD_SEL_PROV::write(1);         // 1_0b 7
            GBS::IF_PRGRSV_CNTRL::write(1);        // 1_00 6
            GBS::IF_SEL_WEN::write(1);             // 1_02 0
            GBS::IF_HS_SEL_LPF::write(0);          // 1_02 1   0 = use interpolator not lpf for EDTV
            GBS::IF_HS_TAP11_BYPS::write(0);       // 1_02 4 filter
            GBS::IF_HS_Y_PDELAY::write(3);         // 1_02 5+6 delays (ps2 test on one board clearly says 3, not 2)
            GBS::VDS_V_DELAY::write(1);            // 3_24 2 // new 24.07.2019 : 1, also set 2_17 to 1
            GBS::MADPT_Y_DELAY_UV_DELAY::write(1); // 2_17 : 1
            GBS::VDS_Y_DELAY::write(3);            // 3_24 4/5 delays (ps2 test saying 3 for 1_02 goes with 3 here)
            if (rto->videoStandardInput == 9) {
                if (GBS::STATUS_SYNC_PROC_VTOTAL::read() > 650) {
                    delay(20);
                    if (GBS::STATUS_SYNC_PROC_VTOTAL::read() > 650) {
                        GBS::PLLAD_KS::write(0);        // 5_16
                        GBS::VDS_VSCALE_BYPS::write(1); // 3_00 5 no vscale
                    }
                }
            }

            // downscale preset: source is EDTV
            if (rto->presetID == 0x06 || rto->presetID == 0x16) {
                GBS::MADPT_Y_VSCALE_BYPS::write(0);     // 2_02 6
                GBS::MADPT_UV_VSCALE_BYPS::write(0);    // 2_02 7
                GBS::MADPT_PD_RAM_BYPS::write(0);       // 2_24 2 one line fifo for line phase adjust
                GBS::MADPT_VSCALE_DEC_FACTOR::write(1); // 2_31 0..1
                GBS::MADPT_SEL_PHASE_INI::write(1);     // 2_31 2 enable
                GBS::MADPT_SEL_PHASE_INI::write(0);     // 2_31 2 disable
            }
        }
        if (rto->videoStandardInput == 3 && rto->presetID != 0x06) { // ED YUV 60
            setCsVsStart(16);                                        // ntsc
            setCsVsStop(13);                                         //
            GBS::IF_HB_ST::write(30);                                // 1_10; magic number
            GBS::IF_HBIN_ST::write(0x20);                            // 1_24
            GBS::IF_HBIN_SP::write(0x60);                            // 1_26
            if (rto->presetID == 0x5) {                              // out 1080p
                GBS::IF_HB_SP2::write(0xB0);                         // 1_1a
                GBS::IF_HB_ST2::write(0x4BC);                        // 1_18
            } else if (rto->presetID == 0x3) {                       // out 720p
                GBS::VDS_VSCALE::write(683);                         // same as base preset
                GBS::IF_HB_ST2::write(0x478);                        // 1_18
                GBS::IF_HB_SP2::write(0x84);                         // 1_1a
            } else if (rto->presetID == 0x2) {                       // out x1024
                GBS::IF_HB_SP2::write(0x84);                         // 1_1a
                GBS::IF_HB_ST2::write(0x478);                        // 1_18
            } else if (rto->presetID == 0x1) {                       // out x960
                GBS::IF_HB_SP2::write(0x84);                         // 1_1a
                GBS::IF_HB_ST2::write(0x478);                        // 1_18
            } else if (rto->presetID == 0x4) {                       // out x480
                GBS::IF_HB_ST2::write(0x478);                        // 1_18
                GBS::IF_HB_SP2::write(0x90);                         // 1_1a
            }
        } else if (rto->videoStandardInput == 4 && rto->presetID != 0x16) { // ED YUV 50
            GBS::IF_HBIN_SP::write(0x40);                                   // 1_26 was 0x80 test: ps2 videomodetester 576p mode
            GBS::IF_HBIN_ST::write(0x20);                                   // 1_24, odd but need to set this here (blue bar)
            GBS::IF_HB_ST::write(0x30);                                     // 1_10
            if (rto->presetID == 0x15) {                                    // out 1080p
                GBS::IF_HB_ST2::write(0x4C0);                               // 1_18
                GBS::IF_HB_SP2::write(0xC8);                                // 1_1a
            } else if (rto->presetID == 0x13) {                             // out 720p
                GBS::IF_HB_ST2::write(0x478);                               // 1_18
                GBS::IF_HB_SP2::write(0x88);                                // 1_1a
            } else if (rto->presetID == 0x12) {                             // out x1024
                // VDS_VB_SP -= 12 used to shift pic up, but seems not necessary anymore
                //GBS::VDS_VB_SP::write(GBS::VDS_VB_SP::read() - 12);
                GBS::IF_HB_ST2::write(0x454);   // 1_18
                GBS::IF_HB_SP2::write(0x88);    // 1_1a
            } else if (rto->presetID == 0x11) { // out x960
                GBS::IF_HB_ST2::write(0x454);   // 1_18
                GBS::IF_HB_SP2::write(0x88);    // 1_1a
            } else if (rto->presetID == 0x14) { // out x576
                GBS::IF_HB_ST2::write(0x478);   // 1_18
                GBS::IF_HB_SP2::write(0x90);    // 1_1a
            }
        } else if (rto->videoStandardInput == 5) { // 720p
            GBS::ADC_FLTR::write(1);               // 5_03
            GBS::IF_PRGRSV_CNTRL::write(1);        // progressive
            GBS::IF_HS_DEC_FACTOR::write(0);
            GBS::INPUT_FORMATTER_02::write(0x74);
            GBS::VDS_Y_DELAY::write(3);
        } else if (rto->videoStandardInput == 6 || rto->videoStandardInput == 7) { // 1080i/p
            GBS::ADC_FLTR::write(1);                                               // 5_03
            GBS::PLLAD_KS::write(0);                                               // 5_16
            GBS::IF_PRGRSV_CNTRL::write(1);
            GBS::IF_HS_DEC_FACTOR::write(0);
            GBS::INPUT_FORMATTER_02::write(0x74);
            GBS::VDS_Y_DELAY::write(3);
        } else if (rto->videoStandardInput == 8) { // 25khz
            // todo: this mode for HV sync
            uint32_t pllRate = 0;
            for (int i = 0; i < 8; i++) {
                pllRate += getPllRate();
            }
            pllRate /= 8;
            SerialM.print(F("(H-PLL) rate: "));
            SerialM.println(pllRate);
            if (pllRate > 200) {             // is PLL even working?
                if (pllRate < 1800) {        // rate very low?
                    GBS::PLLAD_FS::write(0); // then low gain
                }
            }
            GBS::PLLAD_ICP::write(6); // all 25khz submodes have more lines than NTSC
            GBS::ADC_FLTR::write(1);  // 5_03
            GBS::IF_HB_ST::write(30); // 1_10; magic number
            //GBS::IF_HB_ST2::write(0x60);  // 1_18
            //GBS::IF_HB_SP2::write(0x88);  // 1_1a
            GBS::IF_HBIN_SP::write(0x60); // 1_26 works for all output presets
            if (rto->presetID == 0x1) {   // out x960
                GBS::VDS_VSCALE::write(410);
            } else if (rto->presetID == 0x2) { // out x1024
                GBS::VDS_VSCALE::write(402);
            } else if (rto->presetID == 0x3) { // out 720p
                GBS::VDS_VSCALE::write(546);
            } else if (rto->presetID == 0x5) { // out 1080p
                GBS::VDS_VSCALE::write(400);
            }
        }
    }

    if (rto->presetID == 0x06 || rto->presetID == 0x16) {
        rto->isCustomPreset = GBS::GBS_PRESET_CUSTOM::read(); // override back
    }

    resetDebugPort();

    boolean avoidAutoBest = 0;
    if (rto->syncTypeCsync) {
        if (GBS::TEST_BUS_2F::read() == 0) {
            delay(4);
            if (GBS::TEST_BUS_2F::read() == 0) {
                optimizeSogLevel();
                avoidAutoBest = 1;
                delay(4);
            }
        }
    }

    latchPLLAD(); // besthtotal reliable with this (EDTV modes, possibly others)

    if (rto->isCustomPreset) {
        // patch in segments not covered in custom preset files (currently seg 2)
        if (rto->videoStandardInput == 3 || rto->videoStandardInput == 4 || rto->videoStandardInput == 8) {
            GBS::MADPT_Y_DELAY_UV_DELAY::write(1); // 2_17 : 1
        }

        // get OSR
        if (GBS::DEC1_BYPS::read() && GBS::DEC2_BYPS::read()) {
            rto->osr = 1;
        } else if (GBS::DEC1_BYPS::read() && !GBS::DEC2_BYPS::read()) {
            rto->osr = 2;
        } else {
            rto->osr = 4;
        }

        // always start with internal clock active first
        if (GBS::PLL648_CONTROL_01::read() == 0x75 && GBS::GBS_PRESET_DISPLAY_CLOCK::read() != 0) {
            GBS::PLL648_CONTROL_01::write(GBS::GBS_PRESET_DISPLAY_CLOCK::read());
        } else if (GBS::GBS_PRESET_DISPLAY_CLOCK::read() == 0) {
            SerialM.println(F("no stored display clock to use!"));
        }
    }

    if (rto->presetIsPalForce60) {
        if (GBS::GBS_OPTION_PALFORCED60_ENABLED::read() != 1) {
            SerialM.println(F("pal forced 60hz: apply vshift"));
            uint16_t vshift = 56; // default shift
            if (rto->presetID == 0x5) {
                GBS::IF_VB_SP::write(4);
            } // out 1080p
            else {
                GBS::IF_VB_SP::write(GBS::IF_VB_SP::read() + vshift);
            }
            GBS::IF_VB_ST::write(GBS::IF_VB_SP::read() - 2);
            GBS::GBS_OPTION_PALFORCED60_ENABLED::write(1);
        }
    }

    //freezeVideo();

    GBS::ADC_TEST_04::write(0x02);    // 5_04
    GBS::ADC_TEST_0C::write(0x12);    // 5_0c 1 4
    GBS::ADC_TA_05_CTRL::write(0x02); // 5_05

    // auto ADC gain
    if (uopt->enableAutoGain == 1) {
        if (adco->r_gain == 0) {
            // SerialM.printf("ADC gain: reset %x := %x\n", GBS::ADC_RGCTRL::read(), AUTO_GAIN_INIT);
            setAdcGain(AUTO_GAIN_INIT);
            GBS::DEC_TEST_ENABLE::write(1);
        } else {
            // SerialM.printf("ADC gain: transferred %x := %x\n", GBS::ADC_RGCTRL::read(), adco->r_gain);
            GBS::ADC_RGCTRL::write(adco->r_gain);
            GBS::ADC_GGCTRL::write(adco->g_gain);
            GBS::ADC_BGCTRL::write(adco->b_gain);
            GBS::DEC_TEST_ENABLE::write(1);
        }
    } else {
        GBS::DEC_TEST_ENABLE::write(0); // no need for decimation test to be enabled
    }

    // ADC offset if measured
    if (adco->r_off != 0 && adco->g_off != 0 && adco->b_off != 0) {
        GBS::ADC_ROFCTRL::write(adco->r_off);
        GBS::ADC_GOFCTRL::write(adco->g_off);
        GBS::ADC_BOFCTRL::write(adco->b_off);
    }

    SerialM.print(F("ADC offset: R:"));
    SerialM.print(GBS::ADC_ROFCTRL::read(), HEX);
    SerialM.print(" G:");
    SerialM.print(GBS::ADC_GOFCTRL::read(), HEX);
    SerialM.print(" B:");
    SerialM.println(GBS::ADC_BOFCTRL::read(), HEX);

    GBS::IF_AUTO_OFST_U_RANGE::write(0); // 0 seems to be full range, else 1 to 15
    GBS::IF_AUTO_OFST_V_RANGE::write(0); // 0 seems to be full range, else 1 to 15
    GBS::IF_AUTO_OFST_PRD::write(0);     // 0 = by line, 1 = by frame ; by line is easier to spot
    GBS::IF_AUTO_OFST_EN::write(0);      // not reliable yet
    // to get it working with RGB:
    // leave RGB to YUV at the ADC DEC stage (s5_1f 2 = 0)
    // s5s07s42, 1_2a to 0, s5_06 + s5_08 to 0x40
    // 5_06 + 5_08 will be the target center value, 5_07 sets general offset
    // s3s3as00 s3s3bs00 s3s3cs00

    if (uopt->wantVdsLineFilter) {
        GBS::VDS_D_RAM_BYPS::write(0);
    } else {
        GBS::VDS_D_RAM_BYPS::write(1);
    }

    if (uopt->wantPeaking) {
        GBS::VDS_PK_Y_H_BYPS::write(0);
    } else {
        GBS::VDS_PK_Y_H_BYPS::write(1);
    }

    // unused now
    GBS::VDS_TAP6_BYPS::write(0);
    /*if (uopt->wantTap6) { GBS::VDS_TAP6_BYPS::write(0); }
  else {
    GBS::VDS_TAP6_BYPS::write(1);
    if (!isCustomPreset) {
      GBS::MADPT_Y_DELAY_UV_DELAY::write(GBS::MADPT_Y_DELAY_UV_DELAY::read() + 1);
    }
  }*/

    if (uopt->wantStepResponse) {
        // step response requested, but exclude 1080p presets
        if (rto->presetID != 0x05 && rto->presetID != 0x15) {
            GBS::VDS_UV_STEP_BYPS::write(0);
        } else {
            GBS::VDS_UV_STEP_BYPS::write(1);
        }
    } else {
        GBS::VDS_UV_STEP_BYPS::write(1);
    }

    // transfer preset's display clock to ext. gen
    externalClockGenResetClock();

    //unfreezeVideo();
    Menu::init();
    FrameSync::cleanup();
    rto->syncLockFailIgnore = 16;

    // undo eventual rto->useHdmiSyncFix (not using this method atm)
    GBS::VDS_SYNC_EN::write(0);
    GBS::VDS_FLOCK_EN::write(0);

    if (!rto->outModeHdBypass && rto->autoBestHtotalEnabled &&
        GBS::GBS_OPTION_SCALING_RGBHV::read() == 0 && !avoidAutoBest &&
        (rto->videoStandardInput >= 1 && rto->videoStandardInput <= 4)) {
        // autobesthtotal
        updateCoastPosition(0);
        delay(1);
        resetInterruptNoHsyncBadBit();
        resetInterruptSogBadBit();
        delay(10);
        // works reliably now on my test HDMI dongle
        if (rto->useHdmiSyncFix && !uopt->wantOutputComponent) {
            GBS::PAD_SYNC_OUT_ENZ::write(0); // sync out
        }
        delay(70); // minimum delay without random failures: TBD

        for (uint8_t i = 0; i < 4; i++) {
            if (GBS::STATUS_INT_SOG_BAD::read() == 1) {
                optimizeSogLevel();
                resetInterruptSogBadBit();
                delay(40);
            } else if (getStatus16SpHsStable() && getStatus16SpHsStable()) {
                delay(1); // wifi
                if (getVideoMode() == rto->videoStandardInput) {
                    boolean ok = 0;
                    float sfr = getSourceFieldRate(0);
                    //Serial.println(sfr, 3);
                    if (rto->videoStandardInput == 1 || rto->videoStandardInput == 3) {
                        if (sfr > 58.6f && sfr < 61.4f)
                            ok = 1;
                    } else if (rto->videoStandardInput == 2 || rto->videoStandardInput == 4) {
                        if (sfr > 49.1f && sfr < 51.1f)
                            ok = 1;
                    }
                    if (ok) { // else leave it for later
                        runAutoBestHTotal();
                        delay(1); // wifi
                        break;
                    }
                }
            }
            delay(10);
        }
    } else {
        // scaling rgbhv, HD modes, no autobesthtotal
        delay(10);
        // works reliably now on my test HDMI dongle
        if (rto->useHdmiSyncFix && !uopt->wantOutputComponent) {
            GBS::PAD_SYNC_OUT_ENZ::write(0); // sync out
        }
        delay(20);
        updateCoastPosition(0);
        updateClampPosition();
    }

    //SerialM.print("pp time: "); SerialM.println(millis() - postLoadTimer);

    // make sure
    if (rto->useHdmiSyncFix && !uopt->wantOutputComponent) {
        GBS::PAD_SYNC_OUT_ENZ::write(0); // sync out
    }

    // late adjustments that require some delay time first
    if (!rto->isCustomPreset) {
        if (videoStandardInputIsPalNtscSd() && !rto->outModeHdBypass) {
            // SNES has less total lines and a slight offset (only relevant in 60Hz)
            if (GBS::VPERIOD_IF::read() == 523) {
                GBS::IF_VB_SP::write(GBS::IF_VB_SP::read() + 4);
                GBS::IF_VB_ST::write(GBS::IF_VB_ST::read() + 4);
            }
        }
    }

    // new, might be useful (3_6D - 3_72)
    GBS::VDS_EXT_HB_ST::write(GBS::VDS_DIS_HB_ST::read());
    GBS::VDS_EXT_HB_SP::write(GBS::VDS_DIS_HB_SP::read());
    GBS::VDS_EXT_VB_ST::write(GBS::VDS_DIS_VB_ST::read());
    GBS::VDS_EXT_VB_SP::write(GBS::VDS_DIS_VB_SP::read());
    // VDS_VSYN_SIZE1 + VDS_VSYN_SIZE2 to VDS_VSYNC_RST + 2
    GBS::VDS_VSYN_SIZE1::write(GBS::VDS_VSYNC_RST::read() + 2);
    GBS::VDS_VSYN_SIZE2::write(GBS::VDS_VSYNC_RST::read() + 2);
    GBS::VDS_FRAME_RST::write(4); // 3_19
    // VDS_FRAME_NO, VDS_FR_SELECT
    GBS::VDS_FRAME_NO::write(1);  // 3_1f 0-3
    GBS::VDS_FR_SELECT::write(1); // 3_1b, 3_1c, 3_1d, 3_1e

    // noise starts here!
    resetDigital();

    resetPLLAD();             // also turns on pllad
    GBS::PLLAD_LEN::write(1); // 5_11 1

    if (!rto->isCustomPreset) {
        GBS::VDS_IN_DREG_BYPS::write(0); // 3_40 2 // 0 = input data triggered on falling clock edge, 1 = bypass
        GBS::PLLAD_R::write(3);
        GBS::PLLAD_S::write(3);
        GBS::PLL_R::write(1); // PLL lock detector skew
        GBS::PLL_S::write(2);
        GBS::DEC_IDREG_EN::write(1); // 5_1f 7
        GBS::DEC_WEN_MODE::write(1); // 5_1e 7 // 1 keeps ADC phase consistent. around 4 lock positions vs totally random

        // 4 segment
        GBS::CAP_SAFE_GUARD_EN::write(0); // 4_21_5 // does more harm than good
        // memory timings, anti noise
        GBS::PB_CUT_REFRESH::write(1);   // helps with PLL=ICLK mode artefacting
        GBS::RFF_LREQ_CUT::write(0);     // was in motionadaptive toggle function but on, off seems nicer
        GBS::CAP_REQ_OVER::write(0);     // 4_22 0  1=capture stop at hblank 0=free run
        GBS::CAP_STATUS_SEL::write(1);   // 4_22 1  1=capture request when FIFO 50%, 0= FIFO 100%
        GBS::PB_REQ_SEL::write(3);       // PlayBack 11 High request Low request
                                         // 4_2C, 4_2D should be set by preset
        GBS::RFF_WFF_OFFSET::write(0x0); // scanline fix
        if (rto->videoStandardInput == 3 || rto->videoStandardInput == 4) {
            // this also handles mode 14 csync rgbhv
            GBS::PB_CAP_OFFSET::write(GBS::PB_FETCH_NUM::read() + 4); // 4_37 to 4_39 (green bar)
        }
        // 4_12 should be set by preset
    }

    if (!rto->outModeHdBypass) {
        ResetSDRAM();
    }

    setAndUpdateSogLevel(rto->currentLevelSOG); // use this to cycle SP / ADPLL latches

    if (rto->presetID != 0x06 && rto->presetID != 0x16) {
        // IF_VS_SEL = 1 for SD/HD SP mode in HD mode (5_3e 1)
        GBS::IF_VS_SEL::write(0); // 0 = "VCR" IF sync, requires VS_FLIP to be on, more stable?
        GBS::IF_VS_FLIP::write(1);
    }

    GBS::SP_CLP_SRC_SEL::write(0); // 0: 27Mhz clock; 1: pixel clock
    GBS::SP_CS_CLP_ST::write(32);
    GBS::SP_CS_CLP_SP::write(48); // same as reset parameters

    if (!uopt->wantOutputComponent) {
        GBS::PAD_SYNC_OUT_ENZ::write(0); // enable sync out if needed
    }
    GBS::DAC_RGBS_PWDNZ::write(1); // DAC on if needed
    GBS::DAC_RGBS_SPD::write(0);   // 0_45 2 DAC_SVM power down disable, somehow less jailbars
    GBS::DAC_RGBS_S0ENZ::write(0); //
    GBS::DAC_RGBS_S1EN::write(1);  // these 2 also help

    rto->useHdmiSyncFix = 0; // reset flag

    GBS::SP_H_PROTECT::write(0);
    if (rto->videoStandardInput >= 5) {
        GBS::SP_DIS_SUB_COAST::write(1); // might not disable it at all soon
    }

    if (rto->syncTypeCsync) {
        GBS::SP_EXT_SYNC_SEL::write(1); // 5_20 3 disconnect HV input
                                        // stays disconnected until reset condition
    }

    rto->coastPositionIsSet = false; // re-arm these
    rto->clampPositionIsSet = false;

    if (rto->outModeHdBypass) {
        GBS::INTERRUPT_CONTROL_01::write(0xff); // enable interrupts
        GBS::INTERRUPT_CONTROL_00::write(0xff); // reset irq status
        GBS::INTERRUPT_CONTROL_00::write(0x00);
        unfreezeVideo(); // eventhough not used atm
        // DAC and Sync out will be enabled later
        return; // to setOutModeHdBypass();
    }

    if (GBS::GBS_OPTION_SCALING_RGBHV::read() == 1) {
        rto->videoStandardInput = 14;
    }

    if (GBS::GBS_OPTION_SCALING_RGBHV::read() == 0) {
        unsigned long timeout = millis();
        while ((!getStatus16SpHsStable()) && (millis() - timeout < 2002)) {
            delay(4);
            handleWiFi(0);
            updateSpDynamic(0);
        }
        while ((getVideoMode() == 0) && (millis() - timeout < 1505)) {
            delay(4);
            handleWiFi(0);
            updateSpDynamic(0);
        }

        timeout = millis() - timeout;
        if (timeout > 1000) {
            Serial.print(F("to1 is: "));
            Serial.println(timeout);
        }
        if (timeout >= 1500) {
            if (rto->currentLevelSOG >= 7) {
                optimizeSogLevel();
                delay(300);
            }
        }
    }

    // early attempt
    updateClampPosition();
    if (rto->clampPositionIsSet) {
        if (GBS::SP_NO_CLAMP_REG::read() == 1) {
            GBS::SP_NO_CLAMP_REG::write(0);
        }
    }

    updateSpDynamic(0);

    if (!rto->syncWatcherEnabled) {
        GBS::SP_NO_CLAMP_REG::write(0);
    }

    // this was used with ADC write enable, producing about (exactly?) 4 lock positions
    // cycling through the phase let it land favorably
    //for (uint8_t i = 0; i < 8; i++) {
    //  advancePhase();
    //}

    setAndUpdateSogLevel(rto->currentLevelSOG); // use this to cycle SP / ADPLL latches
    //optimizePhaseSP();  // do this later in run loop

    GBS::INTERRUPT_CONTROL_01::write(0xff); // enable interrupts
    GBS::INTERRUPT_CONTROL_00::write(0xff); // reset irq status
    GBS::INTERRUPT_CONTROL_00::write(0x00);

    OutputComponentOrVGA();

    // presetPreference 10 means the user prefers bypass mode at startup
    // it's best to run a normal format detect > apply preset loop, then enter bypass mode
    // this can lead to an endless loop, so applyPresetDoneStage = 10 applyPresetDoneStage = 11
    // are introduced to break out of it.
    // also need to check for mode 15
    // also make sure to turn off autoBestHtotal
    if (uopt->presetPreference == 10 && rto->videoStandardInput != 15) {
        rto->autoBestHtotalEnabled = 0;
        if (rto->applyPresetDoneStage == 11) {
            // we were here before, stop the loop
            rto->applyPresetDoneStage = 1;
        } else {
            rto->applyPresetDoneStage = 10;
        }
    } else {
        // normal modes
        rto->applyPresetDoneStage = 1;
    }

    unfreezeVideo();

    if (uopt->enableFrameTimeLock) {
        activeFrameTimeLockInitialSteps();
    }

    SerialM.print(F("\npreset applied: "));
    if (rto->presetID == 0x01 || rto->presetID == 0x11)
        SerialM.print(F("1280x960"));
    else if (rto->presetID == 0x02 || rto->presetID == 0x12)
        SerialM.print(F("1280x1024"));
    else if (rto->presetID == 0x03 || rto->presetID == 0x13)
        SerialM.print(F("1280x720"));
    else if (rto->presetID == 0x05 || rto->presetID == 0x15)
        SerialM.print(F("1920x1080"));
    else if (rto->presetID == 0x06 || rto->presetID == 0x16)
        SerialM.print(F("downscale"));
    else if (rto->presetID == 0x04)
        SerialM.print(F("720x480"));
    else if (rto->presetID == 0x14)
        SerialM.print(F("768x576"));
    else
        SerialM.print(F("bypass"));

    if (rto->outModeHdBypass) {
        SerialM.print(F(" (bypass)"));
    } else if (rto->isCustomPreset) {
        SerialM.print(F(" (custom)"));
    }

    SerialM.print(F(" for "));
    if (rto->videoStandardInput == 1)
        SerialM.print(F("NTSC 60Hz "));
    else if (rto->videoStandardInput == 2)
        SerialM.print(F("PAL 50Hz "));
    else if (rto->videoStandardInput == 3)
        SerialM.print(F("EDTV 60Hz"));
    else if (rto->videoStandardInput == 4)
        SerialM.print(F("EDTV 50Hz"));
    else if (rto->videoStandardInput == 5)
        SerialM.print(F("720p 60Hz HDTV "));
    else if (rto->videoStandardInput == 6)
        SerialM.print(F("1080i 60Hz HDTV "));
    else if (rto->videoStandardInput == 7)
        SerialM.print(F("1080p 60Hz HDTV "));
    else if (rto->videoStandardInput == 8)
        SerialM.print(F("Medium Res "));
    else if (rto->videoStandardInput == 13)
        SerialM.print(F("VGA/SVGA/XGA/SXGA"));
    else if (rto->videoStandardInput == 14) {
        if (rto->syncTypeCsync)
            SerialM.print(F("scaling RGB (CSync)"));
        else
            SerialM.print(F("scaling RGB (HV Sync)"));
    } else if (rto->videoStandardInput == 15) {
        if (rto->syncTypeCsync)
            SerialM.print(F("RGB Bypass (CSync)"));
        else
            SerialM.print(F("RGB Bypass (HV Sync)"));
    } else if (rto->videoStandardInput == 0)
        SerialM.print(F("!should not go here!"));

    if (rto->presetID == 0x05 || rto->presetID == 0x15) {
        SerialM.print(F("(set your TV aspect ratio to 16:9!)"));
    }
    if (rto->videoStandardInput == 14) {
        SerialM.print(F("\nNote: scaling RGB is still in development"));
    }
    // presetPreference = OutputCustomized may fail to load (missing) preset file and arrive here with defaults
    SerialM.println("\n");
}

void setOutModeHdBypass(bool regsInitialized)
{
    typedef TV5725<GBS_ADDR> GBS;
    if (!rto->boardHasPower) {
        SerialM.println(F("GBS board not responding!"));
        return;
    }

    rto->autoBestHtotalEnabled = false; // disable while in this mode
    rto->outModeHdBypass = 1;           // skips waiting at end of doPostPresetLoadSteps

    externalClockGenResetClock();
    updateSpDynamic(0);
    loadHdBypassSection(); // this would be ignored otherwise
    if (GBS::ADC_UNUSED_62::read() != 0x00) {
        // remember debug view
        if (uopt->presetPreference != 2) {
            serialCommand = 'D';
        }
    }

    GBS::SP_NO_COAST_REG::write(0);  // enable vblank coast (just in case)
    GBS::SP_COAST_INV_REG::write(0); // also just in case

    FrameSync::cleanup();
    GBS::ADC_UNUSED_62::write(0x00);      // clear debug view
    GBS::RESET_CONTROL_0x46::write(0x00); // 0_46 all off, nothing needs to be enabled for bp mode
    GBS::RESET_CONTROL_0x47::write(0x00);
    GBS::PA_ADC_BYPSZ::write(1); // enable phase unit ADC
    GBS::PA_SP_BYPSZ::write(1);  // enable phase unit SP

    GBS::GBS_PRESET_ID::write(PresetHdBypass);
    // If loading from top-level, clear custom preset flag to avoid stale
    // values. If loading after applyPresets() called writeProgramArrayNew(), it
    // has already set the flag to 1.
    if (!regsInitialized) {
        GBS::GBS_PRESET_CUSTOM::write(0);
    }
    doPostPresetLoadSteps(); // todo: remove this, code path for hdbypass is hard to follow

    // doPostPresetLoadSteps() sets rto->presetID = GBS_PRESET_ID::read() =
    // PresetHdBypass, and rto->isCustomPreset = GBS_PRESET_CUSTOM::read().

    resetDebugPort();

    rto->autoBestHtotalEnabled = false; // need to re-set this
    GBS::OUT_SYNC_SEL::write(1);        // 0_4f 1=sync from HDBypass, 2=sync from SP, 0 = sync from VDS

    GBS::PLL_CKIS::write(0);    // 0_40 0 //  0: PLL uses OSC clock | 1: PLL uses input clock
    GBS::PLL_DIVBY2Z::write(0); // 0_40 1 // 1= no divider (full clock, ie 27Mhz) 0 = halved
    //GBS::PLL_ADS::write(0); // 0_40 3 test:  input clock is from PCLKIN (disconnected, not ADC clock)
    GBS::PAD_OSC_CNTRL::write(1); // test: noticed some wave pattern in 720p source, this fixed it
    if (rto->extClockGenDetected) {
        GBS::PLL648_CONTROL_01::write(0x75);
    } else {
        GBS::PLL648_CONTROL_01::write(0x35);
    }
    GBS::PLL648_CONTROL_03::write(0x00);
    GBS::PLL_LEN::write(1); // 0_43
    GBS::DAC_RGBS_R0ENZ::write(1);
    GBS::DAC_RGBS_G0ENZ::write(1); // 0_44
    GBS::DAC_RGBS_B0ENZ::write(1);
    GBS::DAC_RGBS_S1EN::write(1); // 0_45
    // from RGBHV tests: the memory bus can be tri stated for noise reduction
    GBS::PAD_TRI_ENZ::write(1);        // enable tri state
    GBS::PLL_MS::write(2);             // select feedback clock (but need to enable tri state!)
    GBS::MEM_PAD_CLK_INVERT::write(0); // helps also
    GBS::RESET_CONTROL_0x47::write(0x1f);

    // update: found the real use of HDBypass :D
    GBS::DAC_RGBS_BYPS2DAC::write(1);
    GBS::SP_HS_LOOP_SEL::write(1);
    GBS::SP_HS_PROC_INV_REG::write(0); // (5_56_5) do not invert HS
    GBS::SP_CS_P_SWAP::write(0);       // old default, set here to reset between HDBypass formats
    GBS::SP_HS2PLL_INV_REG::write(0);  // same

    GBS::PB_BYPASS::write(1);
    GBS::PLLAD_MD::write(2345); // 2326 looks "better" on my LCD but 2345 looks just correct on scope
    GBS::PLLAD_KS::write(2);    // 5_16 post divider 0 : FCKO1 > 87MHz, 3 : FCKO1<23MHz
    setOverSampleRatio(2, true);
    GBS::PLLAD_ICP::write(5);
    GBS::PLLAD_FS::write(1);

    if (rto->inputIsYpBpR) {
        GBS::DEC_MATRIX_BYPS::write(1); // 5_1f 2 = 1 for YUV / 0 for RGB
        GBS::HD_MATRIX_BYPS::write(0);  // 1_30 1 / input to jacks is yuv, adc leaves it as yuv > convert to rgb for output here
        GBS::HD_DYN_BYPS::write(0);     // don't bypass color expansion
                                        //GBS::HD_U_OFFSET::write(3);     // color adjust via scope
                                        //GBS::HD_V_OFFSET::write(3);     // color adjust via scope
    } else {
        GBS::DEC_MATRIX_BYPS::write(1); // this is normally RGB input for HDBYPASS out > no color matrix at all
        GBS::HD_MATRIX_BYPS::write(1);  // 1_30 1 / input is rgb, adc leaves it as rgb > bypass matrix
        GBS::HD_DYN_BYPS::write(1);     // bypass as well
    }

    GBS::HD_SEL_BLK_IN::write(0); // 0 enables HDB blank timing (1 would be DVI, not working atm)

    GBS::SP_SDCS_VSST_REG_H::write(0); // S5_3B
    GBS::SP_SDCS_VSSP_REG_H::write(0); // S5_3B
    GBS::SP_SDCS_VSST_REG_L::write(0); // S5_3F // 3 for SP sync
    GBS::SP_SDCS_VSSP_REG_L::write(2); // S5_40 // 10 for SP sync // check with interlaced sources

    GBS::HD_HSYNC_RST::write(0x3ff); // max 0x7ff
    GBS::HD_INI_ST::write(0);        // todo: test this at 0 / was 0x298
    // timing into HDB is PLLAD_MD with PLLAD_KS divider: KS = 0 > full PLLAD_MD
    if (rto->videoStandardInput <= 2) {
        // PAL and NTSC are rewrites, the rest is still handled normally
        // These 2 formats now have SP_HS2PLL_INV_REG set. That's the only way I know so far that
        // produces recovered HSyncs that align to the falling edge of the input
        // ToDo: find reliable input active flank detect to then set SP_HS2PLL_INV_REG correctly
        // (for PAL/NTSC polarity is known to be active low, but other formats are variable)
        GBS::SP_HS2PLL_INV_REG::write(1);  //5_56 1 lock to falling HS edge // check > sync issues with MD
        GBS::SP_CS_P_SWAP::write(1);       //5_3e 0 new: this should negate the problem with inverting HS2PLL
        GBS::SP_HS_PROC_INV_REG::write(1); // (5_56_5) invert HS to DEC
        // invert mode detect HS/VS triggers, helps PSX NTSC detection. required with 5_3e 0 set
        GBS::MD_HS_FLIP::write(1);
        GBS::MD_VS_FLIP::write(1);
        GBS::OUT_SYNC_SEL::write(2);   // new: 0_4f 1=sync from HDBypass, 2=sync from SP, 0 = sync from VDS
        GBS::SP_HS_LOOP_SEL::write(0); // 5_57 6 new: use full SP sync, enable HS positioning and pulse length control
        GBS::ADC_FLTR::write(3);       // 5_03 4/5 ADC filter 3=40, 2=70, 1=110, 0=150 Mhz
        //GBS::HD_INI_ST::write(0x76); // 1_39

        GBS::HD_HSYNC_RST::write((GBS::PLLAD_MD::read() / 2) + 8); // ADC output pixel count determined
        GBS::HD_HB_ST::write(GBS::PLLAD_MD::read() * 0.945f);      // 1_3B  // no idea why it's not coupled to HD_RST
        GBS::HD_HB_SP::write(0x90);                                // 1_3D
        GBS::HD_HS_ST::write(0x80);                                // 1_3F  // but better to use SP sync directly (OUT_SYNC_SEL = 2)
        GBS::HD_HS_SP::write(0x00);                                // 1_41  //
        // to use SP sync directly; prepare reasonable out HS length
        GBS::SP_CS_HS_ST::write(0xA0);
        GBS::SP_CS_HS_SP::write(0x00);

        if (rto->videoStandardInput == 1) {
            setCsVsStart(250);         // don't invert VS with direct SP sync mode
            setCsVsStop(1);            // stop relates to HS pulses from CS decoder directly, so mind EQ pulses
            GBS::HD_VB_ST::write(500); // 1_43
            GBS::HD_VS_ST::write(3);   // 1_47 // but better to use SP sync directly (OUT_SYNC_SEL = 2)
            GBS::HD_VS_SP::write(522); // 1_49 //
            GBS::HD_VB_SP::write(16);  // 1_45
        }
        if (rto->videoStandardInput == 2) {
            setCsVsStart(301);         // don't invert
            setCsVsStop(5);            // stop past EQ pulses (6 on psx) normally, but HDMI adapter works with -=1 (5)
            GBS::HD_VB_ST::write(605); // 1_43
            GBS::HD_VS_ST::write(1);   // 1_47
            GBS::HD_VS_SP::write(621); // 1_49
            GBS::HD_VB_SP::write(16);  // 1_45
        }
    } else if (rto->videoStandardInput == 3 || rto->videoStandardInput == 4) { // 480p, 576p
        GBS::ADC_FLTR::write(2);                                               // 5_03 4/5 ADC filter 3=40, 2=70, 1=110, 0=150 Mhz
        GBS::PLLAD_KS::write(1);                                               // 5_16 post divider
        GBS::PLLAD_CKOS::write(0);                                             // 5_16 2x OS (with KS=1)
        //GBS::HD_INI_ST::write(0x76); // 1_39
        GBS::HD_HB_ST::write(0x864); // 1_3B
            // you *must* begin hblank before hsync.
        GBS::HD_HB_SP::write(0xa0);  // 1_3D
        GBS::HD_VB_ST::write(0x00);  // 1_43
        GBS::HD_VB_SP::write(0x40);  // 1_45
        if (rto->videoStandardInput == 3) {
            GBS::HD_HS_ST::write(0x54);  // 1_3F
            GBS::HD_HS_SP::write(0x864); // 1_41
            GBS::HD_VS_ST::write(0x06);  // 1_47 // VS neg
            GBS::HD_VS_SP::write(0x00);  // 1_49
            setCsVsStart(525 - 5);
            setCsVsStop(525 - 3);
        }
        if (rto->videoStandardInput == 4) {
            GBS::HD_HS_ST::write(0x10);  // 1_3F
            GBS::HD_HS_SP::write(0x880); // 1_41
            GBS::HD_VS_ST::write(0x06);  // 1_47 // VS neg
            GBS::HD_VS_SP::write(0x00);  // 1_49
            setCsVsStart(48);
            setCsVsStop(46);
        }
    } else if (rto->videoStandardInput <= 7 || rto->videoStandardInput == 13) {
        //GBS::SP_HS2PLL_INV_REG::write(0); // 5_56 1 use rising edge of tri-level sync // always 0 now
        if (rto->videoStandardInput == 5) { // 720p
            GBS::PLLAD_MD::write(2474);     // override from 2345
            GBS::HD_HSYNC_RST::write(550);  // 1_37
            //GBS::HD_INI_ST::write(78);     // 1_39
            // 720p has high pllad vco output clock, so don't do oversampling
            GBS::PLLAD_KS::write(0);       // 5_16 post divider 0 : FCKO1 > 87MHz, 3 : FCKO1<23MHz
            GBS::PLLAD_CKOS::write(0);     // 5_16 1x OS (with KS=CKOS=0)
            GBS::ADC_FLTR::write(0);       // 5_03 4/5 ADC filter 3=40, 2=70, 1=110, 0=150 Mhz
            GBS::ADC_CLK_ICLK1X::write(0); // 5_00 4 (OS=1)
            GBS::DEC2_BYPS::write(1);      // 5_1f 1 // dec2 disabled (OS=1)
            GBS::PLLAD_ICP::write(6);      // fine at 6 only, FS is 1
            GBS::PLLAD_FS::write(1);
            GBS::HD_HB_ST::write(0);     // 1_3B
            GBS::HD_HB_SP::write(0x140); // 1_3D
            GBS::HD_HS_ST::write(0x20);  // 1_3F
            GBS::HD_HS_SP::write(0x80);  // 1_41
            GBS::HD_VB_ST::write(0x00);  // 1_43
            GBS::HD_VB_SP::write(0x6c);  // 1_45 // ps3 720p tested
            GBS::HD_VS_ST::write(0x00);  // 1_47
            GBS::HD_VS_SP::write(0x05);  // 1_49
            setCsVsStart(2);
            setCsVsStop(0);
        }
        if (rto->videoStandardInput == 6) { // 1080i
            // interl. source
            GBS::HD_HSYNC_RST::write(0x710); // 1_37
            //GBS::HD_INI_ST::write(2);    // 1_39
            GBS::PLLAD_KS::write(1);    // 5_16 post divider
            GBS::PLLAD_CKOS::write(0);  // 5_16 2x OS (with KS=1)
            GBS::ADC_FLTR::write(1);    // 5_03 4/5 ADC filter 3=40, 2=70, 1=110, 0=150 Mhz
            GBS::HD_HB_ST::write(0);    // 1_3B
            GBS::HD_HB_SP::write(0xb8); // 1_3D
            GBS::HD_HS_ST::write(0x04); // 1_3F
            GBS::HD_HS_SP::write(0x50); // 1_41
            GBS::HD_VB_ST::write(0x00); // 1_43
            GBS::HD_VB_SP::write(0x1e); // 1_45
            GBS::HD_VS_ST::write(0x04); // 1_47
            GBS::HD_VS_SP::write(0x09); // 1_49
            setCsVsStart(8);
            setCsVsStop(6);
        }
        if (rto->videoStandardInput == 7) {  // 1080p
            GBS::PLLAD_MD::write(2749);      // override from 2345
            GBS::HD_HSYNC_RST::write(0x710); // 1_37
            //GBS::HD_INI_ST::write(0xf0);     // 1_39
            // 1080p has highest pllad vco output clock, so don't do oversampling
            GBS::PLLAD_KS::write(0);       // 5_16 post divider 0 : FCKO1 > 87MHz, 3 : FCKO1<23MHz
            GBS::PLLAD_CKOS::write(0);     // 5_16 1x OS (with KS=CKOS=0)
            GBS::ADC_FLTR::write(0);       // 5_03 4/5 ADC filter 3=40, 2=70, 1=110, 0=150 Mhz
            GBS::ADC_CLK_ICLK1X::write(0); // 5_00 4 (OS=1)
            GBS::DEC2_BYPS::write(1);      // 5_1f 1 // dec2 disabled (OS=1)
            GBS::PLLAD_ICP::write(6);      // was 5, fine at 6 as well, FS is 1
            GBS::PLLAD_FS::write(1);
            GBS::HD_HB_ST::write(0x00); // 1_3B
            GBS::HD_HB_SP::write(0xb0); // 1_3D // d0
            GBS::HD_HS_ST::write(0x20); // 1_3F
            GBS::HD_HS_SP::write(0x70); // 1_41
            GBS::HD_VB_ST::write(0x00); // 1_43
            GBS::HD_VB_SP::write(0x2f); // 1_45
            GBS::HD_VS_ST::write(0x04); // 1_47
            GBS::HD_VS_SP::write(0x0A); // 1_49
        }
        if (rto->videoStandardInput == 13) { // odd HD mode (PS2 "VGA" over Component)
            applyRGBPatches();               // treat mostly as RGB, clamp R/B to gnd
            rto->syncTypeCsync = true;       // used in loop to set clamps and SP dynamic
            GBS::DEC_MATRIX_BYPS::write(1);  // overwrite for this mode
            GBS::SP_PRE_COAST::write(4);
            GBS::SP_POST_COAST::write(4);
            GBS::SP_DLT_REG::write(0x70);
            GBS::HD_MATRIX_BYPS::write(1);     // bypass since we'll treat source as RGB
            GBS::HD_DYN_BYPS::write(1);        // bypass since we'll treat source as RGB
            GBS::SP_VS_PROC_INV_REG::write(0); // don't invert
            // same as with RGBHV, the ps2 resolution can vary widely
            GBS::PLLAD_KS::write(0);       // 5_16 post divider 0 : FCKO1 > 87MHz, 3 : FCKO1<23MHz
            GBS::PLLAD_CKOS::write(0);     // 5_16 1x OS (with KS=CKOS=0)
            GBS::ADC_CLK_ICLK1X::write(0); // 5_00 4 (OS=1)
            GBS::ADC_CLK_ICLK2X::write(0); // 5_00 3 (OS=1)
            GBS::DEC1_BYPS::write(1);      // 5_1f 1 // dec1 disabled (OS=1)
            GBS::DEC2_BYPS::write(1);      // 5_1f 1 // dec2 disabled (OS=1)
            GBS::PLLAD_MD::write(512);     // could try 856
        }
    }

    if (rto->videoStandardInput == 13) {
        // section is missing HD_HSYNC_RST and HD_INI_ST adjusts
        uint16_t vtotal = GBS::STATUS_SYNC_PROC_VTOTAL::read();
        if (vtotal < 532) { // 640x480 or less
            GBS::PLLAD_KS::write(3);
            GBS::PLLAD_FS::write(1);
        } else if (vtotal >= 532 && vtotal < 810) { // 800x600, 1024x768
            //GBS::PLLAD_KS::write(3); // just a little too much at 1024x768
            GBS::PLLAD_FS::write(0);
            GBS::PLLAD_KS::write(2);
        } else { //if (vtotal > 1058 && vtotal < 1074) { // 1280x1024
            GBS::PLLAD_KS::write(2);
            GBS::PLLAD_FS::write(1);
        }
    }

    GBS::DEC_IDREG_EN::write(1); // 5_1f 7
    GBS::DEC_WEN_MODE::write(1); // 5_1e 7 // 1 keeps ADC phase consistent. around 4 lock positions vs totally random
    rto->phaseSP = 8;
    rto->phaseADC = 24;                         // fix value // works best with yuv input in tests
    setAndUpdateSogLevel(rto->currentLevelSOG); // also re-latch everything

    rto->outModeHdBypass = 1;

    unsigned long timeout = millis();
    while ((!getStatus16SpHsStable()) && (millis() - timeout < 2002)) {
        delay(1);
    }
    while ((getVideoMode() == 0) && (millis() - timeout < 1502)) {
        delay(1);
    }
    // currently SP is using generic settings, switch to format specific ones
    updateSpDynamic(0);
    while ((getVideoMode() == 0) && (millis() - timeout < 1502)) {
        delay(1);
    }

    GBS::DAC_RGBS_PWDNZ::write(1);   // enable DAC
    GBS::PAD_SYNC_OUT_ENZ::write(0); // enable sync out
    delay(200);
    optimizePhaseSP();
    SerialM.println(F("pass-through on"));
}


void setIfHblankParameters()
{
    typedef TV5725<GBS_ADDR> GBS;
    if (!rto->outModeHdBypass) {
        uint16_t pll_divider = GBS::PLLAD_MD::read();

        // if line doubling (PAL, NTSC), div 2 + a couple pixels
        GBS::IF_HSYNC_RST::write(((pll_divider >> 1) + 13) & 0xfffe); // 1_0e
        GBS::IF_LINE_SP::write(GBS::IF_HSYNC_RST::read() + 1);        // 1_22
        if (rto->presetID == 0x05) {
            // override for 1080p manually for now (pll_divider alone isn't correct :/)
            GBS::IF_HSYNC_RST::write(GBS::IF_HSYNC_RST::read() + 32);
            GBS::IF_LINE_SP::write(GBS::IF_LINE_SP::read() + 32);
        }
        if (rto->presetID == 0x15) {
            // override for 1080p manually for now (pll_divider alone isn't correct :/)
            GBS::IF_HSYNC_RST::write(GBS::IF_HSYNC_RST::read() + 20);
            GBS::IF_LINE_SP::write(GBS::IF_LINE_SP::read() + 20);
        }

        if (GBS::IF_LD_RAM_BYPS::read()) {
            // no LD = EDTV or similar
            GBS::IF_HB_SP2::write((uint16_t)((float)pll_divider * 0.06512f) & 0xfffe); // 1_1a // 0.06512f
            // pll_divider / 2 - 3 is minimum IF_HB_ST2
            GBS::IF_HB_ST2::write((uint16_t)((float)pll_divider * 0.4912f) & 0xfffe); // 1_18
        } else {
            // LD mode (PAL, NTSC)
            GBS::IF_HB_SP2::write(4 + ((uint16_t)((float)pll_divider * 0.0224f) & 0xfffe)); // 1_1a
            GBS::IF_HB_ST2::write((uint16_t)((float)pll_divider * 0.4550f) & 0xfffe);       // 1_18

            if (GBS::IF_HB_ST2::read() >= 0x420) {
                // limit (fifo?) (0x420 = 1056) (might be 0x424 instead)
                // limit doesn't apply to EDTV modes, where 1_18 typically = 0x4B0
                GBS::IF_HB_ST2::write(0x420);
            }

            if (rto->presetID == 0x05 || rto->presetID == 0x15) {
                // override 1_1a for 1080p manually for now (pll_divider alone isn't correct :/)
                GBS::IF_HB_SP2::write(0x2A);
            }

            // position move via 1_26 and reserve for deinterlacer: add IF RST pixels
            // seems no extra pixels available at around PLLAD:84A or 2122px
            //uint16_t currentRst = GBS::IF_HSYNC_RST::read();
            //GBS::IF_HSYNC_RST::write((currentRst + (currentRst / 15)) & 0xfffe);  // 1_0e
            //GBS::IF_LINE_SP::write(GBS::IF_HSYNC_RST::read() + 1);                // 1_22
        }
    }
}
