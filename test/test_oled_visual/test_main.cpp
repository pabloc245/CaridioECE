// Vérifications VISUELLES guidées. Le bus ne peut pas dire ce qui s'affiche
// (le SSD1306 ne se relit pas en I2C) : chaque test affiche un motif, écrit
// dans le journal ce que TU dois voir, puis attend. L'assertion ne contrôle
// que le succès du bus ; le reste est à ton oeil.
//
// Sauter cette suite : test_ignore = test_oled_visual
#include "../oled_test_common.h"

constexpr uint16_t VISUAL_PAUSE_MS = 3000;

static DualSSD1306 *g_oled;

static void drawLeftOnly() {
  g_oled->setTextColor(DUALSSD1306_WHITE);
  g_oled->setTextSize(2);
  g_oled->setCursor(8, 24);
  g_oled->print(F("GAUCHE"));
  g_oled->drawRect(0, 0, 128, 64, DUALSSD1306_WHITE);
}
static void drawRightOnly() {
  g_oled->setTextColor(DUALSSD1306_WHITE);
  g_oled->setTextSize(2);
  g_oled->setCursor(136, 24);
  g_oled->print(F("DROITE"));
  g_oled->drawRect(128, 0, 128, 64, DUALSSD1306_WHITE);
}
static void drawJunction() {
  g_oled->setTextColor(DUALSSD1306_WHITE);
  g_oled->setTextSize(2);
  g_oled->setCursor(70, 24);
  g_oled->print(F("TRAVERSE"));
  g_oled->drawFastVLine(127, 0, 64, DUALSSD1306_WHITE);
  g_oled->drawFastVLine(128, 0, 64, DUALSSD1306_WHITE);
}
static void drawStacked() {
  g_oled->setTextColor(DUALSSD1306_WHITE);
  g_oled->setTextSize(2);
  g_oled->setCursor(30, 24);
  g_oled->print(F("HAUT"));
  g_oled->setCursor(30, 88);
  g_oled->print(F("BAS"));
  g_oled->drawFastHLine(0, 63, 128, DUALSSD1306_WHITE);
  g_oled->drawFastHLine(0, 64, 128, DUALSSD1306_WHITE);
}
static void drawMirror() {
  g_oled->setTextColor(DUALSSD1306_WHITE);
  g_oled->setTextSize(2);
  g_oled->setCursor(24, 24);
  g_oled->print(F("MIROIR"));
  g_oled->drawRect(0, 0, 128, 64, DUALSSD1306_WHITE);
}
static void drawBoth() {
  drawLeftOnly();
  drawRightOnly();
}

static void show(DualSSD1306::Layout l, void (*draw)()) {
  DualSSD1306 &o = makeOled(l, true);
  assertBeginOk(o, ADDR_LEFT, ADDR_RIGHT);
  g_oled = &o;
  TEST_ASSERT_TRUE_MESSAGE(o.render(draw), MSG("render I2C=%u", o.i2cStatus()));
  delay(VISUAL_PAUSE_MS);
}

void setUp() {}
void tearDown() { releaseOled(); }

void test_visual_left_only() {
  TEST_MESSAGE(MSG("VOIR: GAUCHE + cadre a gauche, droite VIDE"));
  show(DualSSD1306::SIDE_BY_SIDE, drawLeftOnly);
}
void test_visual_right_only() {
  TEST_MESSAGE(MSG("VOIR: DROITE + cadre a droite, gauche VIDE"));
  show(DualSSD1306::SIDE_BY_SIDE, drawRightOnly);
}
void test_visual_text_crosses_junction() {
  TEST_MESSAGE(MSG("VOIR: TRAVERSE lisible d'un ecran a l'autre, sans trou"));
  show(DualSSD1306::SIDE_BY_SIDE, drawJunction);
}
void test_visual_stacked() {
  TEST_MESSAGE(MSG("VOIR: gauche = HAUT (trait en bas), droite = BAS"));
  show(DualSSD1306::STACKED, drawStacked);
}
void test_visual_mirror() {
  TEST_MESSAGE(MSG("VOIR: MIROIR identique sur les deux ecrans"));
  show(DualSSD1306::MIRROR, drawMirror);
}
void test_visual_invert_right_only() {
  TEST_MESSAGE(MSG("VOIR: seul l'ecran DROIT s'inverse, puis revient"));
  show(DualSSD1306::SIDE_BY_SIDE, drawBoth);
  DualSSD1306 &o = *oledSlot();
  TEST_ASSERT_TRUE(o.command(1, 0xA7));  // inversion du panneau 1 seulement
  delay(VISUAL_PAUSE_MS);
  TEST_ASSERT_TRUE(o.command(1, 0xA6));
  delay(VISUAL_PAUSE_MS / 2);
}

void setup() {
  Serial.begin(9600);
  delay(2000);
  Wire.begin();

  OLED_UNITY_BEGIN();
  RUN_P(test_visual_left_only);
  RUN_P(test_visual_right_only);
  RUN_P(test_visual_text_crosses_junction);
  RUN_P(test_visual_stacked);
  RUN_P(test_visual_mirror);
  RUN_P(test_visual_invert_right_only);
  UNITY_END();
}

void loop() {}
