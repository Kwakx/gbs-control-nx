#include "VideoTiming.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../gbs/GBSController.h"
#include "../gbs/GBSSync.h"
#include "../video/VideoInput.h"
#include "../gbs/tv5725.h"
#include "framesync.h"
#include <Arduino.h>

typedef TV5725<GBS_ADDR> GBS;

void setHSyncStartPosition(uint16_t value)
{
    if (rto->outModeHdBypass) {
        //GBS::HD_HS_ST::write(value);
        GBS::SP_CS_HS_ST::write(value);
    } else {
        GBS::VDS_HS_ST::write(value);
    }
}

void setHSyncStopPosition(uint16_t value)
{
    if (rto->outModeHdBypass) {
        //GBS::HD_HS_SP::write(value);
        GBS::SP_CS_HS_SP::write(value);
    } else {
        GBS::VDS_HS_SP::write(value);
    }
}

void setMemoryHblankStartPosition(uint16_t value)
{
    GBS::VDS_HB_ST::write(value);
    GBS::HD_HB_ST::write(value);
}

void setMemoryHblankStopPosition(uint16_t value)
{
    GBS::VDS_HB_SP::write(value);
    GBS::HD_HB_SP::write(value);
}

void setDisplayHblankStartPosition(uint16_t value)
{
    GBS::VDS_DIS_HB_ST::write(value);
}

void setDisplayHblankStopPosition(uint16_t value)
{
    GBS::VDS_DIS_HB_SP::write(value);
}

void setVSyncStartPosition(uint16_t value)
{
    GBS::VDS_VS_ST::write(value);
    GBS::HD_VS_ST::write(value);
}

void setVSyncStopPosition(uint16_t value)
{
    GBS::VDS_VS_SP::write(value);
    GBS::HD_VS_SP::write(value);
}

void setMemoryVblankStartPosition(uint16_t value)
{
    GBS::VDS_VB_ST::write(value);
    GBS::HD_VB_ST::write(value);
}

void setMemoryVblankStopPosition(uint16_t value)
{
    GBS::VDS_VB_SP::write(value);
    GBS::HD_VB_SP::write(value);
}

void setDisplayVblankStartPosition(uint16_t value)
{
    GBS::VDS_DIS_VB_ST::write(value);
}

void setDisplayVblankStopPosition(uint16_t value)
{
    GBS::VDS_DIS_VB_SP::write(value);
}

void setCsVsStart(uint16_t start)
{
    GBS::SP_SDCS_VSST_REG_H::write(start >> 8);
    GBS::SP_SDCS_VSST_REG_L::write(start & 0xff);
}

void setCsVsStop(uint16_t stop)
{
    GBS::SP_SDCS_VSSP_REG_H::write(stop >> 8);
    GBS::SP_SDCS_VSSP_REG_L::write(stop & 0xff);
}

uint16_t getCsVsStart()
{
    return (GBS::SP_SDCS_VSST_REG_H::read() << 8) + GBS::SP_SDCS_VSST_REG_L::read();
}

uint16_t getCsVsStop()
{
    return (GBS::SP_SDCS_VSSP_REG_H::read() << 8) + GBS::SP_SDCS_VSSP_REG_L::read();
}

void printVideoTimings()
{
    if (rto->presetID < 0x20) {
        SerialM.println("");
        SerialM.print(F("HT / scale   : "));
        SerialM.print(GBS::VDS_HSYNC_RST::read());
        SerialM.print(" ");
        SerialM.println(GBS::VDS_HSCALE::read());
        SerialM.print(F("HS ST/SP     : "));
        SerialM.print(GBS::VDS_HS_ST::read());
        SerialM.print(" ");
        SerialM.println(GBS::VDS_HS_SP::read());
        SerialM.print(F("HB ST/SP(d)  : "));
        SerialM.print(GBS::VDS_DIS_HB_ST::read());
        SerialM.print(" ");
        SerialM.println(GBS::VDS_DIS_HB_SP::read());
        SerialM.print(F("HB ST/SP     : "));
        SerialM.print(GBS::VDS_HB_ST::read());
        SerialM.print(" ");
        SerialM.println(GBS::VDS_HB_SP::read());
        SerialM.println(F("------"));
        // vertical
        SerialM.print(F("VT / scale   : "));
        SerialM.print(GBS::VDS_VSYNC_RST::read());
        SerialM.print(" ");
        SerialM.println(GBS::VDS_VSCALE::read());
        SerialM.print(F("VS ST/SP     : "));
        SerialM.print(GBS::VDS_VS_ST::read());
        SerialM.print(" ");
        SerialM.println(GBS::VDS_VS_SP::read());
        SerialM.print(F("VB ST/SP(d)  : "));
        SerialM.print(GBS::VDS_DIS_VB_ST::read());
        SerialM.print(" ");
        SerialM.println(GBS::VDS_DIS_VB_SP::read());
        SerialM.print(F("VB ST/SP     : "));
        SerialM.print(GBS::VDS_VB_ST::read());
        SerialM.print(" ");
        SerialM.println(GBS::VDS_VB_SP::read());
        // IF V offset
        SerialM.print(F("IF VB ST/SP  : "));
        SerialM.print(GBS::IF_VB_ST::read());
        SerialM.print(" ");
        SerialM.println(GBS::IF_VB_SP::read());
    } else {
        SerialM.println("");
        SerialM.print(F("HD_HSYNC_RST : "));
        SerialM.println(GBS::HD_HSYNC_RST::read());
        SerialM.print(F("HD_INI_ST    : "));
        SerialM.println(GBS::HD_INI_ST::read());
        SerialM.print(F("HS ST/SP     : "));
        SerialM.print(GBS::SP_CS_HS_ST::read());
        SerialM.print(" ");
        SerialM.println(GBS::SP_CS_HS_SP::read());
        SerialM.print(F("HB ST/SP     : "));
        SerialM.print(GBS::HD_HB_ST::read());
        SerialM.print(" ");
        SerialM.println(GBS::HD_HB_SP::read());
        SerialM.println(F("------"));
        // vertical
        SerialM.print(F("VS ST/SP     : "));
        SerialM.print(GBS::HD_VS_ST::read());
        SerialM.print(" ");
        SerialM.println(GBS::HD_VS_SP::read());
        SerialM.print(F("VB ST/SP     : "));
        SerialM.print(GBS::HD_VB_ST::read());
        SerialM.print(" ");
        SerialM.println(GBS::HD_VB_SP::read());
    }

    SerialM.print(F("CsVT         : "));
    SerialM.println(GBS::STATUS_SYNC_PROC_VTOTAL::read());
    SerialM.print(F("CsVS_ST/SP   : "));
    SerialM.print(getCsVsStart());
    SerialM.print(F(" "));
    SerialM.println(getCsVsStop());
}

void set_htotal(uint16_t htotal)
{
    // ModeLine "1280x960" 108.00 1280 1376 1488 1800 960 961 964 1000 +HSync +VSync
    // front porch: H2 - H1: 1376 - 1280
    // back porch : H4 - H3: 1800 - 1488
    // sync pulse : H3 - H2: 1488 - 1376

    uint16_t h_blank_display_start_position = htotal - 1;
    uint16_t h_blank_display_stop_position = htotal - ((htotal * 3) / 4);
    uint16_t center_blank = ((h_blank_display_stop_position / 2) * 3) / 4; // a bit to the left
    uint16_t h_sync_start_position = center_blank - (center_blank / 2);
    uint16_t h_sync_stop_position = center_blank + (center_blank / 2);
    uint16_t h_blank_memory_start_position = h_blank_display_start_position - 1;
    uint16_t h_blank_memory_stop_position = h_blank_display_stop_position - (h_blank_display_stop_position / 50);

    GBS::VDS_HSYNC_RST::write(htotal);
    GBS::VDS_HS_ST::write(h_sync_start_position);
    GBS::VDS_HS_SP::write(h_sync_stop_position);
    GBS::VDS_DIS_HB_ST::write(h_blank_display_start_position);
    GBS::VDS_DIS_HB_SP::write(h_blank_display_stop_position);
    GBS::VDS_HB_ST::write(h_blank_memory_start_position);
    GBS::VDS_HB_SP::write(h_blank_memory_stop_position);
}

void set_vtotal(uint16_t vtotal)
{
    uint16_t VDS_DIS_VB_ST = vtotal - 2;                         // just below vtotal
    uint16_t VDS_DIS_VB_SP = (vtotal >> 6) + 8;                  // positive, above new sync stop position
    uint16_t VDS_VB_ST = ((uint16_t)(vtotal * 0.016f)) & 0xfffe; // small fraction of vtotal
    uint16_t VDS_VB_SP = VDS_VB_ST + 2;                          // always VB_ST + 2
    uint16_t v_sync_start_position = 1;
    uint16_t v_sync_stop_position = 5;
    // most low line count formats have negative sync!
    // exception: 1024x768 (1344x806 total) has both sync neg. // also 1360x768 (1792x795 total)
    if ((vtotal < 530) || (vtotal >= 803 && vtotal <= 809) || (vtotal >= 793 && vtotal <= 798)) {
        uint16_t temp = v_sync_start_position;
        v_sync_start_position = v_sync_stop_position;
        v_sync_stop_position = temp;
    }

    GBS::VDS_VSYNC_RST::write(vtotal);
    GBS::VDS_VS_ST::write(v_sync_start_position);
    GBS::VDS_VS_SP::write(v_sync_stop_position);
    GBS::VDS_VB_ST::write(VDS_VB_ST);
    GBS::VDS_VB_SP::write(VDS_VB_SP);
    GBS::VDS_DIS_VB_ST::write(VDS_DIS_VB_ST);
    GBS::VDS_DIS_VB_SP::write(VDS_DIS_VB_SP);

    // VDS_VSYN_SIZE1 + VDS_VSYN_SIZE2 to VDS_VSYNC_RST + 2
    GBS::VDS_VSYN_SIZE1::write(GBS::VDS_VSYNC_RST::read() + 2);
    GBS::VDS_VSYN_SIZE2::write(GBS::VDS_VSYNC_RST::read() + 2);
}

void fastGetBestHtotal()
{
    uint32_t inStart, inStop;
    signed long inPeriod = 1;
    double inHz = 1.0;
    GBS::TEST_BUS_SEL::write(0xa);
    if (FrameSync::vsyncInputSample(&inStart, &inStop)) {
        inPeriod = (inStop - inStart) >> 1;
        if (inPeriod > 1) {
            inHz = (double)1000000 / (double)inPeriod;
        }
        SerialM.print("inPeriod: ");
        SerialM.println(inPeriod);
        SerialM.print("in hz: ");
        SerialM.println(inHz);
    } else {
        SerialM.println("error");
    }

    uint16_t newVtotal = GBS::VDS_VSYNC_RST::read();
    double bestHtotal = 108000000 / ((double)newVtotal * inHz); // 107840000
    double bestHtotal50 = 108000000 / ((double)newVtotal * 50);
    double bestHtotal60 = 108000000 / ((double)newVtotal * 60);
    SerialM.print("newVtotal: ");
    SerialM.println(newVtotal);
    // display clock probably not exact 108mhz
    SerialM.print("bestHtotal: ");
    SerialM.println(bestHtotal);
    SerialM.print("bestHtotal50: ");
    SerialM.println(bestHtotal50);
    SerialM.print("bestHtotal60: ");
    SerialM.println(bestHtotal60);
    if (bestHtotal > 800 && bestHtotal < 3200) {
        //applyBestHTotal((uint16_t)bestHtotal);
        //FrameSync::resetWithoutRecalculation(); // was single use of this function, function has changed since
    }
}

boolean runAutoBestHTotal()
{
    if (!FrameSync::ready() && rto->autoBestHtotalEnabled == true && rto->videoStandardInput > 0 && rto->videoStandardInput < 15) {

        //Serial.println("running");
        //unsigned long startTime = millis();

        boolean stableNow = 1;

        for (uint8_t i = 0; i < 64; i++) {
            if (!getStatus16SpHsStable()) {
                stableNow = 0;
                //Serial.println("prevented: !getStatus16SpHsStable");
                break;
            }
        }

        if (stableNow) {
            if (GBS::STATUS_INT_SOG_BAD::read()) {
                //Serial.println("prevented_2!");
                resetInterruptSogBadBit();
                delay(40);
                stableNow = false;
            }
            resetInterruptSogBadBit();

            if (stableNow && (getVideoMode() == rto->videoStandardInput)) {
                uint8_t testBusSelBackup = GBS::TEST_BUS_SEL::read();
                uint8_t vdsBusSelBackup = GBS::VDS_TEST_BUS_SEL::read();
                uint8_t ifBusSelBackup = GBS::IF_TEST_SEL::read();

                if (testBusSelBackup != 0)
                    GBS::TEST_BUS_SEL::write(0); // needs decimation + if
                if (vdsBusSelBackup != 0)
                    GBS::VDS_TEST_BUS_SEL::write(0); // VDS test # 0 = VBlank
                if (ifBusSelBackup != 3)
                    GBS::IF_TEST_SEL::write(3); // IF averaged frame time

                yield();
                uint16_t bestHTotal = FrameSync::init(); // critical task
                yield();

                GBS::TEST_BUS_SEL::write(testBusSelBackup); // always restore from backup (TB has changed)
                if (vdsBusSelBackup != 0)
                    GBS::VDS_TEST_BUS_SEL::write(vdsBusSelBackup);
                if (ifBusSelBackup != 3)
                    GBS::IF_TEST_SEL::write(ifBusSelBackup);

                if (GBS::STATUS_INT_SOG_BAD::read()) {
                    //Serial.println("prevented_5 INT_SOG_BAD!");
                    stableNow = false;
                }
                for (uint8_t i = 0; i < 16; i++) {
                    if (!getStatus16SpHsStable()) {
                        stableNow = 0;
                        //Serial.println("prevented_5: !getStatus16SpHsStable");
                        break;
                    }
                }
                resetInterruptSogBadBit();

                if (bestHTotal > 4095) {
                    if (!rto->forceRetime) {
                        stableNow = false;
                    } else {
                        // roll with it
                        bestHTotal = 4095;
                    }
                }

                if (stableNow) {
                    for (uint8_t i = 0; i < 24; i++) {
                        delay(1);
                        if (!getStatus16SpHsStable()) {
                            stableNow = false;
                            //Serial.println("prevented_3!");
                            break;
                        }
                    }
                }

                if (bestHTotal > 0 && stableNow) {
                    boolean success = applyBestHTotal(bestHTotal);
                    if (success) {
                        rto->syncLockFailIgnore = 16;
                        //Serial.print("ok, took: ");
                        //Serial.println(millis() - startTime);
                        return true; // success
                    }
                }
            }
        }

        // reaching here can happen even if stableNow == 1
        if (!stableNow) {
            FrameSync::reset(uopt->frameTimeLockMethod);

            if (rto->syncLockFailIgnore > 0) {
                rto->syncLockFailIgnore--;
                if (rto->syncLockFailIgnore == 0) {
                    GBS::DAC_RGBS_PWDNZ::write(1); // xth chance
                    if (!uopt->wantOutputComponent) {
                        GBS::PAD_SYNC_OUT_ENZ::write(0); // enable sync out // xth chance
                    }
                    rto->autoBestHtotalEnabled = false;
                }
            }
            Serial.print(F("bestHtotal retry ("));
            Serial.print(rto->syncLockFailIgnore);
            Serial.println(")");
        }
    } else if (FrameSync::ready()) {
        // FS ready but mode is 0 or 15 or autoBestHtotal is off
        return true;
    }

    if (rto->continousStableCounter != 0 && rto->continousStableCounter != 255) {
        rto->continousStableCounter++; // stop repetitions
    }

    return false;
}

boolean applyBestHTotal(uint16_t bestHTotal)
{
    if (rto->outModeHdBypass) {
        return true; // false? doesn't matter atm
    }

    uint16_t orig_htotal = GBS::VDS_HSYNC_RST::read();
    int diffHTotal = bestHTotal - orig_htotal;
    uint16_t diffHTotalUnsigned = abs(diffHTotal);

    if (((diffHTotalUnsigned == 0) || (rto->extClockGenDetected && diffHTotalUnsigned == 1)) && // all this
        !rto->forceRetime)                                                                      // and that
    {
        if (!uopt->enableFrameTimeLock) { // FTL can double throw this when it resets to adjust
            SerialM.print(F("HTotal Adjust (skipped)"));

            if (!rto->extClockGenDetected) {
                float sfr = getSourceFieldRate(0);
                yield(); // wifi
                float ofr = getOutputFrameRate();
                if (sfr < 1.0f) {
                    sfr = getSourceFieldRate(0); // retry
                }
                if (ofr < 1.0f) {
                    ofr = getOutputFrameRate(); // retry
                }
                SerialM.print(F(", source Hz: "));
                SerialM.print(sfr, 3); // prec. 3
                SerialM.print(F(", output Hz: "));
                SerialM.println(ofr, 3); // prec. 3
            } else {
                SerialM.println();
            }
        }
        return true; // nothing to do
    }

    if (GBS::GBS_OPTION_PALFORCED60_ENABLED::read() == 1) {
        // source is 50Hz, preset has to stay at 60Hz: return
        return true;
    }

    boolean isLargeDiff = (diffHTotalUnsigned > (orig_htotal * 0.06f)) ? true : false; // typical diff: 1802 to 1794 (=8)

    if (isLargeDiff && (getVideoMode() == 8 || rto->videoStandardInput == 14)) {
        // arcade stuff syncs down from 60 to 52 Hz..
        isLargeDiff = (diffHTotalUnsigned > (orig_htotal * 0.16f)) ? true : false;
    }

    if (isLargeDiff) {
        SerialM.println(F("ABHT: large diff"));
    }

    // rto->forceRetime = true means the correction should be forced (command '.')
    if (isLargeDiff && (rto->forceRetime == false)) {
        if (rto->videoStandardInput != 14) {
            rto->failRetryAttempts++;
            if (rto->failRetryAttempts < 8) {
                SerialM.println(F("retry"));
                FrameSync::reset(uopt->frameTimeLockMethod);
                delay(60);
            } else {
                SerialM.println(F("give up"));
                rto->autoBestHtotalEnabled = false;
            }
        }
        return false; // large diff, no forced
    }

    // bestHTotal 0? could be an invald manual retime
    if (bestHTotal == 0) {
        Serial.println(F("bestHTotal 0"));
        return false;
    }

    if (rto->forceRetime == false) {
        if (GBS::STATUS_INT_SOG_BAD::read() == 1) {
            //Serial.println("prevented in apply");
            return false;
        }
    }

    rto->failRetryAttempts = 0; // else all okay!, reset to 0

    // move blanking (display)
    uint16_t h_blank_display_start_position = GBS::VDS_DIS_HB_ST::read();
    uint16_t h_blank_display_stop_position = GBS::VDS_DIS_HB_SP::read();
    uint16_t h_blank_memory_start_position = GBS::VDS_HB_ST::read();
    uint16_t h_blank_memory_stop_position = GBS::VDS_HB_SP::read();

    // h_blank_memory_start_position usually is == h_blank_display_start_position
    if (h_blank_memory_start_position == h_blank_display_start_position) {
        h_blank_display_start_position += (diffHTotal / 2);
        h_blank_display_stop_position += (diffHTotal / 2);
        h_blank_memory_start_position = h_blank_display_start_position; // normal case
        h_blank_memory_stop_position += (diffHTotal / 2);
    } else {
        h_blank_display_start_position += (diffHTotal / 2);
        h_blank_display_stop_position += (diffHTotal / 2);
        h_blank_memory_start_position += (diffHTotal / 2); // the exception (currently 1280x1024)
        h_blank_memory_stop_position += (diffHTotal / 2);
    }

    if (diffHTotal < 0) {
        h_blank_display_start_position &= 0xfffe;
        h_blank_display_stop_position &= 0xfffe;
        h_blank_memory_start_position &= 0xfffe;
        h_blank_memory_stop_position &= 0xfffe;
    } else if (diffHTotal > 0) {
        h_blank_display_start_position += 1;
        h_blank_display_start_position &= 0xfffe;
        h_blank_display_stop_position += 1;
        h_blank_display_stop_position &= 0xfffe;
        h_blank_memory_start_position += 1;
        h_blank_memory_start_position &= 0xfffe;
        h_blank_memory_stop_position += 1;
        h_blank_memory_stop_position &= 0xfffe;
    }

    // don't move HSync with small diffs
    uint16_t h_sync_start_position = GBS::VDS_HS_ST::read();
    uint16_t h_sync_stop_position = GBS::VDS_HS_SP::read();

    // fix over / underflows
    if (h_blank_display_start_position > (bestHTotal - 8) || isLargeDiff) {
        // typically happens when scaling Hz up (60 to 70)
        //Serial.println("overflow h_blank_display_start_position");
        h_blank_display_start_position = bestHTotal * 0.936f;
    }
    if (h_blank_display_stop_position > bestHTotal || isLargeDiff) {
        //Serial.println("overflow h_blank_display_stop_position");
        h_blank_display_stop_position = bestHTotal * 0.178f;
    }
    if ((h_blank_memory_start_position > bestHTotal) || (h_blank_memory_start_position > h_blank_display_start_position) || isLargeDiff) {
        //Serial.println("overflow h_blank_memory_start_position");
        h_blank_memory_start_position = h_blank_display_start_position * 0.971f;
    }
    if (h_blank_memory_stop_position > bestHTotal || isLargeDiff) {
        //Serial.println("overflow h_blank_memory_stop_position");
        h_blank_memory_stop_position = h_blank_display_stop_position * 0.64f;
    }

    // check whether HS spills over HBSPD
    if (h_sync_start_position > h_sync_stop_position && (h_sync_start_position < (bestHTotal / 2))) { // is neg HSync
        if (h_sync_start_position >= h_blank_display_stop_position) {
            h_sync_start_position = h_blank_display_stop_position * 0.8f;
            h_sync_stop_position = 4; // good idea to move this close to 0 as well
        }
    } else {
        if (h_sync_stop_position >= h_blank_display_stop_position) {
            h_sync_stop_position = h_blank_display_stop_position * 0.8f;
            h_sync_start_position = 4; //
        }
    }

    // just fix HS
    if (isLargeDiff) {
        if (h_sync_start_position > h_sync_stop_position && (h_sync_start_position < (bestHTotal / 2))) { // is neg HSync
            h_sync_stop_position = 4;
            // stop = at least start, then a bit outwards
            h_sync_start_position = 16 + (h_blank_display_stop_position * 0.3f);
        } else {
            h_sync_start_position = 4;
            h_sync_stop_position = 16 + (h_blank_display_stop_position * 0.3f);
        }
    }

    if (diffHTotal != 0) { // apply
        // delay the change to field start, a bit more compatible
        uint16_t timeout = 0;
        while ((GBS::STATUS_VDS_FIELD::read() == 1) && (++timeout < 400))
            ;
        while ((GBS::STATUS_VDS_FIELD::read() == 0) && (++timeout < 800))
            ;

        GBS::VDS_HSYNC_RST::write(bestHTotal);
        GBS::VDS_DIS_HB_ST::write(h_blank_display_start_position);
        GBS::VDS_DIS_HB_SP::write(h_blank_display_stop_position);
        GBS::VDS_HB_ST::write(h_blank_memory_start_position);
        GBS::VDS_HB_SP::write(h_blank_memory_stop_position);
        GBS::VDS_HS_ST::write(h_sync_start_position);
        GBS::VDS_HS_SP::write(h_sync_stop_position);
    }

    boolean print = 1;
    if (uopt->enableFrameTimeLock) {
        if ((GBS::GBS_RUNTIME_FTL_ADJUSTED::read() == 1) && !rto->forceRetime) {
            // FTL enabled and regular update, so don't print
            print = 0;
        }
        GBS::GBS_RUNTIME_FTL_ADJUSTED::write(0);
    }

    rto->forceRetime = false;

    if (print) {
        SerialM.print(F("HTotal Adjust: "));
        if (diffHTotal >= 0) {
            SerialM.print(" "); // formatting to align with negative value readouts
        }
        SerialM.print(diffHTotal);

        if (!rto->extClockGenDetected) {
            float sfr = getSourceFieldRate(0);
            delay(0);
            float ofr = getOutputFrameRate();
            if (sfr < 1.0f) {
                sfr = getSourceFieldRate(0); // retry
            }
            if (ofr < 1.0f) {
                ofr = getOutputFrameRate(); // retry
            }
            SerialM.print(F(", source Hz: "));
            SerialM.print(sfr, 3); // prec. 3
            SerialM.print(F(", output Hz: "));
            SerialM.println(ofr, 3); // prec. 3
        } else {
            SerialM.println();
        }
    }

    return true;
}

