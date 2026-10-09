// Utilitaires partagés par les suites de test OLED (exécutées sur la carte).
// Header seulement, fonctions inline : rien n'est compilé si non utilisé.
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <DualSSD1306.h>
#include <unity.h>
#include <stdio.h>
#include <string.h>

// ===================== A ADAPTER A TON MONTAGE ============================
constexpr uint8_t ADDR_LEFT = 0x3C;    // écran de gauche (panneau 0)
constexpr uint8_t ADDR_RIGHT = 0x3D;   // écran de droite (panneau 1)
constexpr uint8_t ADDR_ABSENT = 0x3F;  // adresse où il n'y a RIEN sur le bus
constexpr uint8_t PANEL_W = 128;
constexpr uint8_t PANEL_H = 64;
// ==========================================================================

// Sur AVR, les chaînes littérales d'Unity vont en RAM (2 Ko seulement !).
// MSG() construit les messages depuis la FLASH dans un petit tampon partagé.
inline char *msgBuf() {
  static char b[64];
  return b;
}
#define MSG(fmt, ...) (snprintf_P(msgBuf(), 64, PSTR(fmt), ##__VA_ARGS__), msgBuf())

// RUN_TEST copie le nom du test en RAM (#fn est un littéral). RUN_P le garde en
// flash et ne le copie que le temps du test : ~30 octets de RAM économisés par test.
// Les noms doivent faire moins de 40 caractères.
#define RUN_P(fn)                                \
  do {                                           \
    static const char name_[] PROGMEM = #fn;     \
    char buf_[40];                               \
    strncpy_P(buf_, name_, sizeof(buf_) - 1);    \
    buf_[sizeof(buf_) - 1] = 0;                  \
    UnityDefaultTestRun(fn, buf_, __LINE__);     \
  } while (0)

// Recherche d'un texte (en flash) dans un StatusText
#define HAS(statusText, literal) (strstr_P((statusText).text(), PSTR(literal)) != nullptr)

inline bool ping(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Capture la sortie de printStatus() dans un tampon.
class StatusText : public Print {
 public:
  using Print::write;
  size_t write(uint8_t c) override {
    if (_n < sizeof(_buf) - 1) {
      _buf[_n++] = (char)c;
      _buf[_n] = 0;
    }
    return 1;
  }
  const char *text() const { return _buf; }

 private:
  char _buf[96] = {0};
  uint8_t _n = 0;
};

// L'objet testé vit sur le tas ; tearDown() le libère même si un test échoue
// en cours de route (Unity saute alors les destructeurs : sans ça, le buffer
// de 1 Ko fuirait et ferait échouer tous les tests suivants).
inline DualSSD1306 *&oledSlot() {
  static DualSSD1306 *p = nullptr;
  return p;
}
inline void releaseOled() {
  delete oledSlot();
  oledSlot() = nullptr;
}
inline DualSSD1306 &makeOled(DualSSD1306::Layout layout, bool sequential) {
  releaseOled();
  oledSlot() = new DualSSD1306(PANEL_W, PANEL_H, layout, &Wire, &Wire, -1, -1,
                               400000UL, 100000UL, sequential);
  return *oledSlot();
}

// begin() doit réussir ; sinon le message Unity est le diagnostic complet.
// La branche d'échec est dans une fonction à part : le compilateur réserve la
// pile d'une fonction dès son entrée, et le tampon de texte (96 octets) ne
// doit pas occuper la pile pendant le calloc du buffer d'écran.
__attribute__((noinline)) inline void failBegin(const DualSSD1306 &o) {
  StatusText t;
  o.printStatus(t);
  TEST_FAIL_MESSAGE(t.text());
}
inline void assertBeginOk(DualSSD1306 &o, uint8_t a0, uint8_t a1) {
  if (!o.begin(DUALSSD1306_SWITCHCAPVCC, a0, a1)) failBegin(o);
}

// Comme UNITY_BEGIN() mais sans __FILE__ en RAM (chemin de ~40 octets).
#define OLED_UNITY_BEGIN() UnityBegin("oled")
