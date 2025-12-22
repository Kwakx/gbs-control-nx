# GBS-Control Pinout

This document outlines the pin connections for both ESP8266 and ESP32 platforms.

## Supported ESP32 Models
- **ESP32-WROOM-32** (and modules based on it, e.g., ESP32-DevKitC)
- Target Board in PlatformIO: `esp32dev`

## Pin Mapping Table

| Function | ESP8266 | ESP32 (DevKitC/WROOM-32) | Notes |
| :--- | :--- | :--- | :--- |
| **I2C SDA** | D2 (GPIO4) | GPIO21 | Connect to GBS I2C SDA |
| **I2C SCL** | D1 (GPIO5) | GPIO22 | Connect to GBS I2C SCL |
| **Encoder CLK** | D5 (GPIO14) | GPIO33 | Rotary Encoder Clock (A) |
| **Encoder DT** | D7 (GPIO13) | GPIO32 | Rotary Encoder Data (B) |
| **Encoder SW** | D3 (GPIO0) | GPIO14 | Rotary Encoder Switch |
| **DebugPin** | D6 (GPIO12) | GPIO27 | Input Sync Detection (Debug) |

