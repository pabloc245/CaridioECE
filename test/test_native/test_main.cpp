// Tests unitaires de DualSSD1306 (Unity, exécutés sur PC avec des mocks).
//   pio test -e native
#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Wire.h>

// --- Suivi des allocations : permet de simuler un calloc() qui échoue, de
// --- vérifier la taille du buffer et l'absence de fuite.
static bool g_failAlloc = false;
static int g_live = 0;
static size_t g_lastAlloc = 0;

static void *t_calloc(size_t n, size_t s) {
  if (g_failAlloc) return nullptr;
  void *p = calloc(n, s);
  if (p) { g_live++; g_lastAlloc = n * s; }
  return p;
}
static void t_free(void *p) {
  if (p) { g_live--; free(p); }
}
#define calloc t_calloc
#define free t_free
#include <DualSSD1306.h>
#undef calloc
#undef free

typedef DualSSD1306 DS;
typedef std::vector<uint8_t> Bytes;
typedef std::vector<Bytes> Seq;

// ============================== Helpers ==================================

// Transmissions d'init attendues, telles qu'Adafruit_SSD1306::begin() les envoie.
static Seq expectedInit(uint8_t h, uint8_t com, uint8_t contrast, uint8_t cp,
                        uint8_t pre) {
  return {{0x00, 0xAE, 0xD5, 0x80, 0xA8}, {0x00, (uint8_t)(h - 1)},
          {0x00, 0xD3, 0x00, 0x40, 0x8D}, {0x00, cp},
          {0x00, 0x20, 0x00, 0xA1, 0xC8}, {0x00, 0xDA},
          {0x00, com},                    {0x00, 0x81},
          {0x00, contrast},               {0x00, 0xD9},
          {0x00, pre},
          {0x00, 0xDB, 0x40, 0xA4, 0xA6, 0x2E, 0xAF}};
}

// Transmissions non vides envoyées à une adresse (les pings sont ignorés).
static Seq txTo(const TwoWire &w, uint8_t addr) {
  Seq r;
  for (size_t i = 0; i < w.log.size(); i++)
    if (w.log[i].addr == addr && !w.log[i].bytes.empty()) r.push_back(w.log[i].bytes);
  return r;
}

// Octets GDDRAM reçus par une adresse (transmissions 0x40 réussies).
static Bytes dataTo(const TwoWire &w, uint8_t addr) {
  Bytes r;
  for (size_t i = 0; i < w.log.size(); i++) {
    const Tx &t = w.log[i];
    if (t.addr == addr && t.status == 0 && t.bytes.size() > 1 && t.bytes[0] == 0x40)
      r.insert(r.end(), t.bytes.begin() + 1, t.bytes.end());
  }
  return r;
}

static void assertSeqEqual(const Seq &e, const Seq &a) {
  TEST_ASSERT_EQUAL_UINT32(e.size(), a.size());
  for (size_t i = 0; i < e.size(); i++) {
    char msg[40];
    snprintf(msg, sizeof msg, "transmission %u", (unsigned)(i + 1));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(e[i].size(), a[i].size(), msg);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(e[i].data(), a[i].data(), e[i].size(), msg);
  }
}

static void assertBytesEqual(const Bytes &e, const Bytes &a, const char *what) {
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(e.size(), a.size(), what);
  if (!e.empty()) TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(e.data(), a.data(), e.size(), what);
}

// Aucune transmission ne dépasse le tampon Wire (32 octets) ni n'échoue.
static void assertAllTxOk(const TwoWire &w) {
  for (size_t i = 0; i < w.log.size(); i++) {
    TEST_ASSERT_TRUE_MESSAGE(w.log[i].bytes.size() <= BUFFER_LENGTH, "transmission > 32 octets");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, w.log[i].status, "transmission en erreur");
  }
}

static int countNonZero(const uint8_t *b, size_t n) {
  int c = 0;
  for (size_t i = 0; i < n; i++) if (b[i]) c++;
  return c;
}

static std::string statusOf(const DS &d) {
  Print p;
  d.printStatus(p);
  return p.out;
}

static bool allZero(const Bytes &b) {
  for (size_t i = 0; i < b.size(); i++) if (b[i]) return false;
  return true;
}

// Dessin déterministe qui traverse la jonction entre les deux panneaux.
static DS *g_ds = nullptr;
static int g_drawCalls = 0;
static void drawPattern() {
  g_drawCalls++;
  uint32_t s = 12345;
  const int w = g_ds->width(), h = g_ds->height();
  for (int i = 0; i < 600; i++) {
    s = s * 1103515245u + 12345u;
    const int x = (int)((s >> 8) % (uint32_t)w);
    s = s * 1103515245u + 12345u;
    const int y = (int)((s >> 8) % (uint32_t)h);
    g_ds->drawPixel(x, y, 1);
  }
  g_ds->fillRect(w / 2 - 6, 3, 12, 20, 1);
  g_ds->drawPixel(w / 2, h / 2, 0);
}

// ============================ setUp / tearDown ===========================

extern "C" {
void setUp(void) {
  Wire.reset();
  Wire1.reset();
  arduino().reset();
  g_failAlloc = false;
  g_live = 0;
  g_lastAlloc = 0;
  g_ds = nullptr;
  g_drawCalls = 0;
}
void tearDown(void) {}
}

// ======================= begin() : séquence d'init =======================

void test_init_128x64_identique_a_adafruit(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  assertSeqEqual(expectedInit(64, 0x12, 0xCF, 0x14, 0xF1), txTo(Wire, 0x3C));
}

void test_init_128x32(void) {
  DS d(128, 32, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  assertSeqEqual(expectedInit(32, 0x02, 0x8F, 0x14, 0xF1), txTo(Wire, 0x3C));
}

void test_init_64x32(void) {
  DS d(64, 32, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  assertSeqEqual(expectedInit(32, 0x12, 0xCF, 0x14, 0xF1), txTo(Wire, 0x3C));
}

void test_init_96x16(void) {
  DS d(96, 16, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  assertSeqEqual(expectedInit(16, 0x02, 0xAF, 0x14, 0xF1), txTo(Wire, 0x3C));
}

void test_init_externalvcc(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_EXTERNALVCC, 0x3C, 0x3C));
  assertSeqEqual(expectedInit(64, 0x12, 0x9F, 0x10, 0x22), txTo(Wire, 0x3C));
}

void test_init_envoyee_a_chaque_panneau_meme_bus(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  const Seq exp = expectedInit(64, 0x12, 0xCF, 0x14, 0xF1);
  assertSeqEqual(exp, txTo(Wire, 0x3C));
  assertSeqEqual(exp, txTo(Wire, 0x3D));
  assertAllTxOk(Wire);
}

void test_init_deux_bus_meme_adresse(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire1);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  const Seq exp = expectedInit(64, 0x12, 0xCF, 0x14, 0xF1);
  assertSeqEqual(exp, txTo(Wire, 0x3C));
  assertSeqEqual(exp, txTo(Wire1, 0x3C));
}

void test_init_mirror_meme_peripherique_une_seule_fois(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  TEST_ASSERT_EQUAL_UINT32(12, txTo(Wire, 0x3C).size());
}

void test_ping_avant_init(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  TEST_ASSERT_TRUE(Wire.log.size() >= 2);
  TEST_ASSERT_EQUAL_UINT8(0x3C, Wire.log[0].addr);
  TEST_ASSERT_TRUE(Wire.log[0].bytes.empty());
  TEST_ASSERT_EQUAL_UINT8(0x3D, Wire.log[1].addr);
  TEST_ASSERT_TRUE(Wire.log[1].bytes.empty());
}

void test_begin_ok_status(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  TEST_ASSERT_EQUAL_STRING("DualSSD1306 begin: OK\n", statusOf(d).c_str());
}

// ========================= begin() : cas d'échec =========================

void test_echec_meme_bus_meme_adresse(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_FALSE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  TEST_ASSERT_NULL(d.getBuffer());
  TEST_ASSERT_EQUAL_INT(0, g_live);
  TEST_ASSERT_TRUE(Wire.log.empty());  // rien envoyé
  TEST_ASSERT_EQUAL_STRING(
      "DualSSD1306 begin: config invalide (adresse, ou meme bus+adresse)\n",
      statusOf(d).c_str());
}

void test_echec_adresse_invalide(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_FALSE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x00));
  TEST_ASSERT_EQUAL_INT8(1, d.failedPanel());
  TEST_ASSERT_EQUAL_STRING(
      "DualSSD1306 begin: config invalide (adresse, ou meme bus+adresse) panneau 1\n",
      statusOf(d).c_str());
}

void test_echec_taille_non_geree(void) {
  DS d(100, 50, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_FALSE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  TEST_ASSERT_NULL(d.getBuffer());
  TEST_ASSERT_EQUAL_STRING("DualSSD1306 begin: taille de panneau non geree\n",
                           statusOf(d).c_str());
}

void test_echec_allocation_puis_nouvel_essai(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  g_failAlloc = true;
  TEST_ASSERT_FALSE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  TEST_ASSERT_NULL(d.getBuffer());
  TEST_ASSERT_TRUE(Wire.log.empty());
  TEST_ASSERT_EQUAL_STRING("DualSSD1306 begin: RAM insuffisante pour le buffer\n",
                           statusOf(d).c_str());
  g_failAlloc = false;
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  TEST_ASSERT_NOT_NULL(d.getBuffer());
}

void test_echec_panneau_muet_libere_le_buffer_et_permet_un_retry(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  Wire.mute.push_back(0x3D);
  TEST_ASSERT_FALSE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  TEST_ASSERT_EQUAL_INT8(1, d.failedPanel());
  TEST_ASSERT_EQUAL_UINT8(2, d.i2cStatus());
  TEST_ASSERT_NULL(d.getBuffer());
  TEST_ASSERT_EQUAL_INT(0, g_live);  // pas de fuite
  TEST_ASSERT_EQUAL_STRING("DualSSD1306 begin: pas d'ACK a l'adresse panneau 1 I2C=2\n",
                           statusOf(d).c_str());
  Wire.mute.clear();
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  TEST_ASSERT_EQUAL_INT(1, g_live);
}

void test_echec_panneau0_muet_rien_envoye_en_init(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  Wire.mute.push_back(0x3C);
  TEST_ASSERT_FALSE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  TEST_ASSERT_EQUAL_INT8(0, d.failedPanel());
  TEST_ASSERT_TRUE(txTo(Wire, 0x3C).empty());
  TEST_ASSERT_TRUE(txTo(Wire, 0x3D).empty());  // on ne configure pas à moitié
}

void test_echec_init_refusee_etape1(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  Wire.refuse.push_back(0x3C);
  TEST_ASSERT_FALSE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  TEST_ASSERT_EQUAL_UINT8(1, d.failedInitStep());
  TEST_ASSERT_EQUAL_UINT8(3, d.i2cStatus());
  TEST_ASSERT_EQUAL_INT8(0, d.failedPanel());
  TEST_ASSERT_NULL(d.getBuffer());
  TEST_ASSERT_EQUAL_INT(0, g_live);
  TEST_ASSERT_EQUAL_STRING(
      "DualSSD1306 begin: echec de l'init, transmission n.1 panneau 0 I2C=3\n",
      statusOf(d).c_str());
  TEST_ASSERT_EQUAL_UINT32(1, txTo(Wire, 0x3C).size());  // arrêt à la 1re erreur
}

void test_echec_init_restaure_l_horloge(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  Wire.refuse.push_back(0x3C);
  TEST_ASSERT_FALSE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  TEST_ASSERT_EQUAL_UINT32(2, Wire.clocks.size());
  TEST_ASSERT_EQUAL_UINT32(400000UL, Wire.clocks[0]);
  TEST_ASSERT_EQUAL_UINT32(100000UL, Wire.clocks[1]);
}

void test_echec_ne_libere_pas_un_buffer_deja_valide(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  Wire.mute.push_back(0x3D);
  TEST_ASSERT_FALSE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  TEST_ASSERT_NOT_NULL(d.getBuffer());
  TEST_ASSERT_EQUAL_INT(1, g_live);
}

void test_destructeur_libere_le_buffer(void) {
  {
    DS d(128, 64, DS::MIRROR, &Wire, &Wire);
    TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
    TEST_ASSERT_EQUAL_INT(1, g_live);
  }
  TEST_ASSERT_EQUAL_INT(0, g_live);
}

// ====================== begin() : bus, reset, horloge ====================

void test_reset_broche_partagee_un_seul_pulse(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire, 4, 4);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  const std::vector<PinWrite> &w = arduino().writes;
  TEST_ASSERT_EQUAL_UINT32(3, w.size());
  TEST_ASSERT_EQUAL_INT(HIGH, w[0].level);
  TEST_ASSERT_EQUAL_INT(LOW, w[1].level);
  TEST_ASSERT_EQUAL_INT(HIGH, w[2].level);
  TEST_ASSERT_EQUAL_INT(4, w[1].pin);
  TEST_ASSERT_EQUAL_UINT32(2, arduino().delays.size());
  TEST_ASSERT_EQUAL_INT(1, arduino().delays[0]);
  TEST_ASSERT_EQUAL_INT(10, arduino().delays[1]);
}

void test_reset_deux_broches_distinctes(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire, 4, 5);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  const std::vector<PinWrite> &w = arduino().writes;
  TEST_ASSERT_EQUAL_UINT32(6, w.size());
  TEST_ASSERT_EQUAL_INT(4, w[0].pin);
  TEST_ASSERT_EQUAL_INT(5, w[3].pin);
}

void test_reset_desactive_ou_sans_broche(void) {
  {
    DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire, 4, 5);
    TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D, false));
    TEST_ASSERT_TRUE(arduino().writes.empty());
  }
  {
    DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);  // rst = -1
    TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
    TEST_ASSERT_TRUE(arduino().writes.empty());
  }
}

void test_wire_begin_appele_une_fois_par_bus(void) {
  {
    DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
    TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
    TEST_ASSERT_EQUAL_INT(1, Wire.beginCalls);
  }
  Wire.reset();
  {
    DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire1);
    TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
    TEST_ASSERT_EQUAL_INT(1, Wire.beginCalls);
    TEST_ASSERT_EQUAL_INT(1, Wire1.beginCalls);
  }
}

void test_wire_begin_non_appele_si_periphbegin_false(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D, true, false));
  TEST_ASSERT_EQUAL_INT(0, Wire.beginCalls);
}

void test_horloge_i2c_restauree_apres_begin(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  const uint32_t exp[] = {400000UL, 100000UL, 400000UL, 100000UL};
  TEST_ASSERT_EQUAL_UINT32(4, Wire.clocks.size());
  for (size_t i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT32(exp[i], Wire.clocks[i]);
}

// ==================== Tailles d'écran et de buffer =======================

void test_taille_ecran_logique_selon_disposition(void) {
  DS a(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  DS b(128, 64, DS::STACKED, &Wire, &Wire);
  DS c(128, 64, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_EQUAL_INT(256, a.width());  TEST_ASSERT_EQUAL_INT(64, a.height());
  TEST_ASSERT_EQUAL_INT(128, b.width());  TEST_ASSERT_EQUAL_INT(128, b.height());
  TEST_ASSERT_EQUAL_INT(128, c.width());  TEST_ASSERT_EQUAL_INT(64, c.height());
}

void test_taille_buffer(void) {
  struct Case { uint8_t w, h; DS::Layout l; bool seq; size_t bytes; };
  const Case cases[] = {
      {128, 64, DS::SIDE_BY_SIDE, false, 2048}, {128, 64, DS::SIDE_BY_SIDE, true, 1024},
      {128, 64, DS::STACKED, false, 2048},      {128, 64, DS::STACKED, true, 1024},
      {128, 64, DS::MIRROR, false, 1024},       {128, 64, DS::MIRROR, true, 1024},
      {128, 32, DS::MIRROR, false, 512},        {64, 32, DS::MIRROR, false, 256},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    setUp();
    DS d(cases[i].w, cases[i].h, cases[i].l, &Wire, &Wire, -1, -1, 400000UL, 100000UL,
         cases[i].seq);
    TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
    char msg[32];
    snprintf(msg, sizeof msg, "cas %u", (unsigned)i);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(cases[i].bytes, g_lastAlloc, msg);
  }
}

// ============================ Dessin / buffer ============================

void test_format_buffer_pages_ssd1306(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  d.drawPixel(10, 13, DUALSSD1306_WHITE);  // page 1, bit 5
  TEST_ASSERT_EQUAL_HEX8(0x20, d.getBuffer()[10 + 1 * 128]);
  TEST_ASSERT_EQUAL_INT(1, countNonZero(d.getBuffer(), 1024));
}

void test_couleurs_blanc_noir_inverse(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  d.drawPixel(3, 3, DUALSSD1306_WHITE);
  TEST_ASSERT_TRUE(d.getPixel(3, 3));
  d.drawPixel(3, 3, DUALSSD1306_INVERSE);
  TEST_ASSERT_FALSE(d.getPixel(3, 3));
  d.drawPixel(3, 3, DUALSSD1306_INVERSE);
  TEST_ASSERT_TRUE(d.getPixel(3, 3));
  d.drawPixel(3, 3, DUALSSD1306_BLACK);
  TEST_ASSERT_FALSE(d.getPixel(3, 3));
}

void test_pixels_hors_ecran_ignores(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  d.drawPixel(-1, 0, 1);
  d.drawPixel(128, 0, 1);
  d.drawPixel(0, -1, 1);
  d.drawPixel(0, 64, 1);
  d.drawPixel(32767, 32767, 1);
  TEST_ASSERT_EQUAL_INT(0, countNonZero(d.getBuffer(), 1024));
  TEST_ASSERT_FALSE(d.getPixel(-1, 0));
  TEST_ASSERT_FALSE(d.getPixel(128, 0));
}

void test_dessin_avant_begin_est_sans_danger(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  d.drawPixel(1, 1, 1);  // pas de buffer : ne doit pas planter
  d.clearDisplay();
  TEST_ASSERT_FALSE(d.getPixel(1, 1));
  TEST_ASSERT_FALSE(d.display());
}

void test_rotation(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  uint8_t *b = d.getBuffer();

  d.setRotation(2);  // (0,0) -> coin opposé
  d.drawPixel(0, 0, 1);
  TEST_ASSERT_EQUAL_HEX8(0x80, b[127 + 7 * 128]);
  d.clearDisplay();

  d.setRotation(1);  // écran logique 64x128
  TEST_ASSERT_EQUAL_INT(64, d.width());
  TEST_ASSERT_EQUAL_INT(128, d.height());
  d.drawPixel(0, 0, 1);
  TEST_ASSERT_EQUAL_HEX8(0x01, b[127]);
  d.drawPixel(63, 0, 1);
  TEST_ASSERT_EQUAL_HEX8(0x80, b[127 + 7 * 128]);
  d.clearDisplay();

  d.setRotation(3);
  d.drawPixel(0, 0, 1);
  TEST_ASSERT_EQUAL_HEX8(0x80, b[0 + 7 * 128]);
}

void test_getpixel_coherent_avec_drawpixel_toutes_rotations(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3C));
  for (uint8_t r = 0; r < 4; r++) {
    d.setRotation(r);
    d.clearDisplay();
    d.drawPixel(5, 9, 1);
    d.drawPixel(d.width() - 1, d.height() - 1, 1);
    TEST_ASSERT_TRUE(d.getPixel(5, 9));
    TEST_ASSERT_TRUE(d.getPixel(d.width() - 1, d.height() - 1));
    TEST_ASSERT_EQUAL_INT(2, countNonZero(d.getBuffer(), 1024));
  }
}

void test_clear_display(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  d.fillRect(0, 0, 256, 64, 1);
  TEST_ASSERT_TRUE(countNonZero(d.getBuffer(), 2048) > 0);
  d.clearDisplay();
  TEST_ASSERT_EQUAL_INT(0, countNonZero(d.getBuffer(), 2048));
}

// ===================== display() : découpage par panneau =================

void test_display_side_by_side_repartition(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  d.drawPixel(5, 3, 1);     // panneau 0, page 0, colonne 5, bit 3
  d.drawPixel(130, 10, 1);  // panneau 1, page 1, colonne 2, bit 2
  Wire.log.clear();
  TEST_ASSERT_TRUE(d.display());

  const Bytes d0 = dataTo(Wire, 0x3C), d1 = dataTo(Wire, 0x3D);
  TEST_ASSERT_EQUAL_UINT32(1024, d0.size());
  TEST_ASSERT_EQUAL_UINT32(1024, d1.size());
  TEST_ASSERT_EQUAL_HEX8(0x08, d0[0 * 128 + 5]);
  TEST_ASSERT_EQUAL_HEX8(0x04, d1[1 * 128 + 2]);

  Bytes e0, e1;  // tranches attendues du buffer
  for (int p = 0; p < 8; p++) {
    e0.insert(e0.end(), d.getBuffer() + p * 256, d.getBuffer() + p * 256 + 128);
    e1.insert(e1.end(), d.getBuffer() + p * 256 + 128, d.getBuffer() + p * 256 + 256);
  }
  assertBytesEqual(e0, d0, "panneau 0");
  assertBytesEqual(e1, d1, "panneau 1");
  assertAllTxOk(Wire);
}

void test_display_fenetre_colonnes_pages(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  Wire.log.clear();
  TEST_ASSERT_TRUE(d.display());
  const Bytes window = {0x00, 0x21, 0, 127, 0x22, 0, 7};
  assertBytesEqual(window, txTo(Wire, 0x3C)[0], "fenetre panneau 0");
  assertBytesEqual(window, txTo(Wire, 0x3D)[0], "fenetre panneau 1");
}

void test_display_forme_traversant_la_jonction(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  d.fillRect(124, 0, 8, 8, 1);  // 4 colonnes de chaque côté, page 0
  Wire.log.clear();
  TEST_ASSERT_TRUE(d.display());
  const Bytes d0 = dataTo(Wire, 0x3C), d1 = dataTo(Wire, 0x3D);
  for (int c = 0; c < 128; c++) {
    TEST_ASSERT_EQUAL_HEX8(c >= 124 ? 0xFF : 0x00, d0[c]);
    TEST_ASSERT_EQUAL_HEX8(c < 4 ? 0xFF : 0x00, d1[c]);
  }
}

void test_display_stacked_repartition(void) {
  DS d(128, 64, DS::STACKED, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  d.drawPixel(7, 70, 1);  // panneau 1, y local 6 -> page 0, bit 6
  Wire.log.clear();
  TEST_ASSERT_TRUE(d.display());
  const Bytes d0 = dataTo(Wire, 0x3C), d1 = dataTo(Wire, 0x3D);
  TEST_ASSERT_EQUAL_UINT32(1024, d0.size());
  TEST_ASSERT_EQUAL_UINT32(1024, d1.size());
  TEST_ASSERT_TRUE(allZero(d0));
  TEST_ASSERT_EQUAL_HEX8(0x40, d1[7]);
  TEST_ASSERT_EQUAL_INT(1, countNonZero(d1.data(), d1.size()));
}

void test_display_mirror_meme_image_sur_les_deux(void) {
  DS d(128, 64, DS::MIRROR, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  d.fillRect(10, 10, 30, 20, 1);
  Wire.log.clear();
  TEST_ASSERT_TRUE(d.display());
  const Bytes d0 = dataTo(Wire, 0x3C), d1 = dataTo(Wire, 0x3D);
  TEST_ASSERT_EQUAL_UINT32(1024, d0.size());
  assertBytesEqual(d0, d1, "mirror");
  const Bytes buf(d.getBuffer(), d.getBuffer() + 1024);
  assertBytesEqual(buf, d0, "mirror = buffer");
}

void test_display_panneau_unique_pushpanel(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  Wire.log.clear();
  TEST_ASSERT_TRUE(d.pushPanel(1));
  TEST_ASSERT_TRUE(txTo(Wire, 0x3C).empty());
  TEST_ASSERT_EQUAL_UINT32(1024, dataTo(Wire, 0x3D).size());
  TEST_ASSERT_FALSE(d.pushPanel(2));
}

void test_display_horloge_restauree(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  Wire.clocks.clear();
  TEST_ASSERT_TRUE(d.display());
  const uint32_t exp[] = {400000UL, 100000UL, 400000UL, 100000UL};
  TEST_ASSERT_EQUAL_UINT32(4, Wire.clocks.size());
  for (size_t i = 0; i < 4; i++) TEST_ASSERT_EQUAL_UINT32(exp[i], Wire.clocks[i]);
}

void test_display_erreur_i2c_signalee_et_arretee(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  Wire.refuse.push_back(0x3D);
  Wire.log.clear();
  TEST_ASSERT_FALSE(d.display());
  TEST_ASSERT_EQUAL_UINT8(3, d.i2cStatus());
  TEST_ASSERT_EQUAL_UINT32(1024, dataTo(Wire, 0x3C).size());  // panneau 0 complet
  TEST_ASSERT_TRUE(dataTo(Wire, 0x3D).empty());
  TEST_ASSERT_EQUAL_UINT32(1, txTo(Wire, 0x3D).size());       // arrêt à la 1re erreur
}

// ===================== Mode séquentiel (buffer d'un panneau) =============

static void compareSequentielAvecNormal(DS::Layout layout) {
  Bytes ref0, ref1;
  {
    DS d(128, 64, layout, &Wire, &Wire);
    TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
    g_ds = &d;
    d.clearDisplay();
    drawPattern();
    Wire.log.clear();
    TEST_ASSERT_TRUE(d.display());
    ref0 = dataTo(Wire, 0x3C);
    ref1 = dataTo(Wire, 0x3D);
  }
  TEST_ASSERT_FALSE(allZero(ref0));  // le test doit être significatif
  TEST_ASSERT_FALSE(allZero(ref1));

  Wire.reset();
  g_drawCalls = 0;
  {
    DS d(128, 64, layout, &Wire, &Wire, -1, -1, 400000UL, 100000UL, true);
    TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
    g_ds = &d;
    Wire.log.clear();
    TEST_ASSERT_TRUE(d.render(drawPattern));
    assertBytesEqual(ref0, dataTo(Wire, 0x3C), "panneau 0 sequentiel");
    assertBytesEqual(ref1, dataTo(Wire, 0x3D), "panneau 1 sequentiel");
    assertAllTxOk(Wire);
  }
  TEST_ASSERT_EQUAL_INT(layout == DS::MIRROR ? 1 : 2, g_drawCalls);
}

void test_sequentiel_identique_au_normal_side_by_side(void) {
  compareSequentielAvecNormal(DS::SIDE_BY_SIDE);
}
void test_sequentiel_identique_au_normal_stacked(void) {
  compareSequentielAvecNormal(DS::STACKED);
}
void test_sequentiel_identique_au_normal_mirror(void) {
  compareSequentielAvecNormal(DS::MIRROR);
}

void test_sequentiel_display_refuse(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire, -1, -1, 400000UL, 100000UL, true);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  Wire.log.clear();
  TEST_ASSERT_FALSE(d.display());
  TEST_ASSERT_TRUE(Wire.log.empty());
}

void test_sequentiel_select_filtre_et_efface(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire, -1, -1, 400000UL, 100000UL, true);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  d.select(0);
  d.drawPixel(130, 10, 1);  // appartient au panneau 1 : ignoré
  TEST_ASSERT_EQUAL_INT(0, countNonZero(d.getBuffer(), 1024));
  d.drawPixel(5, 3, 1);
  TEST_ASSERT_EQUAL_INT(1, countNonZero(d.getBuffer(), 1024));
  d.select(1);  // select() efface
  TEST_ASSERT_EQUAL_INT(0, countNonZero(d.getBuffer(), 1024));
  d.drawPixel(130, 10, 1);
  TEST_ASSERT_TRUE(d.getPixel(130, 10));
  TEST_ASSERT_FALSE(d.getPixel(5, 3));
}

void test_sequentiel_flushpanel_cible_le_bon_panneau(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire, -1, -1, 400000UL, 100000UL, true);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  d.select(1);
  Wire.log.clear();
  TEST_ASSERT_TRUE(d.flushPanel());
  TEST_ASSERT_TRUE(txTo(Wire, 0x3C).empty());
  TEST_ASSERT_EQUAL_UINT32(1024, dataTo(Wire, 0x3D).size());
}

// ========================= Réglages communs ==============================

void test_invert_display(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  Wire.log.clear();
  d.invertDisplay(true);
  assertSeqEqual({{0x00, 0xA7}}, txTo(Wire, 0x3C));
  assertSeqEqual({{0x00, 0xA7}}, txTo(Wire, 0x3D));
  Wire.log.clear();
  d.invertDisplay(false);
  assertSeqEqual({{0x00, 0xA6}}, txTo(Wire, 0x3C));
}

void test_contraste_et_dim(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  Wire.log.clear();
  d.setContrast(0x42);
  assertSeqEqual({{0x00, 0x81, 0x42}}, txTo(Wire, 0x3C));
  assertSeqEqual({{0x00, 0x81, 0x42}}, txTo(Wire, 0x3D));
  Wire.log.clear();
  d.dim(true);
  assertSeqEqual({{0x00, 0x81, 0x00}}, txTo(Wire, 0x3C));
  Wire.log.clear();
  d.dim(false);  // revient au dernier contraste réglé
  assertSeqEqual({{0x00, 0x81, 0x42}}, txTo(Wire, 0x3D));
}

void test_display_on_off_et_commande_brute(void) {
  DS d(128, 64, DS::SIDE_BY_SIDE, &Wire, &Wire);
  TEST_ASSERT_TRUE(d.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D));
  Wire.log.clear();
  d.displayOn(false);
  assertSeqEqual({{0x00, 0xAE}}, txTo(Wire, 0x3C));
  assertSeqEqual({{0x00, 0xAE}}, txTo(Wire, 0x3D));
  Wire.log.clear();
  TEST_ASSERT_TRUE(d.command(1, 0xA7));
  TEST_ASSERT_TRUE(txTo(Wire, 0x3C).empty());
  assertSeqEqual({{0x00, 0xA7}}, txTo(Wire, 0x3D));
  TEST_ASSERT_FALSE(d.command(2, 0xA7));
}

// ================================= main ==================================

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_init_128x64_identique_a_adafruit);
  RUN_TEST(test_init_128x32);
  RUN_TEST(test_init_64x32);
  RUN_TEST(test_init_96x16);
  RUN_TEST(test_init_externalvcc);
  RUN_TEST(test_init_envoyee_a_chaque_panneau_meme_bus);
  RUN_TEST(test_init_deux_bus_meme_adresse);
  RUN_TEST(test_init_mirror_meme_peripherique_une_seule_fois);
  RUN_TEST(test_ping_avant_init);
  RUN_TEST(test_begin_ok_status);

  RUN_TEST(test_echec_meme_bus_meme_adresse);
  RUN_TEST(test_echec_adresse_invalide);
  RUN_TEST(test_echec_taille_non_geree);
  RUN_TEST(test_echec_allocation_puis_nouvel_essai);
  RUN_TEST(test_echec_panneau_muet_libere_le_buffer_et_permet_un_retry);
  RUN_TEST(test_echec_panneau0_muet_rien_envoye_en_init);
  RUN_TEST(test_echec_init_refusee_etape1);
  RUN_TEST(test_echec_init_restaure_l_horloge);
  RUN_TEST(test_echec_ne_libere_pas_un_buffer_deja_valide);
  RUN_TEST(test_destructeur_libere_le_buffer);

  RUN_TEST(test_reset_broche_partagee_un_seul_pulse);
  RUN_TEST(test_reset_deux_broches_distinctes);
  RUN_TEST(test_reset_desactive_ou_sans_broche);
  RUN_TEST(test_wire_begin_appele_une_fois_par_bus);
  RUN_TEST(test_wire_begin_non_appele_si_periphbegin_false);
  RUN_TEST(test_horloge_i2c_restauree_apres_begin);

  RUN_TEST(test_taille_ecran_logique_selon_disposition);
  RUN_TEST(test_taille_buffer);

  RUN_TEST(test_format_buffer_pages_ssd1306);
  RUN_TEST(test_couleurs_blanc_noir_inverse);
  RUN_TEST(test_pixels_hors_ecran_ignores);
  RUN_TEST(test_dessin_avant_begin_est_sans_danger);
  RUN_TEST(test_rotation);
  RUN_TEST(test_getpixel_coherent_avec_drawpixel_toutes_rotations);
  RUN_TEST(test_clear_display);

  RUN_TEST(test_display_side_by_side_repartition);
  RUN_TEST(test_display_fenetre_colonnes_pages);
  RUN_TEST(test_display_forme_traversant_la_jonction);
  RUN_TEST(test_display_stacked_repartition);
  RUN_TEST(test_display_mirror_meme_image_sur_les_deux);
  RUN_TEST(test_display_panneau_unique_pushpanel);
  RUN_TEST(test_display_horloge_restauree);
  RUN_TEST(test_display_erreur_i2c_signalee_et_arretee);

  RUN_TEST(test_sequentiel_identique_au_normal_side_by_side);
  RUN_TEST(test_sequentiel_identique_au_normal_stacked);
  RUN_TEST(test_sequentiel_identique_au_normal_mirror);
  RUN_TEST(test_sequentiel_display_refuse);
  RUN_TEST(test_sequentiel_select_filtre_et_efface);
  RUN_TEST(test_sequentiel_flushpanel_cible_le_bon_panneau);

  RUN_TEST(test_invert_display);
  RUN_TEST(test_contraste_et_dim);
  RUN_TEST(test_display_on_off_et_commande_brute);

  return UNITY_END();
}
