## About This gbs-control Fork

This fork is adapted for building and flashing exclusively through PlatformIO. This is my attempt to improve the original repository. My main motivation was to stabilize WiFi issues that were present in the original codebase but for now the list of changes keeps growing. Most of the changes are “vibe coded” which may make the code and comments seem strange at times, but I am doing my best to test everything thoroughly.

Any suggestions or issues are welcome. 

**Latest releases available at:** https://github.com/Kwakx/gbs-control-nx/releases

**Supported Platforms:**
- **ESP8266**
- **ESP32**: Only classic ESP32 and its variants (ESP32-WROOM-32, ESP32-DevKitC, and compatible boards)

**⚠️ Not Supported ESP32 Variants:**
- ESP32-S3-DevKitC:
- ESP32-C3/C6/S2/H2

For pin connections and wiring information, see **[PINOUT.md](PINOUT.md)**.


## Building and Installation

**Important Notes:**
- **Do not use the original Arduino IDE installation instructions** - This fork is adapted exclusively for PlatformIO
- **PlatformIO automatically downloads the latest required libraries** for the project - no manual library installation needed

This project uses PlatformIO for building and flashing. Follow these steps to set up your development environment:

1. **Install Visual Studio Code**
   - Download and install from: https://code.visualstudio.com/

2. **Install PlatformIO Extension**
   - Open VS Code
   - Go to Extensions (Ctrl+Shift+X)
   - Search for "PlatformIO IDE" and install it
   - Restart VS Code after installation

3. **Build and Upload**
   - Open the project folder in VS Code
   - Open PlatformIO PROJECT TASKS sidebar (PlatformIO icon in the left sidebar)
   - Navigate to your project environment:
     - **ESP8266**: Select `gbsc` environment
     - **ESP32**: Select `gbsc_esp32` environment
   - Follow these steps in order:
     1. **General** → **Build** - Compile the firmware
     2. **Platform** → **Erase Flash** - Erase the flash (recommended for clean install)
     3. **General** → **Upload** - Flash the firmware to your board

For detailed documentation, visit: https://docs.platformio.org/en/latest/integration/ide/vscode.html#installation

---

Original gbs-control documentation: https://ramapcsx2.github.io/gbs-control/, oiginal repo: https://github.com/ramapcsx2/gbs-control with information about this project.
