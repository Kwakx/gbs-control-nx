#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>

// GitHub repository configuration
#define GITHUB_REPO_OWNER "Kwakx"
#define GITHUB_REPO_NAME "gbs-control-nx"
#define GITHUB_API_HOST "api.github.com"
#define GITHUB_API_PATH "/repos/" GITHUB_REPO_OWNER "/" GITHUB_REPO_NAME "/releases/latest"

class FirmwareUpdater {
public:
    enum UpdateStatus {
        UPDATE_AVAILABLE,
        UP_TO_DATE,
        CHECK_FAILED,
        WIFI_NOT_CONNECTED,
        DOWNLOAD_FAILED,
        FLASH_FAILED,
        CHECKSUM_FAILED,
        INSUFFICIENT_SPACE,
        SUCCESS
    };

    // Check if a newer version is available on GitHub
    // Returns status and populates latestVersion if update available
    static UpdateStatus checkForUpdate(String& latestVersion);

    // Perform the firmware update with progress callback
    // progressCallback receives percentage (0-100)
    static UpdateStatus performUpdate(void (*progressCallback)(int) = nullptr);

    // Combined check and update operation
    static UpdateStatus checkAndUpdate(void (*progressCallback)(int) = nullptr);

private:
    static String _latestVersion;
    static String _downloadUrl;
    static String _expectedSha256;
    static bool _updateChecked;

    // Parse GitHub API JSON response from stream
    static bool parseReleaseInfo(Stream& stream, String& version, String& downloadUrl, String& expectedSha256);
    
    // Helper to read JSON string value from stream
    static String readJsonString(Stream& stream);
    
    // Resolve GitHub redirect to final CDN URL (avoids double SSL handshake)
    static String resolveRedirect(const String& url);
};

#endif // OTA_UPDATE_H
