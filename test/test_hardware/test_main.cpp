#include <Arduino.h>
#include <DualSSD1306.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <unity.h>
#include <stdio.h>

#if defined(__has_include)
  #if __has_include(<Adafruit_SSD1306.h>)
    #include <Adafruit_SSD1306.h>
    #define HAS_ADAFRUIT_SSD1306 1
  #endif
#endif



constexpr uint8_t RTC_IO_PIN = 7;
constexpr uint8_t RTC_SCLK_PIN = 6;
constexpr uint8_t RTC_CE_PIN = 8;
constexpr uint8_t PULSE_PIN = A0;
constexpr uint8_t PULSE_RANGE_THRESHOLD = 10;
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;

ThreeWire rtcWire(RTC_IO_PIN, RTC_SCLK_PIN, RTC_CE_PIN);
RtcDS1302<ThreeWire> rtc(rtcWire);

enum ScreenTestStatus : uint8_t {
  SCREEN_TEST_OK,
  SCREEN_TEST_DUAL_BEGIN_FAILED,
  SCREEN_TEST_DUAL_DISPLAY_FAILED,
  SCREEN_TEST_FALLBACK_UNAVAILABLE,
  SCREEN_TEST_FALLBACK_BEGIN_FAILED
};

struct ScreenTestResult {
  ScreenTestStatus status;
  int8_t panel;
  uint8_t initStep;
  uint8_t i2cStatus;
};

ScreenTestResult makeScreenTestResult(ScreenTestStatus status,
                                      int8_t panel = -1,
                                      uint8_t initStep = 0,
                                      uint8_t i2cStatus = 0) {
  return {status, panel, initStep, i2cStatus};
}

ScreenTestResult testScreenWithDual(uint8_t address,
                                    const __FlashStringHelper *label) {
  DualSSD1306 oled(OLED_WIDTH, OLED_HEIGHT, DualSSD1306::MIRROR, &Wire, &Wire,
                   -1, -1, 400000UL, 100000UL);
  if (!oled.begin(DUALSSD1306_SWITCHCAPVCC, address, address)) {
    return makeScreenTestResult(SCREEN_TEST_DUAL_BEGIN_FAILED,
                                oled.failedPanel(), oled.failedInitStep(),
                                oled.i2cStatus());
  }

  oled.clearDisplay();
  oled.setTextColor(DUALSSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(0, 20);
  oled.print(label);
  if (!oled.display())
    return makeScreenTestResult(SCREEN_TEST_DUAL_DISPLAY_FAILED);
  return makeScreenTestResult(SCREEN_TEST_OK);
}

ScreenTestResult testScreen(uint8_t address,
                            const __FlashStringHelper *label) {
  const ScreenTestResult dualResult = testScreenWithDual(address, label);
  if (dualResult.status == SCREEN_TEST_OK) return dualResult;

#if defined(HAS_ADAFRUIT_SSD1306) && HAS_ADAFRUIT_SSD1306
  Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
  if (!display.begin(SSD1306_SWITCHCAPVCC, address))
    return makeScreenTestResult(SCREEN_TEST_FALLBACK_BEGIN_FAILED,
                                dualResult.panel, dualResult.initStep,
                                dualResult.i2cStatus);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 12);
  display.print(label);
  display.display();
  return makeScreenTestResult(SCREEN_TEST_OK);
#else
  return makeScreenTestResult(SCREEN_TEST_FALLBACK_UNAVAILABLE,
                              dualResult.panel, dualResult.initStep,
                              dualResult.i2cStatus);
#endif
}

void assertScreenWorks(uint8_t address, const __FlashStringHelper *label) {
  const ScreenTestResult result = testScreen(address, label);
  const char *reason = "unknown";
  switch (result.status) {
    case SCREEN_TEST_OK: reason = "ok"; break;
    case SCREEN_TEST_DUAL_BEGIN_FAILED: reason = "Dual begin failed"; break;
    case SCREEN_TEST_DUAL_DISPLAY_FAILED: reason = "Dual display failed"; break;
    case SCREEN_TEST_FALLBACK_UNAVAILABLE: reason = "fallback unavailable"; break;
    case SCREEN_TEST_FALLBACK_BEGIN_FAILED: reason = "fallback begin failed"; break;
  }

  char message[80];
  snprintf(message, sizeof(message),
           "OLED 0x%02X: %s; panel=%d step=%u I2C=%u",
           address, reason, result.panel, result.initStep, result.i2cStatus);
  TEST_ASSERT_TRUE_MESSAGE(result.status == SCREEN_TEST_OK, message);
}

void test_left_oled_initializes_and_displays() {
  assertScreenWorks(0x3C, F("0x3C OK"));
}

void test_right_oled_initializes_and_displays() {
  assertScreenWorks(0x3D, F("0x3D OK"));
}

void test_ds1302_writes_and_reads_time() {
  rtc.Begin();
  rtc.SetIsWriteProtected(false);
  rtc.SetIsRunning(true);

  const RtcDateTime expected(2026, 10, 7, 12, 34, 50);
  rtc.SetDateTime(expected);
  const RtcDateTime actual = rtc.GetDateTime();

  TEST_ASSERT_EQUAL_UINT16(expected.Year(), actual.Year());
  TEST_ASSERT_EQUAL_UINT8(expected.Month(), actual.Month());
  TEST_ASSERT_EQUAL_UINT8(expected.Day(), actual.Day());
  TEST_ASSERT_EQUAL_UINT8(expected.Hour(), actual.Hour());
  TEST_ASSERT_EQUAL_UINT8(expected.Minute(), actual.Minute());
  TEST_ASSERT_EQUAL_UINT8(expected.Second(), actual.Second());
}

void test_pulse_sensor_signal_changes_with_finger_on_sensor() {
  uint16_t minimum = 1023;
  uint16_t maximum = 0;

  for (uint16_t i = 0; i < 250; ++i) {
    const uint16_t sample = analogRead(PULSE_PIN);
    if (sample < minimum) minimum = sample;
    if (sample > maximum) maximum = sample;
    delay(20);
  }

  char message[64];
  snprintf(message, sizeof(message),
           "A0 signal range %u-%u (variation %u; expected >= %u)",
           minimum, maximum, maximum - minimum, PULSE_RANGE_THRESHOLD);
  TEST_ASSERT_TRUE_MESSAGE((maximum - minimum) >= PULSE_RANGE_THRESHOLD,
                           message);
}

void setUp() {}
void tearDown() {}

void setup() {
  Serial.begin(9600);
  delay(2000);

  UNITY_BEGIN();
  RUN_TEST(test_left_oled_initializes_and_displays);
  RUN_TEST(test_right_oled_initializes_and_displays);
  RUN_TEST(test_ds1302_writes_and_reads_time);
  RUN_TEST(test_pulse_sensor_signal_changes_with_finger_on_sensor);
  UNITY_END();
}

void loop() {}
