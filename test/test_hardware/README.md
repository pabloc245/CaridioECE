# Hardware checks

Run the Unity tests with the Nano connected:

```sh
pio test -e nanoatmega328new
```

The tests run in this order:

1. Initialize SSD1306 at `0x3C` and show a confirmation message; fail if it does not respond.
2. Initialize SSD1306 at `0x3D` and show a confirmation message; fail if it does not respond.
3. Write a known date/time to the DS1302 and verify the values read back.
4. Sample the pulse sensor on `A0` for five seconds and require its signal to vary by at least 10 ADC counts. Keep a finger on the sensor during this test.

RTC pins are `IO=D7`, `SCLK=D6`, and `CE=D8`.

Each OLED test first uses `DualSSD1306`. If that attempt fails and
`Adafruit_SSD1306.h` is available, it retries with Adafruit's 64x32 driver.
The `DualSSD1306` initialization failure details are printed to Serial.
The DualSSD1306 instance is destroyed before the fallback display is created,
so the two display buffers are never allocated at the same time.
