#include <Arduino.h>
#include <RtcDS1302.h>
#include <ThreeWire.h>
#include <unity.h>

constexpr uint8_t RTC_IO_PIN = 7;
constexpr uint8_t RTC_SCLK_PIN = 6;
constexpr uint8_t RTC_CE_PIN = 8;

ThreeWire rtcWire(RTC_IO_PIN, RTC_SCLK_PIN, RTC_CE_PIN);
RtcDS1302<ThreeWire> rtc(rtcWire);

void test_rtc_keeps_running_and_returns_a_valid_calendar() {
  rtc.Begin();
  TEST_ASSERT_TRUE_MESSAGE(rtc.IsDateTimeValid(),
                           "DS1302 time invalid; set the clock before test");

  const RtcDateTime now = rtc.GetDateTime();
  TEST_ASSERT_TRUE(now.Month() >= 1 && now.Month() <= 12);
  TEST_ASSERT_TRUE(now.Day() >= 1 && now.Day() <= 31);
  TEST_ASSERT_TRUE(now.Hour() <= 23);
  TEST_ASSERT_TRUE(now.Minute() <= 59);
  TEST_ASSERT_TRUE(now.Second() <= 59);

  const uint8_t startSecond = now.Second();
  const unsigned long startedAt = millis();
  while (millis() - startedAt < 2200 &&
         rtc.GetDateTime().Second() == startSecond) {
    delay(10);
  }
  TEST_ASSERT_NOT_EQUAL(startSecond, rtc.GetDateTime().Second());
  TEST_ASSERT_TRUE_MESSAGE(millis() - startedAt < 2200,
                           "RTC seconds did not advance");
}

void setUp() {}
void tearDown() {}

void setup() {
  Serial.begin(9600);
  delay(1500);
  UNITY_BEGIN();
  RUN_TEST(test_rtc_keeps_running_and_returns_a_valid_calendar);
  UNITY_END();
}

void loop() {}
