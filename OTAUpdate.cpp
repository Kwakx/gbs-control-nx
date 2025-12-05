#include "OTAUpdate.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Updater.h>
#include <bearssl/bearssl_hash.h>
#include <time.h>

// Static member initialization
String FirmwareUpdater::_latestVersion = "";
String FirmwareUpdater::_downloadUrl = "";
String FirmwareUpdater::_expectedSha256 = "";
bool FirmwareUpdater::_updateChecked = false;

extern const char* FIRMWARE_VERSION;
extern void SerialM_print(const char* str);
extern void SerialM_println(const char* str);

FirmwareUpdater::UpdateStatus FirmwareUpdater::checkForUpdate(String& latestVersion) {
    _updateChecked = false;
    _latestVersion = "";
    _downloadUrl = "";
    _expectedSha256 = "";

    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[OTA] WiFi not connected"));
        return WIFI_NOT_CONNECTED;
    }

    Serial.print(F("[OTA] WiFi connected, IP: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("[OTA] Free heap: "));
    Serial.println(ESP.getFreeHeap());

    // Ensure time is set for SSL certificate validation
    time_t now = time(nullptr);
    if (now < 8 * 3600 * 2) { // If time is not set (less than 16 hours from epoch)
        Serial.println(F("[OTA] Syncing time with NTP..."));
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        
        // Wait up to 5 seconds for time sync
        int timeout = 50; // 5 seconds
        while (time(nullptr) < 8 * 3600 * 2 && timeout > 0) {
            delay(100);
            timeout--;
        }
        
        now = time(nullptr);
        if (now < 8 * 3600 * 2) {
            Serial.println(F("[OTA] Failed to sync time - SSL may fail"));
        } else {
            Serial.print(F("[OTA] Time synced: "));
            Serial.println(ctime(&now));
        }
    }

    WiFiClientSecure client;
    HTTPClient https;

    // Use insecure mode to avoid certificate validation issues
    // GitHub's certificates change frequently and cert bundles are memory-intensive
    client.setInsecure();
    
    // Set buffer sizes to save memory
    client.setBufferSizes(512, 512);

    Serial.println(F("[OTA] Checking for updates..."));
    
    String url = String("https://") + GITHUB_API_HOST + GITHUB_API_PATH;
    Serial.print(F("[OTA] URL: "));
    Serial.println(url);
    
    if (!https.begin(client, url)) {
        Serial.println(F("[OTA] Failed to begin HTTPS connection"));
        return CHECK_FAILED;
    }

    // Configure HTTP client
    https.addHeader("User-Agent", "GBS-Control");
    https.addHeader("Accept", "application/vnd.github.v3+json");
    https.setTimeout(15000); // 15 second timeout
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Follow redirects
    https.setRedirectLimit(5); // Max 5 redirects

    Serial.println(F("[OTA] Sending GET request..."));
    int httpCode = https.GET();

    Serial.print(F("[OTA] HTTP code: "));
    Serial.println(httpCode);

    if (httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_MOVED_PERMANENTLY && httpCode != HTTP_CODE_FOUND) {
        Serial.print(F("[OTA] HTTP error: "));
        Serial.println(httpCode);
        if (httpCode < 0) {
            Serial.println(F("[OTA] Connection failed"));
        }
        https.end();
        return CHECK_FAILED;
    }

    String payload = https.getString();
    https.end();

    // Parse the JSON response
    if (!parseReleaseInfo(payload, _latestVersion, _downloadUrl, _expectedSha256)) {
        Serial.println(F("[OTA] Failed to parse release info"));
        return CHECK_FAILED;
    }
    
    // SHA256 digest is required
    if (_expectedSha256.isEmpty()) {
        Serial.println(F("[OTA] Error: SHA256 digest not available in API"));
        return CHECK_FAILED;
    }
    
    Serial.print(F("[OTA] Expected SHA256: "));
    Serial.println(_expectedSha256);

    latestVersion = _latestVersion;
    _updateChecked = true;

    Serial.print(F("[OTA] Current version: "));
    Serial.println(FIRMWARE_VERSION);
    Serial.print(F("[OTA] Latest version: "));
    Serial.println(_latestVersion);

    // Compare versions
    // Note: We use simple string comparison (==) instead of semantic version comparison.
    // This allows rollback scenarios: if a newer version has critical bugs, you can
    // change the "latest" GitHub release to an older stable version, and users will
    // be able to update/downgrade to it. Any version mismatch triggers UPDATE_AVAILABLE.
    if (_latestVersion == FIRMWARE_VERSION) {
        Serial.println(F("[OTA] Already up to date"));
        return UP_TO_DATE;
    }

    Serial.println(F("[OTA] Update available!"));
    Serial.print(F("[OTA] Download URL: "));
    Serial.println(_downloadUrl);
    
    return UPDATE_AVAILABLE;
}

// Helper function to resolve GitHub redirect to final CDN URL
// Uses manual header parsing to avoid large memory allocations from HTTPClient
String FirmwareUpdater::resolveRedirect(const String& url) {
    Serial.println(F("[OTA] Resolving redirect manually..."));
    Serial.print(F("[OTA] Original URL: "));
    Serial.println(url);
    Serial.print(F("[OTA] Free heap before redirect: "));
    Serial.println(ESP.getFreeHeap());
    
    String currentUrl = url;
    int redirectCount = 0;
    const int maxRedirects = 5;
    
    while (redirectCount < maxRedirects) {
        //  Parse URL
        String host, path;
        int port = 443;
        
        if (currentUrl.startsWith("https://")) {
            currentUrl.remove(0, 8); // Remove "https://"
        } else if (currentUrl.startsWith("http://")) {
            currentUrl.remove(0, 7); // Remove "http://"
            port = 80;
        }
        
        int slashIndex = currentUrl.indexOf('/');
        if (slashIndex > 0) {
            host = currentUrl.substring(0, slashIndex);
            path = currentUrl.substring(slashIndex);
        } else {
            host = currentUrl;
            path = "/";
        }
        
        Serial.print(F("[OTA] Connecting to: "));
        Serial.println(host);
        
        WiFiClientSecure client;
        client.setInsecure();
        client.setBufferSizes(256, 256); // Small buffers
        client.setTimeout(10000);
        
        if (!client.connect(host.c_str(), port)) {
            Serial.println(F("[OTA] Connection failed"));
            return url; // Return original on error
        }
        
        // Send HEAD request to get headers only
        // Send in chunks to avoid large string allocation
        client.print(F("HEAD "));
        client.print(path);
        client.print(F(" HTTP/1.1\r\n"));
        client.print(F("Host: "));
        client.print(host);
        client.print(F("\r\n"));
        client.print(F("User-Agent: GBS-Control\r\n"));
        client.print(F("Connection: close\r\n\r\n"));
        
        // Read response headers
        bool foundRedirect = false;
        String location = "";
        
        while (client.connected() || client.available()) {
            if (client.available()) {
                String line = client.readStringUntil('\n');
                line.trim();
                
                if (line.startsWith("Location:") || line.startsWith("location:")) {
                    location = line.substring(9);
                    location.trim();
                    foundRedirect = true;
                    Serial.print(F("[OTA] Redirect to: "));
                    Serial.println(location);
                }
                
                if (line.length() == 0) {
                    break; // End of headers
                }
            }
        }
        
        client.stop();
        yield();
        
        if (foundRedirect && location.length() > 0) {
            currentUrl = location;
            redirectCount++;
        } else {
            // No redirect - this is final URL
            String finalUrl = String("https://") + host + path;
            Serial.print(F("[OTA] Final URL: "));
            Serial.println(finalUrl);
            Serial.print(F("[OTA] Free heap after redirect: "));
            Serial.println(ESP.getFreeHeap());
            return finalUrl;
        }
    }
    
    Serial.println(F("[OTA] Too many redirects"));
    return url; // Return original if too many redirects
}

FirmwareUpdater::UpdateStatus FirmwareUpdater::performUpdate(void (*progressCallback)(int)) {
    if (!_updateChecked || _downloadUrl.isEmpty()) {
        Serial.println(F("[OTA] Must check for update first"));
        return CHECK_FAILED;
    }

    Serial.println(F("[OTA] Starting update..."));
    Serial.print(F("[OTA] Initial URL: "));
    Serial.println(_downloadUrl);
    
    String finalUrl = resolveRedirect(_downloadUrl);
    Serial.print(F("[OTA] Final Download URL (HTTPS): "));
    Serial.println(finalUrl);
    
    // Convert HTTPS to HTTP to avoid SSL memory issues
    if (finalUrl.startsWith("https://")) {
        finalUrl.replace("https://", "http://");
        Serial.print(F("[OTA] Converted to HTTP: "));
        Serial.println(finalUrl);
    }
    
    // Parse host and path from final URL
    String host;
    String path;
    int slashIndex = finalUrl.indexOf('/', 7);
    if (slashIndex > 0) {
        host = finalUrl.substring(7, slashIndex);
        path = finalUrl.substring(slashIndex);
    } else {
        Serial.println(F("[OTA] Invalid final URL"));
        return DOWNLOAD_FAILED;
    }
    
    // Free memory before download
    finalUrl = "";
    String expectedSha256 = _expectedSha256; // Save SHA256 before clearing
    _downloadUrl = "";
    _latestVersion = "";
    _expectedSha256 = "";
    
    Serial.print(F("[OTA] Free heap before update: "));
    Serial.println(ESP.getFreeHeap());

    ESP.wdtDisable();
    Serial.println(F("[OTA] Watchdog disabled"));

    WiFiClient client;
    client.setTimeout(60000);
    
    Serial.print(F("[OTA] Connecting to: "));
    Serial.print(host);
    Serial.println(F(":80 (HTTP)"));
    
    if (!client.connect(host, 80)) {
        Serial.println(F("[OTA] Connection failed"));
        ESP.wdtEnable(0);
        return DOWNLOAD_FAILED;
    }
    
    Serial.println(F("[OTA] Requesting firmware..."));
    
    client.print(F("GET "));
    client.print(path);
    client.print(F(" HTTP/1.1\r\n"));
    client.print(F("Host: "));
    client.print(host);
    client.print(F("\r\n"));
    client.print(F("User-Agent: GBS-Control\r\n"));
    client.print(F("Connection: close\r\n\r\n"));
    
    path = "";
    host = "";
    
    // Read headers
    int contentLength = -1;
    bool headersEnded = false;
    int httpCode = 0;
    
    while (client.connected() || client.available()) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();
            
            if (line.startsWith("HTTP/1.")) {
                httpCode = line.substring(9, 12).toInt();
                Serial.print(F("[OTA] HTTP Code: "));
                Serial.println(httpCode);
            }
            
            if (line.startsWith("Content-Length:") || line.startsWith("content-length:")) {
                contentLength = line.substring(15).toInt();
            }
            
            if (line.length() == 0) {
                headersEnded = true;
                break;
            }
        }
        yield();
    }
    
    if (!headersEnded || httpCode != 200 || contentLength <= 0) {
        Serial.println(F("[OTA] Invalid response or headers"));
        Serial.print(F("[OTA] HTTP Code: ")); Serial.println(httpCode);
        Serial.print(F("[OTA] Content Length: ")); Serial.println(contentLength);
        client.stop();
        ESP.wdtEnable(0);
        return DOWNLOAD_FAILED;
    }
    
    Serial.print(F("[OTA] Content length: "));
    Serial.println(contentLength);
    
    if (!Update.begin(contentLength)) {
        Serial.print(F("[OTA] Not enough space. Free: "));
        Serial.println(ESP.getFreeSketchSpace());
        client.stop();
        ESP.wdtEnable(0);
        return INSUFFICIENT_SPACE;
    }
    
    Serial.println(F("[OTA] Starting download..."));
    Serial.print(F("[OTA] Free heap: "));
    Serial.println(ESP.getFreeHeap());
    
    // Initialize SHA256 hash calculation using BearSSL (always required)
    br_sha256_context sha256Ctx;
    br_sha256_init(&sha256Ctx);
    Serial.println(F("[OTA] SHA256 verification enabled"));
    
    uint8_t buff[512];
    int written = 0;
    int lastProgress = -1;
    unsigned long lastData = millis();
    
    while (written < contentLength) {
        if (millis() - lastData > 60000) {
            Serial.println(F("[OTA] Download timeout"));
            break;
        }
        
        size_t available = client.available();
        
        if (available) {
            int readBytes = client.readBytes(buff, min((size_t)sizeof(buff), available));
            
            if (readBytes > 0) {
                lastData = millis();
                
                // Update SHA256 hash calculation
                br_sha256_update(&sha256Ctx, buff, readBytes);
                
                size_t bytesWritten = Update.write(buff, readBytes);
                if (bytesWritten != (size_t)readBytes) {
                    Serial.println(F("[OTA] Write error"));
                    Update.printError(Serial);
                    break;
                }
                
                written += readBytes;
                
                int progress = (written * 100) / contentLength;
                if (progress != lastProgress) {
                    lastProgress = progress;
                    
                    // Always call progress callback
                    if (progressCallback) {
                        progressCallback(progress);
                    }
                    
                    // Serial output every 10%
                    if (progress % 10 == 0) {
                        Serial.print(F("[OTA] Progress: "));
                        Serial.print(progress);
                        Serial.print(F("% ("));
                        Serial.print(written);
                        Serial.print(F(" / "));
                        Serial.print(contentLength);
                        Serial.println(F(")"));
                    }
                }
                
                if (written % (contentLength / 100) == 0) {
                    ESP.wdtFeed();
                }
                
                yield();
            }
        } else {
            delay(10);
            yield();
        }
        
        if (!client.connected() && !client.available()) {
            Serial.println(F("[OTA] Connection closed by server"));
            break;
        }
    }
    
    if (written != contentLength) {
        Serial.println(F("[OTA] Download incomplete"));
        client.stop();
        Update.end(false); // Abort update
        ESP.wdtEnable(0);
        return DOWNLOAD_FAILED;
    }
    
    client.stop();
    
    // Verify SHA256 hash (always required)
    uint8_t calculatedHash[32];
    br_sha256_out(&sha256Ctx, calculatedHash);
    
    // Convert calculated hash to hex string
    String calculatedHashHex = "";
    for (int i = 0; i < 32; i++) {
        if (calculatedHash[i] < 0x10) {
            calculatedHashHex += "0";
        }
        calculatedHashHex += String(calculatedHash[i], HEX);
    }
    calculatedHashHex.toLowerCase();
    
    Serial.print(F("[OTA] Calculated SHA256: "));
    Serial.println(calculatedHashHex);
    Serial.print(F("[OTA] Expected SHA256:   "));
    Serial.println(expectedSha256);
    
    if (calculatedHashHex != expectedSha256) {
        Serial.println(F("[OTA] SHA256 checksum mismatch! Aborting update."));
        Update.end(false); // Abort update
        ESP.wdtEnable(0);
        return CHECKSUM_FAILED;
    }
    
    Serial.println(F("[OTA] SHA256 checksum verified"));
    
    if (!Update.end()) {
        Serial.println(F("[OTA] Update end failed"));
        Update.printError(Serial);
        ESP.wdtEnable(0);
        return FLASH_FAILED;
    }
    
    if (!Update.isFinished()) {
        Serial.println(F("[OTA] Update not finished"));
        ESP.wdtEnable(0);
        return FLASH_FAILED;
    }
    
    Serial.println(F("[OTA] Update successful!"));
    Serial.println(F("[OTA] Ready to reboot (will be handled by menu)"));
    
    return SUCCESS;
}

FirmwareUpdater::UpdateStatus FirmwareUpdater::checkAndUpdate(void (*progressCallback)(int)) {
    String latestVersion;
    UpdateStatus checkStatus = checkForUpdate(latestVersion);
    
    if (checkStatus == UPDATE_AVAILABLE) {
        return performUpdate(progressCallback);
    }
    
    return checkStatus;
}

bool FirmwareUpdater::parseReleaseInfo(const String& json, String& version, String& downloadUrl, String& expectedSha256) {
    // Extract tag_name (version)
    version = extractJsonString(json, "tag_name");
    if (version.isEmpty()) {
        return false;
    }

    // Find the firmware.bin asset in the assets array
    int assetsStart = json.indexOf("\"assets\":");
    if (assetsStart == -1) {
        return false;
    }

    int assetArrayStart = json.indexOf('[', assetsStart);
    int assetArrayEnd = json.indexOf(']', assetArrayStart);
    
    if (assetArrayStart == -1 || assetArrayEnd == -1) {
        return false;
    }

    String assetsSection = json.substring(assetArrayStart, assetArrayEnd);

    // Look for firmware.bin asset
    int nameIndex = assetsSection.indexOf("\"name\":\"firmware.bin\"");
    if (nameIndex == -1) {
        Serial.println(F("[OTA] firmware.bin not found in assets"));
        return false;
    }

    // Find browser_download_url after the firmware.bin name
    int urlStart = assetsSection.indexOf("\"browser_download_url\":", nameIndex);
    if (urlStart == -1) {
        return false;
    }

    downloadUrl = extractJsonString(assetsSection.substring(urlStart), "browser_download_url");
    
    // Find digest (SHA256) after the firmware.bin name
    // Digest format: "sha256:hash" - we need to extract just the hash part
    int digestStart = assetsSection.indexOf("\"digest\":", nameIndex);
    if (digestStart != -1) {
        String digestValue = extractJsonString(assetsSection.substring(digestStart), "digest");
        if (!digestValue.isEmpty() && digestValue.startsWith("sha256:")) {
            // Remove "sha256:" prefix
            expectedSha256 = digestValue.substring(7);
            expectedSha256.toLowerCase(); // Normalize to lowercase for comparison
        }
    }
    
    return !downloadUrl.isEmpty();
}

String FirmwareUpdater::extractJsonString(const String& json, const String& key) {
    String searchKey = "\"" + key + "\":\"";
    int startIndex = json.indexOf(searchKey);
    
    if (startIndex == -1) {
        return "";
    }
    
    startIndex += searchKey.length();
    int endIndex = json.indexOf("\"", startIndex);
    
    if (endIndex == -1) {
        return "";
    }
    
    return json.substring(startIndex, endIndex);
}
