#include <PulseDetector.h>
#include <unity.h>

static uint16_t pulseSample(uint32_t timeMs, uint16_t periodMs,
                            uint16_t baseline, uint16_t amplitude) {
  if (timeMs < 600) return baseline;
  const uint16_t phase = (timeMs - 600) % periodMs;
  const uint16_t width = periodMs < 320 ? periodMs / 2 : 160;
  const uint16_t riseTime = width / 4;
  if (phase >= width) return baseline;
  if (phase < riseTime) return baseline + (amplitude * phase) / riseTime;
  return baseline + (amplitude * (width - phase)) / (width - riseTime);
}

void test_regular_pulses_become_confident_after_two_intervals() {
  PulseDetector detector;
  uint32_t firstConfidentAt = 0;

  for (uint32_t timeMs = 0; timeMs <= 5000; timeMs += 2) {
    detector.process(pulseSample(timeMs, 800, 400, 60), timeMs);
    if (detector.isConfident() && firstConfidentAt == 0) {
      firstConfidentAt = timeMs;
    }
  }

  TEST_ASSERT_TRUE(detector.isConfident());
  TEST_ASSERT_TRUE(firstConfidentAt >= 600 + 2 * 800);
  TEST_ASSERT_TRUE(firstConfidentAt < 600 + 3 * 800);
  TEST_ASSERT_EQUAL_UINT16(75, detector.bpm());
}

void test_bpm_remains_accurate_across_millis_counter_wrap() {
  PulseDetector detector;
  const uint32_t startNearWrap = UINT32_MAX - 3000UL;

  for (uint32_t elapsedMs = 0; elapsedMs <= 5000; elapsedMs += 2) {
    detector.process(pulseSample(elapsedMs, 800, 400, 60),
                     startNearWrap + elapsedMs);
  }

  TEST_ASSERT_TRUE(detector.isConfident());
  TEST_ASSERT_EQUAL_UINT16(75, detector.bpm());
}

void test_peak_candidate_enters_tracking_state() {
  PulseDetector detector;
  bool observedTracking = false;
  for (uint32_t timeMs = 0; timeMs <= 1200; timeMs += 2) {
    detector.process(pulseSample(timeMs, 800, 400, 80), timeMs);
    if (detector.isTracking()) observedTracking = true;
  }
  TEST_ASSERT_TRUE(observedTracking);
}

void test_amplitude_baseline_changes_and_impulse_noise_are_tolerated() {
  PulseDetector detector;
  uint16_t latestBpm = 0;

  for (uint32_t timeMs = 0; timeMs <= 12000; timeMs += 2) {
    const uint16_t baseline =
        (uint16_t)(300 + (timeMs < 8000 ? timeMs / 80 : 100));
    const uint16_t amplitude = timeMs < 5000 ? 28 : 90;
    uint16_t sample = pulseSample(timeMs, 800, baseline, amplitude);
    if (timeMs == 4100 || timeMs == 7300 || timeMs == 10100) {
      sample = 950;
    }
    if (detector.process(sample, timeMs)) latestBpm = detector.bpm();
  }

  TEST_ASSERT_TRUE(detector.isConfident());
  TEST_ASSERT_TRUE(latestBpm >= 65);
  TEST_ASSERT_TRUE(latestBpm <= 85);
}

void test_periodic_single_sample_spikes_do_not_create_beats() {
  PulseDetector detector;

  for (uint32_t timeMs = 0; timeMs <= 10000; timeMs += 2) {
    uint16_t sample = 400;
    if (timeMs >= 1000 && (timeMs - 1000) % 800 == 0) sample = 950;
    detector.process(sample, timeMs);
  }

  TEST_ASSERT_FALSE(detector.isConfident());
}

void test_intervals_above_maximum_rate_do_not_create_confidence() {
  PulseDetector detector;

  for (uint32_t timeMs = 0; timeMs <= 10000; timeMs += 2) {
    detector.process(pulseSample(timeMs, 200, 400, 80), timeMs);
  }

  TEST_ASSERT_FALSE(detector.isConfident());

  PulseDetector tooSlowDetector;
  for (uint32_t timeMs = 0; timeMs <= 12000; timeMs += 2) {
    tooSlowDetector.process(pulseSample(timeMs, 2400, 400, 80), timeMs);
  }
  TEST_ASSERT_FALSE(tooSlowDetector.isConfident());
}

void test_supported_rate_boundaries_produce_valid_bpm() {
  PulseDetector fastDetector;
  for (uint32_t timeMs = 0; timeMs <= 4000; timeMs += 2) {
    fastDetector.process(pulseSample(timeMs, 250, 400, 80), timeMs);
  }
  TEST_ASSERT_TRUE(fastDetector.isConfident());
  TEST_ASSERT_TRUE(fastDetector.bpm() >= 238);
  TEST_ASSERT_TRUE(fastDetector.bpm() <= 240);

  PulseDetector slowDetector;
  for (uint32_t timeMs = 0; timeMs <= 12000; timeMs += 2) {
    slowDetector.process(pulseSample(timeMs, 2000, 400, 80), timeMs);
  }
  TEST_ASSERT_TRUE(slowDetector.isConfident());
  TEST_ASSERT_EQUAL_UINT16(30, slowDetector.bpm());
}

void test_confidence_expires_when_pulses_stop() {
  PulseDetector detector;

  for (uint32_t timeMs = 0; timeMs <= 5000; timeMs += 2) {
    detector.process(pulseSample(timeMs, 800, 400, 60), timeMs);
  }
  TEST_ASSERT_TRUE(detector.isConfident());

  for (uint32_t timeMs = 5002; timeMs <= 11002; timeMs += 2) {
    detector.process(400, timeMs);
  }

  TEST_ASSERT_FALSE(detector.isConfident());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_regular_pulses_become_confident_after_two_intervals);
  RUN_TEST(test_bpm_remains_accurate_across_millis_counter_wrap);
  RUN_TEST(test_peak_candidate_enters_tracking_state);
  RUN_TEST(test_amplitude_baseline_changes_and_impulse_noise_are_tolerated);
  RUN_TEST(test_periodic_single_sample_spikes_do_not_create_beats);
  RUN_TEST(test_intervals_above_maximum_rate_do_not_create_confidence);
  RUN_TEST(test_supported_rate_boundaries_produce_valid_bpm);
  RUN_TEST(test_confidence_expires_when_pulses_stop);
  return UNITY_END();
}
