#pragma once

#include <stdint.h>

class BpmAverager {
 public:
  BpmAverager() : sum_(0), count_(0), average_(0) {}

  bool add(uint16_t bpm, bool valid) {
    if (!valid || bpm == 0) {
      reset();
      return false;
    }

    sum_ += bpm;
    ++count_;
    if (count_ < kMeasurementsPerRecord) return false;

    average_ = (uint16_t)(sum_ / kMeasurementsPerRecord);
    resetMeasurements();
    return true;
  }

  void reset() {
    resetMeasurements();
    average_ = 0;
  }

  uint8_t count() const { return count_; }
  uint16_t average() const { return average_; }

 private:
  enum { kMeasurementsPerRecord = 10 };

  void resetMeasurements() {
    sum_ = 0;
    count_ = 0;
  }

  uint32_t sum_;
  uint8_t count_;
  uint16_t average_;
};
