# gbs-control

## About This Fork

This is a modified fork adapted for building and flashing exclusively through PlatformIO. This is my attempt to improve the original repository. My main motivation was to stabilize WiFi issues that were present in the original codebase. I'm not submitting merge requests to the original repo due to vibe coding. I test this fork on a GBSC board purchased from AliExpress.

**Latest releases available at:** https://github.com/Kwakx/gbs-control-nx/releases

---

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

3. **Configure Your Board**
   - Open the project in VS Code
   - Edit `platformio.ini` file
   - Change the `board` setting to match your specific ESP8266 board model
   - Save the configuration file

4. **Build and Upload**
   - Open PlatformIO PROJECT TASKS sidebar (PlatformIO icon in the left sidebar)
   - Navigate to your project environment (e.g., "gbsc")
   - Follow these steps in order:
     1. **General** → **Build** - Compile the firmware
     2. **Platform** → **Erase Flash** - Erase the flash (recommended for clean install)
     3. **General** → **Upload** - Flash the firmware to your board

For detailed documentation, visit: https://docs.platformio.org/en/latest/integration/ide/vscode.html#installation

---

Documentation: https://ramapcsx2.github.io/gbs-control/

Gbscontrol is an alternative firmware for Tvia Trueview5725 based upscalers / video converter boards.  
Its growing list of features includes:   
- very low lag
- sharp and defined upscaling, comparing well to other -expensive- units
- no synchronization loss switching 240p/480i (output runs independent from input, sync to display never drops)
- on demand motion adaptive deinterlacer that engages automatically and only when needed
- works with almost anything: 8 bit consoles, 16/32 bit consoles, 2000s consoles, home computers, etc
- little compromise, eventhough the hardware is very affordable (less than $30 typically)
- lots of useful features and image enhancements
- optional control interface via web browser, utilizing the ESP8266 WiFi capabilities
- good color reproduction with auto gain and auto offset for the tripple 8 bit @ 160MHz ADC
- optional bypass capability to, for example, transcode Component to RGB/HV in high quality
 
Supported standards are NTSC / PAL, the EDTV and HD formats, as well as VGA from 192p to 1600x1200 (earliest DOS, home computers, PC).
Sources can be connected via RGB/HV (VGA), RGBS (game consoles, SCART) or Component Video (YUV).
Various variations are supported, such as the PlayStation 2's VGA modes that run over Component cables.

Gbscontrol is a continuation of previous work by dooklink, mybook4, Ian Stedman and others.  

Bob from RetroRGB did an overview video on the project. This is a highly recommended watch!   
https://www.youtube.com/watch?v=fmfR0XI5czI

Development threads:  
https://shmups.system11.org/viewtopic.php?f=6&t=52172   
https://circuit-board.de/forum/index.php/Thread/15601-GBS-8220-Custom-Firmware-in-Arbeit/   
