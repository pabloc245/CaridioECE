#pragma once

#include <stdint.h>

inline void pulseGraphPushSample(volatile uint8_t *samples, uint8_t capacity,
                                 volatile uint8_t &writeIndex,
                                 volatile uint8_t &storedCount,
                                 uint8_t sample) {
  if (capacity == 0 || writeIndex >= capacity) return;

  samples[writeIndex] = sample;
  ++writeIndex;
  if (writeIndex == capacity) writeIndex = 0;
  if (storedCount < capacity) ++storedCount;
}

inline uint8_t pulseGraphOldestIndex(uint8_t capacity, uint8_t writeIndex,
                                     uint8_t storedCount) {
  if (capacity == 0 || storedCount == 0 || storedCount > capacity ||
      writeIndex >= capacity) {
    return 0;
  }

  return writeIndex >= storedCount
             ? writeIndex - storedCount
             : capacity - (storedCount - writeIndex);
}

inline int16_t pulseGraphMapX(uint8_t index, uint8_t sampleCount,
                              int16_t plotLeft, uint8_t plotWidth) {
  if (sampleCount == 0 || plotWidth == 0) return plotLeft;
  if (sampleCount > plotWidth) sampleCount = plotWidth;
  if (index >= sampleCount) index = sampleCount - 1;
  return plotLeft + plotWidth - sampleCount + index;
}

inline void pulseGraphFitRange(uint8_t &minimum, uint8_t &maximum) {
  if (maximum - minimum < 20) {
    const uint8_t middle = (uint8_t)(((uint16_t)maximum + minimum) / 2);
    minimum = middle > 10 ? middle - 10 : 0;
    maximum = middle < 245 ? middle + 10 : 255;
  }
}

inline int16_t pulseGraphMapY(uint8_t sample, uint8_t minimum,
                              uint8_t maximum, int16_t top, int16_t bottom) {
  return bottom -
         ((int16_t)(sample - minimum) * (bottom - top)) / (maximum - minimum);
}
