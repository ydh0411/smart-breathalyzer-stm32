# Smart Breathalyzer and Safety Alert System

[![Platform](https://img.shields.io/badge/platform-STM32L432KC-blue)](https://www.st.com/en/microcontrollers-microprocessors/stm32l432kc.html)
[![Framework](https://img.shields.io/badge/framework-Mbed%20OS%206-green)](https://os.mbed.com/docs/mbed-os/v6.17/)
[![License](https://img.shields.io/badge/license-MIT-orange)](LICENSE)

A real-time embedded alcohol detection system that acquires signals from an MQ-3 sensor, processes them through a trimmed-mean filter and a 7-state finite-state machine on the STM32L432KC, and delivers multi-modal feedback through an OLED display, dual-colour LEDs, an active buzzer, and BLE wireless telemetry.

```
MQ-3 Sensor (AO) → 12-bit ADC → Trimmed-Mean Filter → Baseline Calibration
    → 7-State FSM (with hysteresis) → OLED + LED + Buzzer + BLE → Phone
```

---

## Hardware

### Bill of Materials

| Component | Model / Spec | Purpose |
|-----------|-------------|---------|
| MCU board | NUCLEO-L432KC (STM32L432KC) | Central controller |
| Alcohol sensor | MQ-3 module (analog output) | Breath alcohol signal acquisition |
| OLED display | SSD1306 128×64, I2C | Real-time status display |
| Green LED | LED module with resistor | Safe-state indicator |
| Red LED | LED module with resistor | Warning / Danger indicator |
| Active buzzer | 3.3 V active buzzer module | Acoustic alarm |
| BLE module | HM-10 (UART, 3.3 V) | Wireless telemetry to phone |
| Breadboard | 400- or 830-point | Prototyping |
| Jumper wires | Male-to-male + male-to-female | Connections |
| Resistors | 220 Ω, 1 kΩ, 10 kΩ, 20 kΩ | LED limiting, voltage divider |

### Wiring Table

| Module | Module Pin | STM32 Pin (silkscreen) | MCU Pin | Notes |
|--------|-----------|----------------------|---------|-------|
| MQ-3 | AO | A0 | PA_0 | ADC input; add 10k/20k divider if AO > 3.3 V |
| MQ-3 | VCC | 5V | — | |
| MQ-3 | GND | GND | — | Must share ground with MCU |
| SSD1306 OLED | SDA | D4 | PB_7 | I2C data |
| SSD1306 OLED | SCL | D5 | PB_6 | I2C clock |
| SSD1306 OLED | VCC | 3.3V | — | |
| SSD1306 OLED | GND | GND | — | |
| Green LED | SIG | D9 | PA_8 | Safe state |
| Green LED | GND | GND | — | |
| Red LED | SIG | D2 | PA_12 | Warning / Danger |
| Red LED | GND | GND | — | |
| Active Buzzer | SIG | D3 | PB_0 | |
| Active Buzzer | GND | GND | — | |
| HM-10 BLE | TXD | D0 | PA_10 | MCU RX |
| HM-10 BLE | RXD | D1 | PA_9 | MCU TX |
| HM-10 BLE | VCC | 3.3V | — | |
| HM-10 BLE | GND | GND | — | |

> **Ground rule**: All modules must share a common ground with the MCU board. Floating grounds cause ADC instability and I2C communication failures.

---

## Software Architecture

The firmware is modular, with each file responsible for a single concern:

```
src/
├── main.cpp          # Orchestration: ADC loop, state transitions, BLE commands
├── config.h          # Pin definitions, timing constants, tunable thresholds
├── filter.h          # MovingTrimmedAverage — circular buffer, window = 25
├── outputs.h         # SystemState enum, LED/buzzer pattern generator
├── display.h         # OLED rendering + BLE telemetry frame formatting
└── oled_driver.h     # SSD1306 I2C driver, framebuffer, 5×8 font
```

### Finite-State Machine (7 states)

| State | Trigger | Output Behaviour |
|-------|---------|-----------------|
| **Preheating** | Power-on / recalibration | Green LED slow blink, OLED countdown, baseline averaging (30 s) |
| **Safe** | Filtered ADC < Warning threshold | Green LED on, buzzer off |
| **Warning** | ADC ≥ baseline + 250 (×2 consecutive) | Green on, red fast blink, buzzer double-beep every 1.5 s |
| **Danger** | ADC ≥ baseline + 1500 (×2 consecutive) | Red solid, buzzer triple-beep per second |
| **Cooldown** | ADC < Danger − 200 (×3 consecutive) | Red/green alternate, 10 s guard; entry-reference comparison detects re-exposure |
| **Sleep** | Safe idle 5 min + ADC < wake threshold | All outputs off; BLE command `W` or `WAKE` to resume |
| **SensorFault** | ADC ≤ 10 or ≥ 4085 (×20 consecutive) | Red fast blink, fault beep; auto-recover after 20 in-range samples |

State transitions use consecutive-sample counters for both entry and release, plus release hysteresis, eliminating false triggers from single-sample noise. The Cooldown state stores an entry reference value (`cooldown_ref_adc`) to distinguish genuine re-exposure from sensor desorption bounce.

### BLE Telemetry

Every 2 seconds, the system transmits a status frame over UART at 9600 baud:

```
STATE=SAFE,RAW=912,AVG=908,BASE=900,W=1150,D=2400,LVL=3
```

**BLE commands** (send via phone serial monitor):

| Command | Action |
|---------|--------|
| `C` / `CAL` | Force recalibration (return to Preheating) |
| `S` / `SLEEP` | Enter Sleep state |
| `W` / `WAKE` | Wake from Sleep |
| `T` / `STAT` | Request immediate status frame |
| `H` / `HELP` | List available commands |

Commands are buffered and processed after a 60 ms idle gap.

### Key Tunable Parameters (`src/config.h`)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `PREHEAT_TIME` | 30 s | Warm-up + baseline calibration duration |
| `FILTER_WINDOW` | 25 | Moving trimmed-average window size |
| `SAMPLE_PERIOD` | 100 ms | ADC sampling interval |
| `DISPLAY_PERIOD` | 250 ms | OLED refresh interval |
| `WARNING_OFFSET` | 250 | Warning threshold above baseline |
| `DANGER_OFFSET` | 1500 | Danger threshold above baseline |
| `WARNING_HYSTERESIS` | 100 | Release hysteresis for Warning → Safe |
| `DANGER_HYSTERESIS` | 200 | Release hysteresis for Danger → Cooldown |
| `COOLDOWN_TIME` | 10 s | Minimum cooldown before state re-evaluation |
| `AUTO_SLEEP_AFTER` | 5 min | Idle time before auto-sleep |
| `ENABLE_BLE_TELEMETRY` | true | Enable/disable BLE status frames |
| `DATA_LOGGING` | false | Enable CSV serial data logging |

---

## Build & Run

### Prerequisites

- [Mbed Studio](https://os.mbed.com/studio/) or [Mbed CLI](https://os.mbed.com/docs/mbed-os/v6.17/build-tools/mbed-cli.html)
- ARM GNU Toolchain (arm-none-eabi-gcc ≥ 9.0)
- Target: `NUCLEO_L432KC`
- Toolchain: `GCC_ARM`

### Mbed Studio (Recommended)

1. Open Mbed Studio → **File → New Program** → Empty Mbed OS Program.
2. Set target to `NUCLEO_L432KC`.
3. Copy all files from `src/` and `mbed_app.json` into the project root.
4. Click **Build** (hammer icon) → **Run** (play icon) to flash.
5. Open serial monitor at **115200 baud**.

### Mbed CLI

```bash
mbed-tools new .
mbed-tools deploy
mbed-tools configure -m NUCLEO_L432KC -t GCC_ARM
mbed-tools compile -m NUCLEO_L432KC -t GCC_ARM --flash
```

### Expected Serial Output

```
Smart Breathalyzer booting...
BLE CMD: W/WAKE, S/SLEEP, C/CAL, T/STAT, H/HELP
Output self-test start...
Output self-test end.
Calibration done. baseline=908 warning=1158 danger=2408
STATE => SAFE, raw=912 avg=908
STATE => WARNING, raw=1180 avg=1165
STATE => DANGER, raw=2450 avg=2432
STATE => COOLDOWN, raw=1980 avg=1965
STATE => SAFE, raw=910 avg=907
```

---

## Demo Procedure

1. Power on the board. The OLED displays "WARMING" with a countdown.
2. After 30 s, the system enters **Safe** — green LED on, OLED shows "SAFE".
3. Bring a weak alcohol stimulus (e.g. alcohol wipe at 15–20 cm) near the MQ-3 sensor. System enters **Warning** — red LED blinks, buzzer beeps intermittently.
4. Bring the stimulus closer (3–5 cm) for **Danger** — red LED solid, continuous alarm.
5. Remove the stimulus. System enters **Cooldown** — alternating red/green LEDs for 10 s.
6. After cooldown, the system returns to Safe (or Warning, depending on residual signal).
7. (Optional) Open a BLE serial app on your phone, connect to HM-10, and observe `STATE=...` telemetry frames. Send `W` to wake from Sleep, `C` to force recalibration.

---

## License

MIT License. See [LICENSE](LICENSE) for details.
