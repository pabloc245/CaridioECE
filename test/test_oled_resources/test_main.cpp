// Ressources, sur la carte : RAM libre, fuite après échec, marge de pile
// pendant un rendu, durée d'envoi (détecte une horloge I2C trop lente) et
// restauration de l'horloge I2C.
#include "../oled_test_common.h"

// ---- Seuils : à ajuster si besoin ---------------------------------------
constexpr int MIN_FREE_RAM = 40;         // octets libres tas<->pile après begin()
constexpr int MIN_STACK_HEADROOM = 16;   // la pile ne doit pas frôler le tas pendant un rendu
constexpr uint16_t MAX_PANEL_MS = 60;    // ~25 ms attendus à 400 kHz, ~100 ms à 100 kHz

#if defined(__AVR__)
extern char *__brkval;
extern char __heap_start;
static char *heapEnd() { return __brkval ? __brkval : &__heap_start; }
static int freeRam() {
  char v;
  return (int)((uintptr_t)&v - (uintptr_t)heapEnd());
}
// Mesure de pile par "peinture" : on remplit la RAM libre avec 0xAA, on fait
// le travail, puis on compte les octets restés intacts = marge minimale.
static char *s_paintLimit;
static void paintStack() {
  noInterrupts();  // une ISR écrirait juste sous SP et serait écrasée
  char marker;
  s_paintLimit = (char *)((uintptr_t)&marker - 64);
  for (char *p = heapEnd(); p < s_paintLimit; p++) *p = (char)0xAA;
  interrupts();
}
static int untouchedStack() {
  int n = 0;
  for (char *p = heapEnd(); p < s_paintLimit && *p == (char)0xAA; p++) n++;
  return n;
}
#endif

static DualSSD1306 *g_oled;
static void drawHeavy() {  // texte + formes : sollicite la pile au maximum
  g_oled->setTextColor(DUALSSD1306_WHITE);
  g_oled->setTextSize(2);
  g_oled->setCursor(8, 8);
  g_oled->print(F("TEST PILE 123"));
  g_oled->drawCircle(128, 40, 20, DUALSSD1306_WHITE);
  g_oled->fillRect(100, 50, 56, 10, DUALSSD1306_WHITE);
  g_oled->drawLine(0, 0, 255, 63, DUALSSD1306_WHITE);
  g_oled->drawRoundRect(2, 2, 252, 60, 6, DUALSSD1306_WHITE);
}

void setUp() {}
void tearDown() { releaseOled(); }

void test_free_ram_after_begin() {
#if defined(__AVR__)
  DualSSD1306 &o = makeOled(DualSSD1306::SIDE_BY_SIDE, true);
  assertBeginOk(o, ADDR_LEFT, ADDR_RIGHT);
  const int f = freeRam();
  TEST_ASSERT_TRUE_MESSAGE(f >= MIN_FREE_RAM, MSG("RAM libre %d (min %d)", f, MIN_FREE_RAM));
#else
  TEST_IGNORE_MESSAGE("mesure de RAM : AVR uniquement");
#endif
}

void test_failed_begin_does_not_leak() {
#if defined(__AVR__)
  releaseOled();
  const int before = freeRam();
  DualSSD1306 &o = makeOled(DualSSD1306::SIDE_BY_SIDE, true);
  const bool failed = !o.begin(DUALSSD1306_SWITCHCAPVCC, ADDR_LEFT, ADDR_ABSENT);
  const int held = before - freeRam();  // octets encore pris après l'échec
  TEST_ASSERT_TRUE(failed);
  // Seul l'objet lui-même doit rester alloué, pas le buffer de 1 Ko.
  TEST_ASSERT_TRUE_MESSAGE(held < (int)sizeof(DualSSD1306) + 16,
                           MSG("retenu apres echec: %d octets", held));
  TEST_ASSERT_TRUE(o.begin(DUALSSD1306_SWITCHCAPVCC, ADDR_LEFT, ADDR_RIGHT));  // retry
  releaseOled();
  const int after = freeRam();
  TEST_ASSERT_TRUE_MESSAGE(abs(before - after) <= 8,
                           MSG("fuite: avant %d apres %d", before, after));
#else
  TEST_IGNORE_MESSAGE("mesure de RAM : AVR uniquement");
#endif
}

void test_full_buffer_refused_on_328p() {
#if defined(__AVR_ATmega328P__)
  releaseOled();
  const int before = freeRam();
  DualSSD1306 &o = makeOled(DualSSD1306::SIDE_BY_SIDE, false);  // 2 Ko : impossible
  const bool ok = o.begin(DUALSSD1306_SWITCHCAPVCC, ADDR_LEFT, ADDR_RIGHT);
  StatusText t;
  o.printStatus(t);
  TEST_ASSERT_FALSE_MESSAGE(ok, t.text());
  TEST_ASSERT_TRUE_MESSAGE(HAS(t, "RAM insuffisante"), t.text());
  TEST_ASSERT_TRUE((before - freeRam()) < (int)sizeof(DualSSD1306) + 16);
#else
  TEST_IGNORE_MESSAGE("test prevu pour l'ATmega328P (2 Ko de RAM)");
#endif
}

void test_stack_headroom_during_render() {
#if defined(__AVR__)
  DualSSD1306 &o = makeOled(DualSSD1306::SIDE_BY_SIDE, true);
  assertBeginOk(o, ADDR_LEFT, ADDR_RIGHT);
  g_oled = &o;
  paintStack();
  const bool ok = o.render(drawHeavy);
  const int headroom = untouchedStack();
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE_MESSAGE(headroom >= MIN_STACK_HEADROOM,
                           MSG("marge de pile %d octets (min %d)", headroom, MIN_STACK_HEADROOM));
#else
  TEST_IGNORE_MESSAGE("mesure de pile : AVR uniquement");
#endif
}

void test_each_panel_send_time() {
  DualSSD1306 &o = makeOled(DualSSD1306::SIDE_BY_SIDE, true);
  assertBeginOk(o, ADDR_LEFT, ADDR_RIGHT);
  for (uint8_t p = 0; p < 2; p++) {
    const uint32_t t0 = millis();
    const bool ok = o.pushPanel(p);
    const uint32_t dt = millis() - t0;
    TEST_ASSERT_TRUE_MESSAGE(ok, MSG("pushPanel(%u) I2C=%u", p, o.i2cStatus()));
    TEST_ASSERT_TRUE_MESSAGE(
        dt <= MAX_PANEL_MS,
        MSG("panneau %u: %lu ms (max %u)", p, (unsigned long)dt, MAX_PANEL_MS));
  }
}

void test_i2c_clock_restored() {
#if defined(TWBR)
  DualSSD1306 &o = makeOled(DualSSD1306::SIDE_BY_SIDE, true);
  const uint8_t expected = ((F_CPU / 100000UL) - 16) / 2;  // 100 kHz (clkAfter)
  assertBeginOk(o, ADDR_LEFT, ADDR_RIGHT);
  TEST_ASSERT_TRUE_MESSAGE(TWBR == expected,
                           MSG("apres begin: TWBR=%u (attendu %u)", (unsigned)TWBR, expected));
  TEST_ASSERT_TRUE(o.pushPanel(0));
  TEST_ASSERT_TRUE_MESSAGE(TWBR == expected,
                           MSG("apres envoi: TWBR=%u (attendu %u)", (unsigned)TWBR, expected));
#else
  TEST_IGNORE_MESSAGE("lecture de TWBR : AVR uniquement");
#endif
}

void setup() {
  Serial.begin(9600);
  delay(2000);
  Wire.begin();

  OLED_UNITY_BEGIN();
  RUN_P(test_free_ram_after_begin);
  RUN_P(test_failed_begin_does_not_leak);
  RUN_P(test_full_buffer_refused_on_328p);
  RUN_P(test_stack_headroom_during_render);
  RUN_P(test_each_panel_send_time);
  RUN_P(test_i2c_clock_restored);
  UNITY_END();
}

void loop() {}
