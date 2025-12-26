#include "MovingAverage.h"

// Feed the current measurement, get back the moving average
uint8_t getMovingAverage(uint8_t item)
{
    static const uint8_t sz = 16;
    static uint8_t arr[sz] = {0};
    static uint8_t pos = 0;

    arr[pos] = item;
    if (pos < (sz - 1)) {
        pos++;
    } else {
        pos = 0;
    }

    uint16_t sum = 0;
    for (uint8_t i = 0; i < sz; i++) {
        sum += arr[i];
    }
    return sum >> 4; // for array size 16
}

