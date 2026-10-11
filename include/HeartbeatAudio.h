#pragma once

#include <stdint.h>

inline uint16_t heartbeatToneFrequency(uint16_t bpm) {
  if (bpm < 30 || bpm > 240) return 0;
  if (bpm < 60) return 220;
  if (bpm <= 100) return 440;
  return 880;
}
