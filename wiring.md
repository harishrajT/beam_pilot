# Wiring — Aligner Unit (ESP32-WROOM-32U)

| Signal | ESP32 pin | Connects to | Notes |
|---|---|---|---|
| STEP | GPIO 25 | A4988 STEP | |
| DIR | GPIO 26 | A4988 DIR | |
| ENABLE | GPIO 27 | A4988 EN | LOW = driver enabled |
| LIMIT | GPIO 32 | Limit switch → GND | `INPUT_PULLUP`; reads LOW when pressed |
| SDA | GPIO 21 | SSD1306 SDA | I²C address 0x3C |
| SCL | GPIO 22 | SSD1306 SCL | |
| Antenna | U.FL | 2.4 GHz directional antenna | via U.FL → RP-SMA pigtail |

## A4988 notes
- Microstepping: 1/16 (MS1, MS2, MS3 all HIGH) → 3200 steps/rev → `STEPS_PER_DEGREE = 8.89`.
  If your jumpers differ, update `STEPS_PER_DEGREE` in `aligner_receiver.ino`.
- Set the current limit (Vref) to suit your NEMA-17 before connecting the motor.
- Tie RESET and SLEEP together. Put a 100 µF capacitor across VMOT and GND.

## Reference transmitter
A standard ESP32 running `reference_tx.ino`. It needs power only.
