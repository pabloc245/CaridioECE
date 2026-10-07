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
   * Alloue le buffer, initialise les bus, fait le reset et configure
   * les deux contrôleurs.
   * @param vcs          DUALSSD1306_SWITCHCAPVCC (cas courant) ou _EXTERNALVCC
   * @param addr0/addr1  adresses I2C 7 bits (ex. 0x3C et 0x3D)
   * @param reset        pulse sur les broches reset si définies
   * @param periphBegin  appeler Wire.begin() ici ; mettre false si tu l'as
   *                     déjà fait (ex. Wire.begin(SDA, SCL) sur ESP32)
   * @return false si allocation impossible, config invalide ou panneau muet
   */
  bool begin(uint8_t vcs, uint8_t addr0, uint8_t addr1,
             bool reset = true, bool periphBegin = true) {
    if (_layout != MIRROR && _wire[0] == _wire[1] && addr0 == addr1)
      return false;  // deux panneaux distincts ne peuvent pas partager bus+adresse
    if (_layout == STACKED && (_ph & 7)) return false;  // pages de 8 lignes

    if (!_buf) {
      _buf = (uint8_t *)calloc(bufBytes(), 1);
      if (!_buf) {
        Serial.println(F("DualSSD1306 : allocation du buffer impossible"));
        return false;
      }
    }

    _vcs = vcs;
    _addr[0] = addr0;
    _addr[1] = addr1;

    if (periphBegin) {
      _wire[0]->begin();
      if (_wire[1] != _wire[0]) _wire[1]->begin();
    }

    if (reset) {
      resetPin(_rst[0]);
      if (_rst[1] != _rst[0]) resetPin(_rst[1]);  // broche partagée : un seul pulse
    }

    // Valeurs dépendant du modèle
    uint8_t comPins = 0x02;
    _contrast = 0x8F;
    const bool ext = (vcs == DUALSSD1306_EXTERNALVCC);
    if (_pw == 128 && _ph == 64) {
      comPins = 0x12;
      _contrast = ext ? 0x9F : 0xCF;
    } else if (_pw == 96 && _ph == 16) {
      comPins = 0x02;
      _contrast = ext ? 0x10 : 0xAF;
    } else if (_pw == 64 && _ph == 32) {
      comPins = 0x12;
      _contrast = ext ? 0x10 : 0xCF;
    }  // 128x32 : valeurs par défaut ci-dessus

    const uint8_t seq[] = {
        CMD_DISPLAY_OFF,
        CMD_CLOCK_DIV,     0x80,
        CMD_MULTIPLEX,     (uint8_t)(_ph - 1),
        CMD_DISPLAY_OFFSET, 0x00,
        CMD_START_LINE,
        CMD_CHARGE_PUMP,   (uint8_t)(ext ? 0x10 : 0x14),
        CMD_MEMORY_MODE,   0x00,                 // adressage horizontal
        (uint8_t)(CMD_SEG_REMAP | 0x01),
        CMD_COM_SCAN_DEC,
        CMD_COM_PINS,      comPins,
        CMD_CONTRAST,      _contrast,
        CMD_PRECHARGE,     (uint8_t)(ext ? 0x22 : 0xF1),
        CMD_VCOM_DETECT,   0x40,
        CMD_ALL_ON_RESUME,
        CMD_NORMAL,
        CMD_SCROLL_OFF,
        CMD_DISPLAY_ON};

    bool ok = true;
    for (uint8_t i = 0; i < 2; i++) {
      if (_layout == MIRROR && i == 1 &&
          _wire[0] == _wire[1] && _addr[0] == _addr[1])
        break;  // même périphérique, déjà initialisé
      clockStart(i);
      ok &= sendCommands(i, seq, sizeof(seq));
      clockEnd(i);
    }
    return ok;
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
  // buffer Wire.
  bool sendCommands(uint8_t i, const uint8_t *c, uint8_t n) {
    TwoWire *w = _wire[i];
    bool ok = true;
    while (n) {
      uint8_t chunk = (n > DUALSSD1306_WIRE_MAX - 1) ? DUALSSD1306_WIRE_MAX - 1 : n;
      w->beginTransmission(_addr[i]);
      w->write((uint8_t)0x00);
      w->write(c, chunk);
      ok &= (w->endTransmission() == 0);
      c += chunk;
      n -= chunk;
    }
    return ok;
  }

  // Envoie n octets de données GDDRAM (préfixe 0x40).
  bool sendData(uint8_t i, const uint8_t *d, uint16_t n) {
    TwoWire *w = _wire[i];
    bool ok = true;
    while (n) {
      uint8_t chunk = (n > DUALSSD1306_WIRE_MAX - 1) ? DUALSSD1306_WIRE_MAX - 1 : n;
      w->beginTransmission(_addr[i]);
      w->write((uint8_t)0x40);
      w->write(d, chunk);
      ok &= (w->endTransmission() == 0);
      d += chunk;
      n -= chunk;
    }
    return ok;
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
};