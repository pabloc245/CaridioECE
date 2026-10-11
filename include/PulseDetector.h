#pragma once

#include <stdint.h>

class PulseDetector {
 public:
  PulseDetector()
      : initialized_(false),
        historyIndex_(0),
        filteredQ8_(0),
        baselineQ8_(0),
        amplitudeQ8_(0),
        previousAc_(0),
        tracking_(false),
        candidateStartedAt_(0),
        candidatePeakAt_(0),
        candidatePeak_(0),
        hasLastBeat_(false),
        lastBeatAt_(0),
        intervalCount_(0),
        intervalWriteIndex_(0),
        confident_(false),
        bpm_(0) {
    history_[0] = 0;
    history_[1] = 0;
    history_[2] = 0;
    intervals_[0] = 0;
    intervals_[1] = 0;
    intervals_[2] = 0;
  }

  bool process(uint16_t rawSample, uint32_t timeMs) {
    if (!initialized_) {
      history_[0] = rawSample;
      history_[1] = rawSample;
      history_[2] = rawSample;
      filteredQ8_ = (int32_t)rawSample << 8;
      baselineQ8_ = filteredQ8_;
      initialized_ = true;
      return false;
    }

    history_[historyIndex_] = rawSample;
    if (++historyIndex_ == 3) historyIndex_ = 0;
    const uint16_t sample =
        median3(history_[0], history_[1], history_[2]);
    const int32_t inputQ8 = (int32_t)sample << 8;
    filteredQ8_ += (inputQ8 - filteredQ8_) / 8;
    baselineQ8_ += (filteredQ8_ - baselineQ8_) / 512;
    const int16_t ac = (int16_t)((filteredQ8_ - baselineQ8_) >> 8);
    updateAmplitudeEstimate(ac);

    if (hasLastBeat_ && timeMs - lastBeatAt_ > kStaleTimeoutMs) {
      hasLastBeat_ = false;
      intervalCount_ = 0;
      intervalWriteIndex_ = 0;
      confident_ = false;
    }

    const int16_t threshold = detectionThreshold();
    bool bpmUpdated = false;

    if (!tracking_ && ac >= threshold && previousAc_ < threshold) {
      tracking_ = true;
      candidateStartedAt_ = timeMs;
      candidatePeakAt_ = timeMs;
      candidatePeak_ = ac;
    } else if (tracking_) {
      if (ac > candidatePeak_) {
        candidatePeak_ = ac;
        candidatePeakAt_ = timeMs;
      }

      if (timeMs - candidateStartedAt_ > kMaximumCandidateMs) {
        tracking_ = false;
      } else {
        int16_t confirmationDrop = candidatePeak_ / 4;
        if (confirmationDrop < kMinimumConfirmationDrop) {
          confirmationDrop = kMinimumConfirmationDrop;
        }
        if (ac <= candidatePeak_ - confirmationDrop) {
          tracking_ = false;
          if (timeMs - candidateStartedAt_ >= kMinimumCandidateMs &&
              candidatePeak_ >= kMinimumPeakAmplitude) {
            bpmUpdated = acceptPeak(candidatePeakAt_);
          }
        }
      }
    }

    previousAc_ = ac;
    return bpmUpdated;
  }

  bool isTracking() const { return tracking_; }
  bool isConfident() const { return confident_; }
  uint16_t bpm() const { return bpm_; }

 private:
  enum {
    kMinimumBpm = 30,
    kMaximumBpm = 240,
    kMinimumIntervalMs = 60000 / kMaximumBpm,
    kMaximumIntervalMs = 60000 / kMinimumBpm,
    kStaleTimeoutMs = 5000,
    kMinimumCandidateMs = 20,
    kMaximumCandidateMs = 350,
    kMinimumPeakAmplitude = 12,
    kMinimumConfirmationDrop = 6,
    kIntervalHistorySize = 3,
    kIntervalsForConfidence = 2
  };

  static uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
    if (a > b) {
      const uint16_t temporary = a;
      a = b;
      b = temporary;
    }
    if (b > c) {
      const uint16_t temporary = b;
      b = c;
      c = temporary;
    }
    if (a > b) b = a;
    return b;
  }

  void updateAmplitudeEstimate(int16_t ac) {
    const uint16_t magnitude = ac < 0 ? (uint16_t)-ac : (uint16_t)ac;
    const int32_t targetQ8 = (int32_t)magnitude << 8;
    const int32_t difference = targetQ8 - amplitudeQ8_;
    if (difference > 0) {
      amplitudeQ8_ += difference / 8;
    } else {
      amplitudeQ8_ += difference / 256;
    }
  }

  int16_t detectionThreshold() const {
    int16_t threshold = (int16_t)((amplitudeQ8_ >> 8) / 3);
    if (threshold < kMinimumPeakAmplitude) {
      threshold = kMinimumPeakAmplitude;
    }
    return threshold;
  }

  bool acceptPeak(uint32_t peakAt) {
    if (!hasLastBeat_) {
      hasLastBeat_ = true;
      lastBeatAt_ = peakAt;
      return false;
    }

    const uint32_t interval = peakAt - lastBeatAt_;
    if (interval < kMinimumIntervalMs) {
      lastBeatAt_ = peakAt;
      intervalCount_ = 0;
      intervalWriteIndex_ = 0;
      confident_ = false;
      return false;
    }

    lastBeatAt_ = peakAt;
    if (interval > kMaximumIntervalMs) {
      intervalCount_ = 0;
      intervalWriteIndex_ = 0;
      confident_ = false;
      return false;
    }

    if (intervalCount_ > 0) {
      const uint8_t previousIndex =
          intervalWriteIndex_ == 0 ? kIntervalHistorySize - 1
                                   : intervalWriteIndex_ - 1;
      const uint16_t previousInterval = intervals_[previousIndex];
      const uint32_t difference = interval > previousInterval
                                      ? interval - previousInterval
                                      : previousInterval - interval;
      if (difference * 4 > previousInterval) {
        intervalCount_ = 0;
        intervalWriteIndex_ = 0;
        confident_ = false;
      }
    }

    intervals_[intervalWriteIndex_] = (uint16_t)interval;
    intervalWriteIndex_ =
        (intervalWriteIndex_ + 1) % kIntervalHistorySize;
    if (intervalCount_ < kIntervalHistorySize) {
      ++intervalCount_;
    }

    confident_ = intervalCount_ >= kIntervalsForConfidence;
    if (!confident_) return false;

    const uint16_t averageInterval = smoothedInterval();
    if (averageInterval == 0) {
      confident_ = false;
      return false;
    }
    bpm_ = 60000UL / averageInterval;
    return true;
  }

  uint16_t smoothedInterval() const {
    if (intervalCount_ == 2) {
      const uint8_t latestIndex =
          intervalWriteIndex_ == 0 ? kIntervalHistorySize - 1
                                   : intervalWriteIndex_ - 1;
      const uint8_t previousIndex =
          latestIndex == 0 ? kIntervalHistorySize - 1 : latestIndex - 1;
      return (intervals_[latestIndex] + intervals_[previousIndex]) / 2;
    }

    uint16_t a = intervals_[0];
    uint16_t b = intervals_[1];
    uint16_t c = intervals_[2];
    if (a > b) {
      const uint16_t temporary = a;
      a = b;
      b = temporary;
    }
    if (b > c) {
      const uint16_t temporary = b;
      b = c;
      c = temporary;
    }
    if (a > b) b = a;
    return b;
  }

  bool initialized_;
  uint8_t historyIndex_;
  uint16_t history_[3];
  int32_t filteredQ8_;
  int32_t baselineQ8_;
  int32_t amplitudeQ8_;
  int16_t previousAc_;
  bool tracking_;
  uint32_t candidateStartedAt_;
  uint32_t candidatePeakAt_;
  int16_t candidatePeak_;
  bool hasLastBeat_;
  uint32_t lastBeatAt_;
  uint16_t intervals_[kIntervalHistorySize];
  uint8_t intervalCount_;
  uint8_t intervalWriteIndex_;
  bool confident_;
  uint16_t bpm_;
};
