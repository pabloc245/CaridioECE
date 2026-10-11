# Functional test plan

The folders below group tests by the project's FS requirements. A passing
software test does not prove sensor placement, LED color, audible pitch, RTC
accuracy over a day, or EEPROM retention across a real power interruption.
Those checks are marked **bench/manual** and require recording the observed
result for the project report.

## FS1 - Capter le rythme cardiaque

| Requirement | Test and expected result | Type |
|---|---|---|
| ET1.1 | `FS1/test_pulse_detector`: synthetic pulses produce the expected BPM; isolated spikes do not; the filter tolerates baseline changes. | Host |
| ET1.2 | `FS1/test_sensor`: read the analog output on A0 and report its range. Confirm the connected sensor is the specified WPSE340. | Board + sensor identification |
| ET1.3 | Synthetic baseline drift/noise test; then vary ambient light with the WPSE340 on a finger and confirm BPM remains stable. | Host + bench/manual |
| ET1.4 | Keep the device running for 24 hours and compare indicated BPM against a reference at the start/end. The measured drift must be recorded; a simulated clock alone cannot validate the hardware timer. | Bench/manual |
| ET1.5 | Synthetic 75 BPM pulse train is reported as 75 BPM. | Host |

## FS2 - Compter le temps

| Requirement | Test and expected result | Type |
|---|---|---|
| ET2.1 | Compare the DS1302 against a reference at the beginning and after 24 hours; record drift and verify it is no more than 3 seconds/day. | Bench/manual |
| ET2.2 | Set/compare the RTC with a reference during commissioning; record the maximum initial error and verify it is no more than 30 seconds. The diagnostic test never sets the clock. | Bench/manual |
| ET2.3 | `FS2/test_rtc` checks that valid calendar data is returned and seconds advance. Set the RTC, remove main power without removing its backup cell, restart, and confirm time continued. | Board + bench/manual |

## FS3 - Afficher l'état de santé

| Requirement | Test and expected result | Type |
|---|---|---|
| ET3.1 | `FS3/test_heart_rate_indicators` checks one classification per BPM; `FS3/test_leds` lights yellow, green, then red separately and verifies no other output pin is high. Visually confirm each physical lens has the expected color. | Host + board/visual |
| ET3.2 | The classifier maps below 60 BPM to yellow, 60-100 to green, and above 100 to red. These adult-at-rest thresholds are project settings, not a patient-specific medical diagnosis. | Host |
| ET3.3 | The indicator helper returns OFF for an unreliable measurement and implausible values outside the configured 30-240 BPM detector range. | Host |

## FS4 - Beeper le rythme cardiaque

| Requirement | Test and expected result | Type |
|---|---|---|
| ET4.1 | `FS4/test_heartbeat_audio` checks tone bands; `FS4/test_buzzer` emits three simulated beats at 50, 75 and 120 BPM. Confirm audible onset cadence follows each pattern. | Host + board/listen |
| ET4.2 | Listen to the 220 Hz (low), 440 Hz (moderate) and 880 Hz (high) sequences and confirm three distinguishable pitches. | Board/listen |
| ET4.3 | `FS4/test_buzzer` waits for D2 presses during a continuous tone; the first must silence it and the second must allow it to resume. The measured mute response should be immediate. | Board + button |

## FS5 - Afficher les informations

| Requirement | Test and expected result | Type |
|---|---|---|
| ET5.1 | On the application, compare the displayed time/BPM with the RTC and pulse detector; remove the finger or wait for stale data and confirm BPM becomes `---`. | Board/visual |
| ET5.2 | `FS5/test_displays` checks that OLEDs at 0x3C and 0x3D acknowledge and draw a sample trace. Confirm the running application shows the live PPG on the second screen. | Board/visual |
| ET5.3 | `FS5/test_pulse_graph_zoom` verifies time scale labels in seconds at all zoom levels; the display diagnostic shows four one-decimal time labels. | Host + board/visual |
| ET5.4 | Rotate the encoder during `FS5/test_displays` and confirm the time span and all four labels change through the five zoom levels. Pressing the encoder in the application must still open the menu. | Board/input |

## FS6 - Enregistrer et consulter les données

| Requirement | Test and expected result | Type |
|---|---|---|
| ET6.1 | `FS6/test_measurement_store` simulates restart persistence and interrupted clear; the device test verifies 1024-byte EEPROM capacity. In the application, record one measurement, fully power-cycle, and confirm it remains. | Host + board/manual |
| ET6.2 | `FS6/test_measurement_store` verifies the integer mean of ten consecutive readings and reset on invalid input. Trigger a real save with D9 and confirm progress completes after ten valid BPM updates. | Host + board/manual |
| ET6.3 | Store and read a record with BPM/date/time in the host suite; compare the real saved timestamp with the RTC in history. | Host + board/manual |
| ET6.4 | Navigate history and clear confirmation with the encoder; verify the list is empty after confirming and unchanged after cancelling. | Board/manual |
| ET6.5 | Switch French/English in the menu and restart; verify selected language persists. EEPROM language persistence is also covered by the host suite. | Host + board/manual |

## Running the host-only suites

From the repository root in PowerShell, compile/run these six suites with the
Unity source already installed by PlatformIO:

```powershell
$unity = ".pio\libdeps\nanoatmega328new\Unity\src"
$suites = @(
  "FS1\test_pulse_detector",
  "FS3\test_heart_rate_indicators",
  "FS4\test_heartbeat_audio",
  "FS5\test_pulse_graph",
  "FS5\test_pulse_graph_zoom",
  "FS6\test_measurement_store"
)
foreach ($suite in $suites) {
  $source = "test\$suite\test_main.cpp"
  $exe = Join-Path $env:TEMP "cardio_test.exe"
  g++ -std=c++11 -Wall -Wextra -Iinclude -I$unity $source "$unity\unity.c" -o $exe
  if ($LASTEXITCODE -ne 0) { throw "Compile failed: $suite" }
  & $exe
  if ($LASTEXITCODE -ne 0) { throw "Tests failed: $suite" }
}
Remove-Item -LiteralPath $exe -ErrorAction SilentlyContinue
```

Do not run all PlatformIO suites unattended: the `test_sensor`, `test_rtc`,
`test_leds`, `test_buzzer`, `test_displays`, and `test_eeprom_hardware` suites
are intended to execute on the connected board and some require visible or
audible human checks. Never use a hardware test as a substitute for setting or
validating the RTC reference, nor clear real EEPROM data as part of a test.
