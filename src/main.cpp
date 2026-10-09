#include <Arduino.h>
#include <DualSSD1306.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <PulseGraph.h>

constexpr uint8_t WIDTH = 128;
constexpr uint8_t HEIGHT = 64;
constexpr uint8_t GRAPH_SAMPLE_COUNT = 120;
constexpr uint8_t GRAPH_SAMPLE_INTERVAL_TICKS = 10;  // 20 ms at a 2 ms ISR rate
constexpr uint16_t DISPLAY_INTERVAL_MS = 250;
constexpr uint16_t CLOCK_INTERVAL_MS = 1000;

constexpr uint8_t pulsePin = A0;
constexpr uint8_t blinkPin = 13;
constexpr uint8_t RTC_IO_PIN = 7;
constexpr uint8_t RTC_SCLK_PIN = 6;
constexpr uint8_t RTC_CE_PIN = 8;

// Volatile Variables, used in the interrupt service routine!
volatile int BPM;                         // int that holds raw Analog in 0. updated every 2mS
volatile int Signal;                      // holds the incoming raw data
volatile int IBI = 600;                   // int that holds the time interval between beats!
volatile boolean Pulse = false;           // "True" when User's live heartbeat is detected. "False" when not a "live beat". 
volatile boolean QS = false;              // becomes true when Arduoino finds a beat.
volatile int rate[10];                    // array to hold last ten IBI values
volatile unsigned long sampleCounter = 0; // used to determine pulse timing
volatile unsigned long lastBeatTime = 0;  // used to find IBI
volatile int P = 612;                     // used to find peak in pulse wave, seeded
volatile int T = 612;                     // used to find trough in pulse wave, seeded
volatile int thresh = 625;                // used to find instant moment of heart beat, seeded
volatile int amp = 100;                   // used to hold amplitude of pulse waveform, seeded
volatile boolean firstBeat = true;        // used to seed rate array so we startup with reasonable BPM
volatile boolean secondBeat = false;      // used to seed rate array so we startup with reasonable BPM
volatile uint8_t graphSamples[GRAPH_SAMPLE_COUNT];
volatile uint8_t graphWriteIndex = 0;
volatile uint8_t graphStoredCount = 0;
volatile uint8_t graphSampleDivider = 0;

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
DualSSD1306 oled(WIDTH, HEIGHT, DualSSD1306::SIDE_BY_SIDE, &Wire, &Wire,
                 -1, -1, 400000UL, 100000UL, true);

uint8_t graphSnapshot[GRAPH_SAMPLE_COUNT];
uint8_t graphSnapshotCount = 0;
uint8_t graphSnapshotWriteIndex = 0;
RtcDateTime displayTime(2026, 1, 1, 12, 0, 0);
int displayBpm = 0;

void printTwoDigits(uint8_t value) {
  if (value < 10) oled.print('0');
  oled.print(value);
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
  oled.setTextColor(DUALSSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(3, 2);
  oled.print(F("HEURE"));

  oled.setTextSize(2);
  oled.setCursor(2, 14);
  printTwoDigits(displayTime.Hour());
  oled.print(':');
  printTwoDigits(displayTime.Minute());
  oled.print(':');
  printTwoDigits(displayTime.Second());

  oled.setTextSize(1);
  oled.setCursor(3, 36);
  printTwoDigits(displayTime.Day());
  oled.print('/');
  printTwoDigits(displayTime.Month());
  oled.print('/');
  oled.print(displayTime.Year());

  // oled.setCursor(3, 53);
  // oled.print(F("BPM"));
  // oled.setTextSize(2);
  // oled.setCursor(34, 46);
  // oled.print(displayBpm);
  // oled.setTextSize(1);
  // oled.setCursor(83, 53);
  // oled.print(F("bpm"));
}

void drawPulseGraph() {
  constexpr int16_t panelX = WIDTH;
  constexpr int16_t frameX = panelX + 1;
  constexpr int16_t frameY = 14;
  constexpr int16_t frameWidth = WIDTH - 2;
  constexpr int16_t frameHeight = 49;
  constexpr int16_t plotInset = 3;
  constexpr int16_t plotLeft = frameX + plotInset;
  constexpr int16_t plotTop = frameY + 5;
  constexpr int16_t plotWidth = frameWidth - 2 * plotInset;
  constexpr int16_t plotHeight = frameHeight - 9;
  constexpr int16_t plotBottom = plotTop + plotHeight - 1;
  static_assert(GRAPH_SAMPLE_COUNT <= plotWidth,
                "Graph sample count must fit within the plot width");

  // oled.setTextColor(DUALSSD1306_WHITE);
  // oled.setTextSize(1);
  // oled.setCursor(panelX + 2, 2);
  // oled.print(F("POULS - 2.4s"));
  // oled.drawRect(frameX, frameY, frameWidth, frameHeight, DUALSSD1306_WHITE);

  for (int16_t x = plotLeft + 30; x < plotLeft + plotWidth; x += 30) {
    //oled.drawFastVLine(x, plotTop, plotBottom - plotTop + 1, DUALSSD1306_WHITE);
  }
  for (int16_t y = plotTop + 10; y < plotTop + plotHeight; y += 10) {
    //oled.drawFastHLine(plotLeft, y, plotWidth, DUALSSD1306_WHITE);
  }

  const uint8_t sampleCount =
      graphSnapshotCount < GRAPH_SAMPLE_COUNT ? graphSnapshotCount
                                              : GRAPH_SAMPLE_COUNT;
  if (sampleCount == 0) {
    //oled.setCursor(panelX + 10, 36);
    //oled.print(F("En attente"));
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

  int16_t previousX = pulseGraphMapX(0, sampleCount, plotLeft, plotWidth);
  int16_t previousY = pulseGraphMapY(
      graphSnapshot[oldest], minimum, maximum, plotTop, plotBottom);

  for (uint8_t i = 1; i < sampleCount; ++i) {
    const int16_t x = pulseGraphMapX(i, sampleCount, plotLeft, plotWidth);
    const uint8_t sample = graphSnapshot[(oldest + i) % GRAPH_SAMPLE_COUNT];
    const int16_t y =
        pulseGraphMapY(sample, minimum, maximum, plotTop, plotBottom);
    //oled.drawLine(previousX, previousY, x, y, DUALSSD1306_WHITE);
    previousX = x;
    previousY = y;
  }
}

void drawScreens() {
  drawClockAndBpm();
  drawPulseGraph();
}

void setup()
{
  pinMode(blinkPin, OUTPUT);
  pinMode(pulsePin, INPUT);
  Serial.begin(9600);

  if (!oled.begin(DUALSSD1306_SWITCHCAPVCC, 0x3C, 0x3D)) {
    oled.printStatus(Serial);
    for (;;) delay(1000);
  }

  rtc.Begin();
  rtc.SetIsWriteProtected(false);
  rtc.SetDateTime(RtcDateTime(2026, 1, 1, 12, 0, 0));
  rtc.SetIsRunning(true);
  displayTime = rtc.GetDateTime();

  interruptSetup();
}

void loop()
{
  int bpmSnapshot;
  bool beatDetected;
  noInterrupts();
  bpmSnapshot = BPM;
  beatDetected = QS;
  QS = false;
  interrupts();

  if (beatDetected) {
    displayBpm = bpmSnapshot;
    Serial.print(F("BPM: "));
    Serial.println(displayBpm);
  }

  static unsigned long lastDisplayUpdate = 0;
  const unsigned long now = millis();
  if (now - lastDisplayUpdate >= DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = now;
    snapshotGraph();

    static unsigned long lastClockUpdate = 0;
    if (now - lastClockUpdate >= CLOCK_INTERVAL_MS) {
      lastClockUpdate = now;
      displayTime = rtc.GetDateTime();
    }

    if (!oled.render(drawScreens)) {
      Serial.println(F("Erreur lors de l'affichage des écrans SSD1306"));
    }
  }
}

ISR(TIMER2_COMPA_vect) //triggered when Timer2 counts to 124
{  
  Signal = analogRead(pulsePin);              // read the Pulse Sensor
  if (++graphSampleDivider >= GRAPH_SAMPLE_INTERVAL_TICKS) {
    graphSampleDivider = 0;
    pulseGraphPushSample(graphSamples, GRAPH_SAMPLE_COUNT, graphWriteIndex,
                         graphStoredCount, (uint8_t)(Signal >> 2));
  }
  sampleCounter += 2;                         // keep track of the time in mS with this variable
  int N = sampleCounter - lastBeatTime;       // monitor the time since the last beat to avoid noise
                                              // find the peak and trough of the pulse wave
  if(Signal < thresh && N > (IBI/5)*3)        // avoid dichrotic noise by waiting 3/5 of last IBI
    {      
      if (Signal < T) // T is the trough
      {                        
        T = Signal; // keep track of lowest point in pulse wave 
      }
    }

  if(Signal > thresh && Signal > P)
    {          // thresh condition helps avoid noise
      P = Signal;                             // P is the peak
    }                                        // keep track of highest point in pulse wave

  //  NOW IT'S TIME TO LOOK FOR THE HEART BEAT
  // signal surges up in value every time there is a pulse
  if (N > 250)
  {                                   // avoid high frequency noise
    if ( (Signal > thresh) && (Pulse == false) && (N > (IBI/5)*3) )
      {        
        Pulse = true;                               // set the Pulse flag when we think there is a pulse
        digitalWrite(blinkPin,HIGH);                // turn on pin 13 LED
        IBI = sampleCounter - lastBeatTime;         // measure time between beats in mS
        lastBeatTime = sampleCounter;               // keep track of time for next pulse
  
        if(secondBeat)
        {                        // if this is the second beat, if secondBeat == TRUE
          secondBeat = false;                  // clear secondBeat flag
          for(int i=0; i<=9; i++) // seed the running total to get a realisitic BPM at startup
          {             
            rate[i] = IBI;                      
          }
        }
  
        if(firstBeat) // if it's the first time we found a beat, if firstBeat == TRUE
        {                         
          firstBeat = false;                   // clear firstBeat flag
          secondBeat = true;                   // set the secondBeat flag
          return;                              // IBI value is unreliable so discard it
        }   
      // keep a running total of the last 10 IBI values
      word runningTotal = 0;                  // clear the runningTotal variable    

      for(int i=0; i<=8; i++)
        {                // shift data in the rate array
          rate[i] = rate[i+1];                  // and drop the oldest IBI value 
          runningTotal += rate[i];              // add up the 9 oldest IBI values
        }

      rate[9] = IBI;                          // add the latest IBI to the rate array
      runningTotal += rate[9];                // add the latest IBI to runningTotal
      runningTotal /= 10;                     // average the last 10 IBI values 
      BPM = 60000/runningTotal;               // how many beats can fit into a minute? that's BPM!
      QS = true;                              // set Quantified Self flag 
      // QS FLAG IS NOT CLEARED INSIDE THIS ISR
    }                       
  }

  if (Signal < thresh && Pulse == true)
    {   // when the values are going down, the beat is over
      digitalWrite(blinkPin,LOW);            // turn off pin 13 LED
      Pulse = false;                         // reset the Pulse flag so we can do it again
      amp = P - T;                           // get amplitude of the pulse wave
      thresh = amp/2 + T;                    // set thresh at 50% of the amplitude
      P = thresh;                            // reset these for next time
      T = thresh;
    }

  if (N > 2500)
    {                           // if 2.5 seconds go by without a beat
      thresh = 512;                          // set thresh default
      P = 512;                               // set P default
      T = 512;                               // set T default
      lastBeatTime = sampleCounter;          // bring the lastBeatTime up to date        
      firstBeat = true;                      // set these to avoid noise
      secondBeat = false;                    // when we get the heartbeat back
    }

}// end isr