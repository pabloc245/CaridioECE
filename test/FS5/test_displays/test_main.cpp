#include <Arduino.h>
#include <PulseGraphZoom.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <unity.h>

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
constexpr uint8_t ENCODER_A_PIN = A1;
constexpr uint8_t ENCODER_B_PIN = A2;
constexpr uint8_t ENCODER_BUTTON_PIN = A3;
uint8_t previousEncoderState = 0;
int8_t encoderQuarterSteps = 0;
uint8_t currentZoomLevel = 2;

bool panelResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void drawTimeScaleDemo(uint8_t address, const char *panelName) {
  u8g2.setI2CAddress(address << 1);
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
  const uint8_t windowTenths =
      pulseGraphWindowTenths(currentZoomLevel, 120);
  char title[20];
  snprintf(title, sizeof(title), "%s %u.%us",
           panelName, windowTenths / 10, windowTenths % 10);
  u8g2.firstPage();
  do {
    u8g2.setDrawColor(1);
    u8g2.setFontPosTop();
    u8g2.drawStr(2, 1, title);
    u8g2.drawStr(2, 12, "TURN A1/A2 TO ZOOM");
    u8g2.drawFrame(0, 0, 128, 64);
    for (uint8_t mark = 0; mark < 4; ++mark) {
      const uint8_t x = 5 + mark * 39;
      u8g2.drawVLine(x, 25, 24);
      const uint8_t timeTenths =
          pulseGraphTimeLabelTenths(currentZoomLevel, 120, mark);
      char label[6];
      snprintf(label, sizeof(label), "%u.%u", timeTenths / 10,
               timeTenths % 10);
      int16_t labelX = x - u8g2.getStrWidth(label) / 2;
      if (labelX < 0) labelX = 0;
      if (labelX + u8g2.getStrWidth(label) > 127) {
        labelX = 127 - u8g2.getStrWidth(label);
      }
      u8g2.drawStr(labelX, 51, label);
    }
    u8g2.drawLine(5, 40, 15, 34);
    u8g2.drawLine(15, 34, 27, 42);
    u8g2.drawLine(27, 42, 38, 30);
    u8g2.drawLine(38, 30, 51, 43);
    u8g2.drawLine(51, 43, 64, 33);
    u8g2.drawLine(64, 33, 78, 39);
    u8g2.drawLine(78, 39, 92, 28);
    u8g2.drawLine(92, 28, 105, 41);
    u8g2.drawLine(105, 41, 122, 32);
  } while (u8g2.nextPage());
}

void pollDiagnosticEncoder() {
  static const int8_t transitions[16] = {
      0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  const uint8_t state =
      (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
  const uint8_t transition = (previousEncoderState << 2) | state;
  encoderQuarterSteps += transitions[transition];
  previousEncoderState = state;

  if (encoderQuarterSteps >= 4 && currentZoomLevel + 1 <
                                      PULSE_GRAPH_ZOOM_LEVEL_COUNT) {
    ++currentZoomLevel;
    encoderQuarterSteps = 0;
  } else if (encoderQuarterSteps <= -4 && currentZoomLevel > 0) {
    --currentZoomLevel;
    encoderQuarterSteps = 0;
  }
}

void test_both_oleds_acknowledge_and_show_time_scale() {
  Wire.begin();
  u8g2.setBusClock(100000UL);
  TEST_ASSERT_TRUE_MESSAGE(panelResponds(0x3C), "OLED 0x3C missing");
  drawTimeScaleDemo(0x3C, "LEFT OLED 0x3C");
  delay(2000);
  TEST_ASSERT_TRUE_MESSAGE(panelResponds(0x3D), "OLED 0x3D missing");
  drawTimeScaleDemo(0x3D, "RIGHT OLED 0x3D");
  delay(2000);
  Serial.println(F("Rotate A1/A2 for 15 s: the four time labels must change."));
  const unsigned long zoomTestStartedAt = millis();
  while (millis() - zoomTestStartedAt < 15000) {
    const uint8_t previousZoom = currentZoomLevel;
    pollDiagnosticEncoder();
    if (currentZoomLevel != previousZoom) {
      drawTimeScaleDemo(0x3D, "RIGHT OLED 0x3D");
    }
    if (digitalRead(ENCODER_BUTTON_PIN) == LOW) break;
    delay(1);
  }
  TEST_ASSERT_TRUE(true);
}

void setUp() {}
void tearDown() {}

void setup() {
  Serial.begin(9600);
  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  pinMode(ENCODER_BUTTON_PIN, INPUT_PULLUP);
  previousEncoderState =
      (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
  delay(1500);
  UNITY_BEGIN();
  RUN_TEST(test_both_oleds_acknowledge_and_show_time_scale);
  UNITY_END();
}

void loop() {}
