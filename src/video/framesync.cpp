#include "framesync.h"
#include "../config/Config.h"
#include "../core/Globals.h"
#include "../wifi/WiFiManager.h"

// Define variables for MeasurePeriod namespace
namespace MeasurePeriod {
    volatile uint32_t stopTime = 0;
    volatile uint32_t startTime = 0;
    volatile uint32_t armed = 0;
    volatile uint32_t isrPrepareCount = 0;
    volatile uint32_t isrMeasureCount = 0;

    void IRAM_ATTR _risingEdgeISR_prepare()
    {
        noInterrupts();
        isrPrepareCount++;
#if defined(ESP32)
        // ESP32: use micros() so timing is independent of CPU frequency scaling.
        startTime = (uint32_t)micros();
#else
        // ESP8266: cycle counter is stable at fixed CPU freq (commonly 80/160MHz).
        __asm__ __volatile__("rsr %0,ccount"
                            : "=a"(startTime));
#endif
        detachInterrupt(DEBUG_IN_PIN);
        armed = 1;
        attachInterrupt(DEBUG_IN_PIN, _risingEdgeISR_measure, RISING);
        interrupts();
    }

    void IRAM_ATTR _risingEdgeISR_measure()
    {
        noInterrupts();
        isrMeasureCount++;
#if defined(ESP32)
        stopTime = (uint32_t)micros();
#else
        __asm__ __volatile__("rsr %0,ccount"
                            : "=a"(stopTime));
#endif
        detachInterrupt(DEBUG_IN_PIN);
        interrupts();
    }

    void start() {
        startTime = 0;
        stopTime = 0;
        armed = 0;
        isrPrepareCount = 0;
        isrMeasureCount = 0;
        attachInterrupt(DEBUG_IN_PIN, _risingEdgeISR_prepare, RISING);
    }
}

void setExternalClockGenFrequencySmooth(uint32_t freq) {
    uint32_t current = rto->freqExtClockGen;

    rto->freqExtClockGen = freq;

    constexpr uint32_t STEP_SIZE_HZ = 1000;

    if (current > rto->freqExtClockGen) {
        if ((current - rto->freqExtClockGen) < 750000) {
            while (current > (rto->freqExtClockGen + STEP_SIZE_HZ)) {
                current -= STEP_SIZE_HZ;
                Si.setFreq(0, current);
                handleWiFi(0);
            }
        }
    } else if (current < rto->freqExtClockGen) {
        if ((rto->freqExtClockGen - current) < 750000) {
            while ((current + STEP_SIZE_HZ) < rto->freqExtClockGen) {
                current += STEP_SIZE_HZ;
                Si.setFreq(0, current);
                handleWiFi(0);
            }
        }
    }

    Si.setFreq(0, rto->freqExtClockGen);
}

