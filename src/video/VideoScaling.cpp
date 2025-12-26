#include "VideoScaling.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "VideoShifting.h"
#include "../gbs/tv5725.h"

void scaleHorizontal(uint16_t amountToScale, bool subtracting)
{
    typedef TV5725<GBS_ADDR> GBS;
    uint16_t hscale = GBS::VDS_HSCALE::read();

    // smooth out areas of interest
    if (subtracting && (hscale == 513 || hscale == 512))
        amountToScale = 1;
    if (!subtracting && (hscale == 511 || hscale == 512))
        amountToScale = 1;

    if (subtracting && (((int)hscale - amountToScale) <= 256)) {
        hscale = 256;
        GBS::VDS_HSCALE::write(hscale);
        SerialM.println("limit");
        return;
    }

    if (subtracting && (hscale - amountToScale > 255)) {
        hscale -= amountToScale;
    } else if (hscale + amountToScale < 1023) {
        hscale += amountToScale;
    } else if (hscale + amountToScale == 1023) { // exact max > bypass but apply VDS fetch changes
        hscale = 1023;
        GBS::VDS_HSCALE::write(hscale);
        GBS::VDS_HSCALE_BYPS::write(1);
    } else if (hscale + amountToScale > 1023) { // max + overshoot > bypass and no VDS fetch adjust
        hscale = 1023;
        GBS::VDS_HSCALE::write(hscale);
        GBS::VDS_HSCALE_BYPS::write(1);
        SerialM.println("limit");
        return;
    }

    // will be scaling
    GBS::VDS_HSCALE_BYPS::write(0);

    // move within VDS VB fetch area (within reason)
    uint16_t htotal = GBS::VDS_HSYNC_RST::read();
    uint16_t toShift = 0;
    if (hscale < 540)
        toShift = 4;
    else if (hscale < 640)
        toShift = 3;
    else
        toShift = 2;

    if (subtracting) {
        shiftHorizontal(toShift, true);
        if ((GBS::VDS_HB_ST::read() + 5) < GBS::VDS_DIS_HB_ST::read()) {
            GBS::VDS_HB_ST::write(GBS::VDS_HB_ST::read() + 5);
        } else if ((GBS::VDS_DIS_HB_ST::read() + 5) < htotal) {
            GBS::VDS_DIS_HB_ST::write(GBS::VDS_DIS_HB_ST::read() + 5);
            GBS::VDS_HB_ST::write(GBS::VDS_DIS_HB_ST::read()); // dis_hbst = hbst
        }

        // fix HB_ST > HB_SP conditions
        if (GBS::VDS_HB_SP::read() < (GBS::VDS_HB_ST::read() + 16)) { // HB_SP < HB_ST
            if ((GBS::VDS_HB_SP::read()) > (htotal / 2)) {            // but HB_SP > some small value
                GBS::VDS_HB_ST::write(GBS::VDS_HB_SP::read() - 16);
            }
        }
    }

    // !subtracting check just for readability
    if (!subtracting) {
        shiftHorizontal(toShift, false);
        if ((GBS::VDS_HB_ST::read() - 5) > 0) {
            GBS::VDS_HB_ST::write(GBS::VDS_HB_ST::read() - 5);
        }
    }

    // fix scaling < 512 glitch: factor even, htotal even: hbst / hbsp should be even, etc
    if (hscale < 512) {
        if (hscale % 2 == 0) { // hscale 512 / even
            if (GBS::VDS_HB_ST::read() % 2 == 1) {
                GBS::VDS_HB_ST::write(GBS::VDS_HB_ST::read() + 1);
            }
            if (htotal % 2 == 1) {
                if (GBS::VDS_HB_SP::read() % 2 == 0) {
                    GBS::VDS_HB_SP::write(GBS::VDS_HB_SP::read() - 1);
                }
            } else {
                if (GBS::VDS_HB_SP::read() % 2 == 1) {
                    GBS::VDS_HB_SP::write(GBS::VDS_HB_SP::read() - 1);
                }
            }
        } else { // hscale 499 / uneven
            if (GBS::VDS_HB_ST::read() % 2 == 1) {
                GBS::VDS_HB_ST::write(GBS::VDS_HB_ST::read() + 1);
            }
            if (htotal % 2 == 0) {
                if (GBS::VDS_HB_SP::read() % 2 == 1) {
                    GBS::VDS_HB_SP::write(GBS::VDS_HB_SP::read() - 1);
                }
            } else {
                if (GBS::VDS_HB_SP::read() % 2 == 0) {
                    GBS::VDS_HB_SP::write(GBS::VDS_HB_SP::read() - 1);
                }
            }
        }
        // if scaling was < 512 and HB_ST moved, align with VDS_DIS_HB_ST
        if (GBS::VDS_DIS_HB_ST::read() < GBS::VDS_HB_ST::read()) {
            GBS::VDS_DIS_HB_ST::write(GBS::VDS_HB_ST::read());
        }
    }

    SerialM.print("HScale: ");
    SerialM.println(hscale);
    GBS::VDS_HSCALE::write(hscale);
}

void scaleVertical(uint16_t amountToScale, bool subtracting)
{
    typedef TV5725<GBS_ADDR> GBS;
    uint16_t vscale = GBS::VDS_VSCALE::read();

    // least invasive "is vscaling enabled" check
    if (vscale == 1023) {
        GBS::VDS_VSCALE_BYPS::write(0);
    }

    // smooth out areas of interest
    if (subtracting && (vscale == 513 || vscale == 512))
        amountToScale = 1;
    if (subtracting && (vscale == 684 || vscale == 683))
        amountToScale = 1;
    if (!subtracting && (vscale == 511 || vscale == 512))
        amountToScale = 1;
    if (!subtracting && (vscale == 682 || vscale == 683))
        amountToScale = 1;

    if (subtracting && (vscale - amountToScale > 128)) {
        vscale -= amountToScale;
    } else if (subtracting) {
        vscale = 128;
    } else if (vscale + amountToScale <= 1023) {
        vscale += amountToScale;
    } else if (vscale + amountToScale > 1023) {
        vscale = 1023;
        // don't enable vscale bypass here, since that disables ie line filter
    }

    SerialM.print("VScale: ");
    SerialM.println(vscale);
    GBS::VDS_VSCALE::write(vscale);
}

