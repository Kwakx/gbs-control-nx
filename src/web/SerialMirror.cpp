#include "SerialMirror.h"
#include "../core/Globals.h"
#include <Arduino.h>

void SerialMirror::flushToWebSocket() {
    if (wsBufferPos > 0 && ESP.getFreeHeap() > 10000) {
        webSocket.broadcastTXT((uint8_t*)wsBuffer, wsBufferPos);
    }
    wsBufferPos = 0;
    lastFlushTime = millis();
}

void SerialMirror::addToBuffer(char c) {
    if (wsBufferPos >= WS_BUFFER_SIZE - 1) {
        flushToWebSocket();
    }
    wsBufferPos++;
    wsBuffer[wsBufferPos - 1] = c;
    
    // Flush on newline or after interval
    if (c == '\n' || (millis() - lastFlushTime) > FLUSH_INTERVAL_MS) {
        flushToWebSocket();
    }
}

size_t SerialMirror::write(const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        addToBuffer((char)data[i]);
    }
    Serial.write(data, size);
    return size;
}

size_t SerialMirror::write(const char *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        addToBuffer(data[i]);
    }
    Serial.write(data, size);
    return size;
}

size_t SerialMirror::write(uint8_t data) {
    addToBuffer((char)data);
    Serial.write(data);
    return 1;
}

size_t SerialMirror::write(char data) {
    addToBuffer(data);
    Serial.write(data);
    return 1;
}

void SerialMirror::flush() { 
    flushToWebSocket(); 
}

// Global instance
SerialMirror SerialM;

