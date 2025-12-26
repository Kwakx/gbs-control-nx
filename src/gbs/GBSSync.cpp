#include "GBSSync.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../gbs/GBSRegister.h"
#include "../gbs/GBSPresets.h"
#include "../gbs/GBSPhase.h"
#include "../gbs/GBSController.h"
#include "../gbs/GBSVideoProcessing.h"
#include "../utils/DebugHelpers.h"
#include "../video/framesync.h"
#include "tv5725.h"
#include "../core/options.h"

void activeFrameTimeLockInitialSteps()
{
    typedef TV5725<GBS_ADDR> GBS;
    // skip if using external clock gen
    if (rto->extClockGenDetected) {
        SerialM.println(F("Active FrameTime Lock enabled, adjusting external clock gen frequency"));
        return;
    }
    // skip when out mode = bypass
    if (rto->presetID != PresetHdBypass && rto->presetID != PresetBypassRGBHV) {
        SerialM.print(F("Active FrameTime Lock enabled, disable if display unstable or stays blank! Method: "));
        if (uopt->frameTimeLockMethod == 0) {
            SerialM.println(F("0 (vtotal + VSST)"));
        }
        if (uopt->frameTimeLockMethod == 1) {
            SerialM.println(F("1 (vtotal only)"));
        }
        if (GBS::VDS_VS_ST::read() == 0) {
            SerialM.println(F("Warning: Check VDS_VS_ST!"));
        }
    }
}

void resetInterruptSogSwitchBit()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::INT_CONTROL_RST_SOGSWITCH::write(1);
    GBS::INT_CONTROL_RST_SOGSWITCH::write(0);
}

void resetInterruptSogBadBit()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::INT_CONTROL_RST_SOGBAD::write(1);
    GBS::INT_CONTROL_RST_SOGBAD::write(0);
}

void resetInterruptNoHsyncBadBit()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::INT_CONTROL_RST_NOHSYNC::write(1);
    GBS::INT_CONTROL_RST_NOHSYNC::write(0);
}

void setResetParameters()
{
    typedef TV5725<GBS_ADDR> GBS;
    SerialM.println("<reset>");
    rto->videoStandardInput = 0;
    rto->videoIsFrozen = false;
    rto->applyPresetDoneStage = 0;
    rto->presetVlineShift = 0;
    rto->sourceDisconnected = true;
    rto->outModeHdBypass = 0;
    rto->clampPositionIsSet = 0;
    rto->coastPositionIsSet = 0;
    rto->phaseIsSet = 0;
    rto->continousStableCounter = 0;
    rto->noSyncCounter = 0;
    rto->isInLowPowerMode = false;
    rto->currentLevelSOG = 5;
    rto->thisSourceMaxLevelSOG = 31; // 31 = auto sog has not (yet) run
    rto->failRetryAttempts = 0;
    rto->HPLLState = 0;
    rto->motionAdaptiveDeinterlaceActive = false;
    rto->scanlinesEnabled = false;
    rto->syncTypeCsync = false;
    rto->isValidForScalingRGBHV = false;
    rto->medResLineCount = 0x33; // 51*8=408
    rto->osr = 0;
    rto->useHdmiSyncFix = 0;
    rto->notRecognizedCounter = 0;

    adco->r_gain = 0;
    adco->g_gain = 0;
    adco->b_gain = 0;

    // clear temp storage
    GBS::ADC_UNUSED_64::write(0);
    GBS::ADC_UNUSED_65::write(0);
    GBS::ADC_UNUSED_66::write(0);
    GBS::ADC_UNUSED_67::write(0);
    GBS::GBS_PRESET_CUSTOM::write(0);
    GBS::GBS_PRESET_ID::write(0);
    GBS::GBS_OPTION_SCALING_RGBHV::write(0);
    GBS::GBS_OPTION_PALFORCED60_ENABLED::write(0);

    // set minimum IF parameters
    GBS::IF_VS_SEL::write(1);
    GBS::IF_VS_FLIP::write(1);
    GBS::IF_HSYNC_RST::write(0x3FF);
    GBS::IF_VB_ST::write(0);
    GBS::IF_VB_SP::write(2);

    // could stop ext clock gen output here?
    FrameSync::cleanup();

    GBS::OUT_SYNC_CNTRL::write(0);    // no H / V sync out to PAD
    GBS::DAC_RGBS_PWDNZ::write(0);    // disable DAC
    GBS::ADC_TA_05_CTRL::write(0x02); // 5_05 1 // minor SOG clamp effect
    GBS::ADC_TEST_04::write(0x02);    // 5_04
    GBS::ADC_TEST_0C::write(0x12);    // 5_0c 1 4
    GBS::ADC_CLK_PA::write(0);        // 5_00 0/1 PA_ADC input clock = PLLAD CLKO2
    GBS::ADC_SOGEN::write(1);
    GBS::SP_SOG_MODE::write(1);
    GBS::ADC_INPUT_SEL::write(1); // 1 = RGBS / RGBHV adc data input
    GBS::ADC_POWDZ::write(1);     // ADC on
    setAndUpdateSogLevel(rto->currentLevelSOG);
    GBS::RESET_CONTROL_0x46::write(0x00); // all units off
    GBS::RESET_CONTROL_0x47::write(0x00);
    GBS::GPIO_CONTROL_00::write(0x67);     // most GPIO pins regular GPIO
    GBS::GPIO_CONTROL_01::write(0x00);     // all GPIO outputs disabled
    GBS::DAC_RGBS_PWDNZ::write(0);         // disable DAC (output)
    GBS::PLL648_CONTROL_01::write(0x00);   // VCLK(1/2/4) display clock // needs valid for debug bus
    GBS::PAD_CKIN_ENZ::write(0);           // 0 = clock input enable (pin40)
    GBS::PAD_CKOUT_ENZ::write(1);          // clock output disable
    GBS::IF_SEL_ADC_SYNC::write(1);        // ! 1_28 2
    GBS::PLLAD_VCORST::write(1);           // reset = 1
    GBS::PLL_ADS::write(1);                // When = 1, input clock is from ADC ( otherwise, from unconnected clock at pin 40 )
    GBS::PLL_CKIS::write(0);               // PLL use OSC clock
    GBS::PLL_MS::write(2);                 // fb memory clock can go lower power
    GBS::PAD_CONTROL_00_0x48::write(0x2b); //disable digital inputs, enable debug out pin
    GBS::PAD_CONTROL_01_0x49::write(0x1f); //pad control pull down/up transistors on
    loadHdBypassSection();                 // 1_30 to 1_55
    loadPresetMdSection();                 // 1_60 to 1_83
    setAdcParametersGainAndOffset();
    GBS::SP_PRE_COAST::write(9);    // was 0x07 // need pre / poast to allow all sources to detect
    GBS::SP_POST_COAST::write(18);  // was 0x10 // ps2 1080p 18
    GBS::SP_NO_COAST_REG::write(0); // can be 1 in some soft reset situations, will prevent sog vsync decoding << really?
    GBS::SP_CS_CLP_ST::write(32);   // define it to something at start
    GBS::SP_CS_CLP_SP::write(48);
    GBS::SP_SOG_SRC_SEL::write(0);  // SOG source = ADC
    GBS::SP_EXT_SYNC_SEL::write(0); // connect HV input ( 5_20 bit 3 )
    GBS::SP_NO_CLAMP_REG::write(1);
    GBS::PLLAD_ICP::write(0); // lowest charge pump current
    GBS::PLLAD_FS::write(0);  // low gain (have to deal with cold and warm startups)
    GBS::PLLAD_5_16::write(0x1f);
    GBS::PLLAD_MD::write(0x700);
    resetPLL(); // cycles PLL648
    delay(2);
    resetPLLAD();                            // same for PLLAD
    GBS::PLL_VCORST::write(1);               // reset on
    GBS::PLLAD_CONTROL_00_5x11::write(0x01); // reset on
    resetDebugPort();

    //GBS::RESET_CONTROL_0x47::write(0x16);
    GBS::RESET_CONTROL_0x46::write(0x41);   // new 23.07.19
    GBS::RESET_CONTROL_0x47::write(0x17);   // new 23.07.19 (was 0x16)
    GBS::INTERRUPT_CONTROL_01::write(0xff); // enable interrupts
    GBS::INTERRUPT_CONTROL_00::write(0xff); // reset irq status
    GBS::INTERRUPT_CONTROL_00::write(0x00);
    GBS::PAD_SYNC_OUT_ENZ::write(0); // sync output enabled, will be low (HC125 fix)
    rto->clampPositionIsSet = 0;     // some functions override these, so make sure
    rto->coastPositionIsSet = 0;
    rto->phaseIsSet = 0;
    rto->continousStableCounter = 0;
    serialCommand = '@';
    userCommand = '@';
}

void updateHVSyncEdge()
{
    typedef TV5725<GBS_ADDR> GBS;
    static uint8_t printHS = 0, printVS = 0;
    uint16_t temp = 0;

    if (GBS::STATUS_INT_SOG_BAD::read() == 1) {
        resetInterruptSogBadBit();
        return;
    }

    uint8_t syncStatus = GBS::STATUS_16::read();
    if (rto->syncTypeCsync) {
        // sog check, only check H
        if ((syncStatus & 0x02) != 0x02)
            return;
    } else {
        // HV check, check H + V
        if ((syncStatus & 0x0a) != 0x0a)
            return;
    }

    if ((syncStatus & 0x02) != 0x02) // if !hs active
    {
        //SerialM.println("(SP) can't detect sync edge");
    } else {
        if ((syncStatus & 0x01) == 0x00) {
            if (printHS != 1) {
                SerialM.println(F("(SP) HS active low"));
            }
            printHS = 1;

            temp = GBS::HD_HS_SP::read();
            if (GBS::HD_HS_ST::read() < temp) { // if out sync = ST < SP
                GBS::HD_HS_SP::write(GBS::HD_HS_ST::read());
                GBS::HD_HS_ST::write(temp);
                GBS::SP_HS2PLL_INV_REG::write(1);
            }
        } else {
            if (printHS != 2) {
                SerialM.println(F("(SP) HS active high"));
            }
            printHS = 2;

            temp = GBS::HD_HS_SP::read();
            if (GBS::HD_HS_ST::read() > temp) { // if out sync = ST > SP
                GBS::HD_HS_SP::write(GBS::HD_HS_ST::read());
                GBS::HD_HS_ST::write(temp);
                GBS::SP_HS2PLL_INV_REG::write(0);
            }
        }

        // VS check, but only necessary for separate sync (CS should have VS always active low)
        if (rto->syncTypeCsync == false) {
            if ((syncStatus & 0x08) != 0x08) // if !vs active
            {
                Serial.println(F("VS can't detect sync edge"));
            } else {
                if ((syncStatus & 0x04) == 0x00) {
                    if (printVS != 1) {
                        SerialM.println(F("(SP) VS active low"));
                    }
                    printVS = 1;

                    temp = GBS::HD_VS_SP::read();
                    if (GBS::HD_VS_ST::read() < temp) { // if out sync = ST < SP
                        GBS::HD_VS_SP::write(GBS::HD_VS_ST::read());
                        GBS::HD_VS_ST::write(temp);
                    }
                } else {
                    if (printVS != 2) {
                        SerialM.println(F("(SP) VS active high"));
                    }
                    printVS = 2;

                    temp = GBS::HD_VS_SP::read();
                    if (GBS::HD_VS_ST::read() > temp) { // if out sync = ST > SP
                        GBS::HD_VS_SP::write(GBS::HD_VS_ST::read());
                        GBS::HD_VS_ST::write(temp);
                    }
                }
            }
        }
    }
}

void prepareSyncProcessor()
{
    typedef TV5725<GBS_ADDR> GBS;
    writeOneByte(0xF0, 5);
    GBS::SP_SOG_P_ATO::write(0); // 5_20 disable sog auto polarity // hpw can be > ht, but auto is worse
    GBS::SP_JITTER_SYNC::write(0);
    // H active detect control
    writeOneByte(0x21, 0x18); // SP_SYNC_TGL_THD    H Sync toggle time threshold  0x20; lower than 5_33(not always); 0 to turn off (?) 0x18 for 53.69 system @ 33.33
    writeOneByte(0x22, 0x0F); // SP_L_DLT_REG       Sync pulse width difference threshold (less than this = equal)
    writeOneByte(0x23, 0x00); // UNDOCUMENTED       range from 0x00 to at least 0x1d
    writeOneByte(0x24, 0x40); // SP_T_DLT_REG       H total width difference threshold rgbhv: b // range from 0x02 upwards
    writeOneByte(0x25, 0x00); // SP_T_DLT_REG
    writeOneByte(0x26, 0x04); // SP_SYNC_PD_THD     H sync pulse width threshold // from 0(?) to 0x50 // in yuv 720p range only to 0x0a!
    writeOneByte(0x27, 0x00); // SP_SYNC_PD_THD
    writeOneByte(0x2a, 0x0F); // SP_PRD_EQ_THD      How many legal lines as valid; scales with 5_33 (needs to be below)
    // V active detect control
    writeOneByte(0x2d, 0x03); // SP_VSYNC_TGL_THD   V sync toggle time threshold // at 5 starts to drop many 0_16 vsync events
    writeOneByte(0x2e, 0x00); // SP_SYNC_WIDTH_DTHD V sync pulse width threshold
    writeOneByte(0x2f, 0x02); // SP_V_PRD_EQ_THD    How many continue legal v sync as valid // at 4 starts to drop 0_16 vsync events
    writeOneByte(0x31, 0x2f); // SP_VT_DLT_REG      V total difference threshold
    // Timer value control
    writeOneByte(0x33, 0x3a); // SP_H_TIMER_VAL     H timer value for h detect (was 0x28)
    writeOneByte(0x34, 0x06); // SP_V_TIMER_VAL     V timer for V detect // 0_16 vsactive // was 0x05
    // Sync separation control
    if (rto->videoStandardInput == 0)
        GBS::SP_DLT_REG::write(0x70); // 5_35  130 too much for ps2 1080i, 0xb0 for 1080p
    else if (rto->videoStandardInput <= 4)
        GBS::SP_DLT_REG::write(0xC0); // old: extended to 0x150 later if mode = 1 or 2
    else if (rto->videoStandardInput <= 6)
        GBS::SP_DLT_REG::write(0xA0);
    else if (rto->videoStandardInput == 7)
        GBS::SP_DLT_REG::write(0x70);
    else
        GBS::SP_DLT_REG::write(0x70);

    if (videoStandardInputIsPalNtscSd()) {
        GBS::SP_H_PULSE_IGNOR::write(0x6b);
    } else {
        GBS::SP_H_PULSE_IGNOR::write(0x02); // test with MS / Genesis mode (wsog 2) vs ps2 1080p (0x13 vs 0x05)
    }
    GBS::SP_H_TOTAL_EQ_THD::write(3);

    GBS::SP_SDCS_VSST_REG_H::write(0);
    GBS::SP_SDCS_VSSP_REG_H::write(0);
    GBS::SP_SDCS_VSST_REG_L::write(4); // 5_3f // was 12
    GBS::SP_SDCS_VSSP_REG_L::write(1); // 5_40 // was 11

    GBS::SP_CS_HS_ST::write(0x10); // 5_45
    GBS::SP_CS_HS_SP::write(0x00); // 5_47 720p source needs ~20 range, may be necessary to adjust at runtime, based on source res

    writeOneByte(0x49, 0x00); // retime HS start for RGB+HV rgbhv: 20
    writeOneByte(0x4a, 0x00); //
    writeOneByte(0x4b, 0x44); // retime HS stop rgbhv: 50
    writeOneByte(0x4c, 0x00); //

    writeOneByte(0x51, 0x02); // 0x00 rgbhv: 2
    writeOneByte(0x52, 0x00); // 0xc0
    writeOneByte(0x53, 0x00); // 0x05 rgbhv: 6
    writeOneByte(0x54, 0x00); // 0xc0

    if (rto->videoStandardInput != 15 && (GBS::GBS_OPTION_SCALING_RGBHV::read() != 1)) {
        GBS::SP_CLAMP_MANUAL::write(0); // 0 = automatic on/off possible
        GBS::SP_CLP_SRC_SEL::write(0);  // clamp source 1: pixel clock, 0: 27mhz // was 1 but the pixel clock isn't available at first
        GBS::SP_NO_CLAMP_REG::write(1); // 5_57_0 unlock clamp
        GBS::SP_SOG_MODE::write(1);
        GBS::SP_H_CST_ST::write(0x10);   // 5_4d
        GBS::SP_H_CST_SP::write(0x100);  // 5_4f
        GBS::SP_DIS_SUB_COAST::write(0); // 5_3e 5
        GBS::SP_H_PROTECT::write(1);     // SP_H_PROTECT on for detection
        GBS::SP_HCST_AUTO_EN::write(0);
        GBS::SP_NO_COAST_REG::write(0);
    }

    GBS::SP_HS_REG::write(1);          // 5_57 7
    GBS::SP_HS_PROC_INV_REG::write(0); // no SP sync inversion
    GBS::SP_VS_PROC_INV_REG::write(0); //

    writeOneByte(0x58, 0x05); //rgbhv: 0
    writeOneByte(0x59, 0x00); //rgbhv: 0
    writeOneByte(0x5a, 0x01); //rgbhv: 0 // was 0x05 but 480p ps2 doesnt like it
    writeOneByte(0x5b, 0x00); //rgbhv: 0
    writeOneByte(0x5c, 0x03); //rgbhv: 0
    writeOneByte(0x5d, 0x02); //rgbhv: 0 // range: 0 - 0x20 (how long should clamp stay off)
}

