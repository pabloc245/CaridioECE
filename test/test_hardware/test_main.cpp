#include <Arduino.h>
#include <unity.h>
#include <DualSSD1306.h>
#include <Adafruit_SSD1306.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>

constexpr uint8_t RTC_IO_PIN = 7;
constexpr uint8_t RTC_SCLK_PIN = 6;
constexpr uint8_t RTC_CE_PIN = 8;
constexpr uint8_t PULSE_PIN = A0;
constexpr uint8_t PULSE_RANGE_THRESHOLD = 10;

DualSSD1306 oled(128, 64, DualSSD1306::MIRROR, &Wire, &Wire,
                 -1, -1, 400000UL, 100000UL);
ThreeWire rtcWire(RTC_IO_PIN, RTC_SCLK_PIN, RTC_CE_PIN);
RtcDS1302<ThreeWire> rtc(rtcWire);

bool initializeAndShow(uint8_t address, const __FlashStringHelper *label) {
  if (!oled.begin(DUALSSD1306_SWITCHCAPVCC, address, address)) return false;

  oled.clearDisplay();
  oled.setTextColor(DUALSSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(0, 20);
  oled.print(label);
  return oled.display();
}

void test_left_oled_initializes_and_displays() {
  TEST_ASSERT_TRUE(initializeAndShow(0x3C, F("0x3C OK")));
}

void test_right_oled_initializes_and_displays() {
  TEST_ASSERT_TRUE(initializeAndShow(0x3D, F("0x3D OK")));
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

  Serial.print(F("Pulse sensor ADC range: "));
  Serial.print(minimum);
  Serial.print('-');
  Serial.println(maximum);
  TEST_ASSERT_TRUE((maximum - minimum) >= PULSE_RANGE_THRESHOLD);
}

void setUp() {}
void tearDown() {}

void setup() {
  Serial.begin(9600);
  Wire.begin();
  delay(2000);

  UNITY_BEGIN();
  RUN_TEST(test_left_oled_initializes_and_displays);
  RUN_TEST(test_right_oled_initializes_and_displays);
  RUN_TEST(test_ds1302_writes_and_reads_time);
  RUN_TEST(test_pulse_sensor_signal_changes_with_finger_on_sensor);
  UNITY_END();
}

void loop() {}
