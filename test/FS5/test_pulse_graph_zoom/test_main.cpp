#include <PulseGraphZoom.h>
#include <unity.h>

void test_zoom_levels_change_the_sampling_interval_exponentially() {
  TEST_ASSERT_EQUAL_UINT8(2, pulseGraphSampleIntervalTicks(0));
  TEST_ASSERT_EQUAL_UINT8(4, pulseGraphSampleIntervalTicks(1));
  TEST_ASSERT_EQUAL_UINT8(8, pulseGraphSampleIntervalTicks(2));
  TEST_ASSERT_EQUAL_UINT8(16, pulseGraphSampleIntervalTicks(3));
  TEST_ASSERT_EQUAL_UINT8(32, pulseGraphSampleIntervalTicks(4));
}

void test_zoom_windows_are_reported_in_real_seconds() {
  TEST_ASSERT_EQUAL_UINT8(5, pulseGraphWindowTenths(0, 120));
  TEST_ASSERT_EQUAL_UINT8(10, pulseGraphWindowTenths(1, 120));
  TEST_ASSERT_EQUAL_UINT8(19, pulseGraphWindowTenths(2, 120));
  TEST_ASSERT_EQUAL_UINT8(38, pulseGraphWindowTenths(3, 120));
  TEST_ASSERT_EQUAL_UINT8(76, pulseGraphWindowTenths(4, 120));
}

void test_time_labels_cover_four_marks_of_the_displayed_window() {
  TEST_ASSERT_EQUAL_UINT8(0, pulseGraphTimeLabelTenths(4, 120, 0));
  TEST_ASSERT_EQUAL_UINT8(25, pulseGraphTimeLabelTenths(4, 120, 1));
  TEST_ASSERT_EQUAL_UINT8(51, pulseGraphTimeLabelTenths(4, 120, 2));
  TEST_ASSERT_EQUAL_UINT8(76, pulseGraphTimeLabelTenths(4, 120, 3));
}

void test_zoom_helpers_clamp_invalid_levels_and_empty_windows() {
  TEST_ASSERT_EQUAL_UINT8(32, pulseGraphSampleIntervalTicks(99));
  TEST_ASSERT_EQUAL_UINT8(0, pulseGraphWindowTenths(0, 1));
  TEST_ASSERT_EQUAL_UINT8(0, pulseGraphWindowTenths(0, 0));
  TEST_ASSERT_EQUAL_UINT8(76, pulseGraphTimeLabelTenths(4, 120, 99));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_zoom_levels_change_the_sampling_interval_exponentially);
  RUN_TEST(test_zoom_windows_are_reported_in_real_seconds);
  RUN_TEST(test_time_labels_cover_four_marks_of_the_displayed_window);
  RUN_TEST(test_zoom_helpers_clamp_invalid_levels_and_empty_windows);
  return UNITY_END();
}
