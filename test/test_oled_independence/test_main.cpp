// Indépendance des deux écrans, sur la carte :
//   - chaque adresse répond seule
//   - chaque écran fonctionne seul (l'autre n'est pas nécessaire)
//   - chaque panneau s'envoie seul, dans n'importe quel ordre d'adresses
//   - une erreur est attribuée au BON panneau, sans accuser l'autre
//   - le bus reste sain après un panneau manquant
#include "../oled_test_common.h"

static DualSSD1306 *g_oled;
static uint8_t g_calls;
static void drawCount() {
  g_calls++;
  g_oled->fillRect(0, 0, 4, 4, DUALSSD1306_WHITE);
}

void setUp() {}
void tearDown() { releaseOled(); }

// ---- 1. Le bus ----------------------------------------------------------
void test_left_acks() { TEST_ASSERT_TRUE(ping(ADDR_LEFT)); }
void test_right_acks() { TEST_ASSERT_TRUE(ping(ADDR_RIGHT)); }
// Précondition des tests d'erreur : cette adresse doit rester muette.
void test_absent_address_is_silent() { TEST_ASSERT_FALSE(ping(ADDR_ABSENT)); }

// ---- 2. Chaque écran seul ----------------------------------------------
static void aloneWorks(uint8_t addr) {
  DualSSD1306 &o = makeOled(DualSSD1306::MIRROR, false);
  assertBeginOk(o, addr, addr);
  o.clearDisplay();
  o.fillRect(0, 0, PANEL_W, PANEL_H, DUALSSD1306_WHITE);
  TEST_ASSERT_TRUE_MESSAGE(o.display(),
                           MSG("display 0x%02X, I2C=%u", addr, o.i2cStatus()));
  TEST_ASSERT_EQUAL_UINT8(0, o.i2cStatus());
}
void test_left_works_alone() { aloneWorks(ADDR_LEFT); }
void test_right_works_alone() { aloneWorks(ADDR_RIGHT); }

// ---- 3. Chaque panneau s'envoie seul ------------------------------------
void test_each_panel_can_be_pushed_alone() {
  DualSSD1306 &o = makeOled(DualSSD1306::SIDE_BY_SIDE, true);
  assertBeginOk(o, ADDR_LEFT, ADDR_RIGHT);
  o.select(0);
  o.fillRect(0, 0, PANEL_W, PANEL_H, DUALSSD1306_WHITE);
  TEST_ASSERT_TRUE_MESSAGE(o.pushPanel(0), MSG("pushPanel(0) I2C=%u", o.i2cStatus()));
  TEST_ASSERT_TRUE_MESSAGE(o.pushPanel(1), MSG("pushPanel(1) I2C=%u", o.i2cStatus()));
}

static void orderWorks(uint8_t a0, uint8_t a1) {
  DualSSD1306 &o = makeOled(DualSSD1306::SIDE_BY_SIDE, true);
  assertBeginOk(o, a0, a1);
  TEST_ASSERT_TRUE_MESSAGE(o.pushPanel(0), MSG("pushPanel(0) I2C=%u", o.i2cStatus()));
  TEST_ASSERT_TRUE_MESSAGE(o.pushPanel(1), MSG("pushPanel(1) I2C=%u", o.i2cStatus()));
}
void test_address_order_left_right() { orderWorks(ADDR_LEFT, ADDR_RIGHT); }
void test_address_order_right_left() { orderWorks(ADDR_RIGHT, ADDR_LEFT); }

// ---- 4. Dispositions ----------------------------------------------------
static void layoutRenders(DualSSD1306::Layout l, uint8_t expectedCalls) {
  DualSSD1306 &o = makeOled(l, true);
  assertBeginOk(o, ADDR_LEFT, ADDR_RIGHT);
  g_oled = &o;
  g_calls = 0;
  TEST_ASSERT_TRUE_MESSAGE(o.render(drawCount), MSG("render I2C=%u", o.i2cStatus()));
  TEST_ASSERT_EQUAL_UINT8(expectedCalls, g_calls);
}
void test_layout_side_by_side() { layoutRenders(DualSSD1306::SIDE_BY_SIDE, 2); }
void test_layout_stacked() { layoutRenders(DualSSD1306::STACKED, 2); }
void test_layout_mirror() { layoutRenders(DualSSD1306::MIRROR, 1); }

// ---- 5. Attribution des erreurs ----------------------------------------
static void expectMissing(uint8_t a0, uint8_t a1, int8_t panel) {
  DualSSD1306 &o = makeOled(DualSSD1306::SIDE_BY_SIDE, true);
  const bool ok = o.begin(DUALSSD1306_SWITCHCAPVCC, a0, a1);
  StatusText t;
  o.printStatus(t);
  TEST_ASSERT_FALSE_MESSAGE(ok, t.text());
  TEST_ASSERT_EQUAL_INT8_MESSAGE(panel, o.failedPanel(), t.text());
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, o.i2cStatus(), t.text());  // 2 = NACK adresse
  TEST_ASSERT_TRUE_MESSAGE(HAS(t, "ACK"), t.text());
  TEST_ASSERT_NULL(o.getBuffer());  // buffer libéré
}
void test_missing_right_blames_panel_1() { expectMissing(ADDR_LEFT, ADDR_ABSENT, 1); }
void test_missing_left_blames_panel_0() { expectMissing(ADDR_ABSENT, ADDR_RIGHT, 0); }
void test_both_missing_reports_panel_0() { expectMissing(ADDR_ABSENT, ADDR_ABSENT, 0); }

void test_same_address_rejected() {
  DualSSD1306 &o = makeOled(DualSSD1306::SIDE_BY_SIDE, true);
  TEST_ASSERT_FALSE(o.begin(DUALSSD1306_SWITCHCAPVCC, ADDR_LEFT, ADDR_LEFT));
  StatusText t;
  o.printStatus(t);
  TEST_ASSERT_TRUE_MESSAGE(HAS(t, "config invalide"), t.text());
  TEST_ASSERT_EQUAL_INT8(-1, o.failedPanel());  // pas un problème de panneau
  TEST_ASSERT_NULL(o.getBuffer());
}

// ---- 6. Récupération ----------------------------------------------------
void test_bus_healthy_after_missing_panel() {
  expectMissing(ADDR_LEFT, ADDR_ABSENT, 1);
  TEST_ASSERT_TRUE(ping(ADDR_LEFT));
  TEST_ASSERT_TRUE(ping(ADDR_RIGHT));
  DualSSD1306 &o = makeOled(DualSSD1306::SIDE_BY_SIDE, true);
  assertBeginOk(o, ADDR_LEFT, ADDR_RIGHT);  // et begin() réussit ensuite
}

void setup() {
  Serial.begin(9600);
  delay(2000);
  Wire.begin();

  OLED_UNITY_BEGIN();
  RUN_P(test_left_acks);
  RUN_P(test_right_acks);
  RUN_P(test_absent_address_is_silent);
  RUN_P(test_left_works_alone);
  RUN_P(test_right_works_alone);
  RUN_P(test_each_panel_can_be_pushed_alone);
  RUN_P(test_address_order_left_right);
  RUN_P(test_address_order_right_left);
  RUN_P(test_layout_side_by_side);
  RUN_P(test_layout_stacked);
  RUN_P(test_layout_mirror);
  RUN_P(test_missing_right_blames_panel_1);
  RUN_P(test_missing_left_blames_panel_0);
  RUN_P(test_both_missing_reports_panel_0);
  RUN_P(test_same_address_rejected);
  RUN_P(test_bus_healthy_after_missing_panel);
  UNITY_END();
}

void loop() {}
