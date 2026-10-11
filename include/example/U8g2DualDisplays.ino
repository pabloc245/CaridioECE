/*
 * Deux écrans SSD1306 128x64 sur le même bus I2C :
 * gauche = 0x3C, droite = 0x3D.
 * Les pages U8G2 sont rendues séparément pour chaque adresse.
 */
#include <U8g2lib.h>
#include <Wire.h>

constexpr uint8_t ADDR_LEFT = 0x3C;
constexpr uint8_t ADDR_RIGHT = 0x3D;

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
uint16_t counter = 0;
uint8_t level = 0;

void drawLeft() {
  char text[12];
  snprintf(text, sizeof(text), "%u", counter);
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  u8g2.drawStr(4, 4, "GAUCHE 0x3C");
  u8g2.drawStr(4, 24, text);
  u8g2.drawFrame(0, 0, 128, 64);
}

void drawRight() {
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  u8g2.drawStr(4, 4, "DROITE 0x3D");
  u8g2.drawFrame(4, 24, 120, 16);
  u8g2.drawBox(6, 26, level, 12);
}

bool panelResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void drawPanel(uint8_t address, void (*draw)()) {
  u8g2.setI2CAddress(address << 1);
  u8g2.firstPage();
  do {
    draw();
  } while (u8g2.nextPage());
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  u8g2.setBusClock(100000UL);

  if (!panelResponds(ADDR_LEFT) || !panelResponds(ADDR_RIGHT)) {
    Serial.println(F("Verifier les adresses I2C 0x3C et 0x3D"));
    for (;;) delay(1000);
  }
  u8g2.setI2CAddress(ADDR_LEFT << 1);
  u8g2.begin();
  u8g2.setI2CAddress(ADDR_RIGHT << 1);
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
}

void loop() {
  drawPanel(ADDR_LEFT, drawLeft);
  drawPanel(ADDR_RIGHT, drawRight);
  ++counter;
  level = (level + 2) % 117;
}
