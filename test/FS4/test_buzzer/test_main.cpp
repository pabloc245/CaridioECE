#include <Arduino.h>
#include <unity.h>

constexpr uint8_t BUZZER_PIN = 10;
constexpr uint8_t BUTTON_PIN = 2;
constexpr unsigned long DEBOUNCE_US = 30000UL;

volatile bool beepEnabled = true;
volatile unsigned long lastButtonAt = 0;
volatile bool toneNeedsRestart = false;

void buttonPressed() {
  const unsigned long now = micros();
  if (now - lastButtonAt < DEBOUNCE_US) return;
  lastButtonAt = now;
  beepEnabled = !beepEnabled;
  if (!beepEnabled) {
    noTone(BUZZER_PIN);
  } else {
    toneNeedsRestart = true;
  }
}

uint8_t countToneTransitions(unsigned long durationMs) {
  uint8_t transitions = 0;
  int previous = digitalRead(BUZZER_PIN);
  const unsigned long startedAt = millis();
  while (millis() - startedAt < durationMs) {
    const int current = digitalRead(BUZZER_PIN);
    if (current != previous) {
      previous = current;
      ++transitions;
    }
  }
  return transitions;
}

void test_manual_bpm_beat_patterns_have_distinct_tones() {
  struct ToneCheck {
    uint16_t bpm;
    uint16_t frequency;
    const __FlashStringHelper *label;
  };
  const ToneCheck tones[] = {
      {50, 220, F("LOW: 50 BPM")},
      {75, 440, F("NORMAL: 75 BPM")},
      {120, 880, F("HIGH: 120 BPM")}};

  for (uint8_t i = 0; i < 3; ++i) {
    Serial.print(F("Listen for "));
    Serial.println(tones[i].label);
    const uint16_t beatInterval = 60000UL / tones[i].bpm;
    for (uint8_t beat = 0; beat < 3; ++beat) {
      tone(BUZZER_PIN, tones[i].frequency, 80);
      delay(beatInterval);
    }
    noTone(BUZZER_PIN);
    delay(500);
  }
  TEST_ASSERT_TRUE(true);
}

void test_d2_button_interrupt_mutes_and_reenables_beeping() {
  beepEnabled = true;
  toneNeedsRestart = false;
  tone(BUZZER_PIN, 440);
  TEST_ASSERT_TRUE(countToneTransitions(40) > 4);

  Serial.println(F("Press D2 to mute the continuous tone"));
  const unsigned long muteStartedAt = millis();
  while (beepEnabled && millis() - muteStartedAt < 10000) delay(1);
  TEST_ASSERT_FALSE_MESSAGE(beepEnabled, "D2 was not pressed to mute");
  TEST_ASSERT_TRUE_MESSAGE(countToneTransitions(40) <= 2,
                           "Tone continued after D2 mute");

  Serial.println(F("Press D2 again to re-enable the tone"));
  const unsigned long enableStartedAt = millis();
  while (!toneNeedsRestart && millis() - enableStartedAt < 10000) delay(1);
  TEST_ASSERT_TRUE_MESSAGE(beepEnabled, "D2 was not pressed to re-enable");
  tone(BUZZER_PIN, 440);
  toneNeedsRestart = false;
  TEST_ASSERT_TRUE(countToneTransitions(40) > 4);
  noTone(BUZZER_PIN);
}

void setUp() {}
void tearDown() {}

void setup() {
  Serial.begin(9600);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  beepEnabled = true;
  lastButtonAt = micros() - DEBOUNCE_US;
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonPressed, FALLING);
  delay(1500);
  UNITY_BEGIN();
  RUN_TEST(test_manual_bpm_beat_patterns_have_distinct_tones);
  RUN_TEST(test_d2_button_interrupt_mutes_and_reenables_beeping);
  UNITY_END();
}

void loop() {}
