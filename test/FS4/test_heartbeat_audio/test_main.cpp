#include <HeartbeatAudio.h>
#include <unity.h>

void test_low_heart_rate_uses_low_tone() {
  TEST_ASSERT_EQUAL_UINT16(220, heartbeatToneFrequency(50));
  TEST_ASSERT_EQUAL_UINT16(220, heartbeatToneFrequency(59));
}

void test_normal_heart_rate_uses_moderate_tone() {
  TEST_ASSERT_EQUAL_UINT16(440, heartbeatToneFrequency(60));
  TEST_ASSERT_EQUAL_UINT16(440, heartbeatToneFrequency(100));
}

void test_high_heart_rate_uses_high_tone() {
  TEST_ASSERT_EQUAL_UINT16(880, heartbeatToneFrequency(101));
  TEST_ASSERT_EQUAL_UINT16(880, heartbeatToneFrequency(180));
  TEST_ASSERT_EQUAL_UINT16(880, heartbeatToneFrequency(240));
}

void test_out_of_range_heart_rate_is_silent() {
  TEST_ASSERT_EQUAL_UINT16(0, heartbeatToneFrequency(29));
  TEST_ASSERT_EQUAL_UINT16(0, heartbeatToneFrequency(241));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_low_heart_rate_uses_low_tone);
  RUN_TEST(test_normal_heart_rate_uses_moderate_tone);
  RUN_TEST(test_high_heart_rate_uses_high_tone);
  RUN_TEST(test_out_of_range_heart_rate_is_silent);
  return UNITY_END();
}
