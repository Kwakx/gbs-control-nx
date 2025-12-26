// Presets
#include "presets/ntsc_1280x1024.h"
#include "presets/ntsc_1280x720.h"
#include "presets/ntsc_1920x1080.h"
#include "presets/ntsc_240p.h"
#include "presets/ntsc_720x480.h"
#include "presets/ntsc_downscale.h"
#include "presets/ofw_RGBS.h"
#include "presets/pal_1280x1024.h"
#include "presets/pal_1280x720.h"
#include "presets/pal_1920x1080.h"
#include "presets/pal_240p.h"
#include "presets/pal_768x576.h"
#include "presets/pal_downscale.h"
#include "presets/presetDeinterlacerSection.h"
#include "presets/presetHdBypassSection.h"
#include "presets/presetMdSection.h"

// Arduino libraries
#include <ArduinoOTA.h>
#include <Wire.h>

// ESP8266/ESP32 platform libraries
#ifdef ESP32
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>
#include "FS.h"
#else
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <WiFiUdp.h>
#include "FS.h"
#endif

// Third-party libraries
#include <ESPAsyncWebServer.h>
// Optional:
// ESP8266-ping library to aid debugging WiFi issues, install via Arduino library manager
//#define HAVE_PINGER_LIBRARY
#ifdef HAVE_PINGER_LIBRARY
#include <Pinger.h>
#include <PingerResponse.h>
#endif

// Project libraries
#include <SSD1306Wire.h>
// WebSockets library by Markus Sattler
// https://github.com/Links2004/arduinoWebSockets
// included in lib folder to allow header modifications within limitations of the Arduino framework
// See 3rdparty/WebSockets for unmodified source and license
#include "lib/WebSockets/WebSockets.h"
#include "lib/WebSockets/WebSocketsServer.h"
// PersWiFiManager library by Ryan Downing
// https://github.com/r-downing/PersWiFiManager
// included in lib folder to allow modifications within limitations of the Arduino framework
// See 3rdparty/PersWiFiManager for unmodified source and license
#include "lib/PersWiFiManager/PersWiFiManager.h"
#include "src/gbs/tv5725.h"

// Project headers - src/config
#include "src/config/Config.h"
#include "src/config/FirmwareVersion.h"
#include "src/config/WiFiConfig.h"

// Project headers - src/core
#include "src/core/Globals.h"
#include "src/core/options.h"
#include "src/core/slot.h"

// Project headers - src/clock
#include "src/clock/Si5351Manager.h"

// Project headers - src/commands
#include "src/commands/SerialCommandHandler.h"
#include "src/commands/UserCommandHandler.h"

// Project headers - src/data
#include "src/data/images.h"

// Project headers - src/gbs
#include "src/gbs/GBSController.h"
#include "src/gbs/GBSPhase.h"
#include "src/gbs/GBSPresets.h"
#include "src/gbs/GBSRegister.h"
#include "src/gbs/GBSSync.h"
#include "src/gbs/GBSVideoProcessing.h"

// Project headers - src/hardware
#include "src/hardware/I2CManager.h"
#include "src/hardware/LEDControl.h"
#include "src/hardware/RotaryEncoder.h"

// Project headers - src/menu
#include "src/menu/OLEDMenu.h"
#include "src/menu/osd.h"
#if USE_NEW_OLED_MENU
#include "src/menu/OLEDMenuImplementation.h"
#include "src/menu/OSDManager.h"
#include "src/menu/OTAUpdate.h"
#endif

// Project headers - src/storage
#include "src/storage/PreferencesManager.h"
#include "src/storage/SlotManager.h"

// Project headers - src/utils
#include "src/utils/DebugHelpers.h"
#include "src/utils/Logging.h"

// Project headers - src/video
#include "src/video/Deinterlacing.h"
#include "src/video/Scanlines.h"
#include "src/video/SyncWatcher.h"
#include "src/video/VideoInput.h"
#include "src/video/VideoShifting.h"
#include "src/video/VideoTiming.h"

// Project headers - src/web
#include "src/web/SerialMirror.h"
#include "src/web/WebServer.h"
#include "src/web/WebSocketHandler.h"

// Project headers - src/wifi
#include "src/wifi/WiFiManager.h"

// Generated web UI
#if defined(ESP8266) || defined(ESP32)
#include "webui/build/webui_html.h"
#endif

#ifdef ESP8266
// fast ESP8266 digitalRead (21 cycles vs 77), *should* work with all possible input pins
// but only "D7" and "D6" have been tested so far
#define digitalRead(x) ((GPIO_REG_READ(GPIO_IN_ADDRESS) >> x) & 1)
#endif

// Menu and FrameSync are now defined in src/core/Globals.h
// TODO replace result with VideoStandardInput enum

void setup()
{
    display.init();                 //inits OLED on I2C bus
    display.flipScreenVertically(); //orientation fix for OLED

    pinMode(pin_clk, INPUT_PULLUP);
    pinMode(pin_data, INPUT_PULLUP);
    pinMode(pin_switch, INPUT_PULLUP);

#if USE_NEW_OLED_MENU
    attachInterrupt(digitalPinToInterrupt(pin_clk), isrRotaryEncoderRotateForNewMenu, FALLING);
    attachInterrupt(digitalPinToInterrupt(pin_switch), isrRotaryEncoderPushForNewMenu, FALLING);
    initOLEDMenu();
    initOSD();
#else
    // ISR TO PIN
    attachInterrupt(digitalPinToInterrupt(pin_clk), isrRotaryEncoder, FALLING);
#endif

    rto->webServerEnabled = true;
    rto->webServerStarted = false; // make sure this is set

    Serial.begin(115200); // Arduino IDE Serial Monitor requires the same 115200 bauds!
    Serial.setTimeout(10);

    // millis() at this point: typically 65ms
    // WiFi hostname will be set in PersWiFiManager::attemptConnection()

    startWire();
    // run some dummy commands to init I2C to GBS and cached segments
    GBS::SP_SOG_MODE::read();
    writeOneByte(0xF0, 0);
    writeOneByte(0x00, 0);
    GBS::STATUS_00::read();

    if (rto->webServerEnabled) {
        // Initialize WiFi settings before starting
        // Force WiFi to sleep first to ensure clean state
#ifdef ESP8266
        WiFi.forceSleepBegin();
#endif
        delay(100);
#ifdef ESP8266
        WiFi.forceSleepWake();
#endif
        delay(100);
        
        // Avoid ESP8266 Core 3.1.x bug and flash wear (ESP8266 only).
        // On ESP32, WiFi.persistent(false) changes config storage to RAM and can break loading saved creds.
#ifdef ESP8266
        WiFi.persistent(false);
#endif
        WiFi.setAutoConnect(false);  // Let PersWiFiManager handle connections
        WiFi.setAutoReconnect(true); // Enable auto-reconnect on disconnect
        WiFi.mode(WIFI_OFF);         // Start with WiFi off
        delay(100);
        
        rto->allowUpdatesOTA = false;       // need to initialize for handleWiFi()
#ifdef ESP8266
        WiFi.setSleepMode(WIFI_NONE_SLEEP); // low latency responses, less chance for missing packets
        WiFi.setPhyMode(WIFI_PHY_MODE_11G); // Force 11g mode for better stability
        WiFi.setOutputPower(18.0f);         // float: min 0.0f, max 20.5f
#else
        // ESP32
        // WiFi.setSleep(false);
#endif
        startWebserver();
        rto->webServerStarted = true;
    } else {
        //WiFi.disconnect(); // deletes credentials
        WiFi.mode(WIFI_OFF);
#ifdef ESP8266
        WiFi.forceSleepBegin();
#endif
    }
#ifdef HAVE_PINGER_LIBRARY
    pingLastTime = millis();
#endif

    SerialM.println(F("\nstartup"));

    loadDefaultUserOptions();
    //globalDelay = 0;

    rto->allowUpdatesOTA = false; // ESP over the air updates. default to off, enable via web interface
    rto->enableDebugPings = false;
    rto->autoBestHtotalEnabled = true; // automatically find the best horizontal total pixel value for a given input timing
    rto->syncLockFailIgnore = 16;      // allow syncLock to fail x-1 times in a row before giving up (sync glitch immunity)
    rto->forceRetime = false;
    rto->syncWatcherEnabled = true; // continously checks the current sync status. required for normal operation
    rto->phaseADC = 16;
    rto->phaseSP = 16;
    rto->failRetryAttempts = 0;
    rto->presetID = 0;
    rto->isCustomPreset = false;
    rto->HPLLState = 0;
    rto->motionAdaptiveDeinterlaceActive = false;
    rto->deinterlaceAutoEnabled = true;
    rto->scanlinesEnabled = false;
    rto->boardHasPower = true;
    rto->presetIsPalForce60 = false;
    rto->syncTypeCsync = false;
    rto->isValidForScalingRGBHV = false;
    rto->medResLineCount = 0x33; // 51*8=408
    rto->osr = 0;
    rto->useHdmiSyncFix = 0;
    rto->notRecognizedCounter = 0;

    // more run time variables
    rto->inputIsYpBpR = false;
    rto->videoStandardInput = 0;
    rto->outModeHdBypass = false;
    rto->videoIsFrozen = false;
    if (!rto->webServerEnabled)
        rto->webServerStarted = false;
    rto->printInfos = false;
    rto->sourceDisconnected = true;
    rto->isInLowPowerMode = false;
    rto->applyPresetDoneStage = 0;
    rto->presetVlineShift = 0;
    rto->clampPositionIsSet = 0;
    rto->coastPositionIsSet = 0;
    rto->continousStableCounter = 0;
    rto->currentLevelSOG = 5;
    rto->thisSourceMaxLevelSOG = 31; // 31 = auto sog has not (yet) run

    adco->r_gain = 0;
    adco->g_gain = 0;
    adco->b_gain = 0;
    adco->r_off = 0;
    adco->g_off = 0;
    adco->b_off = 0;

    serialCommand = '@'; // ASCII @ = 0
    userCommand = '@';

    pinMode(DEBUG_IN_PIN, INPUT);
#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
    LEDON; // enable the LED, lets users know the board is starting up
#endif

    //Serial.setDebugOutput(true); // if you want simple wifi debug info

    // delay 1 of 2
    unsigned long initDelay = millis();
    // upped from < 500 to < 1500, allows more time for wifi and GBS startup
    while (millis() - initDelay < 1500) {
        display.drawXbm(2, 2, gbsicon_width, gbsicon_height, gbsicon_bits);
        display.display();
        handleWiFi(0);
        delay(1);
    }
    display.clear();
    // if i2c established and chip running, issue software reset now
    GBS::RESET_CONTROL_0x46::write(0);
    GBS::RESET_CONTROL_0x47::write(0);
    GBS::PLLAD_VCORST::write(1);
    GBS::PLLAD_PDZ::write(0); // AD PLL off

    // file system (web page, custom presets, ect)
#ifdef ESP32
    if (!LittleFS.begin(true)) {
#else
    if (!LittleFS.begin()) {
#endif
        SerialM.println(F("LittleFS mount failed! ((1M LittleFS) selected?)"));
    } else {
        // load user preferences file
        File f = LittleFS.open("/preferencesv2.txt", "r");
        if (!f) {
            SerialM.println(F("no preferences file yet, create new"));
            loadDefaultUserOptions();
            saveUserPrefs(); // if this fails, there must be a LittleFS problem
        } else {
            //on a fresh / LittleFS not formatted yet MCU:  userprefs.txt open ok //result = 207
            uopt->presetPreference = (PresetPreference)(f.read() - '0'); // #1
            if (uopt->presetPreference > 10)
                uopt->presetPreference = Output960P; // fresh LittleFS ?

            uopt->enableFrameTimeLock = (uint8_t)(f.read() - '0');
            if (uopt->enableFrameTimeLock > 1)
                uopt->enableFrameTimeLock = 0;

            uopt->presetSlot = lowByte(f.read());

            uopt->frameTimeLockMethod = (uint8_t)(f.read() - '0');
            if (uopt->frameTimeLockMethod > 1)
                uopt->frameTimeLockMethod = 0;

            uopt->enableAutoGain = (uint8_t)(f.read() - '0');
            if (uopt->enableAutoGain > 1)
                uopt->enableAutoGain = 0;

            uopt->wantScanlines = (uint8_t)(f.read() - '0');
            if (uopt->wantScanlines > 1)
                uopt->wantScanlines = 0;

            uopt->wantOutputComponent = (uint8_t)(f.read() - '0');
            if (uopt->wantOutputComponent > 1)
                uopt->wantOutputComponent = 0;

            uopt->deintMode = (uint8_t)(f.read() - '0');
            if (uopt->deintMode > 2)
                uopt->deintMode = 0;

            uopt->wantVdsLineFilter = (uint8_t)(f.read() - '0');
            if (uopt->wantVdsLineFilter > 1)
                uopt->wantVdsLineFilter = 0;

            uopt->wantPeaking = (uint8_t)(f.read() - '0');
            if (uopt->wantPeaking > 1)
                uopt->wantPeaking = 1;

            uopt->preferScalingRgbhv = (uint8_t)(f.read() - '0');
            if (uopt->preferScalingRgbhv > 1)
                uopt->preferScalingRgbhv = 1;

            uopt->wantTap6 = (uint8_t)(f.read() - '0');
            if (uopt->wantTap6 > 1)
                uopt->wantTap6 = 1;

            uopt->PalForce60 = (uint8_t)(f.read() - '0');
            if (uopt->PalForce60 > 1)
                uopt->PalForce60 = 0;

            uopt->matchPresetSource = (uint8_t)(f.read() - '0'); // #14
            if (uopt->matchPresetSource > 1)
                uopt->matchPresetSource = 1;

            uopt->wantStepResponse = (uint8_t)(f.read() - '0'); // #15
            if (uopt->wantStepResponse > 1)
                uopt->wantStepResponse = 1;

            uopt->wantFullHeight = (uint8_t)(f.read() - '0'); // #16
            if (uopt->wantFullHeight > 1)
                uopt->wantFullHeight = 1;

            uopt->enableCalibrationADC = (uint8_t)(f.read() - '0'); // #17
            if (uopt->enableCalibrationADC > 1)
                uopt->enableCalibrationADC = 1;

            uopt->scanlineStrength = (uint8_t)(f.read() - '0'); // #18
            if (uopt->scanlineStrength > 0x60)
                uopt->enableCalibrationADC = 0x30;

            uopt->disableExternalClockGenerator = (uint8_t)(f.read() - '0'); // #19
            if (uopt->disableExternalClockGenerator > 1)
                uopt->disableExternalClockGenerator = 0;

            f.close();
        }
    }


    GBS::PAD_CKIN_ENZ::write(1); // disable to prevent startup spike damage
    // Ensure I2C is configured before touching Si5351 (ESP32 Wire.begin() defaults can differ).
    startWire();
    externalClockGenDetectAndInitialize();
    // library may change i2c clock or pins, so restart
    startWire();
    GBS::STATUS_00::read();
    GBS::STATUS_00::read();
    GBS::STATUS_00::read();

    // delay 2 of 2
    initDelay = millis();
    while (millis() - initDelay < 1000) {
        handleWiFi(0);
        delay(1);
    }

    if (WiFi.status() == WL_CONNECTED) {
        SerialM.print(F("(WiFi): Connected to "));
        SerialM.println(WiFi.SSID());
    } else if (WiFi.SSID().length() == 0) {
        SerialM.println(FPSTR(ap_info_string));
    } else {
#ifdef ESP8266
        SerialM.print(F("(WiFi): Connecting to "));
        SerialM.print(WiFi.SSID());
        SerialM.println(F("..."));
#else
        SerialM.println(F("(WiFi): Connecting..."));
#endif
        // Don't call reconnect here, let PersWiFiManager handle it
    }

    // dummy commands
    GBS::STATUS_00::read();
    GBS::STATUS_00::read();
    GBS::STATUS_00::read();

    boolean powerOrWireIssue = 0;
    if (!checkBoardPower()) {
        stopWire(); // sets pinmodes SDA, SCL to INPUT
        for (int i = 0; i < 40; i++) {
            // I2C SDA probably stuck, attempt recovery (max attempts in tests was around 10)
            startWire();
            GBS::STATUS_00::read();
            digitalWrite(SCL, 0);
            delayMicroseconds(12);
            stopWire();
            if (digitalRead(SDA) == 1) {
                break;
            } // unstuck
            if ((i % 7) == 0) {
                delay(1);
            }
        }

        // restart and dummy
        startWire();
        delay(1);
        GBS::STATUS_00::read();
        delay(1);

        if (!checkBoardPower()) {
            stopWire();
            powerOrWireIssue = 1; // fail
            rto->boardHasPower = false;
            rto->syncWatcherEnabled = false;
        } else { // recover success
            rto->syncWatcherEnabled = true;
            rto->boardHasPower = true;
            SerialM.println(F("recovered"));
        }
    }

    if (powerOrWireIssue == 0) {
        // second init, in cases where i2c got stuck earlier but has recovered
        // *if* ext clock gen is installed and *if* i2c got stuck, then clockgen must be already running
        if (!rto->extClockGenDetected) {
            externalClockGenDetectAndInitialize();
        }
        if (rto->extClockGenDetected == 1) {
            Serial.println(F("ext clockgen detected"));
        } else {
            Serial.println(F("no ext clockgen"));
        }

        zeroAll();
        setResetParameters();
        prepareSyncProcessor();

        uint8_t productId = GBS::CHIP_ID_PRODUCT::read();
        uint8_t revisionId = GBS::CHIP_ID_REVISION::read();
        SerialM.print(F("Chip ID: "));
        SerialM.print(productId, HEX);
        SerialM.print(" ");
        SerialM.println(revisionId, HEX);

        if (uopt->enableCalibrationADC) {
            // enabled by default
            calibrateAdcOffset();
        }
        setResetParameters();

        delay(4); // help wifi (presets are unloaded now)
        handleWiFi(1);
        delay(4);

        // startup reliability test routine
        /*rto->videoStandardInput = 1;
    writeProgramArrayNew(ntsc_240p, 0);
    doPostPresetLoadSteps();
    int i = 100000;
    while (i-- > 0) loop();
    ESP.restart();*/

        //rto->syncWatcherEnabled = false; // allows passive operation by disabling syncwatcher here
        //inputAndSyncDetect();
        //if (rto->syncWatcherEnabled == true) {
        //  rto->isInLowPowerMode = true; // just for initial detection; simplifies code later
        //  for (uint8_t i = 0; i < 3; i++) {
        //    if (inputAndSyncDetect()) {
        //      break;
        //    }
        //  }
        //  rto->isInLowPowerMode = false;
        //}
    } else {
        SerialM.println(F("Please check board power and cabling or restart!"));
    }

    LEDOFF; // LED behaviour: only light LED on active sync

    // some debug tools leave garbage in the serial rx buffer
    if (Serial.available()) {
        discardSerialRxData();
    }
}

void loop()
{
    static uint8_t readout = 0;
    static uint8_t segmentCurrent = 255;
    static uint8_t registerCurrent = 255;
    static uint8_t inputToogleBit = 0;
    static uint8_t inputStage = 0;
    static unsigned long lastTimeSyncWatcher = millis();
    static unsigned long lastTimeSourceCheck = 500; // 500 to start right away (after setup it will be 2790ms when we get here)
    static unsigned long lastTimeInterruptClear = millis();

#if HAVE_BUTTONS
    static unsigned long lastButton = micros();
    if (micros() - lastButton > buttonPollInterval) {
        lastButton = micros();
        handleButtons();
    }
#endif

#if USE_NEW_OLED_MENU
    uint8_t oldIsrID = rotaryIsrID;
    // make sure no rotary encoder isr happened while menu was updating.
    // skipping this check will make the rotary encoder not responsive randomly.
    // (oledNav change will be lost if isr happened during menu updating)
    oledMenu.tick(oledNav);
    if (oldIsrID == rotaryIsrID) {
        oledNav = OLEDMenuNav::IDLE;
    }
#else
    settingsMenuOLED();
    if (oled_encoder_pos != oled_lastCount) {
        oled_lastCount = oled_encoder_pos;
    }
#endif

    handleWiFi(0); // WiFi + OTA + WS + MDNS, checks for server enabled + started

    // is there a command from Terminal or web ui?
    // Serial takes precedence
    if (Serial.available()) {
        serialCommand = Serial.read();
    } else if (serialCommandBuffer.length() > 0) {
        serialCommand = serialCommandBuffer.charAt(0);
        serialCommandBuffer.remove(0, 1);
    } else if (inputStage > 0) {
        // multistage with no more data
        SerialM.println(F(" abort"));
        discardSerialRxData();
        serialCommand = ' ';
    }
    if (serialCommand != '@') {
        // multistage with bad characters?
        if (inputStage > 0) {
            // need 's', 't' or 'g'
            if (serialCommand != 's' && serialCommand != 't' && serialCommand != 'g') {
                discardSerialRxData();
                SerialM.println(F(" abort"));
                serialCommand = ' ';
            }
        }
        myLog("serial", serialCommand);

        switch (serialCommand) {
            case ' ':
                // skip spaces
                inputStage = segmentCurrent = registerCurrent = 0; // and reset these
                break;
            case 'd': {
                // don't store scanlines
                if (GBS::GBS_OPTION_SCANLINES_ENABLED::read() == 1) {
                    disableScanlines();
                }
                // pal forced 60hz: no need to revert here. let the voffset function handle it

                if (uopt->enableFrameTimeLock && FrameSync::getSyncLastCorrection() != 0) {
                    FrameSync::reset(uopt->frameTimeLockMethod);
                }
                // dump
                for (int segment = 0; segment <= 5; segment++) {
                    dumpRegisters(segment);
                }
                SerialM.println("};");
            } break;
            case '+':
                SerialM.println("hor. +");
                shiftHorizontalRight();
                //shiftHorizontalRightIF(4);
                break;
            case '-':
                SerialM.println("hor. -");
                shiftHorizontalLeft();
                //shiftHorizontalLeftIF(4);
                break;
            case '*':
                shiftVerticalUpIF();
                break;
            case '/':
                shiftVerticalDownIF();
                break;
            case 'z':
                SerialM.println(F("scale+"));
                scaleHorizontal(2, true);
                break;
            case 'h':
                SerialM.println(F("scale-"));
                scaleHorizontal(2, false);
                break;
            case 'q':
                resetDigital();
                delay(2);
                ResetSDRAM();
                delay(2);
                togglePhaseAdjustUnits();
                //enableVDS();
                break;
            case 'D':
                SerialM.print(F("debug view: "));
                if (GBS::ADC_UNUSED_62::read() == 0x00) { // "remembers" debug view
                    //if (uopt->wantPeaking == 0) { GBS::VDS_PK_Y_H_BYPS::write(0); } // 3_4e 0 // enable peaking but don't store
                    GBS::VDS_PK_LB_GAIN::write(0x3f);                   // 3_45
                    GBS::VDS_PK_LH_GAIN::write(0x3f);                   // 3_47
                    GBS::ADC_UNUSED_60::write(GBS::VDS_Y_OFST::read()); // backup
                    GBS::ADC_UNUSED_61::write(GBS::HD_Y_OFFSET::read());
                    GBS::ADC_UNUSED_62::write(1); // remember to remove on restore
                    GBS::VDS_Y_OFST::write(GBS::VDS_Y_OFST::read() + 0x24);
                    GBS::HD_Y_OFFSET::write(GBS::HD_Y_OFFSET::read() + 0x24);
                    if (!rto->inputIsYpBpR) {
                        // RGB input that has HD_DYN bypassed, use it now
                        GBS::HD_DYN_BYPS::write(0);
                        GBS::HD_U_OFFSET::write(GBS::HD_U_OFFSET::read() + 0x24);
                        GBS::HD_V_OFFSET::write(GBS::HD_V_OFFSET::read() + 0x24);
                    }
                    //GBS::IF_IN_DREG_BYPS::write(1); // enhances noise from not delaying IF processing properly
                    SerialM.println("on");
                } else {
                    //if (uopt->wantPeaking == 0) { GBS::VDS_PK_Y_H_BYPS::write(1); } // 3_4e 0
                    if (rto->presetID == 0x05) {
                        GBS::VDS_PK_LB_GAIN::write(0x16); // 3_45
                        GBS::VDS_PK_LH_GAIN::write(0x0A); // 3_47
                    } else {
                        GBS::VDS_PK_LB_GAIN::write(0x16); // 3_45
                        GBS::VDS_PK_LH_GAIN::write(0x18); // 3_47
                    }
                    GBS::VDS_Y_OFST::write(GBS::ADC_UNUSED_60::read()); // restore
                    GBS::HD_Y_OFFSET::write(GBS::ADC_UNUSED_61::read());
                    if (!rto->inputIsYpBpR) {
                        // RGB input, HD_DYN_BYPS again
                        GBS::HD_DYN_BYPS::write(1);
                        GBS::HD_U_OFFSET::write(0); // probably just 0 by default
                        GBS::HD_V_OFFSET::write(0); // probably just 0 by default
                    }
                    //GBS::IF_IN_DREG_BYPS::write(0);
                    GBS::ADC_UNUSED_60::write(0); // .. and clear
                    GBS::ADC_UNUSED_61::write(0);
                    GBS::ADC_UNUSED_62::write(0);
                    SerialM.println("off");
                }
                serialCommand = '@';
                break;
            case 'C':
                SerialM.println(F("PLL: ICLK"));
                // display clock in last test best at 0x85
                GBS::PLL648_CONTROL_01::write(0x85);
                GBS::PLL_CKIS::write(1); // PLL use ICLK (instead of oscillator)
                latchPLLAD();
                //GBS::VDS_HSCALE::write(512);
                rto->syncLockFailIgnore = 16;
                FrameSync::reset(uopt->frameTimeLockMethod); // adjust htotal to new display clock
                rto->forceRetime = true;
                //applyBestHTotal(FrameSync::init()); // adjust htotal to new display clock
                //applyBestHTotal(FrameSync::init()); // twice
                //GBS::VDS_FLOCK_EN::write(1); //risky
                delay(200);
                break;
            case 'Y':
                writeProgramArrayNew(ntsc_1280x720, false);
                doPostPresetLoadSteps();
                break;
            case 'y':
                writeProgramArrayNew(pal_1280x720, false);
                doPostPresetLoadSteps();
                break;
            case 'P':
                SerialM.print(F("auto deinterlace: "));
                rto->deinterlaceAutoEnabled = !rto->deinterlaceAutoEnabled;
                if (rto->deinterlaceAutoEnabled) {
                    SerialM.println("on");
                } else {
                    SerialM.println("off");
                }
                break;
            case 'p':
                if (!rto->motionAdaptiveDeinterlaceActive) {
                    if (GBS::GBS_OPTION_SCANLINES_ENABLED::read() == 1) { // don't rely on rto->scanlinesEnabled
                        disableScanlines();
                    }
                    enableMotionAdaptDeinterlace();
                } else {
                    disableMotionAdaptDeinterlace();
                }
                break;
            case 'k':
                bypassModeSwitch_RGBHV();
                break;
            case 'K':
                setOutModeHdBypass(false);
                uopt->presetPreference = OutputBypass;
                saveUserPrefs();
                break;
            case 'T':
                SerialM.print(F("auto gain "));
                if (uopt->enableAutoGain == 0) {
                    uopt->enableAutoGain = 1;
                    setAdcGain(AUTO_GAIN_INIT);
                    GBS::DEC_TEST_ENABLE::write(1);
                    SerialM.println("on");
                } else {
                    uopt->enableAutoGain = 0;
                    GBS::DEC_TEST_ENABLE::write(0);
                    SerialM.println("off");
                }
                saveUserPrefs();
                break;
            case 'e':
                writeProgramArrayNew(ntsc_240p, false);
                doPostPresetLoadSteps();
                break;
            case 'r':
                writeProgramArrayNew(pal_240p, false);
                doPostPresetLoadSteps();
                break;
            case '.': {
                if (!rto->outModeHdBypass) {
                    // bestHtotal recalc
                    rto->autoBestHtotalEnabled = true;
                    rto->syncLockFailIgnore = 16;
                    rto->forceRetime = true;
                    FrameSync::reset(uopt->frameTimeLockMethod);

                    if (!rto->syncWatcherEnabled) {
                        boolean autoBestHtotalSuccess = 0;
                        delay(30);
                        autoBestHtotalSuccess = runAutoBestHTotal();
                        if (!autoBestHtotalSuccess) {
                            SerialM.println(F("(unchanged)"));
                        }
                    }
                }
            } break;
            case '!':
                //fastGetBestHtotal();
                //readEeprom();
                Serial.print(F("sfr: "));
                Serial.println(getSourceFieldRate(1));
                Serial.print(F("pll: "));
                Serial.println(getPllRate());
                break;
            case '$': {
                // EEPROM write protect pin (7, next to Vcc) is under original MCU control
                // MCU drags to Vcc to write, EEPROM drags to Gnd normally
                // This routine only works with that "WP" pin disconnected
                // 0x17 = input selector? // 0x18 = input selector related?
                // 0x54 = coast start 0x55 = coast end
                uint16_t writeAddr = 0x54;
                const uint8_t eepromAddr = 0x50;
                for (; writeAddr < 0x56; writeAddr++) {
                    Wire.beginTransmission(eepromAddr);
                    Wire.write(writeAddr >> 8);     // high addr byte, 4 bits +
                    Wire.write((uint8_t)writeAddr); // mlow addr byte, 8 bits = 12 bits (0xfff max)
                    Wire.write(0x10);               // coast end value ?
                    Wire.endTransmission();
                    delay(5);
                }

                //Wire.beginTransmission(eepromAddr);
                //Wire.write((uint8_t)0); Wire.write((uint8_t)0);
                //Wire.write(0xff); // force eeprom clear probably
                //Wire.endTransmission();
                //delay(5);

                Serial.println("done");
            } break;
            case 'j':
                //resetPLL();
                latchPLLAD();
                break;
            case 'J':
                resetPLLAD();
                break;
            case 'v':
                rto->phaseSP += 1;
                rto->phaseSP &= 0x1f;
                SerialM.print("SP: ");
                SerialM.println(rto->phaseSP);
                setAndLatchPhaseSP();
                //setAndLatchPhaseADC();
                break;
            case 'b':
                advancePhase();
                latchPLLAD();
                SerialM.print("ADC: ");
                SerialM.println(rto->phaseADC);
                break;
            case '#':
                rto->videoStandardInput = 13;
                applyPresets(13);
                //Serial.println(getStatus00IfHsVsStable());
                //globalDelay++;
                //SerialM.println(globalDelay);
                break;
            case 'n': {
                uint16_t pll_divider = GBS::PLLAD_MD::read();
                pll_divider += 1;
                GBS::PLLAD_MD::write(pll_divider);
                GBS::IF_HSYNC_RST::write((pll_divider / 2));
                GBS::IF_LINE_SP::write(((pll_divider / 2) + 1) + 0x40);
                SerialM.print(F("PLL div: "));
                SerialM.print(pll_divider, HEX);
                SerialM.print(" ");
                SerialM.println(pll_divider);
                // set IF before latching
                //setIfHblankParameters();
                latchPLLAD();
                delay(1);
                //applyBestHTotal(GBS::VDS_HSYNC_RST::read());
                updateClampPosition();
                updateCoastPosition(0);
            } break;
            case 'N': {
                //if (GBS::RFF_ENABLE::read()) {
                //  disableMotionAdaptDeinterlace();
                //}
                //else {
                //  enableMotionAdaptDeinterlace();
                //}

                //GBS::RFF_ENABLE::write(!GBS::RFF_ENABLE::read());

                if (rto->scanlinesEnabled) {
                    rto->scanlinesEnabled = false;
                    disableScanlines();
                } else {
                    rto->scanlinesEnabled = true;
                    enableScanlines();
                }
            } break;
            case 'a':
                SerialM.print(F("HTotal++: "));
                SerialM.println(GBS::VDS_HSYNC_RST::read() + 1);
                if (GBS::VDS_HSYNC_RST::read() < 4095) {
                    if (uopt->enableFrameTimeLock) {
                        // syncLastCorrection != 0 check is included
                        FrameSync::reset(uopt->frameTimeLockMethod);
                    }
                    rto->forceRetime = 1;
                    applyBestHTotal(GBS::VDS_HSYNC_RST::read() + 1);
                }
                break;
            case 'A':
                SerialM.print(F("HTotal--: "));
                SerialM.println(GBS::VDS_HSYNC_RST::read() - 1);
                if (GBS::VDS_HSYNC_RST::read() > 0) {
                    if (uopt->enableFrameTimeLock) {
                        // syncLastCorrection != 0 check is included
                        FrameSync::reset(uopt->frameTimeLockMethod);
                    }
                    rto->forceRetime = 1;
                    applyBestHTotal(GBS::VDS_HSYNC_RST::read() - 1);
                }
                break;
            case 'M': {
            } break;
            case 'm':
                SerialM.print(F("syncwatcher "));
                if (rto->syncWatcherEnabled == true) {
                    rto->syncWatcherEnabled = false;
                    if (rto->videoIsFrozen) {
                        unfreezeVideo();
                    }
                    SerialM.println("off");
                } else {
                    rto->syncWatcherEnabled = true;
                    SerialM.println("on");
                }
                break;
            case ',':
                printVideoTimings();
                break;
            case 'i':
                rto->printInfos = !rto->printInfos;
                break;
            case 'c':
                SerialM.println(F("OTA Updates on"));
                initUpdateOTA();
                rto->allowUpdatesOTA = true;
                break;
            case 'G':
                SerialM.print(F("Debug Pings "));
                if (!rto->enableDebugPings) {
                    SerialM.println("on");
                    rto->enableDebugPings = 1;
                } else {
                    SerialM.println("off");
                    rto->enableDebugPings = 0;
                }
                break;
            case 'u':
                ResetSDRAM();
                break;
            case 'f':
                SerialM.print(F("peaking "));
                if (uopt->wantPeaking == 0) {
                    uopt->wantPeaking = 1;
                    GBS::VDS_PK_Y_H_BYPS::write(0);
                    SerialM.println("on");
                } else {
                    uopt->wantPeaking = 0;
                    GBS::VDS_PK_Y_H_BYPS::write(1);
                    SerialM.println("off");
                }
                saveUserPrefs();
                break;
            case 'F':
                SerialM.print(F("ADC filter "));
                if (GBS::ADC_FLTR::read() > 0) {
                    GBS::ADC_FLTR::write(0);
                    SerialM.println("off");
                } else {
                    GBS::ADC_FLTR::write(3);
                    SerialM.println("on");
                }
                break;
            case 'L': {
                // Component / VGA Output
                uopt->wantOutputComponent = !uopt->wantOutputComponent;
                OutputComponentOrVGA();
                saveUserPrefs();
                // apply 1280x720 preset now, otherwise a reboot would be required
                uint8_t videoMode = getVideoMode();
                if (videoMode == 0)
                    videoMode = rto->videoStandardInput;
                PresetPreference backup = uopt->presetPreference;
                uopt->presetPreference = Output720P;
                rto->videoStandardInput = 0; // force hard reset
                applyPresets(videoMode);
                uopt->presetPreference = backup;
            } break;
            case 'l':
                SerialM.println(F("resetSyncProcessor"));
                //freezeVideo();
                resetSyncProcessor();
                //delay(10);
                //unfreezeVideo();
                break;
            case 'Z': {
                uopt->matchPresetSource = !uopt->matchPresetSource;
                saveUserPrefs();
                uint8_t vidMode = getVideoMode();
                if (uopt->presetPreference == 0 && GBS::GBS_PRESET_ID::read() == 0x11) {
                    applyPresets(vidMode);
                } else if (uopt->presetPreference == 4 && GBS::GBS_PRESET_ID::read() == 0x02) {
                    applyPresets(vidMode);
                }
            } break;
            case 'W':
                uopt->enableFrameTimeLock = !uopt->enableFrameTimeLock;
                break;
            case 'E':
                writeProgramArrayNew(ntsc_1280x1024, false);
                doPostPresetLoadSteps();
                break;
            case 'R':
                writeProgramArrayNew(pal_1280x1024, false);
                doPostPresetLoadSteps();
                break;
            case '0':
                moveHS(4, true);
                break;
            case '1':
                moveHS(4, false);
                break;
            case '2':
                writeProgramArrayNew(pal_768x576, false); // ModeLine "720x576@50" 27 720 732 795 864 576 581 586 625 -hsync -vsync
                doPostPresetLoadSteps();
                break;
            case '3':
                //
                break;
            case '4': {
                // scale vertical +
                if (GBS::VDS_VSCALE::read() <= 256) {
                    SerialM.println("limit");
                    break;
                }
                scaleVertical(2, true);
                // actually requires full vertical mask + position offset calculation
            } break;
            case '5': {
                // scale vertical -
                if (GBS::VDS_VSCALE::read() == 1023) {
                    SerialM.println("limit");
                    break;
                }
                scaleVertical(2, false);
                // actually requires full vertical mask + position offset calculation
            } break;
            case '6':
                if (videoStandardInputIsPalNtscSd() && !rto->outModeHdBypass) {
                    if (GBS::IF_HBIN_SP::read() >= 10) {                     // IF_HBIN_SP: min 2
                        GBS::IF_HBIN_SP::write(GBS::IF_HBIN_SP::read() - 8); // canvas move right
                        if ((GBS::IF_HSYNC_RST::read() - 4) > ((GBS::PLLAD_MD::read() >> 1) + 5)) {
                            GBS::IF_HSYNC_RST::write(GBS::IF_HSYNC_RST::read() - 4); // shrink 1_0e
                            GBS::IF_LINE_SP::write(GBS::IF_LINE_SP::read() - 4);     // and 1_22 to go with it
                        }
                    } else {
                        SerialM.println("limit");
                    }
                } else if (!rto->outModeHdBypass) {
                    if (GBS::IF_HB_SP2::read() >= 4)
                        GBS::IF_HB_SP2::write(GBS::IF_HB_SP2::read() - 4); // 1_1a
                    else
                        GBS::IF_HB_SP2::write(GBS::IF_HSYNC_RST::read() - 0x30);
                    if (GBS::IF_HB_ST2::read() >= 4)
                        GBS::IF_HB_ST2::write(GBS::IF_HB_ST2::read() - 4); // 1_18
                    else
                        GBS::IF_HB_ST2::write(GBS::IF_HSYNC_RST::read() - 0x30);
                    SerialM.print(F("IF_HB_ST2: "));
                    SerialM.print(GBS::IF_HB_ST2::read(), HEX);
                    SerialM.print(F(" IF_HB_SP2: "));
                    SerialM.println(GBS::IF_HB_SP2::read(), HEX);
                }
                break;
            case '7':
                if (videoStandardInputIsPalNtscSd() && !rto->outModeHdBypass) {
                    if (GBS::IF_HBIN_SP::read() < 0x150) {                   // (arbitrary) max limit
                        GBS::IF_HBIN_SP::write(GBS::IF_HBIN_SP::read() + 8); // canvas move left
                    } else {
                        SerialM.println("limit");
                    }
                } else if (!rto->outModeHdBypass) {
                    if (GBS::IF_HB_SP2::read() < (GBS::IF_HSYNC_RST::read() - 0x30))
                        GBS::IF_HB_SP2::write(GBS::IF_HB_SP2::read() + 4); // 1_1a
                    else
                        GBS::IF_HB_SP2::write(0);
                    if (GBS::IF_HB_ST2::read() < (GBS::IF_HSYNC_RST::read() - 0x30))
                        GBS::IF_HB_ST2::write(GBS::IF_HB_ST2::read() + 4); // 1_18
                    else
                        GBS::IF_HB_ST2::write(0);
                    SerialM.print(F("IF_HB_ST2: "));
                    SerialM.print(GBS::IF_HB_ST2::read(), HEX);
                    SerialM.print(F(" IF_HB_SP2: "));
                    SerialM.println(GBS::IF_HB_SP2::read(), HEX);
                }
                break;
            case '8':
                //SerialM.println("invert sync");
                invertHS();
                invertVS();
                //optimizePhaseSP();
                break;
            case '9':
                writeProgramArrayNew(ntsc_720x480, false);
                doPostPresetLoadSteps();
                break;
            case 'o': {
                if (rto->osr == 1) {
                    setOverSampleRatio(2, false);
                } else if (rto->osr == 2) {
                    setOverSampleRatio(4, false);
                } else if (rto->osr == 4) {
                    setOverSampleRatio(1, false);
                }
                delay(4);
                optimizePhaseSP();
                SerialM.print("OSR ");
                SerialM.print(rto->osr);
                SerialM.println("x");
                rto->phaseIsSet = 0; // do it again in modes applicable
            } break;
            case 'g':
                inputStage++;
                // we have a multibyte command
                if (inputStage > 0) {
                    if (inputStage == 1) {
                        segmentCurrent = Serial.parseInt();
                        SerialM.print("G");
                        SerialM.print(segmentCurrent);
                    } else if (inputStage == 2) {
                        char szNumbers[3];
                        szNumbers[0] = Serial.read();
                        szNumbers[1] = Serial.read();
                        szNumbers[2] = '\0';
                        //char * pEnd;
                        registerCurrent = strtol(szNumbers, NULL, 16);
                        SerialM.print("R");
                        SerialM.print(registerCurrent, HEX);
                        if (segmentCurrent <= 5) {
                            writeOneByte(0xF0, segmentCurrent);
                            readFromRegister(registerCurrent, 1, &readout);
                            SerialM.print(" value: 0x");
                            SerialM.println(readout, HEX);
                        } else {
                            discardSerialRxData();
                            SerialM.println("abort");
                        }
                        inputStage = 0;
                    }
                }
                break;
            case 's':
                inputStage++;
                // we have a multibyte command
                if (inputStage > 0) {
                    if (inputStage == 1) {
                        segmentCurrent = Serial.parseInt();
                        SerialM.print("S");
                        SerialM.print(segmentCurrent);
                    } else if (inputStage == 2) {
                        char szNumbers[3];
                        for (uint8_t a = 0; a <= 1; a++) {
                            // ascii 0x30 to 0x39 for '0' to '9'
                            if ((Serial.peek() >= '0' && Serial.peek() <= '9') ||
                                (Serial.peek() >= 'a' && Serial.peek() <= 'f') ||
                                (Serial.peek() >= 'A' && Serial.peek() <= 'F')) {
                                szNumbers[a] = Serial.read();
                            } else {
                                szNumbers[a] = 0; // NUL char
                                Serial.read();    // but consume the char
                            }
                        }
                        szNumbers[2] = '\0';
                        //char * pEnd;
                        registerCurrent = strtol(szNumbers, NULL, 16);
                        SerialM.print("R");
                        SerialM.print(registerCurrent, HEX);
                    } else if (inputStage == 3) {
                        char szNumbers[3];
                        for (uint8_t a = 0; a <= 1; a++) {
                            if ((Serial.peek() >= '0' && Serial.peek() <= '9') ||
                                (Serial.peek() >= 'a' && Serial.peek() <= 'f') ||
                                (Serial.peek() >= 'A' && Serial.peek() <= 'F')) {
                                szNumbers[a] = Serial.read();
                            } else {
                                szNumbers[a] = 0; // NUL char
                                Serial.read();    // but consume the char
                            }
                        }
                        szNumbers[2] = '\0';
                        //char * pEnd;
                        inputToogleBit = strtol(szNumbers, NULL, 16);
                        if (segmentCurrent <= 5) {
                            writeOneByte(0xF0, segmentCurrent);
                            readFromRegister(registerCurrent, 1, &readout);
                            SerialM.print(" (was 0x");
                            SerialM.print(readout, HEX);
                            SerialM.print(")");
                            writeOneByte(registerCurrent, inputToogleBit);
                            readFromRegister(registerCurrent, 1, &readout);
                            SerialM.print(" is now: 0x");
                            SerialM.println(readout, HEX);
                        } else {
                            discardSerialRxData();
                            SerialM.println("abort");
                        }
                        inputStage = 0;
                    }
                }
                break;
            case 't':
                inputStage++;
                // we have a multibyte command
                if (inputStage > 0) {
                    if (inputStage == 1) {
                        segmentCurrent = Serial.parseInt();
                        SerialM.print("T");
                        SerialM.print(segmentCurrent);
                    } else if (inputStage == 2) {
                        char szNumbers[3];
                        for (uint8_t a = 0; a <= 1; a++) {
                            // ascii 0x30 to 0x39 for '0' to '9'
                            if ((Serial.peek() >= '0' && Serial.peek() <= '9') ||
                                (Serial.peek() >= 'a' && Serial.peek() <= 'f') ||
                                (Serial.peek() >= 'A' && Serial.peek() <= 'F')) {
                                szNumbers[a] = Serial.read();
                            } else {
                                szNumbers[a] = 0; // NUL char
                                Serial.read();    // but consume the char
                            }
                        }
                        szNumbers[2] = '\0';
                        //char * pEnd;
                        registerCurrent = strtol(szNumbers, NULL, 16);
                        SerialM.print("R");
                        SerialM.print(registerCurrent, HEX);
                    } else if (inputStage == 3) {
                        if (Serial.peek() >= '0' && Serial.peek() <= '7') {
                            inputToogleBit = Serial.parseInt();
                        } else {
                            inputToogleBit = 255; // will get discarded next step
                        }
                        SerialM.print(" Bit: ");
                        SerialM.print(inputToogleBit);
                        inputStage = 0;
                        if ((segmentCurrent <= 5) && (inputToogleBit <= 7)) {
                            writeOneByte(0xF0, segmentCurrent);
                            readFromRegister(registerCurrent, 1, &readout);
                            SerialM.print(" (was 0x");
                            SerialM.print(readout, HEX);
                            SerialM.print(")");
                            writeOneByte(registerCurrent, readout ^ (1 << inputToogleBit));
                            readFromRegister(registerCurrent, 1, &readout);
                            SerialM.print(" is now: 0x");
                            SerialM.println(readout, HEX);
                        } else {
                            discardSerialRxData();
                            inputToogleBit = registerCurrent = 0;
                            SerialM.println("abort");
                        }
                    }
                }
                break;
            case '<': {
                if (segmentCurrent != 255 && registerCurrent != 255) {
                    writeOneByte(0xF0, segmentCurrent);
                    readFromRegister(registerCurrent, 1, &readout);
                    writeOneByte(registerCurrent, readout - 1); // also allow wrapping
                    Serial.print("S");
                    Serial.print(segmentCurrent);
                    Serial.print("_");
                    Serial.print(registerCurrent, HEX);
                    readFromRegister(registerCurrent, 1, &readout);
                    Serial.print(" : ");
                    Serial.println(readout, HEX);
                }
            } break;
            case '>': {
                if (segmentCurrent != 255 && registerCurrent != 255) {
                    writeOneByte(0xF0, segmentCurrent);
                    readFromRegister(registerCurrent, 1, &readout);
                    writeOneByte(registerCurrent, readout + 1);
                    Serial.print("S");
                    Serial.print(segmentCurrent);
                    Serial.print("_");
                    Serial.print(registerCurrent, HEX);
                    readFromRegister(registerCurrent, 1, &readout);
                    Serial.print(" : ");
                    Serial.println(readout, HEX);
                }
            } break;
            case '_': {
                uint32_t ticks = FrameSync::getPulseTicks();
                Serial.println(ticks);
            } break;
            case '~':
                goLowPowerWithInputDetection(); // test reset + input detect
                break;
            case 'w': {
                //Serial.flush();
                uint16_t value = 0;
                String what = Serial.readStringUntil(' ');

                if (what.length() > 5) {
                    SerialM.println(F("abort"));
                    inputStage = 0;
                    break;
                }
                if (what.equals("f")) {
                    if (rto->extClockGenDetected) {
                        Serial.print(F("old freqExtClockGen: "));
                        Serial.println((uint32_t)rto->freqExtClockGen);
                        rto->freqExtClockGen = Serial.parseInt();
                        // safety range: 1 - 250 MHz
                        if (rto->freqExtClockGen >= 1000000 && rto->freqExtClockGen <= 250000000) {
                            Si.setFreq(0, rto->freqExtClockGen);
                            rto->clampPositionIsSet = 0;
                            rto->coastPositionIsSet = 0;
                        }
                        Serial.print(F("set freqExtClockGen: "));
                        Serial.println((uint32_t)rto->freqExtClockGen);
                    }
                    break;
                }

                value = Serial.parseInt();
                if (value < 4096) {
                    SerialM.print("set ");
                    SerialM.print(what);
                    SerialM.print(" ");
                    SerialM.println(value);
                    if (what.equals("ht")) {
                        //set_htotal(value);
                        if (!rto->outModeHdBypass) {
                            rto->forceRetime = 1;
                            applyBestHTotal(value);
                        } else {
                            GBS::VDS_HSYNC_RST::write(value);
                        }
                    } else if (what.equals("vt")) {
                        set_vtotal(value);
                    } else if (what.equals("hsst")) {
                        setHSyncStartPosition(value);
                    } else if (what.equals("hssp")) {
                        setHSyncStopPosition(value);
                    } else if (what.equals("hbst")) {
                        setMemoryHblankStartPosition(value);
                    } else if (what.equals("hbsp")) {
                        setMemoryHblankStopPosition(value);
                    } else if (what.equals("hbstd")) {
                        setDisplayHblankStartPosition(value);
                    } else if (what.equals("hbspd")) {
                        setDisplayHblankStopPosition(value);
                    } else if (what.equals("vsst")) {
                        setVSyncStartPosition(value);
                    } else if (what.equals("vssp")) {
                        setVSyncStopPosition(value);
                    } else if (what.equals("vbst")) {
                        setMemoryVblankStartPosition(value);
                    } else if (what.equals("vbsp")) {
                        setMemoryVblankStopPosition(value);
                    } else if (what.equals("vbstd")) {
                        setDisplayVblankStartPosition(value);
                    } else if (what.equals("vbspd")) {
                        setDisplayVblankStopPosition(value);
                    } else if (what.equals("sog")) {
                        setAndUpdateSogLevel(value);
                    } else if (what.equals("ifini")) {
                        GBS::IF_INI_ST::write(value);
                        //Serial.println(GBS::IF_INI_ST::read());
                    } else if (what.equals("ifvst")) {
                        GBS::IF_VB_ST::write(value);
                        //Serial.println(GBS::IF_VB_ST::read());
                    } else if (what.equals("ifvsp")) {
                        GBS::IF_VB_SP::write(value);
                        //Serial.println(GBS::IF_VB_ST::read());
                    } else if (what.equals("vsstc")) {
                        setCsVsStart(value);
                    } else if (what.equals("vsspc")) {
                        setCsVsStop(value);
                    }
                } else {
                    SerialM.println("abort");
                }
            } break;
            case 'x': {
                uint16_t if_hblank_scale_stop = GBS::IF_HBIN_SP::read();
                GBS::IF_HBIN_SP::write(if_hblank_scale_stop + 1);
                SerialM.print("1_26: ");
                SerialM.println((if_hblank_scale_stop + 1), HEX);
            } break;
            case 'X': {
                uint16_t if_hblank_scale_stop = GBS::IF_HBIN_SP::read();
                GBS::IF_HBIN_SP::write(if_hblank_scale_stop - 1);
                SerialM.print("1_26: ");
                SerialM.println((if_hblank_scale_stop - 1), HEX);
            } break;
            case '(': {
                writeProgramArrayNew(ntsc_1920x1080, false);
                doPostPresetLoadSteps();
            } break;
            case ')': {
                writeProgramArrayNew(pal_1920x1080, false);
                doPostPresetLoadSteps();
            } break;
            case 'V': {
                SerialM.print(F("step response "));
                uopt->wantStepResponse = !uopt->wantStepResponse;
                if (uopt->wantStepResponse) {
                    GBS::VDS_UV_STEP_BYPS::write(0);
                    SerialM.println("on");
                } else {
                    GBS::VDS_UV_STEP_BYPS::write(1);
                    SerialM.println("off");
                }
                saveUserPrefs();
            } break;
            case 'S': {
                snapToIntegralFrameRate();
                break;
            }
            case ':':
                externalClockGenSyncInOutRate();
                break;
            case ';':
                externalClockGenResetClock();
                if (rto->extClockGenDetected) {
                    rto->extClockGenDetected = 0;
                    Serial.println(F("ext clock gen bypass"));
                } else {
                    rto->extClockGenDetected = 1;
                    Serial.println(F("ext clock gen active"));
                    externalClockGenSyncInOutRate();
                }
                //{
                //  float bla = 0;
                //  double tickRateHz = ESP.getCpuFreqMHz() * 1000000;
                //  bla = tickRateHz / (double)FrameSync::getPulseTicks();
                //  Serial.println(bla, 5);
                //}
                break;
            default:
                Serial.print(F("unknown command "));
                Serial.println(serialCommand, HEX);
                break;
        }

        delay(1); // give some time to read in eventual next chars

        // a web ui or terminal command has finished. good idea to reset sync lock timer
        // important if the command was to change presets, possibly others
        lastVsyncLock = millis();

        if (!Serial.available()) {
            // in case we handled a Serial or web server command and there's no more extra commands
            // but keep debug view command (resets once called)
            if (serialCommand != 'D') {
                serialCommand = '@';
            }
            handleWiFi(1);
        }
    }

    if (userCommand == '@' && userCommandBuffer.length() > 0) {
        userCommand = userCommandBuffer.charAt(0);
        userCommandBuffer.remove(0, 1);
    }

    if (userCommand != '@') {
        handleType2Command(userCommand);
        userCommand = '@'; // in case we handled web server command
        lastVsyncLock = millis();
        handleWiFi(1);
    }

    // run FrameTimeLock if enabled
    if (uopt->enableFrameTimeLock && rto->sourceDisconnected == false && rto->autoBestHtotalEnabled &&
        rto->syncWatcherEnabled && FrameSync::ready() && millis() - lastVsyncLock > FrameSyncAttrs::lockInterval && rto->continousStableCounter > 20 && rto->noSyncCounter == 0)
    {
        uint16_t htotal = GBS::STATUS_SYNC_PROC_HTOTAL::read();
        uint16_t pllad = GBS::PLLAD_MD::read();

        if (((htotal > (pllad - 3)) && (htotal < (pllad + 3)))) {
            uint8_t debug_backup = GBS::TEST_BUS_SEL::read();
            if (debug_backup != 0x0) {
                GBS::TEST_BUS_SEL::write(0x0);
            }
            //unsigned long startTime = millis();
            fsDebugPrintf("running frame sync, clock gen enabled = %d\n", rto->extClockGenDetected);
            bool success = rto->extClockGenDetected
                ? FrameSync::runFrequency()
                : FrameSync::runVsync(uopt->frameTimeLockMethod);
            if (!success) {
                if (rto->syncLockFailIgnore-- == 0) {
                    FrameSync::reset(uopt->frameTimeLockMethod); // in case run() failed because we lost sync signal
                }
            } else if (rto->syncLockFailIgnore > 0) {
                rto->syncLockFailIgnore = 16;
            }
            //Serial.println(millis() - startTime);

            if (debug_backup != 0x0) {
                GBS::TEST_BUS_SEL::write(debug_backup);
            }
        }
        lastVsyncLock = millis();
    }

    if (rto->syncWatcherEnabled && rto->boardHasPower) {
        if ((millis() - lastTimeInterruptClear) > 3000) {
            GBS::INTERRUPT_CONTROL_00::write(0xfe); // reset except for SOGBAD
            GBS::INTERRUPT_CONTROL_00::write(0x00);
            lastTimeInterruptClear = millis();
        }
    }

    // information mode
    if (rto->printInfos == true) {
        printInfo();
    }

    //uint16_t testbus = GBS::TEST_BUS::read() & 0x0fff;
    //if (testbus >= 0x0FFD){
    //  Serial.println(testbus,HEX);
    //}
    //if (rto->videoIsFrozen && (rto->continousStableCounter >= 2)) {
    //    unfreezeVideo();
    //}

    // syncwatcher polls SP status. when necessary, initiates adjustments or preset changes
    if (rto->sourceDisconnected == false && rto->syncWatcherEnabled == true && (millis() - lastTimeSyncWatcher) > 20) {
        runSyncWatcher();
        lastTimeSyncWatcher = millis();

        // auto adc gain
        if (uopt->enableAutoGain == 1 && !rto->sourceDisconnected && rto->videoStandardInput > 0 && rto->clampPositionIsSet && rto->noSyncCounter == 0 && rto->continousStableCounter > 90 && rto->boardHasPower) {
            uint16_t htotal = GBS::STATUS_SYNC_PROC_HTOTAL::read();
            uint16_t pllad = GBS::PLLAD_MD::read();
            if (((htotal > (pllad - 3)) && (htotal < (pllad + 3)))) {
                uint8_t debugRegBackup = 0, debugPinBackup = 0;
                debugPinBackup = GBS::PAD_BOUT_EN::read();
                debugRegBackup = GBS::TEST_BUS_SEL::read();
                GBS::PAD_BOUT_EN::write(0);    // disable output to pin for test
                GBS::DEC_TEST_SEL::write(1);   // luma and G channel
                GBS::TEST_BUS_SEL::write(0xb); // decimation
                if (GBS::STATUS_INT_SOG_BAD::read() == 0) {
                    runAutoGain();
                }
                GBS::TEST_BUS_SEL::write(debugRegBackup);
                GBS::PAD_BOUT_EN::write(debugPinBackup); // debug output pin back on
            }
        }
    }

    // init frame sync + besthtotal routine
    if (rto->autoBestHtotalEnabled && !FrameSync::ready() && rto->syncWatcherEnabled) {
        if (rto->continousStableCounter >= 10 && rto->coastPositionIsSet &&
            ((millis() - lastVsyncLock) > 500)) {
            if ((rto->continousStableCounter % 5) == 0) { // 5, 10, 15, .., 255
                uint16_t htotal = GBS::STATUS_SYNC_PROC_HTOTAL::read();
                uint16_t pllad = GBS::PLLAD_MD::read();
                if (((htotal > (pllad - 3)) && (htotal < (pllad + 3)))) {
                    runAutoBestHTotal();
                }
            }
        }
    }

    // update clamp + coast positions after preset change // do it quickly
    if ((rto->videoStandardInput <= 14 && rto->videoStandardInput != 0) &&
        rto->syncWatcherEnabled && !rto->coastPositionIsSet) {
        if (rto->continousStableCounter >= 7) {
            if ((getStatus16SpHsStable() == 1) && (getVideoMode() == rto->videoStandardInput)) {
                updateCoastPosition(0);
                if (rto->coastPositionIsSet) {
                    if (videoStandardInputIsPalNtscSd()) {
                        // todo: verify for other csync formats
                        GBS::SP_DIS_SUB_COAST::write(0); // enable 5_3e 5
                        GBS::SP_H_PROTECT::write(0);     // no 5_3e 4
                    }
                }
            }
        }
    }

    // don't exclude modes 13 / 14 / 15 (rgbhv bypass)
    if ((rto->videoStandardInput != 0) && (rto->continousStableCounter >= 4) &&
        !rto->clampPositionIsSet && rto->syncWatcherEnabled) {
        updateClampPosition();
        if (rto->clampPositionIsSet) {
            if (GBS::SP_NO_CLAMP_REG::read() == 1) {
                GBS::SP_NO_CLAMP_REG::write(0);
            }
        }
    }

    // later stage post preset adjustments
    if ((rto->applyPresetDoneStage == 1) &&
        ((rto->continousStableCounter > 35 && rto->continousStableCounter < 45) || // this
         !rto->syncWatcherEnabled))                                                // or that
    {
        if (rto->applyPresetDoneStage == 1) {
            // 2nd chance
            GBS::DAC_RGBS_PWDNZ::write(1); // 2nd chance
            if (!uopt->wantOutputComponent) {
                GBS::PAD_SYNC_OUT_ENZ::write(0); // enable sync out // 2nd chance
            }
            if (!rto->syncWatcherEnabled) {
                updateClampPosition();
                GBS::SP_NO_CLAMP_REG::write(0); // 5_57 0
            }

            if (rto->extClockGenDetected && !uopt->disableExternalClockGenerator && rto->videoStandardInput != 14) {
                // Use external clock if detected and enabled in options
                if (!rto->outModeHdBypass) {
                    if (GBS::PLL648_CONTROL_01::read() != 0x75) {
                        // Store current clock if it's not our target or internal default
                        if (GBS::PLL648_CONTROL_01::read() != 0x35) {
                            GBS::GBS_PRESET_DISPLAY_CLOCK::write(GBS::PLL648_CONTROL_01::read());
                        }
                        
                        // Check lock for logging purposes only
                        int st = -999;
                        Wire.beginTransmission(SIADDR);
                        Wire.write(0x00);
                        if (Wire.endTransmission() == 0) {
                            int n = (int)Wire.requestFrom((uint8_t)SIADDR, (size_t)1, false);
                            if (n == 1) st = Wire.read();
                        }
                        // Relaxed check: ignore SYS_INIT (bit7) for monitoring.
                        bool siLocked = (st >= 0) && ((st & 0x20) == 0);
                        
                        static uint32_t lastSyncLog = 0;
                        if (!siLocked && (millis() - lastSyncLog > 1000)) {
                            SerialM.printf("Si5351 not locked (st:0x%02X), continuing with external clock.\n", st);
                            lastSyncLog = millis();
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
            } else if (rto->extClockGenDetected && uopt->disableExternalClockGenerator && GBS::PLL648_CONTROL_01::read() == 0x75) {
                // User changed option to disabled: revert to internal clock
                GBS::PLL648_CONTROL_01::write(0x35);
                SerialM.println(F("External clock disabled by user, switching to internal."));
            }
            rto->applyPresetDoneStage = 0;
        }
    } else if (rto->applyPresetDoneStage == 1 && (rto->continousStableCounter > 35)) {
        // 3rd chance
        GBS::DAC_RGBS_PWDNZ::write(1); // enable DAC // 3rd chance
        if (!uopt->wantOutputComponent) {
            GBS::PAD_SYNC_OUT_ENZ::write(0); // enable sync out // 3rd chance
        }

        // sync clocks now
        externalClockGenSyncInOutRate();
        rto->applyPresetDoneStage = 0; // timeout
    }

    if (rto->applyPresetDoneStage == 10) {
        rto->applyPresetDoneStage = 11; // set first, so we don't loop applying presets
        setOutModeHdBypass(false);
    }

    if (rto->syncWatcherEnabled == true && rto->sourceDisconnected == true && rto->boardHasPower) {
        if ((millis() - lastTimeSourceCheck) >= 500) {
            if (checkBoardPower()) {
                inputAndSyncDetect(); // source is off or just started; keep looking for new input
            } else {
                rto->boardHasPower = false;
                rto->continousStableCounter = 0;
                rto->syncWatcherEnabled = false;
            }
            lastTimeSourceCheck = millis();

            // vary SOG slicer level from 2 to 6
            uint8_t currentSOG = GBS::ADC_SOGCTRL::read();
            if (currentSOG >= 3) {
                rto->currentLevelSOG = currentSOG - 1;
                GBS::ADC_SOGCTRL::write(rto->currentLevelSOG);
            } else {
                rto->currentLevelSOG = 6;
                GBS::ADC_SOGCTRL::write(rto->currentLevelSOG);
            }
        }
    }

    // has the GBS board lost power? // check at 2 points, in case one doesn't register
    // low values chosen to not do this check on small sync issues
    if ((rto->noSyncCounter == 61 || rto->noSyncCounter == 62) && rto->boardHasPower) {
        if (!checkBoardPower()) {
            rto->noSyncCounter = 1; // some neutral "no sync" value
            rto->boardHasPower = false;
            rto->continousStableCounter = 0;
            //rto->syncWatcherEnabled = false;
            stopWire(); // sets pinmodes SDA, SCL to INPUT
        } else {
            rto->noSyncCounter = 63; // avoid checking twice
        }
    }

    // power good now? // added syncWatcherEnabled check to enable passive mode
    // (passive mode = watching OFW without interrupting)
    if (!rto->boardHasPower && rto->syncWatcherEnabled) { // then check if power has come on
        if (digitalRead(SCL) && digitalRead(SDA)) {
            delay(50);
            if (digitalRead(SCL) && digitalRead(SDA)) {
                Serial.println(F("power good"));
                delay(350); // i've seen the MTV230 go on briefly on GBS power cycle
                startWire();
                {
                    // run some dummy commands to init I2C
                    GBS::SP_SOG_MODE::read();
                    GBS::SP_SOG_MODE::read();
                    writeOneByte(0xF0, 0);
                    writeOneByte(0x00, 0); // update cached segment
                    GBS::STATUS_00::read();
                }
                rto->syncWatcherEnabled = true;
                rto->boardHasPower = true;
                delay(100);
                goLowPowerWithInputDetection();
            }
        }
    }

#ifdef HAVE_PINGER_LIBRARY
    // periodic pings for debugging WiFi issues
    if (WiFi.status() == WL_CONNECTED) {
        if (rto->enableDebugPings && millis() - pingLastTime > 1000) {
            // regular interval pings
            if (pinger.Ping(WiFi.gatewayIP(), 1, 750) == false) {
                Serial.println("Error during last ping command.");
            }
            pingLastTime = millis();
        }
    }
#endif
}

#if defined(ESP8266) || defined(ESP32)
// Generated by webui/scripts/html2h.sh from webui/build/webui.html


//void webSocketEvent(uint8_t num, uint8_t type, uint8_t * payload, size_t length) {
//  switch (type) {
//  case WStype_DISCONNECTED:
//    //Serial.print("WS: #"); Serial.print(num); Serial.print(" disconnected,");
//    //Serial.print(" remaining: "); Serial.println(webSocket.connectedClients());
//  break;
//  case WStype_CONNECTED:
//    //Serial.print("WS: #"); Serial.print(num); Serial.print(" connected, ");
//    //Serial.print(" total: "); Serial.println(webSocket.connectedClients());
//    updateWebSocketData();
//  break;
//  case WStype_PONG:
//    //Serial.print("p");
//    updateWebSocketData();
//  break;
//  }
//}
#endif // defined(ESP8266) || defined(ESP32)

