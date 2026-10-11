#include <Arduino.h>
#include <EEPROM.h>
#include <unity.h>

void test_atmega328p_exposes_internal_eeprom_capacity() {
  TEST_ASSERT_EQUAL_UINT16(1024, EEPROM.length());
  Serial.println(F("EEPROM capacity is 1024 bytes; no addresses were written."));
  Serial.println(
      F("Record/reboot/review persistence must be checked with application UI."));
}

void setUp() {}
void tearDown() {}

void setup() {
  Serial.begin(9600);
  delay(1500);
  UNITY_BEGIN();
  RUN_TEST(test_atmega328p_exposes_internal_eeprom_capacity);
  UNITY_END();
}

void loop() {}
