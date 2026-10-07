/*
 * Deux SSD1306 128x64 sur le MÊME bus I2C, avec deux adresses différentes.
 *
 * Câblage (les deux écrans en parallèle) :
 *   SDA -> SDA écran 1 + SDA écran 2     (Uno/Nano : A4)
 *   SCL -> SCL écran 1 + SCL écran 2     (Uno/Nano : A5)
 *   VCC / GND communs
 *
 * Adresses : écran gauche = 0x3C (défaut), écran droit = 0x3D.
 * Sur la plupart des modules, 0x3D s'obtient en déplaçant une résistance
 * (ou un pad à souder) près de la broche SA0 / "ADDR" au dos de l'écran.
 * Sur les boîtiers marqués 0x78 / 0x7A, c'est la même chose en notation
 * 8 bits : 0x78 = 0x3C et 0x7A = 0x3D.
 */
#include <Wire.h>
#include "DualSSD1306.h"

constexpr uint8_t ADDR_LEFT  = 0x3C;
constexpr uint8_t ADDR_RIGHT = 0x3D;

// Dernier paramètre : true  = mode séquentiel (buffer 1 Ko, pour Uno/Nano)
//                     false = buffer complet 2 Ko (ESP32, RP2040, STM32...)
// Les deux utilisent &Wire : même bus, la différence se fait par l'adresse.
DualSSD1306 oled(128, 64, DualSSD1306::SIDE_BY_SIDE, &Wire, &Wire,
                 -1, -1, 400000UL, 100000UL, true);

uint16_t counter = 0;  // état de l'animation : modifié hors de dessine()
uint8_t level = 0;

bool present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Écran logique 256x64 : x 0..127 = écran gauche, x 128..255 = écran droit.
// Doit donner le même résultat à chaque appel (appelée une fois par écran
// en mode séquentiel).
void dessine() {
  oled.setTextColor(DUALSSD1306_WHITE);

  // Écran gauche
  oled.setTextSize(1);
  oled.setCursor(4, 4);
  oled.print(F("GAUCHE 0x3C"));
  oled.setTextSize(3);
  oled.setCursor(4, 24);
  oled.print(counter);

  // Écran droit
  oled.setTextSize(1);
  oled.setCursor(132, 4);
  oled.print(F("DROITE 0x3D"));
  oled.drawRect(132, 24, 120, 16, DUALSSD1306_WHITE);
  oled.fillRect(134, 26, level, 12, DUALSSD1306_WHITE);

  // Éléments qui traversent la jonction entre les deux écrans
  oled.drawCircle(128, 50, 8, DUALSSD1306_WHITE);
  oled.drawFastHLine(0, 62, 256, DUALSSD1306_WHITE);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();  // fait ici pour pouvoir tester les adresses avant begin()

  Serial.print(F("0x3C : "));
  Serial.println(present(ADDR_LEFT) ? F("OK") : F("absent"));
  Serial.print(F("0x3D : "));
  Serial.println(present(ADDR_RIGHT) ? F("OK") : F("absent"));

  // periphBegin = false : Wire.begin() déjà appelé ci-dessus
  if (!oled.begin(DUALSSD1306_SWITCHCAPVCC, ADDR_LEFT, ADDR_RIGHT,
                  true, false)) {
    Serial.println(F("Init echouee : voir les adresses ci-dessus"));
    for (;;) delay(1000);
  }
}

void loop() {
  oled.render(dessine);  // marche dans les deux modes (séquentiel ou non)

  counter++;
  level = (level + 2) % 117;  // barre de 0 à 116 pixels
}
