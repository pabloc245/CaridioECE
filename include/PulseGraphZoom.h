#pragma once

#include <stdint.h>

enum { PULSE_GRAPH_ZOOM_LEVEL_COUNT = 5 };

inline uint8_t pulseGraphSampleIntervalTicks(uint8_t zoomLevel) {
  if (zoomLevel >= PULSE_GRAPH_ZOOM_LEVEL_COUNT) {
    zoomLevel = PULSE_GRAPH_ZOOM_LEVEL_COUNT - 1;
  }
  return (uint8_t)(2U << zoomLevel);
}

inline uint8_t pulseGraphWindowTenths(uint8_t zoomLevel,
                                     uint8_t sampleCount) {
  if (sampleCount < 2) return 0;
  const uint32_t durationMs =
      (uint32_t)(sampleCount - 1) *
      pulseGraphSampleIntervalTicks(zoomLevel) * 2;
  return (uint8_t)((durationMs + 50) / 100);
}

inline uint8_t pulseGraphTimeLabelTenths(uint8_t zoomLevel,
                                         uint8_t sampleCount,
                                         uint8_t markIndex) {
  if (markIndex > 3) markIndex = 3;
  return (uint8_t)((pulseGraphWindowTenths(zoomLevel, sampleCount) *
                        markIndex +
                    1) /
                   3);
}
