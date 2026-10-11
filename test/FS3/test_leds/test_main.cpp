#include <Arduino.h>
#include <unity.h>

constexpr uint8_t RED_LED_PIN = 3;
constexpr uint8_t GREEN_LED_PIN = 4;
constexpr uint8_t YELLOW_LED_PIN = 5;

void expectOnlyLed(uint8_t activePin, const char *colorName) {
  digitalWrite(RED_LED_PIN, activePin == RED_LED_PIN ? HIGH : LOW);
  digitalWrite(GREEN_LED_PIN, activePin == GREEN_LED_PIN ? HIGH : LOW);
  digitalWrite(YELLOW_LED_PIN, activePin == YELLOW_LED_PIN ? HIGH : LOW);

  Serial.print(F("Visually check: "));
  Serial.println(colorName);
  delay(1500);

  TEST_ASSERT_EQUAL(activePin == RED_LED_PIN ? HIGH : LOW,
                    digitalRead(RED_LED_PIN));
  TEST_ASSERT_EQUAL(activePin == GREEN_LED_PIN ? HIGH : LOW,
                    digitalRead(GREEN_LED_PIN));
  TEST_ASSERT_EQUAL(activePin == YELLOW_LED_PIN ? HIGH : LOW,
                    digitalRead(YELLOW_LED_PIN));
}

void test_leds_light_one_at_a_time_in_color_order() {
  expectOnlyLed(YELLOW_LED_PIN, "YELLOW (low heart rate)");
  expectOnlyLed(GREEN_LED_PIN, "GREEN (normal heart rate)");
  expectOnlyLed(RED_LED_PIN, "RED (high heart rate)");

  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);
  TEST_ASSERT_EQUAL(LOW, digitalRead(RED_LED_PIN));
  TEST_ASSERT_EQUAL(LOW, digitalRead(GREEN_LED_PIN));
  TEST_ASSERT_EQUAL(LOW, digitalRead(YELLOW_LED_PIN));
}

void setUp() {}
void tearDown() {}

void setup() {
  Serial.begin(9600);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);
  delay(1500);
  UNITY_BEGIN();
  RUN_TEST(test_leds_light_one_at_a_time_in_color_order);
  UNITY_END();
}

void loop() {}
