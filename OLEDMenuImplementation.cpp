#define OSD_TIMEOUT 8000

#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include "OLEDMenuImplementation.h"
#include "OTAUpdate.h"
#include "options.h"
#include "tv5725.h"
#include "slot.h"
#include "src/WebSockets.h"
#include "src/WebSocketsServer.h"
#include "fonts.h"
#include "OSDManager.h"

typedef TV5725<GBS_ADDR> GBS;
extern void applyPresets(uint8_t videoMode);
extern void setOutModeHdBypass(bool bypass);
extern void saveUserPrefs();
extern float getOutputFrameRate();
extern void loadDefaultUserOptions();
extern uint8_t getVideoMode();
extern runTimeOptions *rto;
extern userOptions *uopt;
extern const char *ap_ssid;
extern const char *ap_password;
extern const char *device_hostname_full;
extern const char *FIRMWARE_VERSION;
extern WebSocketsServer webSocket;
extern OLEDMenuManager oledMenu;
extern OSDManager osdManager;
unsigned long oledMenuFreezeStartTime;
unsigned long oledMenuFreezeTimeoutInMS;

bool resolutionMenuHandler(OLEDMenuManager *manager, OLEDMenuItem *item, OLEDMenuNav, bool isFirstTime)
{
    if (!isFirstTime) {
        if (millis() - oledMenuFreezeStartTime >= oledMenuFreezeTimeoutInMS) {
            manager->unfreeze();
        }
        return false;
    }
    oledMenuFreezeTimeoutInMS = 1000; // freeze for 1s
    oledMenuFreezeStartTime = millis();
    OLEDDisplay *display = manager->getDisplay();
    display->clear();
    display->setColor(OLEDDISPLAY_COLOR::WHITE);
    display->setFont(ArialMT_Plain_16);
    display->setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT::TEXT_ALIGN_CENTER);
    display->drawString(OLED_MENU_WIDTH / 2, 16, item->str);
    display->drawXbm((OLED_MENU_WIDTH - TEXT_LOADED_WIDTH) / 2, OLED_MENU_HEIGHT / 2, IMAGE_ITEM(TEXT_LOADED));
    display->display();
    uint8_t videoMode = getVideoMode();
    PresetPreference preset = PresetPreference::Output1080P;
    switch (item->tag) {
        case MT_1280x960:
            preset = PresetPreference::Output960P;
            break;
        case MT1280x1024:
            preset = PresetPreference::Output1024P;
            break;
        case MT1280x720:
            preset = PresetPreference::Output720P;
            break;
        case MT1920x1080:
            preset = PresetPreference::Output1080P;
            break;
        case MT_480s576:
            preset = PresetPreference::Output480P;
            break;
        case MT_DOWNSCALE:
            preset = PresetPreference::OutputDownscale;
            break;
        case MT_BYPASS:
            preset = PresetPreference::OutputCustomized;
            break;
        default:
            break;
    }
    if (videoMode == 0 && GBS::STATUS_SYNC_PROC_HSACT::read()) {
        videoMode = rto->videoStandardInput;
    }
    if (item->tag != MT_BYPASS) {
        uopt->presetPreference = preset;
        rto->useHdmiSyncFix = 1;
        if (rto->videoStandardInput == 14) {
            rto->videoStandardInput = 15;
        } else {
            applyPresets(videoMode);
        }
    } else {
        setOutModeHdBypass(false);
        uopt->presetPreference = preset;
        if (rto->videoStandardInput != 15) {
            rto->autoBestHtotalEnabled = 0;
            if (rto->applyPresetDoneStage == 11) {
                rto->applyPresetDoneStage = 1;
            } else {
                rto->applyPresetDoneStage = 10;
            }
        } else {
            rto->applyPresetDoneStage = 1;
        }
    }
    saveUserPrefs();
    manager->freeze();
    return false;
}
bool presetSelectionMenuHandler(OLEDMenuManager *manager, OLEDMenuItem *item, OLEDMenuNav, bool isFirstTime)
{
    if (!isFirstTime) {
        if (millis() - oledMenuFreezeStartTime >= oledMenuFreezeTimeoutInMS) {
            manager->unfreeze();
        }
        return false;
    }
    OLEDDisplay *display = manager->getDisplay();
    display->clear();
    display->setColor(OLEDDISPLAY_COLOR::WHITE);
    display->setFont(ArialMT_Plain_16);
    display->setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT::TEXT_ALIGN_CENTER);
    display->drawString(OLED_MENU_WIDTH / 2, 16, item->str);
    display->drawXbm((OLED_MENU_WIDTH - TEXT_LOADED_WIDTH) / 2, OLED_MENU_HEIGHT / 2, IMAGE_ITEM(TEXT_LOADED));
    display->display();
    uopt->presetSlot = 'A' + item->tag; // ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~()!*:,
    uopt->presetPreference = PresetPreference::OutputCustomized;
    saveUserPrefs();
    if (rto->videoStandardInput == 14) {
        // vga upscale path: let synwatcher handle it
        rto->videoStandardInput = 15;
    } else {
        // normal path
        applyPresets(rto->videoStandardInput);
    }
    saveUserPrefs();
    manager->freeze();
    oledMenuFreezeTimeoutInMS = 2000;
    oledMenuFreezeStartTime = millis();

    return false;
}
bool presetsCreationMenuHandler(OLEDMenuManager *manager, OLEDMenuItem *item, OLEDMenuNav, bool)
{
    SlotMetaArray slotsObject;
    File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");
    manager->clearSubItems(item);
    int curNumSlot = 0;
    if (slotsBinaryFileRead) {
        slotsBinaryFileRead.read((byte *)&slotsObject, sizeof(slotsObject));
        slotsBinaryFileRead.close();
        for (int i = 0; i < SLOTS_TOTAL; ++i) {
            const SlotMeta &slot = slotsObject.slot[i];
            if (strcmp(EMPTY_SLOT_NAME, slot.name) == 0 || !strlen(slot.name)) {
                continue;
            }
            curNumSlot++;
            if (curNumSlot > OLED_MENU_MAX_SUBITEMS_NUM) {
                break;
            }
            manager->registerItem(item, slot.slot, slot.name, presetSelectionMenuHandler);
        }
    }

    if (curNumSlot > OLED_MENU_MAX_SUBITEMS_NUM) {
        manager->registerItem(item, 0, IMAGE_ITEM(TEXT_TOO_MANY_PRESETS));
    }

    if (!item->numSubItem) {
        manager->registerItem(item, 0, IMAGE_ITEM(TEXT_NO_PRESETS));
    }
    return true;
}
bool resetMenuHandler(OLEDMenuManager *manager, OLEDMenuItem *item, OLEDMenuNav, bool isFirstTime)
{
    if (!isFirstTime) {
        // not precise
        if (millis() - oledMenuFreezeStartTime >= oledMenuFreezeTimeoutInMS) {
            manager->unfreeze();
            ESP.restart();
            return false;
        }
        return false;
    }

    OLEDDisplay *display = manager->getDisplay();
    display->clear();
    display->setColor(OLEDDISPLAY_COLOR::WHITE);
    switch (item->tag) {
        case MT_RESET_GBS:
            display->drawXbm(CENTER_IMAGE(TEXT_RESETTING_GBS));
            break;
        case MT_RESTORE_FACTORY:
            display->drawXbm(CENTER_IMAGE(TEXT_RESTORING));
            break;
        case MT_RESET_WIFI:
            display->drawXbm(CENTER_IMAGE(TEXT_RESETTING_WIFI));
            break;
    }
    display->display();
    // Drop all websocket clients but keep the server running
    webSocket.disconnect();
    delay(50);
    switch (item->tag) {
        case MT_RESET_WIFI:
            // Keep ESP8266 behavior (persistent toggle used to erase creds),
            // but don't use WiFi.persistent() on ESP32 (it changes storage to RAM).
#ifdef ESP8266
            WiFi.persistent(true);
            WiFi.disconnect();
            WiFi.persistent(false);
#else
            // ESP32: erase saved AP config (credentials) explicitly
            WiFi.disconnect(true, true);
#endif
            break;
        case MT_RESTORE_FACTORY:
            loadDefaultUserOptions();
            saveUserPrefs();
            break;
    }
    manager->freeze();
    oledMenuFreezeStartTime = millis();
    oledMenuFreezeTimeoutInMS = 2000; // freeze for 2 seconds
    return false;
}
bool currentSettingHandler(OLEDMenuManager *manager, OLEDMenuItem *, OLEDMenuNav nav, bool isFirstTime)
{
    static unsigned long lastUpdateTime = 0;
    if (isFirstTime) {
        lastUpdateTime = 0;
        oledMenuFreezeStartTime = millis();
        oledMenuFreezeTimeoutInMS = 2000; // freeze for 2 seconds if no input
        manager->freeze();
    } else if (nav != OLEDMenuNav::IDLE) {
        manager->unfreeze();
        return false;
    }
    if (millis() - lastUpdateTime <= 200) {
        return false;
    }
    OLEDDisplay &display = *manager->getDisplay();
    display.clear();
    display.setColor(OLEDDISPLAY_COLOR::WHITE);
    display.setFont(ArialMT_Plain_16);
    if (rto->sourceDisconnected || !rto->boardHasPower) {
        if (millis() - oledMenuFreezeStartTime >= oledMenuFreezeTimeoutInMS) {
            manager->unfreeze();
            return false;
        }
        display.setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT::TEXT_ALIGN_CENTER);
        display.drawXbm(CENTER_IMAGE(TEXT_NO_INPUT));
    } else {
        // TODO translations
        boolean vsyncActive = 0;
        boolean hsyncActive = 0;
        float ofr = getOutputFrameRate();
        uint8_t currentInput = GBS::ADC_INPUT_SEL::read();
        rto->presetID = GBS::GBS_PRESET_ID::read();

        display.setFont(URW_Gothic_L_Book_20);
        display.setTextAlignment(TEXT_ALIGN_LEFT);

        if (rto->presetID == 0x01 || rto->presetID == 0x11) {
            display.drawString(0, 0, "1280x960");
        } else if (rto->presetID == 0x02 || rto->presetID == 0x12) {
            display.drawString(0, 0, "1280x1024");
        } else if (rto->presetID == 0x03 || rto->presetID == 0x13) {
            display.drawString(0, 0, "1280x720");
        } else if (rto->presetID == 0x05 || rto->presetID == 0x15) {
            display.drawString(0, 0, "1920x1080");
        } else if (rto->presetID == 0x06 || rto->presetID == 0x16) {
            display.drawString(0, 0, "Downscale");
        } else if (rto->presetID == 0x04) {
            display.drawString(0, 0, "720x480");
        } else if (rto->presetID == 0x14) {
            display.drawString(0, 0, "768x576");
        } else {
            display.drawString(0, 0, "bypass");
        }

        display.drawString(0, 20, String(ofr, 5) + "Hz");

        if (currentInput == 1) {
            display.drawString(0, 41, "RGB");
        } else {
            display.drawString(0, 41, "YpBpR");
        }

        if (currentInput == 1) {
            vsyncActive = GBS::STATUS_SYNC_PROC_VSACT::read();
            if (vsyncActive) {
                display.drawString(70, 41, "V");
                hsyncActive = GBS::STATUS_SYNC_PROC_HSACT::read();
                if (hsyncActive) {
                    display.drawString(53, 41, "H");
                }
            }
        }
    }
    display.display();
    lastUpdateTime = millis();

    return false;
}
bool wifiMenuHandler(OLEDMenuManager *manager, OLEDMenuItem *item, OLEDMenuNav, bool)
{
    static char ssid[64];
    static char ip[25];
    static char domain[25];
    WiFiMode_t wifiMode = WiFi.getMode();
    manager->clearSubItems(item);
    if (wifiMode == WIFI_STA) {
        sprintf(ssid, "SSID: %s", WiFi.SSID().c_str());
        manager->registerItem(item, 0, ssid);
        if (WiFi.isConnected()) {
            manager->registerItem(item, 0, IMAGE_ITEM(TEXT_WIFI_CONNECTED));
            manager->registerItem(item, 0, IMAGE_ITEM(TEXT_WIFI_URL));
            sprintf(ip, "http://%s", WiFi.localIP().toString().c_str());
            manager->registerItem(item, 0, ip);
            sprintf(domain, "http://%s", device_hostname_full);
            manager->registerItem(item, 0, domain);
        } else {
            // shouldn't happen?
            manager->registerItem(item, 0, IMAGE_ITEM(TEXT_WIFI_DISCONNECTED));
        }
    } else if (wifiMode == WIFI_AP) {
        manager->registerItem(item, 0, IMAGE_ITEM(TEXT_WIFI_CONNECT_TO));
        sprintf(ssid, "SSID: %s (%s)", ap_ssid, ap_password);
        manager->registerItem(item, 0, ssid);
        manager->registerItem(item, 0, IMAGE_ITEM(TEXT_WIFI_URL));
        manager->registerItem(item, 0, "http://192.168.4.1");
        sprintf(domain, "http://%s", device_hostname_full);
        manager->registerItem(item, 0, domain);
    } else {
        // shouldn't happen?
        manager->registerItem(item, 0, IMAGE_ITEM(TEXT_WIFI_DISCONNECTED));
    }
    return true;
}
bool osdMenuHanlder(OLEDMenuManager *manager, OLEDMenuItem *, OLEDMenuNav nav, bool isFirstTime)
{
    static unsigned long start;
    static long left;
    char buf[30];
    auto display = manager->getDisplay();

    if (isFirstTime) {
        left = OSD_TIMEOUT;
        start = millis();
        manager->freeze();
        osdManager.tick(OSDNav::ENTER);
    } else {
        display->clear();
        display->setColor(OLEDDISPLAY_COLOR::WHITE);
        display->setFont(ArialMT_Plain_16);
        display->setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT::TEXT_ALIGN_CENTER);
        display->drawStringf(OLED_MENU_WIDTH / 2, 16, buf, "OSD (%ds)", left / 1000 + 1);
        display->display();
        if (REVERSE_ROTARY_ENCODER_FOR_OLED_MENU){
            // reverse nav back to normal
            if(nav == OLEDMenuNav::DOWN) {
                nav = OLEDMenuNav::UP;
            } else if(nav == OLEDMenuNav::UP) {
                nav = OLEDMenuNav::DOWN;
            }
        }
        switch (nav) {
            case OLEDMenuNav::ENTER:
                osdManager.tick(OSDNav::ENTER);
                start = millis();
                break;
            case OLEDMenuNav::DOWN:
                if(REVERSE_ROTARY_ENCODER_FOR_OSD) {
                    osdManager.tick(OSDNav::RIGHT);
                } else {
                    osdManager.tick(OSDNav::LEFT);
                }
                start = millis();
                break;
            case OLEDMenuNav::UP:
                if(REVERSE_ROTARY_ENCODER_FOR_OSD) {
                    osdManager.tick(OSDNav::LEFT);
                } else {
                    osdManager.tick(OSDNav::RIGHT);
                }
                start = millis();
                break;
            default:
                break;
        }
        left = OSD_TIMEOUT - (millis() - start);
        if (left <= 0) {
            manager->unfreeze();
            osdManager.menuOff();
        }
    }
    return true;
}

// System menu handler - shows firmware version
bool systemMenuHandler(OLEDMenuManager *manager, OLEDMenuItem *item, OLEDMenuNav, bool)
{
    manager->clearSubItems(item);
    
    // Show current firmware version
    extern const char* FIRMWARE_VERSION;
    static char versionText[32];
    sprintf(versionText, "Version: %s", FIRMWARE_VERSION);
    manager->registerItem(item, 0, versionText);
    
    // Add "Check for Update" button
    manager->registerItem(item, MT_CHECK_UPDATE, IMAGE_ITEM(OM_CHECK_UPDATE), firmwareUpdateMenuHandler);
    
    return true;
}

// Firmware update menu handler
bool firmwareUpdateMenuHandler(OLEDMenuManager *manager, OLEDMenuItem *item, OLEDMenuNav nav, bool isFirstTime)
{
    static enum UpdateState {
        STATE_CHECKING,
        STATE_SHOW_RESULT,
        STATE_CONFIRM_UPDATE,
        STATE_DOWNLOADING,
        STATE_COMPLETE,
        STATE_ERROR
    } updateState;
    
    static const int RESTART_COUNTDOWN_SECONDS = 5;
    
    static String latestVersion = "";
    static FirmwareUpdater::UpdateStatus updateStatus;
    static int downloadProgress = 0;
    static unsigned long stateStartTime = 0;
    
    OLEDDisplay *display = manager->getDisplay();
    
    if (isFirstTime) {
        updateState = STATE_CHECKING;
        latestVersion = "";
        downloadProgress = 0;
        stateStartTime = millis();
        manager->freeze();
        
        // Close WebSocket and web server to free memory
        extern WebSocketsServer webSocket;
        extern AsyncWebServer server;
        
        Serial.println(F("[Menu] Stopping web services for update..."));
        // Only drop WS clients (closing/rewrapping the server causes rebind issues)
        webSocket.disconnect();
        // HTTP server must be stopped to free resources / avoid handling requests
        // On ESP8266 only - ESP32 has sufficient resources and stopping/restarting causes bind errors
#ifdef ESP8266
        server.end();
#endif
        
        // Clear display to free frame buffer memory
        display->clear();
        display->display();
        
        // Wait longer for connections to close and memory to be freed
        delay(1000);
        yield();
        
        Serial.print(F("[Menu] Free heap after cleanup: "));
        Serial.println(ESP.getFreeHeap());
    }
    
    display->clear();
    display->setColor(OLEDDISPLAY_COLOR::WHITE);
    display->setFont(ArialMT_Plain_16);
    display->setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT::TEXT_ALIGN_CENTER);
    
    switch (updateState) {
        case STATE_CHECKING:
            display->drawXbm(CENTER_IMAGE(TEXT_CHECKING));
            display->display();
            
            // Check for update
            updateStatus = FirmwareUpdater::checkForUpdate(latestVersion);
            
            if (updateStatus == FirmwareUpdater::UPDATE_AVAILABLE) {
                updateState = STATE_CONFIRM_UPDATE;
            } else {
                updateState = STATE_SHOW_RESULT;
            }
            stateStartTime = millis();
            break;
            
        case STATE_SHOW_RESULT:
            if (updateStatus == FirmwareUpdater::UP_TO_DATE) {
                display->drawXbm(CENTER_IMAGE(TEXT_UP_TO_DATE));
                extern const char* FIRMWARE_VERSION;
                display->setFont(ArialMT_Plain_10);
                display->setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT::TEXT_ALIGN_CENTER);
                display->drawString(OLED_MENU_WIDTH / 2, 36, FIRMWARE_VERSION);
            } else if (updateStatus == FirmwareUpdater::WIFI_NOT_CONNECTED) {
                display->drawXbm(CENTER_IMAGE(TEXT_WIFI_NOT_CONNECTED));
            } else {
                display->drawXbm(CENTER_IMAGE(TEXT_CHECK_FAILED));
            }
            display->display();
            
            // Auto-return after 3 seconds
            if (millis() - stateStartTime > 3000 || nav != OLEDMenuNav::IDLE) {
                // Restart web server; WS server stayed up, only clients were dropped
                extern AsyncWebServer server;
                extern runTimeOptions *rto;
                
                if (rto->webServerEnabled) {
#ifdef ESP8266
                    Serial.println(F("[Menu] Restarting web services..."));
                    server.begin();
#endif
                    rto->webServerStarted = true;
                }
                
                manager->unfreeze();
                return false;
            }
            break;
            
        case STATE_CONFIRM_UPDATE:
            display->drawXbm((OLED_MENU_WIDTH - TEXT_UPDATE_FOUND_WIDTH) / 2, 0, IMAGE_ITEM(TEXT_UPDATE_FOUND));
            display->setFont(ArialMT_Plain_10);
            display->setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT::TEXT_ALIGN_LEFT);
            display->drawString(0, 18, String("Current: ") + String(FIRMWARE_VERSION));
            display->drawString(0, 30, String("Latest: ") + latestVersion);
            display->setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT::TEXT_ALIGN_CENTER);
            display->setFont(ArialMT_Plain_10);
            display->drawString(OLED_MENU_WIDTH / 2, 44, "ENTER = Update");
            display->drawString(OLED_MENU_WIDTH / 2, 54, "DOWN = Cancel");
            display->display();
            
            if (nav == OLEDMenuNav::ENTER) {
                updateState = STATE_DOWNLOADING;
                stateStartTime = millis();
            } else if (nav == OLEDMenuNav::DOWN) {
                // User canceled: bring web services back up
                extern AsyncWebServer server;
                extern runTimeOptions *rto;
                if (rto->webServerEnabled) {
#ifdef ESP8266
                    Serial.println(F("[Menu] Update canceled, restarting web services..."));
                    server.begin();
#endif
                    rto->webServerStarted = true;
                }
                manager->unfreeze();
                return false;
            }
            break;
            
        case STATE_DOWNLOADING: {
            // Start update if not already started
            if (millis() - stateStartTime < 500) {
                display->drawXbm((OLED_MENU_WIDTH - TEXT_DOWNLOADING_WIDTH) / 2, 10, IMAGE_ITEM(TEXT_DOWNLOADING));
                display->setFont(ArialMT_Plain_10);
                
                // Draw initial progress bar
                int barWidth = 100;
                int barHeight = 10;
                int barX = (OLED_MENU_WIDTH - barWidth) / 2;
                int barY = 35;
                display->drawRect(barX, barY, barWidth, barHeight);
                display->drawString(OLED_MENU_WIDTH / 2, 50, "0%");
                display->display();
                break;
            }
            
            if (downloadProgress == 0) {
                // Set static pointers for callback (safe during update as only one update at a time)
                static OLEDMenuManager* callbackManager = nullptr;
                static int* callbackProgress = nullptr;
                callbackManager = manager;
                callbackProgress = &downloadProgress;
                
                // Perform update with progress callback that updates display
                updateStatus = FirmwareUpdater::performUpdate([](int progress) {
                    *callbackProgress = progress;
                    
                    // Update display in real-time
                    OLEDDisplay *display = callbackManager->getDisplay();
                    display->clear();
                    display->setColor(OLEDDISPLAY_COLOR::WHITE);
                    display->setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT::TEXT_ALIGN_CENTER);
                    display->drawXbm((OLED_MENU_WIDTH - TEXT_DOWNLOADING_WIDTH) / 2, 10, IMAGE_ITEM(TEXT_DOWNLOADING));
                    display->setFont(ArialMT_Plain_10);
                    
                    // Draw progress bar
                    int barWidth = 100;
                    int barHeight = 10;
                    int barX = (OLED_MENU_WIDTH - barWidth) / 2;
                    int barY = 35;
                    display->drawRect(barX, barY, barWidth, barHeight);
                    int fillWidth = (barWidth - 2) * progress / 100;
                    if (fillWidth > 0) {
                        display->fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2);
                    }
                    
                    String progressText = String(progress) + "%";
                    display->drawString(OLED_MENU_WIDTH / 2, 50, progressText);
                    display->display();
                });
                
                if (updateStatus == FirmwareUpdater::SUCCESS) {
                    updateState = STATE_COMPLETE;
                    stateStartTime = millis(); // Start countdown timer
                } else {
                    updateState = STATE_ERROR;
                    stateStartTime = millis();
                }
            }
            break;
        }
            
        case STATE_COMPLETE: {
            // Calculate remaining seconds
            unsigned long elapsed = millis() - stateStartTime;
            int remainingSeconds = RESTART_COUNTDOWN_SECONDS - (elapsed / 1000);
            
            if (remainingSeconds <= 0) {
                // Time's up - restart now
                display->clear();
                display->setColor(OLEDDISPLAY_COLOR::WHITE);
                display->drawXbm(CENTER_IMAGE(TEXT_REBOOTING));
                display->display();
                delay(500);
                ESP.restart();
                break;
            }
            
            // Update display with countdown
            display->clear();
            display->setColor(OLEDDISPLAY_COLOR::WHITE);
            display->setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT::TEXT_ALIGN_CENTER);
            display->drawXbm(CENTER_IMAGE(TEXT_UPDATE_SUCCESS));
            display->setFont(ArialMT_Plain_10);
            
            char countdownText[32];
            sprintf(countdownText, "Restart in %d...", remainingSeconds);
            display->drawString(OLED_MENU_WIDTH / 2, 35, countdownText);
            display->display();
            break;
        }
            
        case STATE_ERROR: {
            display->clear();
            display->setColor(OLEDDISPLAY_COLOR::WHITE);
            display->setTextAlignment(OLEDDISPLAY_TEXT_ALIGNMENT::TEXT_ALIGN_CENTER);
            
            // Display specific error message based on update status
            switch (updateStatus) {
                case FirmwareUpdater::WIFI_NOT_CONNECTED:
                    display->drawXbm(CENTER_IMAGE(TEXT_WIFI_NOT_CONNECTED));
                    break;
                case FirmwareUpdater::CHECK_FAILED:
                    display->drawXbm(CENTER_IMAGE(TEXT_CHECK_FAILED));
                    break;
                case FirmwareUpdater::DOWNLOAD_FAILED:
                    display->drawXbm(CENTER_IMAGE(TEXT_DOWNLOAD_FAILED));
                    break;
                case FirmwareUpdater::FLASH_FAILED:
                    display->drawXbm(CENTER_IMAGE(TEXT_FLASH_FAILED));
                    break;
                case FirmwareUpdater::CHECKSUM_FAILED:
                    display->drawXbm(CENTER_IMAGE(TEXT_CHECKSUM_ERROR));
                    display->drawXbm((OLED_MENU_WIDTH - TEXT_SHA256_MISMATCH_WIDTH) / 2, TEXT_CHECKSUM_ERROR_HEIGHT + 10, IMAGE_ITEM(TEXT_SHA256_MISMATCH));
                    break;
                case FirmwareUpdater::INSUFFICIENT_SPACE:
                    display->drawXbm(CENTER_IMAGE(TEXT_NOT_ENOUGH_SPACE));
                    display->drawXbm((OLED_MENU_WIDTH - TEXT_FREE_FLASH_MEMORY_WIDTH) / 2, TEXT_NOT_ENOUGH_SPACE_HEIGHT + 10, IMAGE_ITEM(TEXT_FREE_FLASH_MEMORY));
                    break;
                default:
                    display->drawXbm(CENTER_IMAGE(TEXT_UPDATE_FAILED));
                    display->drawXbm((OLED_MENU_WIDTH - TEXT_UNKNOWN_ERROR_WIDTH) / 2, TEXT_UPDATE_FAILED_HEIGHT + 10, IMAGE_ITEM(TEXT_UNKNOWN_ERROR));
                    break;
            }
            display->display();
            
            if (millis() - stateStartTime > 5000 || nav != OLEDMenuNav::IDLE) {
                // Restart web server; WS server stayed up, only clients were dropped
                extern AsyncWebServer server;
                extern runTimeOptions *rto;
                
                if (rto->webServerEnabled) {
#ifdef ESP8266
                    Serial.println(F("[Menu] Restarting web services after failed update..."));
                    server.begin();
#endif
                    rto->webServerStarted = true;
                }
                
                manager->unfreeze();
                return false;
            }
            break;
        }
    }
    
    return true;
}

void initOLEDMenu()
{
    OLEDMenuItem *root = oledMenu.rootItem;

    // OSD Menu
    oledMenu.registerItem(root, MT_NULL, IMAGE_ITEM(OM_OSD), osdMenuHanlder);

    // Resolutions
    OLEDMenuItem *resMenu = oledMenu.registerItem(root, MT_NULL, IMAGE_ITEM(OM_RESOLUTION));
    const char *resolutions[5] = {"1280x960", "1280x1024", "1280x720", "1920x1080", "480/576"};
    uint8_t tags[5] = {MT_1280x960, MT1280x1024, MT1280x720, MT1920x1080, MT_480s576};
    for (int i = 0; i < 5; ++i) {
        oledMenu.registerItem(resMenu, tags[i], resolutions[i], resolutionMenuHandler);
    }
    // downscale and passthrough
    oledMenu.registerItem(resMenu, MT_DOWNSCALE, IMAGE_ITEM(OM_DOWNSCALE), resolutionMenuHandler);
    oledMenu.registerItem(resMenu, MT_BYPASS, IMAGE_ITEM(OM_PASSTHROUGH), resolutionMenuHandler);

    // Presets
    oledMenu.registerItem(root, MT_NULL, IMAGE_ITEM(OM_PRESET), presetsCreationMenuHandler);

    // WiFi
    oledMenu.registerItem(root, MT_NULL, IMAGE_ITEM(OM_WIFI), wifiMenuHandler);

    // Current Settings
    oledMenu.registerItem(root, MT_NULL, IMAGE_ITEM(OM_CURRENT), currentSettingHandler);

    // System (contains firmware version and update check)
    oledMenu.registerItem(root, MT_NULL, IMAGE_ITEM(OM_SYSTEM), systemMenuHandler);

    // Reset (Misc.)
    OLEDMenuItem *resetMenu = oledMenu.registerItem(root, MT_NULL, IMAGE_ITEM(OM_RESET_RESTORE));
    oledMenu.registerItem(resetMenu, MT_RESET_GBS, IMAGE_ITEM(OM_RESET_GBS), resetMenuHandler);
    oledMenu.registerItem(resetMenu, MT_RESTORE_FACTORY, IMAGE_ITEM(OM_RESTORE_FACTORY), resetMenuHandler);
    oledMenu.registerItem(resetMenu, MT_RESET_WIFI, IMAGE_ITEM(OM_RESET_WIFI), resetMenuHandler);
}
