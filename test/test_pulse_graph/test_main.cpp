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

void test_map_x_keeps_partial_and_full_graph_within_plot() {
  TEST_ASSERT_EQUAL_INT16(139, pulseGraphMapX(0, 1, 20, 120));
  TEST_ASSERT_EQUAL_INT16(139, pulseGraphMapX(119, 120, 20, 120));
  TEST_ASSERT_EQUAL_INT16(20, pulseGraphMapX(0, 200, 20, 120));
  TEST_ASSERT_EQUAL_INT16(139, pulseGraphMapX(199, 200, 20, 120));
  TEST_ASSERT_EQUAL_INT16(20, pulseGraphMapX(0, 0, 20, 120));
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
}

void test_map_y_places_minimum_middle_and_maximum() {
  TEST_ASSERT_EQUAL_INT16(40, pulseGraphMapY(50, 50, 150, 0, 40));
  TEST_ASSERT_EQUAL_INT16(20, pulseGraphMapY(100, 50, 150, 0, 40));
  TEST_ASSERT_EQUAL_INT16(0, pulseGraphMapY(150, 50, 150, 0, 40));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_push_sample_advances_and_counts_until_full);
  RUN_TEST(test_oldest_index_tracks_partial_and_wrapped_buffer);
  RUN_TEST(test_invalid_buffer_state_has_safe_oldest_index);
  RUN_TEST(test_map_x_keeps_partial_and_full_graph_within_plot);
  RUN_TEST(test_fit_range_expands_flat_signal_and_respects_limits);
  RUN_TEST(test_map_y_places_minimum_middle_and_maximum);
  return UNITY_END();
}
