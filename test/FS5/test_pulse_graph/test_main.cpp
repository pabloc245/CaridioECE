#include <unity.h>
#include <PulseGraph.h>

void test_push_sample_advances_and_counts_until_full() {
  volatile uint8_t samples[3] = {};
  volatile uint8_t writeIndex = 0;
  volatile uint8_t storedCount = 0;

  pulseGraphPushSample(samples, 3, writeIndex, storedCount, 10);
  pulseGraphPushSample(samples, 3, writeIndex, storedCount, 20);

  TEST_ASSERT_EQUAL_UINT8(2, writeIndex);
  TEST_ASSERT_EQUAL_UINT8(2, storedCount);
  TEST_ASSERT_EQUAL_UINT8(10, samples[0]);
  TEST_ASSERT_EQUAL_UINT8(20, samples[1]);

  pulseGraphPushSample(samples, 3, writeIndex, storedCount, 30);
  pulseGraphPushSample(samples, 3, writeIndex, storedCount, 40);

  TEST_ASSERT_EQUAL_UINT8(1, writeIndex);
  TEST_ASSERT_EQUAL_UINT8(3, storedCount);
  TEST_ASSERT_EQUAL_UINT8(40, samples[0]);
}

void test_oldest_index_tracks_partial_and_wrapped_buffer() {
  TEST_ASSERT_EQUAL_UINT8(0, pulseGraphOldestIndex(5, 3, 3));
  TEST_ASSERT_EQUAL_UINT8(3, pulseGraphOldestIndex(5, 1, 3));
  TEST_ASSERT_EQUAL_UINT8(0, pulseGraphOldestIndex(5, 0, 5));
}

void test_invalid_buffer_state_has_safe_oldest_index() {
  TEST_ASSERT_EQUAL_UINT8(0, pulseGraphOldestIndex(0, 0, 0));
  TEST_ASSERT_EQUAL_UINT8(0, pulseGraphOldestIndex(5, 2, 6));
  TEST_ASSERT_EQUAL_UINT8(0, pulseGraphOldestIndex(5, 5, 2));
}

void test_push_sample_ignores_invalid_buffers() {
  volatile uint8_t samples[2] = {11, 22};
  volatile uint8_t writeIndex = 2;
  volatile uint8_t storedCount = 2;
  pulseGraphPushSample(samples, 2, writeIndex, storedCount, 33);
  TEST_ASSERT_EQUAL_UINT8(2, writeIndex);
  TEST_ASSERT_EQUAL_UINT8(2, storedCount);
  TEST_ASSERT_EQUAL_UINT8(11, samples[0]);
  TEST_ASSERT_EQUAL_UINT8(22, samples[1]);

  writeIndex = 0;
  pulseGraphPushSample(samples, 0, writeIndex, storedCount, 44);
  TEST_ASSERT_EQUAL_UINT8(0, writeIndex);
  TEST_ASSERT_EQUAL_UINT8(2, storedCount);

  pulseGraphPushSample(0, 2, writeIndex, storedCount, 55);
  TEST_ASSERT_EQUAL_UINT8(0, writeIndex);
  TEST_ASSERT_EQUAL_UINT8(2, storedCount);
}

void test_fit_range_expands_flat_signal_and_respects_limits() {
  uint8_t minimum = 128;
  uint8_t maximum = 128;
  pulseGraphFitRange(minimum, maximum);

  TEST_ASSERT_EQUAL_UINT8(118, minimum);
  TEST_ASSERT_EQUAL_UINT8(138, maximum);

  minimum = 0;
  maximum = 0;
  pulseGraphFitRange(minimum, maximum);
  TEST_ASSERT_EQUAL_UINT8(0, minimum);
  TEST_ASSERT_EQUAL_UINT8(10, maximum);

  minimum = 255;
  maximum = 255;
  pulseGraphFitRange(minimum, maximum);
  TEST_ASSERT_EQUAL_UINT8(245, minimum);
  TEST_ASSERT_EQUAL_UINT8(255, maximum);

  minimum = 40;
  maximum = 100;
  pulseGraphFitRange(minimum, maximum);
  TEST_ASSERT_EQUAL_UINT8(40, minimum);
  TEST_ASSERT_EQUAL_UINT8(100, maximum);
}

void test_map_y_places_minimum_middle_and_maximum() {
  TEST_ASSERT_EQUAL_INT16(40, pulseGraphMapY(50, 50, 150, 0, 40));
  TEST_ASSERT_EQUAL_INT16(20, pulseGraphMapY(100, 50, 150, 0, 40));
  TEST_ASSERT_EQUAL_INT16(0, pulseGraphMapY(150, 50, 150, 0, 40));
  TEST_ASSERT_EQUAL_INT16(40, pulseGraphMapY(20, 50, 150, 0, 40));
  TEST_ASSERT_EQUAL_INT16(0, pulseGraphMapY(200, 50, 150, 0, 40));
}

void test_map_y_handles_degenerate_ranges_without_division_by_zero() {
  TEST_ASSERT_EQUAL_INT16(40, pulseGraphMapY(128, 128, 128, 0, 40));
  TEST_ASSERT_EQUAL_INT16(40, pulseGraphMapY(128, 200, 100, 0, 40));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_push_sample_advances_and_counts_until_full);
  RUN_TEST(test_oldest_index_tracks_partial_and_wrapped_buffer);
  RUN_TEST(test_invalid_buffer_state_has_safe_oldest_index);
  RUN_TEST(test_push_sample_ignores_invalid_buffers);
  RUN_TEST(test_fit_range_expands_flat_signal_and_respects_limits);
  RUN_TEST(test_map_y_places_minimum_middle_and_maximum);
  RUN_TEST(test_map_y_handles_degenerate_ranges_without_division_by_zero);
  return UNITY_END();
}
