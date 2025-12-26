#include "Si5351Manager.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../gbs/GBSController.h"
#include "../utils/DebugHelpers.h"
#include "../gbs/tv5725.h"
#include "../video/framesync.h"
#include "../../lib/si5351mcu/si5351mcu.h"

typedef TV5725<GBS_ADDR> GBS;

void externalClockGenResetClock()
{
    if (!rto->extClockGenDetected) {
        return;
    }
    fsDebugPrintf("externalClockGenResetClock()\n");

    auto readSi5351Reg = [](uint8_t reg) -> int {
        Wire.beginTransmission(SIADDR);
        Wire.write(reg);
        if (Wire.endTransmission() != 0) return -1;
        size_t n = Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (n != 1) return -2;
        return Wire.read();
    };
    auto writeSi5351Reg = [](uint8_t reg, uint8_t val) -> bool {
        Wire.beginTransmission(SIADDR);
        Wire.write(reg);
        Wire.write(val);
        return Wire.endTransmission() == 0;
    };
    auto waitSi5351Lock = [&](const char *where) -> bool {
        uint32_t t0 = millis();
        uint32_t lastLog = 0;
        int st = -999;
        // Wait longer on ESP32 after reprogramming PLL/MS for 108MHz.
        while (millis() - t0 < 250) {
            st = readSi5351Reg(0x00);     // device status

            uint32_t elapsed = millis() - t0;
            if (elapsed == 0 || elapsed - lastLog >= 25) {
                lastLog = elapsed;

            }

            // We only use CLK0 (PLLA) in this firmware; some modules report LOLB even if PLLB is unused.
            // Treat LOLA as the lock indicator; LOS may remain asserted on some modules.
            if (st >= 0 && (st & 0x20) == 0 && (st & 0x80) == 0) {
                return true;
            }
            delay(1);
        }
        return false;
    };

    uint8_t activeDisplayClock = GBS::PLL648_CONTROL_01::read();

    if (activeDisplayClock == 0x25)
        rto->freqExtClockGen = 40500000;
    else if (activeDisplayClock == 0x45)
        rto->freqExtClockGen = 54000000;
    else if (activeDisplayClock == 0x55)
        rto->freqExtClockGen = 64800000;
    else if (activeDisplayClock == 0x65)
        rto->freqExtClockGen = 81000000;
    else if (activeDisplayClock == 0x85)
        rto->freqExtClockGen = 108000000;
    else if (activeDisplayClock == 0x95)
        rto->freqExtClockGen = 129600000;
    else if (activeDisplayClock == 0xa5)
        rto->freqExtClockGen = 162000000;
    else if (activeDisplayClock == 0x35)
        rto->freqExtClockGen = 81000000; // clock unused
    else if (activeDisplayClock == 0)
        rto->freqExtClockGen = 81000000; // no preset loaded
    else if (!rto->outModeHdBypass) {
        SerialM.print(F("preset display clock: 0x"));
        SerialM.println(activeDisplayClock, HEX);
    }

    // NOTE: Historically there was a "pre-step" workaround for 108MHz/40.5MHz.
    // On ESP32 this can interact badly with Si5351 lock; we'll skip the pre-step
    // and rely on direct programming
#ifndef ESP32
    // problem: around 108MHz the library seems to double the clock
    if (rto->freqExtClockGen == 108000000) {
        Si.setFreq(0, 87000000);
        delay(1); // quick fix
    }
    // same thing it seems at 40500000
    if (rto->freqExtClockGen == 40500000) {
        Si.setFreq(0, 48500000);
        delay(1); // quick fix
    }
#endif
    // Re-apply best XTAL_CL discovered during init before programming new freq.
    writeSi5351Reg(183, g_si5351_best_xtal_cl);
    (void)readSi5351Reg(183);

    // Keep CKIN disabled while (re)programming the Si5351 to avoid loading the output
    // during the critical lock window. We'll only enable CKIN after we confirm lock.
    GBS::PAD_CKIN_ENZ::write(1); // 1 = clock input disable (pin40)

    Si.setFreq(0, rto->freqExtClockGen);
    // Mirror init behavior: enable output and clear interrupt flags before waiting for lock.
    Si.enable(0);
    writeSi5351Reg(0x01, 0xFF); // clear sticky interrupt status
    (void)readSi5351Reg(0x00);
    (void)readSi5351Reg(0x01);
    FrameSync::clearFrequency();

    SerialM.print(F("clock gen reset: "));
    SerialM.println(rto->freqExtClockGen);

    // If Si5351 isn't locked yet, it will continue to try and lock in the background.
    // We keep CKIN enabled (0) because the user wants to stay on the external clock regardless.
    bool locked = waitSi5351Lock("externalClockGenResetClock");
    if (!locked) {
        // Try a PLL reset once, then wait again.
        Si5351mcu::reset();
        locked = waitSi5351Lock("externalClockGenResetClock_after_reset");
    }
    
    GBS::PAD_CKIN_ENZ::write(0); // 0 = clock input enable (pin40)
    
    if (locked) {
        (void)readSi5351Reg(0x00);
    }
    (void)writeSi5351Reg(0x01, 0xFF);
    (void)readSi5351Reg(0x00);
}

void externalClockGenSyncInOutRate()
{
    fsDebugPrintf("externalClockGenSyncInOutRate()\n");

    if (!rto->extClockGenDetected) {
        return;
    }
    if (GBS::PAD_CKIN_ENZ::read() != 0) {
        return;
    }
    if (rto->outModeHdBypass) {
        return;
    }
    if (GBS::PLL648_CONTROL_01::read() != 0x75) {
        return;
    }

    float sfr = getSourceFieldRate(0);

    if (sfr < 47.0f || sfr > 86.0f) {
        SerialM.print(F("sync skipped sfr wrong: "));
        SerialM.println(sfr);
        return;
    }

    float ofr = getOutputFrameRate();

    if (ofr < 47.0f || ofr > 86.0f) {
        SerialM.print(F("sync skipped ofr wrong: "));
        SerialM.println(ofr);
        return;
    }

    // ESP32 can exhibit timing jitter in the measurement path; instead of skipping
    // (which stalls convergence), clamp the correction per step to avoid large jumps.
    float ratio = (ofr > 0.0f) ? (sfr / ofr) : 1.0f;
    constexpr float MAX_RATIO_STEP = 0.0006f; // match FrameSyncManager::runFrequency clamp
    if (ratio > 1.0f + MAX_RATIO_STEP) ratio = 1.0f + MAX_RATIO_STEP;
    if (ratio < 1.0f - MAX_RATIO_STEP) ratio = 1.0f - MAX_RATIO_STEP;

    uint32_t old = rto->freqExtClockGen;
    FrameSync::initFrequency(ofr, old);

    uint32_t newClockFreq = ratio * rto->freqExtClockGen;
    // Program Si5351 safely: disconnect GBS clock input during programming/lock window.
    // Avoid using setExternalClockGenFrequencySmooth() here because it iterates many Si.setFreq()
    // calls without any CKIN gating, which can glitch the active video clock.

    // keep CKIN disabled while reprogramming
    GBS::PAD_CKIN_ENZ::write(1);
    // re-apply the best XTAL_CL we discovered earlier for consistency
    Wire.beginTransmission(SIADDR);
    Wire.write(183);
    Wire.write(g_si5351_best_xtal_cl);
    Wire.endTransmission();
    // program the new frequency in one shot (small ratio steps already clamp the delta)
    Si.setFreq(0, newClockFreq);
    Si.enable(0);
    Wire.beginTransmission(SIADDR);
    Wire.write(0x01);
    Wire.write(0xFF);
    Wire.endTransmission();
    // Wait until LOLA/LOLB clear (ignore LOS), then re-enable CKIN.
    // Reuse the same lock criteria as externalClockGenResetClock.
    uint32_t t0 = millis();
    int st = -999;
    while (millis() - t0 < 80) {
        Wire.beginTransmission(SIADDR);
        Wire.write(0x00);
        if (Wire.endTransmission() == 0) {
            int n = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
            if (n == 1) st = Wire.read();
        }
        if (st >= 0 && (st & 0x20) == 0 && (st & 0x80) == 0) break;
        delay(1);
    }
    bool ok = (st >= 0 && (st & 0x20) == 0 && (st & 0x80) == 0);
    if (!ok) {
        static uint32_t lastSyncFailLog = 0;
        if (millis() - lastSyncFailLog > 1000) {
            SerialM.printf("Si5351 sync-tune lock failed (st:0x%02X). Rolling back to %u Hz.\n", st, old);
            lastSyncFailLog = millis();
        }
        Si.setFreq(0, old);
        Si.enable(0);
        Wire.beginTransmission(SIADDR);
        Wire.write(0x01);
        Wire.write(0xFF);
        Wire.endTransmission();
        rto->freqExtClockGen = old;
    } else {
        rto->freqExtClockGen = newClockFreq;
    }
    // Reconnect GBS to the (restored/new) external clock.
    GBS::PAD_CKIN_ENZ::write(0);


    int32_t diff = rto->freqExtClockGen - old;

    SerialM.print(F("source Hz: "));
    SerialM.print(sfr, 5);
    SerialM.print(F(" new out: "));
    SerialM.print(getOutputFrameRate(), 5);
    SerialM.print(F(" clock: "));
    SerialM.print(rto->freqExtClockGen);
    SerialM.print(F(" ("));
    SerialM.print(diff >= 0 ? "+" : "");
    SerialM.print(diff);
    SerialM.println(F(")"));
    // (no extra delay here; video clock is already back on after CKIN enable)
}

void externalClockGenDetectAndInitialize()
{

    // XTAL load capacitance. Some Si5351 boards are sensitive and may fail to oscillate (LOS=1)
    // depending on this setting and supply/noise. We'll probe a few options at runtime.
    // 10pF (0xD2), 8pF (0x92), 6pF (0x52)
    const uint8_t xtal_cl_default = 0xD2; // NOTE: Per AN619, the low bytes should be written 0b010010

    // MHz: 27, 32.4, 40.5, 54, 64.8, 81, 108, 129.6, 162
    rto->freqExtClockGen = 81000000;
    rto->extClockGenDetected = 0;

    // Snapshot Si5351 status before detection/init.
    int st_pre = -999, st_pre2 = -999;
    int et0 = -999; // endTransmission return
    int n0 = -999;  // bytes read
    Wire.beginTransmission(SIADDR);
    Wire.write(0x00);
    et0 = Wire.endTransmission();
    if (et0 == 0) {
        n0 = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (n0 == 1) st_pre = Wire.read();
    }
    // read twice to detect flaky bus
    Wire.beginTransmission(SIADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() == 0) {
        int n1 = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (n1 == 1) st_pre2 = Wire.read();
    }
    (void)st_pre;
    (void)st_pre2;
    (void)et0;
    (void)n0;

    if (uopt->disableExternalClockGenerator) {
        SerialM.println(F("ExternalClockGenerator disabled, skipping detection"));
        return;
    }

    uint8_t retVal = 0;
    Wire.beginTransmission(SIADDR);
    retVal = Wire.endTransmission();

    if (retVal != 0) {
        return;
    }

    Wire.beginTransmission(SIADDR);
    Wire.write(0); // Device Status
    Wire.endTransmission();
    size_t bytes_read = Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);

    if (bytes_read == 1) {
        retVal = Wire.read();
        if ((retVal & 0x80) == 0) {
            // SYS_INIT indicates device is ready.
            rto->extClockGenDetected = 1;
        } else {
            return;
        }
    } else {
        return;
    }

    auto readSt = []() -> int {
        Wire.beginTransmission(SIADDR);
        Wire.write(0x00);
        if (Wire.endTransmission() != 0) return -1;
        int n = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (n != 1) return -2;
        return Wire.read();
    };
    auto clearInt = []() -> bool {
        Wire.beginTransmission(SIADDR);
        Wire.write(0x01); // interrupt status (sticky)
        Wire.write(0xFF); // clear all
        return Wire.endTransmission() == 0;
    };
    auto isLockedSt = [](int st) -> bool {
        // We only use CLK0 (PLLA) in this firmware; ignore LOLB (PLLB) for lock.
        // st bit5=LOLA, bit6=LOLB, bit4=LOS, bit7=SYS_INIT
        return (st >= 0) && ((st & 0x20) == 0) && ((st & 0x80) == 0);
    };
    auto waitLock = [&](uint32_t timeoutMs) -> int {
        uint32_t t0 = millis();
        int st = -999;
        while (millis() - t0 < timeoutMs) {
            st = readSt();
            if (isLockedSt(st)) return st;
            delay(1);
        }
        return st;
    };
    auto readRegs = [](uint8_t startReg, uint8_t count, uint8_t *out) -> bool {
        Wire.beginTransmission(SIADDR);
        Wire.write(startReg);
        if (Wire.endTransmission() != 0) return false;
        int n = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)count, false);
        if (n != (int)count) return false;
        for (uint8_t i = 0; i < count; i++) out[i] = (uint8_t)Wire.read();
        return true;
    };

    // IMPORTANT (runtime-proven): Using the wrong XTAL here scales the output frequency.
    // Default to 25MHz, as most Si5351 boards use it.
    uint32_t xtal = 25000000UL;

    // Probe XTAL_CL settings to see if we can clear LOS/LOLA on ESP32.
    uint8_t xtal_cls[3] = {xtal_cl_default, 0x92, 0x52};
    int st_after = -999;
    // Keep the best (lowest) status we observed so later clock resets reuse it.
    uint8_t best_xtal_cl = xtal_cl_default;
    int best_score = 0x7fffffff;
    auto scoreSt = [](int st) -> int {
        // Lower is better. We care mainly about lock-related flags.
        // bit4=LOS, bit5=LOLA, bit6=LOLB, bit7=SYS_INIT
        if (st < 0) return 1000;
        int score = 0;
        if (st & 0x80) score += 100; // SYS_INIT
        if (st & 0x10) score += 10;  // LOS
        if (st & 0x20) score += 5;   // LOLA
        if (st & 0x40) score += 5;   // LOLB
        return score;
    };
    for (uint8_t t = 0; t < 3; t++) {
        uint8_t xtal_cl = xtal_cls[t];
        Si.init(xtal);
        Wire.beginTransmission(SIADDR);
        Wire.write(183); // XTAL_CL
        Wire.write(xtal_cl);
        Wire.endTransmission();
        Si.setPower(0, SIOUT_6mA);
        Si.setFreq(0, rto->freqExtClockGen);
        Si.enable(0);
        bool clrOk = clearInt();
        delay(2);
        int st0 = readSt();
        int stw = waitLock(200);
        st_after = readSt();

        // Read back key PLL/MS registers to detect partial programming.
        uint8_t pllA[8] = {0}, ms0[8] = {0};
        bool pllOk = readRegs(26, 8, pllA); // PLLA params
        bool msOk = readRegs(42, 8, ms0);   // MS0 params (regs 42..49)
        // Also read OE/CLK0 control for sanity.
        int oe = -999, clk0 = -999;
        Wire.beginTransmission(SIADDR);
        Wire.write(0x03);
        if (Wire.endTransmission() == 0) {
            int n = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
            if (n == 1) oe = Wire.read();
        }
        Wire.beginTransmission(SIADDR);
        Wire.write(0x10);
        if (Wire.endTransmission() == 0) {
            int n = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
            if (n == 1) clk0 = Wire.read();
        }

        Si.disable(0);
        (void)clrOk;
        (void)st0;
        (void)stw;
        (void)st_after;
        (void)pllOk;
        (void)msOk;
        (void)pllA;
        (void)ms0;
        (void)oe;
        (void)clk0;

        int sc = scoreSt(st_after);
        if (sc < best_score) {
            best_score = sc;
            best_xtal_cl = xtal_cl;
        }
        if (isLockedSt(st_after)) {
            break;
        }
    }
    // Persist best observed XTAL_CL for later use in externalClockGenResetClock().
    g_si5351_best_xtal_cl = best_xtal_cl;
    (void)best_score;

    int st_post = -999, st_post2 = -999;
    int etp0 = -999, np0 = -999;
    Wire.beginTransmission(SIADDR);
    Wire.write(0x00);
    etp0 = Wire.endTransmission();
    if (etp0 == 0) {
        np0 = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (np0 == 1) st_post = Wire.read();
    }
    Wire.beginTransmission(SIADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() == 0) {
        int np1 = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
        if (np1 == 1) st_post2 = Wire.read();
    }
    (void)st_post;
    (void)st_post2;
    (void)etp0;
    (void)np0;
}

// used for RGBHV to determine the ADPLL speed "level" / can jitter with SOG Sync
uint32_t getPllRate()
{
    typedef TV5725<GBS_ADDR> GBS;
#ifdef ESP32
    uint32_t tickRateHz = 1000000;
#else
    uint32_t tickRateHz = ESP.getCpuFreqMHz() * 1000000;
#endif
    uint8_t testBusSelBackup = GBS::TEST_BUS_SEL::read();
    uint8_t spBusSelBackup = GBS::TEST_BUS_SP_SEL::read();
    uint8_t debugPinBackup = GBS::PAD_BOUT_EN::read();

    if (testBusSelBackup != 0xa) {
        GBS::TEST_BUS_SEL::write(0xa);
    }
    if (rto->syncTypeCsync) {
        if (spBusSelBackup != 0x6b)
            GBS::TEST_BUS_SP_SEL::write(0x6b);
    } else {
        if (spBusSelBackup != 0x09)
            GBS::TEST_BUS_SP_SEL::write(0x09);
    }
    GBS::PAD_BOUT_EN::write(1); // enable output to pin for test
    yield();                    // BOUT signal and wifi
    delayMicroseconds(200);
    uint32_t ticks = FrameSync::getPulseTicks();

    // restore
    GBS::PAD_BOUT_EN::write(debugPinBackup);
    if (testBusSelBackup != 0xa) {
        GBS::TEST_BUS_SEL::write(testBusSelBackup);
    }
    GBS::TEST_BUS_SP_SEL::write(spBusSelBackup);

    uint32_t retVal = 0;
    if (ticks > 0) {
        retVal = tickRateHz / ticks;
    }

    return retVal;
}
