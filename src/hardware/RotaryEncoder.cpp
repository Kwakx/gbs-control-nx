#include "RotaryEncoder.h"
#include "../core/Globals.h"
#include "../config/Config.h"
#include "../menu/OLEDMenuImplementation.h"

#if !USE_NEW_OLED_MENU
void IRAM_ATTR isrRotaryEncoder()
{
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();

    if (interruptTime - lastInterruptTime > 120) {
        if (!digitalRead(pin_data)) {
            oled_encoder_pos++;
            oled_main_pointer += 15;
            oled_sub_pointer += 15;
            oled_pointer_count++;
        } else {
            oled_encoder_pos--;
            oled_main_pointer -= 15;
            oled_sub_pointer -= 15;
            oled_pointer_count--;
        }
    }
    lastInterruptTime = interruptTime;
}
#endif

#if USE_NEW_OLED_MENU
void IRAM_ATTR isrRotaryEncoderRotateForNewMenu()
{
    unsigned long interruptTime = millis();
    static unsigned long lastInterruptTime = 0;
    static unsigned long lastNavUpdateTime = 0;
    static OLEDMenuNav lastNav;
    OLEDMenuNav newNav;
    if (interruptTime - lastInterruptTime > 150) {
        if (!digitalRead(pin_data)) {
            newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::DOWN : OLEDMenuNav::UP;
        } else {
            newNav = REVERSE_ROTARY_ENCODER_FOR_OLED_MENU ? OLEDMenuNav::UP : OLEDMenuNav::DOWN;
        }
        if (newNav != lastNav && (interruptTime - lastNavUpdateTime < 120)) {
            // ignore rapid changes to filter out mis-reads. besides, you are not supposed to rotate the encoder this fast anyway
            oledNav = lastNav = OLEDMenuNav::IDLE;
        }
        else{
            lastNav = oledNav = newNav;
            ++rotaryIsrID;
            lastNavUpdateTime = interruptTime;
        }
        lastInterruptTime = interruptTime;
    }
}

void IRAM_ATTR isrRotaryEncoderPushForNewMenu()
{
    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();
    if (interruptTime - lastInterruptTime > 500) {
        oledNav = OLEDMenuNav::ENTER;
        ++rotaryIsrID;
    }
    lastInterruptTime = interruptTime;
}
#endif

