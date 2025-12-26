#ifndef SERIAL_MIRROR_H
#define SERIAL_MIRROR_H

#include <Arduino.h>
#include <Stream.h>
#include "WebSocketsServer.h"

// Buffered serial mirror class for websocket logs
// 
// Why buffering?
// - printf() calls write() for each character individually, creating separate WebSocket frames
// - Buffering collects characters and sends them as a single frame (on newline or buffer full)
// - String concatenation would also fragment heap; this 128-byte static buffer is predictable
// - Works automatically for all SerialM.print/println/printf calls
class SerialMirror : public Stream
{
private:
    static constexpr size_t WS_BUFFER_SIZE = 128;
    char wsBuffer[WS_BUFFER_SIZE];
    size_t wsBufferPos = 0;
    unsigned long lastFlushTime = 0;
    static constexpr unsigned long FLUSH_INTERVAL_MS = 50;

    void flushToWebSocket();
    void addToBuffer(char c);

public:
    size_t write(const uint8_t *data, size_t size);
    size_t write(const char *data, size_t size);
    size_t write(uint8_t data);
    size_t write(char data);
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }
    void flush();
};

extern SerialMirror SerialM;

#endif // SERIAL_MIRROR_H

