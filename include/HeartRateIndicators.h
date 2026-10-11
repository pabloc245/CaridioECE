#pragma once

#include <stdint.h>

enum HeartRateIndicator : uint8_t {
  HEART_RATE_INDICATOR_OFF,
  HEART_RATE_INDICATOR_YELLOW,
  HEART_RATE_INDICATOR_GREEN,
  HEART_RATE_INDICATOR_RED
};

inline HeartRateIndicator heartRateIndicator(uint16_t bpm, bool reliable) {
  if (!reliable || bpm < 30 || bpm > 240) {
    return HEART_RATE_INDICATOR_OFF;
  }
  if (bpm < 60) return HEART_RATE_INDICATOR_YELLOW;
  if (bpm <= 100) return HEART_RATE_INDICATOR_GREEN;
  return HEART_RATE_INDICATOR_RED;
}
