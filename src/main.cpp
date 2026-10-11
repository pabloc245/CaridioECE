#include <Arduino.h>
#include <EEPROM.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <PulseGraph.h>
#include <PulseGraphZoom.h>
#include <PulseDetector.h>
#include <BpmAverager.h>
#include <MeasurementStore.h>
#include <HeartbeatAudio.h>
#include <HeartRateIndicators.h>
#include <U8g2lib.h>
#include <Wire.h>

constexpr uint8_t WIDTH = 128;
constexpr uint8_t GRAPH_SAMPLE_COUNT = 120;
constexpr uint16_t DISPLAY_INTERVAL_MS = 250;
constexpr uint16_t CLOCK_INTERVAL_MS = 1000;
constexpr uint16_t BPM_STALE_TIMEOUT_MS = 5000;
constexpr uint8_t RECORD_BUTTON_PIN = 9;
constexpr uint8_t HEART_RATE_LED_RED_PIN = 3;
constexpr uint8_t HEART_RATE_LED_GREEN_PIN = 4;
constexpr uint8_t HEART_RATE_LED_YELLOW_PIN = 5;
constexpr uint8_t BEEP_BUTTON_PIN = 2;
constexpr uint8_t ENCODER_A_PIN = A1;
constexpr uint8_t ENCODER_B_PIN = A2;
constexpr uint8_t ENCODER_BUTTON_PIN = A3;
constexpr uint8_t BUZZER_PIN = 10;
constexpr uint16_t BEEP_DURATION_MS = 80;
constexpr uint32_t BEEP_BUTTON_DEBOUNCE_US = 30000UL;

constexpr uint8_t pulsePin = A0;
constexpr uint8_t blinkPin = 13;
constexpr uint8_t RTC_IO_PIN = 7;
constexpr uint8_t RTC_SCLK_PIN = 6;
constexpr uint8_t RTC_CE_PIN = 8;

class ArduinoEeprom {
 public:
  uint8_t read(uint16_t address) const { return EEPROM.read(address); }
  void update(uint16_t address, uint8_t value) {
    EEPROM.update(address, value);
  }
  uint16_t length() const { return E2END + 1; }
};

enum UiState : uint8_t { UI_MAIN, UI_MENU, UI_HISTORY, UI_CLEAR_CONFIRM };
enum RecordNotice : uint8_t {
  NOTICE_NONE,
  NOTICE_SAVED,
  NOTICE_FULL,
  NOTICE_INVALID_TIME
};

volatile int BPM = 0;
volatile int Signal = 0;
volatile boolean QS = false;
volatile boolean detectorConfidence = false;
volatile unsigned long sampleCounter = 0;
volatile uint8_t graphSamples[GRAPH_SAMPLE_COUNT];
volatile uint8_t graphWriteIndex = 0;
volatile uint8_t graphStoredCount = 0;
volatile uint8_t graphSampleDivider = 0;
volatile uint8_t graphSampleIntervalTicks =
    pulseGraphSampleIntervalTicks(2);
PulseDetector pulseDetector;
ArduinoEeprom eeprom;
MeasurementStore<ArduinoEeprom> measurementStore(eeprom);
UiState uiState = UI_MAIN;
RecordNotice recordNotice = NOTICE_NONE;
uint8_t menuSelection = 0;
uint16_t historySelection = 0;
bool clearConfirmSelection = false;
bool isRecording = false;
volatile bool beepEnabled = true;
volatile bool buzzerActive = false;
volatile uint16_t buzzerTogglesRemaining = 0;
volatile unsigned long lastBeepButtonAt = 0;
BpmAverager bpmAverager;
unsigned long recordNoticeUntil = 0;

void interruptSetup()
{     
  // Initializes Timer2 to throw an interrupt every 2mS.
  TCCR2A = 0x02;     // DISABLE PWM ON DIGITAL PINS 3 AND 11, AND GO INTO CTC MODE
  TCCR2B = 0x06;     // DON'T FORCE COMPARE, 256 PRESCALER 
  OCR2A = 0X7C;      // SET THE TOP OF THE COUNT TO 124 FOR 500Hz SAMPLE RATE
  TIMSK2 = 0x02;     // ENABLE INTERRUPT ON MATCH BETWEEN TIMER2 AND OCR2A
  sei();             // MAKE SURE GLOBAL INTERRUPTS ARE ENABLED      
} 
ThreeWire rtcWire(RTC_IO_PIN, RTC_SCLK_PIN, RTC_CE_PIN);
RtcDS1302<ThreeWire> rtc(rtcWire);
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

uint8_t graphSnapshot[GRAPH_SAMPLE_COUNT];
uint8_t graphSnapshotCount = 0;
uint8_t graphSnapshotWriteIndex = 0;
uint8_t graphZoomLevel = 2;
RtcDateTime displayTime(2026, 1, 1, 12, 0, 0);
int displayBpm = 0;
unsigned long lastBpmMeasurementAt = 0;
bool hasConclusiveBpm = false;
bool bpmIsReliable = false;

class DebouncedButton {
 public:
  explicit DebouncedButton(uint8_t pin)
      : pin_(pin), stableState_(HIGH), lastReading_(HIGH), changedAt_(0) {}

  void begin() {
    pinMode(pin_, INPUT_PULLUP);
    stableState_ = digitalRead(pin_);
    lastReading_ = stableState_;
    changedAt_ = millis();
  }

  bool pressed() {
    const int reading = digitalRead(pin_);
    const unsigned long now = millis();
    if (reading != lastReading_) {
      lastReading_ = reading;
      changedAt_ = now;
    }
    if (reading != stableState_ && now - changedAt_ >= kDebounceMs) {
      stableState_ = reading;
      return stableState_ == LOW;
    }
    return false;
  }

 private:
  enum { kDebounceMs = 35 };
  uint8_t pin_;
  int stableState_;
  int lastReading_;
  unsigned long changedAt_;
};

DebouncedButton recordButton(RECORD_BUTTON_PIN);
DebouncedButton encoderButton(ENCODER_BUTTON_PIN);
uint8_t previousEncoderState = 0;
int8_t encoderQuarterSteps = 0;

void stopBuzzerHardware() {
  TCCR1B = 0;
  TIMSK1 &= (uint8_t)~_BV(OCIE1A);
  PORTB &= (uint8_t)~_BV(PB2);
  buzzerActive = false;
  buzzerTogglesRemaining = 0;
}

void startHeartbeatBeep(uint16_t bpm) {
  const uint16_t frequency = heartbeatToneFrequency(bpm);
  if (!beepEnabled || frequency == 0) return;

  noInterrupts();
  if (!beepEnabled) {
    interrupts();
    return;
  }
  // Timer2 drives the 500 Hz sensor sampling interrupt; use Timer1 for audio.
  TCCR1A = 0;
  TCCR1B = 0;
  TIMSK1 &= (uint8_t)~_BV(OCIE1A);
  TCNT1 = 0;
  TIFR1 = _BV(OCF1A);
  OCR1A = (uint16_t)(F_CPU / (16UL * frequency) - 1);
  buzzerTogglesRemaining =
      (uint16_t)(((uint32_t)frequency * 2 * BEEP_DURATION_MS) / 1000);
  PORTB &= (uint8_t)~_BV(PB2);
  buzzerActive = true;
  TIMSK1 |= _BV(OCIE1A);
  TCCR1B = _BV(WGM12) | _BV(CS11);
  interrupts();
}

void setHeartRateLeds(uint16_t bpm, bool reliable) {
  digitalWrite(HEART_RATE_LED_RED_PIN, LOW);
  digitalWrite(HEART_RATE_LED_GREEN_PIN, LOW);
  digitalWrite(HEART_RATE_LED_YELLOW_PIN, LOW);

  switch (heartRateIndicator(bpm, reliable)) {
    case HEART_RATE_INDICATOR_YELLOW:
      digitalWrite(HEART_RATE_LED_YELLOW_PIN, HIGH);
      break;
    case HEART_RATE_INDICATOR_GREEN:
      digitalWrite(HEART_RATE_LED_GREEN_PIN, HIGH);
      break;
    case HEART_RATE_INDICATOR_RED:
      digitalWrite(HEART_RATE_LED_RED_PIN, HIGH);
      break;
    default:
      break;
  }
}

bool isEnglish() { return measurementStore.language() == 1; }

void drawCentered(const char *text, int16_t y) {
  const int16_t x = (WIDTH - u8g2.getStrWidth(text)) / 2;
  u8g2.drawStr(x, y, text);
}

void snapshotGraph() {
  noInterrupts();
  graphSnapshotCount = graphStoredCount;
  graphSnapshotWriteIndex = graphWriteIndex;
  for (uint8_t i = 0; i < GRAPH_SAMPLE_COUNT; ++i) {
    graphSnapshot[i] = graphSamples[i];
  }
  interrupts();
}

void drawClockAndBpm() {
  char timeText[9];
  char dateText[11];
  char bpmText[6];
  snprintf(timeText, sizeof(timeText), "%02u:%02u:%02u",
           displayTime.Hour(), displayTime.Minute(), displayTime.Second());
  snprintf(dateText, sizeof(dateText), "%02u/%02u/%04u",
           displayTime.Day(), displayTime.Month(), displayTime.Year());
  if (bpmIsReliable) {
    snprintf(bpmText, sizeof(bpmText), "%d", displayBpm);
  } else {
    snprintf(bpmText, sizeof(bpmText), "---");
  }

  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(3, 2, isEnglish() ? "TIME" : "HEURE");
  u8g2.drawStr(75, 2, beepEnabled
                           ? (isEnglish() ? "BEEP ON" : "BIP ON")
                           : (isEnglish() ? "BEEP OFF" : "BIP OFF"));
  u8g2.setFont(u8g2_font_helvB12_tf);
  u8g2.drawStr(2, 14, timeText);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(3, 36, dateText);
  u8g2.setFont(u8g2_font_helvB12_tf);
  if (isRecording) {
    char progress[12];
    snprintf(progress, sizeof(progress), "%u/10", bpmAverager.count());
    u8g2.drawStr(34, 46, progress);
  } else {
    u8g2.drawStr(34, 46, bpmText);
  }
  u8g2.setFont(u8g2_font_6x10_tf);
  if (isRecording) {
    u8g2.drawStr(3, 53, isEnglish() ? "SAVE" : "MESURE");
  } else {
    u8g2.drawStr(3, 53, "BPM");
    u8g2.drawStr(83, 53, "bpm");
  }
  if (!isRecording && recordNotice != NOTICE_NONE &&
      millis() < recordNoticeUntil) {
    drawCentered(recordNotice == NOTICE_SAVED
                     ? (isEnglish() ? "Saved" : "Enregistre")
                     : recordNotice == NOTICE_FULL
                           ? (isEnglish() ? "Memory full" : "Memoire pleine")
                           : (isEnglish() ? "Check RTC date/time"
                                          : "Verifier date/heure RTC"),
                 60);
  }
}

void drawMenu() {
  u8g2.setFont(u8g2_font_6x10_tf);
  drawCentered(isEnglish() ? "MENU" : "MENU", 2);
  const char *items[4] = {
      isEnglish() ? "View records" : "Voir mesures",
      isEnglish() ? "Erase records" : "Effacer mesures",
      isEnglish() ? "Language: English" : "Langue: Francais",
      isEnglish() ? "Back" : "Retour"};
  for (uint8_t i = 0; i < 4; ++i) {
    if (i == menuSelection) u8g2.drawStr(2, 15 + i * 11, ">");
    u8g2.drawStr(12, 15 + i * 11, items[i]);
  }
  u8g2.drawStr(2, 59, isEnglish() ? "Turn / press" : "Tourner / valider");
}

void drawHistory() {
  u8g2.setFont(u8g2_font_6x10_tf);
  drawCentered(isEnglish() ? "RECORDS" : "MESURES", 2);
  const uint16_t recordCount = measurementStore.count();
  if (recordCount == 0) {
    drawCentered(isEnglish() ? "No records" : "Aucune mesure", 27);
  } else {
    MeasurementRecord record;
    if (measurementStore.read(recordCount - historySelection - 1, record)) {
      char bpmText[12];
      char dateText[16];
      char timeText[12];
      char indexText[16];
      snprintf(bpmText, sizeof(bpmText), "%u BPM", record.bpm);
      snprintf(dateText, sizeof(dateText), "%02u/%02u/%04u", record.day,
               record.month, record.year);
      snprintf(timeText, sizeof(timeText), "%02u:%02u:%02u", record.hour,
               record.minute, record.second);
      snprintf(indexText, sizeof(indexText), "%u/%u",
               recordCount - historySelection, recordCount);
      drawCentered(bpmText, 16);
      drawCentered(dateText, 29);
      drawCentered(timeText, 42);
      drawCentered(indexText, 53);
    }
  }
  u8g2.drawStr(2, 59, isEnglish() ? "Turn: older; press: back"
                                  : "Tourner / retour");
}

void drawClearConfirmation() {
  u8g2.setFont(u8g2_font_6x10_tf);
  drawCentered(isEnglish() ? "Erase all records?" : "Effacer les mesures?", 8);
  const char *yes = isEnglish() ? "Yes" : "Oui";
  const char *no = isEnglish() ? "No" : "Non";
  u8g2.drawStr(clearConfirmSelection ? 78 : 25, 30, ">");
  u8g2.drawStr(38, 30, yes);
  u8g2.drawStr(91, 30, no);
  drawCentered(isEnglish() ? "Turn / press" : "Tourner / valider", 52);
}

void drawPulseGraph() {
  constexpr int16_t frameX = 1;
  constexpr int16_t frameY = 14;
  constexpr int16_t frameWidth = WIDTH - 2;
  constexpr int16_t frameHeight = 49;
  constexpr int16_t plotInset = 3;
  constexpr int16_t plotLeft = frameX + plotInset;
  constexpr int16_t plotTop = frameY + 5;
  constexpr int16_t plotWidth = frameWidth - 2 * plotInset;
  constexpr int16_t plotHeight = frameHeight - 17;
  constexpr int16_t plotBottom = plotTop + plotHeight - 1;
  static_assert(GRAPH_SAMPLE_COUNT <= plotWidth,
                "Graph sample count must fit within the plot width");

  const uint8_t windowTenths =
      pulseGraphWindowTenths(graphZoomLevel, GRAPH_SAMPLE_COUNT);
  char title[16];
  snprintf(title, sizeof(title), "%s %u.%us",
           isEnglish() ? "PULSE" : "POULS", windowTenths / 10,
           windowTenths % 10);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(2, 2, title);
  u8g2.drawStr(77, 2, "ENC +/-");
  u8g2.drawFrame(frameX, frameY, frameWidth, frameHeight);
  for (uint8_t mark = 0; mark < 4; ++mark) {
    const int16_t x = plotLeft + (plotWidth - 1) * mark / 3;
    u8g2.drawVLine(x, plotTop, plotHeight);
    char label[6];
    const uint8_t timeTenths =
        pulseGraphTimeLabelTenths(graphZoomLevel, GRAPH_SAMPLE_COUNT, mark);
    snprintf(label, sizeof(label), "%u.%u", timeTenths / 10,
             timeTenths % 10);
    const int16_t labelWidth = u8g2.getStrWidth(label);
    int16_t labelX = x - labelWidth / 2;
    if (labelX < plotLeft) labelX = plotLeft;
    if (labelX + labelWidth > plotLeft + plotWidth) {
      labelX = plotLeft + plotWidth - labelWidth;
    }
    u8g2.drawStr(labelX, plotBottom + 2, label);
  }
  for (int16_t y = plotTop + 10; y < plotTop + plotHeight; y += 10) {
    u8g2.drawHLine(plotLeft, y, plotWidth);
  }

  const uint8_t sampleCount =
      graphSnapshotCount < GRAPH_SAMPLE_COUNT ? graphSnapshotCount
                                              : GRAPH_SAMPLE_COUNT;
  if (sampleCount == 0) {
    u8g2.drawStr(10, 36, isEnglish() ? "Waiting" : "En attente");
    return;
  }

  uint8_t minimum = 255;
  uint8_t maximum = 0;
  const uint8_t oldest = pulseGraphOldestIndex(
      GRAPH_SAMPLE_COUNT, graphSnapshotWriteIndex, sampleCount);
  for (uint8_t i = 0; i < sampleCount; ++i) {
    const uint8_t sample = graphSnapshot[(oldest + i) % GRAPH_SAMPLE_COUNT];
    if (sample < minimum) minimum = sample;
    if (sample > maximum) maximum = sample;
  }

  pulseGraphFitRange(minimum, maximum);

  const uint8_t firstSampleOffset = GRAPH_SAMPLE_COUNT - sampleCount;
  int16_t previousX =
      plotLeft + (plotWidth - 1) * firstSampleOffset /
                     (GRAPH_SAMPLE_COUNT - 1);
  int16_t previousY = pulseGraphMapY(
      graphSnapshot[oldest], minimum, maximum, plotTop, plotBottom);

  for (uint8_t i = 1; i < sampleCount; ++i) {
    const uint8_t sampleOffset = firstSampleOffset + i;
    const int16_t x =
        plotLeft + (plotWidth - 1) * sampleOffset /
                       (GRAPH_SAMPLE_COUNT - 1);
    const uint8_t sample = graphSnapshot[(oldest + i) % GRAPH_SAMPLE_COUNT];
    const int16_t y =
        pulseGraphMapY(sample, minimum, maximum, plotTop, plotBottom);
    u8g2.drawLine(previousX, previousY, x, y);
    previousX = x;
    previousY = y;
  }
}

void drawClockScreen() {
  switch (uiState) {
    case UI_MENU:
      drawMenu();
      break;
    case UI_HISTORY:
      drawHistory();
      break;
    case UI_CLEAR_CONFIRM:
      drawClearConfirmation();
      break;
    default:
      drawClockAndBpm();
      break;
  }
}

void drawPulseScreen() {
  drawPulseGraph();
}

void drawPanel(uint8_t address, void (*draw)()) {
  u8g2.setI2CAddress(address << 1);
  u8g2.firstPage();
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  do {
    draw();
  } while (u8g2.nextPage());
}

bool panelResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void changeSelection(int8_t direction) {
  if (uiState == UI_MAIN) {
    const int8_t nextZoom = (int8_t)graphZoomLevel + direction;
    if (nextZoom >= 0 && nextZoom < PULSE_GRAPH_ZOOM_LEVEL_COUNT) {
      graphZoomLevel = (uint8_t)nextZoom;
      noInterrupts();
      graphSampleIntervalTicks =
          pulseGraphSampleIntervalTicks(graphZoomLevel);
      graphSampleDivider = 0;
      graphWriteIndex = 0;
      graphStoredCount = 0;
      interrupts();
    }
  } else if (uiState == UI_MENU) {
    menuSelection = (menuSelection + 4 + direction) % 4;
  } else if (uiState == UI_HISTORY) {
    const uint16_t count = measurementStore.count();
    if (count != 0) {
      historySelection = (historySelection + count + direction) % count;
    }
  } else if (uiState == UI_CLEAR_CONFIRM) {
    clearConfirmSelection = !clearConfirmSelection;
  }
}

void pollEncoder() {
  const uint8_t state =
      (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
  const uint8_t transition = (previousEncoderState << 2) | state;
  static const int8_t transitions[16] = {
      0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  encoderQuarterSteps += transitions[transition];
  previousEncoderState = state;

  if (encoderQuarterSteps >= 4) {
    encoderQuarterSteps = 0;
    changeSelection(1);
  } else if (encoderQuarterSteps <= -4) {
    encoderQuarterSteps = 0;
    changeSelection(-1);
  }
}

void handleEncoderPress() {
  switch (uiState) {
    case UI_MAIN:
      uiState = UI_MENU;
      menuSelection = 0;
      break;
    case UI_MENU:
      if (menuSelection == 0) {
        historySelection = 0;
        uiState = UI_HISTORY;
      } else if (menuSelection == 1) {
        clearConfirmSelection = false;
        uiState = UI_CLEAR_CONFIRM;
      } else if (menuSelection == 2) {
        measurementStore.setLanguage(isEnglish() ? 0 : 1);
      } else {
        uiState = UI_MAIN;
      }
      break;
    case UI_HISTORY:
      uiState = UI_MENU;
      break;
    case UI_CLEAR_CONFIRM:
      if (clearConfirmSelection) measurementStore.clear();
      uiState = UI_MENU;
      break;
  }
}

void startRecording() {
  if (measurementStore.count() >= measurementStore.capacity()) {
    recordNotice = NOTICE_FULL;
    recordNoticeUntil = millis() + 2500;
    return;
  }
  bpmAverager.reset();
  isRecording = true;
  recordNotice = NOTICE_NONE;
}

void collectRecordingSample(int bpm, bool confident) {
  if (!isRecording) return;
  if (!confident || bpm <= 0) {
    bpmAverager.reset();
    return;
  }

  if (!bpmAverager.add((uint16_t)bpm, true)) return;

  if (!rtc.IsDateTimeValid()) {
    recordNotice = NOTICE_INVALID_TIME;
    recordNoticeUntil = millis() + 2500;
    isRecording = false;
    bpmAverager.reset();
    return;
  }
  const RtcDateTime timestamp = rtc.GetDateTime();
  const MeasurementRecord record = {
      bpmAverager.average(),
      timestamp.Year(),
      timestamp.Month(),
      timestamp.Day(),
      timestamp.Hour(),
      timestamp.Minute(),
      timestamp.Second()};
  recordNotice = measurementStore.save(record) ? NOTICE_SAVED : NOTICE_FULL;
  recordNoticeUntil = millis() + 2500;
  isRecording = false;
  bpmAverager.reset();
}

void setup()
{
  pinMode(blinkPin, OUTPUT);
  pinMode(pulsePin, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(HEART_RATE_LED_RED_PIN, OUTPUT);
  pinMode(HEART_RATE_LED_GREEN_PIN, OUTPUT);
  pinMode(HEART_RATE_LED_YELLOW_PIN, OUTPUT);
  setHeartRateLeds(0, false);
  pinMode(BEEP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  previousEncoderState =
      (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
  lastBeepButtonAt = micros() - BEEP_BUTTON_DEBOUNCE_US;
  EICRA = (EICRA & (uint8_t)~(_BV(ISC01) | _BV(ISC00))) | _BV(ISC01);
  EIFR = _BV(INTF0);
  EIMSK |= _BV(INT0);
  recordButton.begin();
  encoderButton.begin();
  measurementStore.initialize();
#ifdef FS6_ENABLE_SERIAL_DEBUG
  Serial.begin(9600);
#endif
  Wire.begin();
  u8g2.setBusClock(100000UL);

  if (!panelResponds(0x3C) || !panelResponds(0x3D)) {
#ifdef FS6_ENABLE_SERIAL_DEBUG
    Serial.println(F("Un ecran SSD1306 ne repond pas sur 0x3C/0x3D"));
#endif
    for (;;) delay(1000);
  }
  u8g2.setI2CAddress(0x3C << 1);
  u8g2.begin();
  u8g2.setI2CAddress(0x3D << 1);
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);

  rtc.Begin();
  rtc.SetIsWriteProtected(false);
  rtc.SetIsRunning(true);
  displayTime = rtc.GetDateTime();

  interruptSetup();
}

void loop()
{
  pollEncoder();
  if (encoderButton.pressed()) handleEncoderPress();
  if (recordButton.pressed() && uiState == UI_MAIN && !isRecording) {
    startRecording();
  }

  int bpmSnapshot;
#ifdef FS6_ENABLE_SERIAL_DEBUG
  int signalSnapshot;
#endif
  bool beatDetected;
  bool detectorIsConfident;
  noInterrupts();
  bpmSnapshot = BPM;
#ifdef FS6_ENABLE_SERIAL_DEBUG
  signalSnapshot = Signal;
#endif
  beatDetected = QS;
  detectorIsConfident = detectorConfidence;
  QS = false;
  interrupts();

  if (beatDetected && bpmSnapshot > 0) {
    displayBpm = bpmSnapshot;
    lastBpmMeasurementAt = millis();
    hasConclusiveBpm = true;
    collectRecordingSample(bpmSnapshot, detectorIsConfident);
    if (detectorIsConfident) {
      startHeartbeatBeep((uint16_t)bpmSnapshot);
    }
#ifdef FS6_ENABLE_SERIAL_DEBUG
    Serial.print(F("BPM: "));
    Serial.println(signalSnapshot);
    Serial.println(displayBpm);
#endif
  }
  if (isRecording && !detectorIsConfident) {
    bpmAverager.reset();
  }
  static unsigned long lastDisplayUpdate = 0;
  const unsigned long now = millis();
  bpmIsReliable = hasConclusiveBpm &&
                  now - lastBpmMeasurementAt <= BPM_STALE_TIMEOUT_MS;
  setHeartRateLeds((uint16_t)displayBpm, bpmIsReliable);
  if (now - lastDisplayUpdate >= DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = now;
    snapshotGraph();

    static unsigned long lastClockUpdate = 0;
    if (now - lastClockUpdate >= CLOCK_INTERVAL_MS) {
      lastClockUpdate = now;
      displayTime = rtc.GetDateTime();
    }

    drawPanel(0x3C, drawClockScreen);
    drawPanel(0x3D, drawPulseScreen);
  }
}

ISR(TIMER2_COMPA_vect)
{
  Signal = analogRead(pulsePin);
  if (++graphSampleDivider >= graphSampleIntervalTicks) {
    graphSampleDivider = 0;
    pulseGraphPushSample(graphSamples, GRAPH_SAMPLE_COUNT, graphWriteIndex,
                         graphStoredCount, (uint8_t)(Signal >> 2));
  }

  sampleCounter += 2;
  const bool wasTracking = pulseDetector.isTracking();
  const bool newBpm = pulseDetector.process(Signal, sampleCounter);
  const bool isTracking = pulseDetector.isTracking();
  if (!wasTracking && isTracking) digitalWrite(blinkPin, HIGH);
  if (wasTracking && !isTracking) digitalWrite(blinkPin, LOW);
  if (newBpm) {
    BPM = pulseDetector.bpm();
    QS = true;
  }
  detectorConfidence = pulseDetector.isConfident();
  if (buzzerActive && !detectorConfidence) {
    stopBuzzerHardware();
  }
}

ISR(TIMER1_COMPA_vect)
{
  if (buzzerTogglesRemaining == 0) {
    stopBuzzerHardware();
    return;
  }

  PINB = _BV(PB2);
  --buzzerTogglesRemaining;
  if (buzzerTogglesRemaining == 0) stopBuzzerHardware();
}

ISR(INT0_vect)
{
  if ((PIND & _BV(PD2)) == 0) {
    const unsigned long now = micros();
    if (now - lastBeepButtonAt >= BEEP_BUTTON_DEBOUNCE_US) {
      lastBeepButtonAt = now;
      beepEnabled = !beepEnabled;
      if (!beepEnabled) stopBuzzerHardware();
    }
  }
}
