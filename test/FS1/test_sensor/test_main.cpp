#include <Arduino.h>
#include <unity.h>

constexpr uint8_t PULSE_PIN = A0;

void test_sensor_produces_a_changing_analog_signal() {
  uint16_t minimum = 1023;
  uint16_t maximum = 0;

  for (uint16_t sampleIndex = 0; sampleIndex < 250; ++sampleIndex) {
    const uint16_t sample = analogRead(PULSE_PIN);
    if (sample < minimum) minimum = sample;
    if (sample > maximum) maximum = sample;
    delay(20);
  }

  char message[64];
  snprintf(message, sizeof(message), "A0 min=%u max=%u delta=%u; keep finger "
                                    "on sensor and expect delta >= 10",
           minimum, maximum, maximum - minimum);
  TEST_ASSERT_TRUE_MESSAGE(maximum - minimum >= 10, message);
}

void setUp() {}
void tearDown() {}

void setup() {
  Serial.begin(9600);
  delay(1500);
  UNITY_BEGIN();
  RUN_TEST(test_sensor_produces_a_changing_analog_signal);
  UNITY_END();
}

void loop() {}
