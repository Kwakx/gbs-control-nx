#include "Deinterlacing.h"
#include "../core/Globals.h"
#include "../gbs/GBSController.h"
#include "../gbs/tv5725.h"

void enableMotionAdaptDeinterlace()
{
    typedef TV5725<GBS_ADDR> GBS;
    freezeVideo();
    GBS::DEINT_00::write(0x19);          // 2_00 // bypass angular (else 0x00)
    GBS::MADPT_Y_MI_OFFSET::write(0x00); // 2_0b  // also used for scanline mixing
    GBS::MADPT_Y_MI_DET_BYPS::write(0); //2_0a_7  // switch to automatic motion indexing

    if (rto->videoStandardInput == 1)
        GBS::MADPT_VTAP2_COEFF::write(6); // 2_19 vertical filter
    if (rto->videoStandardInput == 2)
        GBS::MADPT_VTAP2_COEFF::write(4);

    GBS::RFF_ADR_ADD_2::write(1);
    GBS::RFF_REQ_SEL::write(3);
    GBS::RFF_FETCH_NUM::write(0x80);    // part of RFF disable fix, could leave 0x80 always otherwise
    GBS::RFF_WFF_OFFSET::write(0x100);  // scanline fix
    GBS::RFF_YUV_DEINTERLACE::write(0); // scanline fix 2
    GBS::WFF_FF_STA_INV::write(0);      // 4_42_2
    GBS::WFF_ENABLE::write(1); // 4_42 0 // enable before RFF
    GBS::RFF_ENABLE::write(1); // 4_4d 7
    unfreezeVideo();
    delay(60);
    GBS::MAPDT_VT_SEL_PRGV::write(0); // 2_16_7
    rto->motionAdaptiveDeinterlaceActive = true;
}

void disableMotionAdaptDeinterlace()
{
    typedef TV5725<GBS_ADDR> GBS;
    GBS::MAPDT_VT_SEL_PRGV::write(1); // 2_16_7
    GBS::DEINT_00::write(0xff);       // 2_00

    GBS::RFF_FETCH_NUM::write(0x1);  // RFF disable fix
    GBS::RFF_WFF_OFFSET::write(0x1); // RFF disable fix
    delay(2);
    GBS::WFF_ENABLE::write(0);
    GBS::RFF_ENABLE::write(0); // can cause mem reset requirement, procedure above should fix it
    GBS::MADPT_Y_MI_DET_BYPS::write(1); //2_0a_7
    GBS::RFF_ADR_ADD_2::write(0);
    GBS::RFF_REQ_SEL::write(1);
    GBS::RFF_FETCH_NUM::write(0);
    GBS::RFF_WFF_OFFSET::write(0);
    GBS::RFF_YUV_DEINTERLACE::write(0);
    rto->motionAdaptiveDeinterlaceActive = false;
}

