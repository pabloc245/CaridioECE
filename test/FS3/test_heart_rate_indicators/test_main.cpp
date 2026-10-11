#include <HeartRateIndicators.h>
#include <unity.h>

void test_no_reliable_bpm_turns_all_indicators_off() {
  TEST_ASSERT_EQUAL(HEART_RATE_INDICATOR_OFF, heartRateIndicator(0, true));
  TEST_ASSERT_EQUAL(HEART_RATE_INDICATOR_OFF, heartRateIndicator(75, false));
  TEST_ASSERT_EQUAL(HEART_RATE_INDICATOR_OFF, heartRateIndicator(29, true));
  TEST_ASSERT_EQUAL(HEART_RATE_INDICATOR_OFF, heartRateIndicator(241, true));
}

void test_low_heart_rate_uses_yellow_indicator() {
  TEST_ASSERT_EQUAL(HEART_RATE_INDICATOR_YELLOW, heartRateIndicator(50, true));
  TEST_ASSERT_EQUAL(HEART_RATE_INDICATOR_YELLOW, heartRateIndicator(59, true));
}

void test_normal_heart_rate_uses_green_indicator() {
  TEST_ASSERT_EQUAL(HEART_RATE_INDICATOR_GREEN, heartRateIndicator(60, true));
  TEST_ASSERT_EQUAL(HEART_RATE_INDICATOR_GREEN, heartRateIndicator(100, true));
}

void test_high_heart_rate_uses_red_indicator() {
  TEST_ASSERT_EQUAL(HEART_RATE_INDICATOR_RED, heartRateIndicator(101, true));
  TEST_ASSERT_EQUAL(HEART_RATE_INDICATOR_RED, heartRateIndicator(180, true));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_reliable_bpm_turns_all_indicators_off);
  RUN_TEST(test_low_heart_rate_uses_yellow_indicator);
  RUN_TEST(test_normal_heart_rate_uses_green_indicator);
  RUN_TEST(test_high_heart_rate_uses_red_indicator);
  return UNITY_END();
}
