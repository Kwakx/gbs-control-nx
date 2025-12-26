#include "DebugHelpers.h"
#include "../gbs/GBSRegister.h"
#include "../video/VideoInput.h"
#include "MovingAverage.h"
#include "../core/Globals.h"
#include "../gbs/tv5725.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

void resetDebugPort()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::PAD_BOUT_EN::write(1); // output to pad enabled
    GBS::IF_TEST_EN::write(1);
    GBS::IF_TEST_SEL::write(3);    // IF vertical period signal
    GBS::TEST_BUS_SEL::write(0xa); // test bus to SP
    GBS::TEST_BUS_EN::write(1);
    GBS::TEST_BUS_SP_SEL::write(0x0f); // SP test signal select (vsync in, after SOG separation)
    GBS::MEM_FF_TOP_FF_SEL::write(1);  // g0g13/14 shows FIFO stats (capture, rff, wff, etc)
    // SP test bus enable bit is included in TEST_BUS_SP_SEL
    GBS::VDS_TEST_EN::write(1); // VDS test enable
}

void printInfo()
{
    typedef TV5725<GBS_ADDR> GBS;
    static char print[121]; // Increase if compiler complains about sprintf
    static uint8_t clearIrqCounter = 0;
    static uint8_t lockCounterPrevious = 0;
    uint8_t lockCounter = 0;

    int32_t wifi = 0;
    if ((WiFi.status() == WL_CONNECTED) || (WiFi.getMode() == WIFI_AP)) {
        wifi = WiFi.RSSI();
    }

    uint16_t hperiod = GBS::HPERIOD_IF::read();
    uint16_t vperiod = GBS::VPERIOD_IF::read();
    uint8_t stat0FIrq = GBS::STATUS_0F::read();
    char HSp = GBS::STATUS_SYNC_PROC_HSPOL::read() ? '+' : '-'; // 0 = neg, 1 = pos
    char VSp = GBS::STATUS_SYNC_PROC_VSPOL::read() ? '+' : '-'; // 0 = neg, 1 = pos
    char h = 'H', v = 'V';
    if (!GBS::STATUS_SYNC_PROC_HSACT::read()) {
        h = HSp = ' ';
    }
    if (!GBS::STATUS_SYNC_PROC_VSACT::read()) {
        v = VSp = ' ';
    }

    sprintf(print, "h:%4u v:%4u PLL:%01u A:%02x%02x%02x S:%02x.%02x.%02x %c%c%c%c I:%02x D:%04x m:%hu ht:%4d vt:%4d hpw:%4d u:%3x s:%2x S:%2d W:%2d\n",
            hperiod, vperiod, lockCounterPrevious,
            GBS::ADC_RGCTRL::read(), GBS::ADC_GGCTRL::read(), GBS::ADC_BGCTRL::read(),
            GBS::STATUS_00::read(), GBS::STATUS_05::read(), GBS::SP_CS_0x3E::read(),
            h, HSp, v, VSp, stat0FIrq, GBS::TEST_BUS::read(), getVideoMode(),
            GBS::STATUS_SYNC_PROC_HTOTAL::read(), GBS::STATUS_SYNC_PROC_VTOTAL::read() /*+ 1*/, // emucrt: without +1 is correct line count
            GBS::STATUS_SYNC_PROC_HLOW_LEN::read(), rto->noSyncCounter, rto->continousStableCounter,
            rto->currentLevelSOG, wifi);

    SerialM.print(print);

    if (stat0FIrq != 0x00) {
        // clear 0_0F interrupt bits regardless of syncwatcher status
        clearIrqCounter++;
        if (clearIrqCounter >= 50) {
            clearIrqCounter = 0;
            GBS::INTERRUPT_CONTROL_00::write(0xff); // reset irq status
            GBS::INTERRUPT_CONTROL_00::write(0x00);
        }
    }

    yield();
    if (GBS::STATUS_SYNC_PROC_HSACT::read()) { // else source might not be active
        for (uint8_t i = 0; i < 9; i++) {
            if (GBS::STATUS_MISC_PLLAD_LOCK::read() == 1) {
                lockCounter++;
            } else {
                for (int i = 0; i < 10; i++) {
                    if (GBS::STATUS_MISC_PLLAD_LOCK::read() == 1) {
                        lockCounter++;
                        break;
                    }
                }
            }
        }
    }
    lockCounterPrevious = getMovingAverage(lockCounter);
}

