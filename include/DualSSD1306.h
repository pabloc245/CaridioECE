/*
 * DualSSD1306 - deux écrans OLED SSD1306 (I2C) pilotés comme un seul écran
 * logique, avec un unique buffer partagé. Dérive de Adafruit_GFX.
 *
 * Dispositions :
 *   SIDE_BY_SIDE : 2 panneaux côte à côte  -> écran logique (2*pw) x ph
 *   STACKED      : 2 panneaux empilés      -> écran logique pw x (2*ph)
 *   MIRROR       : même image sur les deux -> écran logique pw x ph
 *
 * Mode séquentiel (sequential = true) : le buffer ne contient qu'UN panneau
 * (1 Ko pour du 128x64). On dessine la scène deux fois, une par panneau :
 *   select(0); dessine(); flush(); select(1); dessine(); flush();
 * ou simplement render(dessine). Les pixels hors du panneau courant sont
 * ignorés dans drawPixel().
 *
 * Format du buffer : identique à SSD1306 (1 octet = 8 pixels verticaux),
 * index = x + (y / 8) * largeur_logique.
 */
#pragma once

#include <Adafruit_GFX.h>
#include <Wire.h>

#define DUALSSD1306_BLACK   0
#define DUALSSD1306_WHITE   1
#define DUALSSD1306_INVERSE 2

#define DUALSSD1306_EXTERNALVCC  0x01
#define DUALSSD1306_SWITCHCAPVCC 0x02

// Taille max d'un paquet I2C (octet de contrôle inclus)
#if defined(I2C_BUFFER_LENGTH)
  #define DUALSSD1306_WIRE_MAX ((I2C_BUFFER_LENGTH) > 128 ? 128 : (I2C_BUFFER_LENGTH))
#elif defined(BUFFER_LENGTH)
  #define DUALSSD1306_WIRE_MAX ((BUFFER_LENGTH) > 128 ? 128 : (BUFFER_LENGTH))
#else
  #define DUALSSD1306_WIRE_MAX 32
#endif

class DualSSD1306 : public Adafruit_GFX {
public:
  enum Layout : uint8_t { SIDE_BY_SIDE, STACKED, MIRROR };

  /**
   * @param pw, ph   taille d'UN panneau (ex. 128 x 64)
   * @param layout   disposition des deux panneaux
   * @param wire0/1  bus I2C de chaque panneau (peuvent être identiques si
   *                 les adresses diffèrent)
   * @param rst0/1   broches reset (-1 si non utilisées ; peuvent être communes)
   * @param clkDuring  fréquence I2C pendant les transferts
   * @param clkAfter   fréquence I2C restaurée ensuite
   */
  DualSSD1306(uint8_t pw, uint8_t ph, Layout layout,
              TwoWire *wire0 = &Wire, TwoWire *wire1 = &Wire,
              int8_t rst0 = -1, int8_t rst1 = -1,
              uint32_t clkDuring = 400000UL, uint32_t clkAfter = 100000UL,
              bool sequential = false)
      : Adafruit_GFX(layout == SIDE_BY_SIDE ? pw * 2 : pw,
                     layout == STACKED ? ph * 2 : ph),
        _pw(pw), _ph(ph), _layout(layout),
        _clkDuring(clkDuring), _clkAfter(clkAfter), _seq(sequential) {
    _wire[0] = wire0 ? wire0 : &Wire;
    _wire[1] = wire1 ? wire1 : &Wire;
    _rst[0] = rst0;
    _rst[1] = rst1;
    _addr[0] = _addr[1] = 0;
  }

  ~DualSSD1306() {
    if (_buf) free(_buf);
  }

  /**
   * Alloue le buffer, initialise les bus, fait le reset, vérifie que chaque
   * panneau répond, puis envoie l'init dans le même découpage qu'Adafruit :
   * petites transmissions (une liste de commandes, puis les commandes à
   * argument une par une).
   *
   * @param vcs          DUALSSD1306_SWITCHCAPVCC (cas courant) ou _EXTERNALVCC
   * @param addr0/addr1  adresses I2C 7 bits (ex. 0x3C et 0x3D)
   * @param reset        pulse sur les broches reset si définies
   * @param periphBegin  appeler Wire.begin() ici ; false si déjà fait
   * @return true si tout est OK. Sinon false : printStatus(Serial) dit
   *         quelle étape a échoué, sur quel panneau, avec quel code I2C.
   *
   * Après un échec, le buffer alloué par CET appel est libéré : on peut
   * corriger et rappeler begin().
   */
  bool begin(uint8_t vcs, uint8_t addr0, uint8_t addr1,
             bool reset = true, bool periphBegin = true) {
    _fail = FAIL_NONE;
    _failPanel = -1;
    _initStep = 0;
    _i2c = 0;

    // 1. Validation : rien n'est alloué ni envoyé si la config est invalide
    if (!validAddr(addr0)) return fail(FAIL_ARGS, 0);
    if (!validAddr(addr1)) return fail(FAIL_ARGS, 1);
    const bool sameDevice = (_wire[0] == _wire[1]) && (addr0 == addr1);
    if (sameDevice && _layout != MIRROR) return fail(FAIL_ARGS);

    const bool ext = (vcs == DUALSSD1306_EXTERNALVCC);
    uint8_t comPins = 0;
    if (!modelParams(ext, comPins, _contrast)) return fail(FAIL_SIZE);

    // 2. Mémoire
    const bool allocHere = (_buf == nullptr);
    if (allocHere) {
      _buf = (uint8_t *)calloc(bufBytes(), 1);
      if (!_buf) return fail(FAIL_ALLOC);
    }

    _vcs = vcs;
    _addr[0] = addr0;
    _addr[1] = addr1;
    const uint8_t nPanels = sameDevice ? 1 : 2;  // MIRROR sur un seul périphérique

    // 3. Bus et reset
    if (periphBegin) {
      _wire[0]->begin();
      if (_wire[1] != _wire[0]) _wire[1]->begin();
    }
    if (reset) {
      resetPin(_rst[0]);
      if (_rst[1] != _rst[0]) resetPin(_rst[1]);  // broche partagée : un seul pulse
    }

    // 4. Chaque panneau acquitte-t-il son adresse ?
    for (uint8_t i = 0; i < nPanels; i++) {
      const uint8_t st = ping(i);
      if (st) {
        _i2c = st;
        return fail(FAIL_PROBE, i, allocHere);
      }
    }

    // 5. Init. Chaque entrée = [nombre d'octets, octets...] = UNE transmission
    //    I2C, exactement comme Adafruit_SSD1306::begin().
    const uint8_t plan[] = {
        4, CMD_DISPLAY_OFF, CMD_CLOCK_DIV, 0x80, CMD_MULTIPLEX,
        1, (uint8_t)(_ph - 1),
        4, CMD_DISPLAY_OFFSET, 0x00, CMD_START_LINE, CMD_CHARGE_PUMP,
        1, (uint8_t)(ext ? 0x10 : 0x14),
        4, CMD_MEMORY_MODE, 0x00, (uint8_t)(CMD_SEG_REMAP | 0x01), CMD_COM_SCAN_DEC,
        1, CMD_COM_PINS,
        1, comPins,
        1, CMD_CONTRAST,
        1, _contrast,
        1, CMD_PRECHARGE,
        1, (uint8_t)(ext ? 0x22 : 0xF1),
        6, CMD_VCOM_DETECT, 0x40, CMD_ALL_ON_RESUME, CMD_NORMAL, CMD_SCROLL_OFF,
           CMD_DISPLAY_ON};

    for (uint8_t i = 0; i < nPanels; i++) {
      bool ok = true;
      uint8_t k = 0;  // numéro de la transmission en cours
      clockStart(i);
      for (const uint8_t *q = plan; ok && q < plan + sizeof(plan);) {
        const uint8_t n = *q++;
        ++k;
        ok = sendCommands(i, q, n);
        q += n;
      }
      clockEnd(i);  // horloge restaurée même en cas d'échec
      if (!ok) {
        _initStep = k;
        return fail(FAIL_INIT, i, allocHere);
      }
    }
    return true;
  }

  // ---- Diagnostic après un begin() qui a renvoyé false -------------------

  /// Dernier code d'erreur Wire non nul (endTransmission) :
  /// 1 trop long, 2 NACK adresse, 3 NACK donnée, 4 autre, 5 timeout.
  uint8_t i2cStatus() const { return _i2c; }

  /// Panneau concerné (0 ou 1), -1 si non applicable.
  int8_t failedPanel() const { return _failPanel; }

  /// Numéro de la transmission d'init qui a échoué (1..12), 0 si autre cause.
  uint8_t failedInitStep() const { return _initStep; }

  /// Affiche la cause de l'échec, ex. printStatus(Serial).
  void printStatus(Print &out) const {
    out.print(F("DualSSD1306 begin: "));
    switch (_fail) {
      case FAIL_NONE:  out.print(F("OK")); break;
      case FAIL_ARGS:  out.print(F("config invalide (adresse, ou meme bus+adresse)")); break;
      case FAIL_SIZE:  out.print(F("taille de panneau non geree")); break;
      case FAIL_ALLOC: out.print(F("RAM insuffisante pour le buffer")); break;
      case FAIL_PROBE: out.print(F("pas d'ACK a l'adresse")); break;
      case FAIL_INIT:
        out.print(F("echec de l'init, transmission n."));
        out.print(_initStep);
        break;
    }
    if (_failPanel >= 0) {
      out.print(F(" panneau "));
      out.print(_failPanel);
    }
    if (_i2c) {
      out.print(F(" I2C="));
      out.print(_i2c);
    }
    out.println();
  }

  // ---- Dessin ------------------------------------------------------------

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (!_buf || x < 0 || x >= _width || y < 0 || y >= _height) return;

    switch (getRotation()) {
      case 1: { int16_t t = x; x = WIDTH - 1 - y; y = t; break; }
      case 2: x = WIDTH - 1 - x; y = HEIGHT - 1 - y; break;
      case 3: { int16_t t = x; x = y; y = HEIGHT - 1 - t; break; }
    }

    if (_seq) {  // ne garder que le panneau courant
      x -= _offX;
      y -= _offY;
      if (x < 0 || x >= _pw || y < 0 || y >= _ph) return;
    }

    uint8_t *p = &_buf[x + (y >> 3) * stride()];
    const uint8_t m = 1 << (y & 7);
    switch (color) {
      case DUALSSD1306_WHITE:   *p |= m;  break;
      case DUALSSD1306_BLACK:   *p &= ~m; break;
      case DUALSSD1306_INVERSE: *p ^= m;  break;
    }
  }

  bool getPixel(int16_t x, int16_t y) const {
    if (!_buf || x < 0 || x >= _width || y < 0 || y >= _height) return false;
    switch (rotation) {
      case 1: { int16_t t = x; x = WIDTH - 1 - y; y = t; break; }
      case 2: x = WIDTH - 1 - x; y = HEIGHT - 1 - y; break;
      case 3: { int16_t t = x; x = y; y = HEIGHT - 1 - t; break; }
    }
    if (_seq) {
      x -= _offX;
      y -= _offY;
      if (x < 0 || x >= _pw || y < 0 || y >= _ph) return false;
    }
    return _buf[x + (y >> 3) * stride()] & (1 << (y & 7));
  }

  void clearDisplay() {
    if (_buf) memset(_buf, 0, bufBytes());
  }

  uint8_t *getBuffer() { return _buf; }

  // ---- Envoi aux panneaux ------------------------------------------------

  /// Mode séquentiel : choisit le panneau à dessiner (0 ou 1) et efface le
  /// buffer. Après select(), redessine toute la scène.
  void select(uint8_t i) {
    _cur = i ? 1 : 0;
    _offX = (_layout == SIDE_BY_SIDE) ? _cur * _pw : 0;
    _offY = (_layout == STACKED) ? _cur * _ph : 0;
    clearDisplay();
  }

  /// Mode séquentiel : envoie le buffer au panneau sélectionné.
  bool flushScreen() { return pushPanel(_cur); }

  /// Dessine et envoie toute l'image. `draw` est appelée une fois par
  /// panneau en mode séquentiel (une seule fois sinon, ou en MIRROR).
  /// `draw` doit être déterministe : même résultat à chaque appel.
  bool render(void (*draw)()) {
    if (!_seq) {
      clearDisplay();
      draw();
      return display();
    }
    bool ok = true;
    const uint8_t passes = (_layout == MIRROR) ? 1 : 2;
    for (uint8_t p = 0; p < passes; p++) {
      select(p);
      draw();
      if (_layout == MIRROR) {
        ok &= pushPanel(0);
        ok &= pushPanel(1);
      } else {
        ok &= pushPanel(p);
      }
    }
    return ok;
  }

  /// Envoie le buffer aux deux panneaux (mode normal uniquement).
  bool display() {
    if (_seq) return false;  // utiliser select()/flush() ou render()
    bool ok = pushPanel(0);
    ok &= pushPanel(1);
    return ok;
  }

  /// Envoie le buffer à un seul panneau (0 ou 1).
  bool pushPanel(uint8_t i) {
    if (!_buf || i > 1) return false;

    uint16_t colOff = 0, pageOff = 0;
    if (!_seq) {
      if (_layout == SIDE_BY_SIDE) colOff = i * _pw;
      else if (_layout == STACKED) pageOff = i * (_ph / 8);
    }

    const uint8_t pages = (_ph + 7) / 8;
    const uint8_t window[] = {CMD_COL_ADDR,  0, (uint8_t)(_pw - 1),
                              CMD_PAGE_ADDR, 0, (uint8_t)(pages - 1)};

    clockStart(i);
    bool ok = sendCommands(i, window, sizeof(window));
    for (uint8_t p = 0; ok && p < pages; p++)
      ok = sendData(i, _buf + (size_t)(pageOff + p) * stride() + colOff, _pw);
    clockEnd(i);
    return ok;
  }

  // ---- Réglages communs aux deux panneaux --------------------------------

  void invertDisplay(bool inv) { sendBoth(inv ? CMD_INVERT : CMD_NORMAL); }

  void setContrast(uint8_t c) {
    _contrast = c;
    sendBoth2(CMD_CONTRAST, c);
  }

  void dim(bool d) { sendBoth2(CMD_CONTRAST, d ? 0 : _contrast); }

  void displayOn(bool on) { sendBoth(on ? CMD_DISPLAY_ON : CMD_DISPLAY_OFF); }

  /// Commande brute vers un seul panneau.
  bool command(uint8_t panel, uint8_t c) {
    if (panel > 1) return false;
    clockStart(panel);
    bool ok = sendCommands(panel, &c, 1);
    clockEnd(panel);
    return ok;
  }

private:
  // Codes de commande SSD1306
  enum : uint8_t {
    CMD_CONTRAST      = 0x81,
    CMD_ALL_ON_RESUME = 0xA4,
    CMD_NORMAL        = 0xA6,
    CMD_INVERT        = 0xA7,
    CMD_DISPLAY_OFF   = 0xAE,
    CMD_DISPLAY_ON    = 0xAF,
    CMD_DISPLAY_OFFSET = 0xD3,
    CMD_COM_PINS      = 0xDA,
    CMD_VCOM_DETECT   = 0xDB,
    CMD_CLOCK_DIV     = 0xD5,
    CMD_PRECHARGE     = 0xD9,
    CMD_MULTIPLEX     = 0xA8,
    CMD_START_LINE    = 0x40,
    CMD_MEMORY_MODE   = 0x20,
    CMD_COL_ADDR      = 0x21,
    CMD_PAGE_ADDR     = 0x22,
    CMD_COM_SCAN_DEC  = 0xC8,
    CMD_SEG_REMAP     = 0xA0,
    CMD_CHARGE_PUMP   = 0x8D,
    CMD_SCROLL_OFF    = 0x2E
  };

  enum : uint8_t {
    FAIL_NONE = 0, FAIL_ARGS, FAIL_SIZE, FAIL_ALLOC, FAIL_PROBE, FAIL_INIT
  };

  // Point de sortie unique des erreurs de begin() : mémorise la cause et
  // libère le buffer si c'est begin() qui vient de l'allouer.
  bool fail(uint8_t code, int8_t panel = -1, bool freeBuf = false) {
    if (freeBuf && _buf) {
      free(_buf);
      _buf = nullptr;
    }
    _fail = code;
    _failPanel = panel;
    return false;
  }

  static bool validAddr(uint8_t a) { return a >= 0x08 && a <= 0x77; }

  // Paquet vide : renvoie 0 si le panneau acquitte son adresse.
  uint8_t ping(uint8_t i) {
    _wire[i]->beginTransmission(_addr[i]);
    return _wire[i]->endTransmission();
  }

  // comPins et contraste selon le modèle ; false si taille inconnue.
  bool modelParams(bool ext, uint8_t &comPins, uint8_t &contrast) const {
    if (_pw == 128 && _ph == 64) {
      comPins = 0x12; contrast = ext ? 0x9F : 0xCF;
    } else if (_pw == 128 && _ph == 32) {
      comPins = 0x02; contrast = 0x8F;
    } else if (_pw == 96 && _ph == 16) {
      comPins = 0x02; contrast = ext ? 0x10 : 0xAF;
    } else if (_pw == 64 && _ph == 32) {
      comPins = 0x12; contrast = ext ? 0x10 : 0xCF;
    } else {
      return false;
    }
    return true;
  }

  uint16_t stride() const { return _seq ? _pw : WIDTH; }
  size_t bufBytes() const {
    return (size_t)stride() * (((_seq ? _ph : HEIGHT) + 7) / 8);
  }

  static void resetPin(int8_t pin) {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    delay(1);
    digitalWrite(pin, LOW);
    delay(10);
    digitalWrite(pin, HIGH);
  }

  void clockStart(uint8_t i) { _wire[i]->setClock(_clkDuring); }
  void clockEnd(uint8_t i)   { _wire[i]->setClock(_clkAfter); }

  // Envoie n octets de commande (préfixe 0x00), découpés selon la taille du
  // buffer Wire. S'arrête à la première erreur et mémorise son code.
  bool sendCommands(uint8_t i, const uint8_t *c, uint8_t n) {
    TwoWire *w = _wire[i];
    while (n) {
      uint8_t chunk = (n > DUALSSD1306_WIRE_MAX - 1) ? DUALSSD1306_WIRE_MAX - 1 : n;
      w->beginTransmission(_addr[i]);
      w->write((uint8_t)0x00);
      w->write(c, chunk);
      const uint8_t st = w->endTransmission();
      if (st) { _i2c = st; return false; }
      c += chunk;
      n -= chunk;
    }
    return true;
  }

  // Envoie n octets de données GDDRAM (préfixe 0x40), même principe.
  bool sendData(uint8_t i, const uint8_t *d, uint16_t n) {
    TwoWire *w = _wire[i];
    while (n) {
      uint8_t chunk = (n > DUALSSD1306_WIRE_MAX - 1) ? DUALSSD1306_WIRE_MAX - 1 : n;
      w->beginTransmission(_addr[i]);
      w->write((uint8_t)0x40);
      w->write(d, chunk);
      const uint8_t st = w->endTransmission();
      if (st) { _i2c = st; return false; }
      d += chunk;
      n -= chunk;
    }
    return true;
  }

  void sendBoth(uint8_t c) {
    command(0, c);
    command(1, c);
  }

  void sendBoth2(uint8_t c, uint8_t arg) {
    const uint8_t buf[2] = {c, arg};
    for (uint8_t i = 0; i < 2; i++) {
      clockStart(i);
      sendCommands(i, buf, 2);
      clockEnd(i);
    }
  }

  uint8_t _pw, _ph;
  Layout _layout;
  TwoWire *_wire[2];
  uint8_t _addr[2];
  int8_t _rst[2];
  uint32_t _clkDuring, _clkAfter;
  bool _seq;
  uint8_t _cur = 0;
  int16_t _offX = 0, _offY = 0;
  uint8_t *_buf = nullptr;
  uint8_t _vcs = DUALSSD1306_SWITCHCAPVCC;
  uint8_t _contrast = 0x8F;
  uint8_t _fail = FAIL_NONE;
  int8_t _failPanel = -1;
  uint8_t _initStep = 0;
  uint8_t _i2c = 0;
};
