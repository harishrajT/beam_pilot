# Beam Pilot – Autonomous Antenna Alignment & Link Recovery System

> A compact unit that automatically points a directional 2.4 GHz antenna at the strongest signal, and re-aligns itself when the link degrades, without a technician on site.

**Course:** EE356 – Product Design Project, University of Peradeniya · **Duration:** Sep 2026 – present (in progress)
**Group 14:** Harishraj T., *(teammate)*, Selvakumar K.
**My role:** PCB design (KiCad) and hardware implementation of the aligner unit.

---

## Problem
Point-to-point wireless links, such as Wi-Fi backhaul and microwave radio, only perform well when the antennas are precisely aligned. Today alignment is manual: one technician turns the antenna while another reads signal strength over a radio. Once installed, antennas drift because of wind, vibration or maintenance, and the link stays degraded until someone revisits the site.

## How it works

```
 Reference TX (ESP32)                     Aligner unit
 ┌──────────────────┐    ESP-NOW       ┌──────────────────────────────┐
 │ broadcasts packet│ ~~~~~~~~~~~~~~>  │ Yagi ─ ESP32-WROOM-32U       │
 │ every 10 ms      │   2.4 GHz        │        │ RSSI per packet     │
 └──────────────────┘                  │        ▼                     │
                                       │  search algorithm ─> A4988 ─> NEMA-17 (pan)
                                       │        │                     │
                                       │        ▼                     │
                                       │   SSD1306 OLED (status)      │
                                       └──────────────────────────────┘
```

1. **Reference transmitter:** a fixed ESP32 broadcasts small ESP-NOW packets about 100 times per second.
2. **RSSI measurement:** the aligner reads the RSSI of each received packet in the receive callback and averages several packets at each angle. If no packets arrive, it treats that angle as very weak (−127 dBm).
3. **Homing:** at startup, the mount turns slowly until it hits a limit switch, which sets a known zero angle.
4. **Coarse sweep:** 10° steps across a ±150° range find the main lobe, so the system doesn't lock onto a side lobe.
5. **Fine sweep:** 2° steps within ±15° of the coarse peak converge on the true maximum.
6. **Monitor and recover:** every 2 s, the unit compares live RSSI with the recorded peak. If it has dropped by more than 6 dB, it runs the full alignment again.
7. **OLED display:** shows the current state (HOMING, SWEEPING, ALIGNED, MONITORING, RE-ALIGNING), the angle and the RSSI.

Every threshold and step size is a named constant at the top of `aligner_receiver.ino`, so it can be tuned without touching the logic.

## Components
| Component | Purpose |
|---|---|
| ESP32-WROOM-32U | Aligner controller, with a U.FL connector for an external antenna |
| ESP32 (standard) | Fixed reference transmitter |
| 2.4 GHz Yagi / panel antenna | Directional receive antenna |
| NEMA-17 + A4988 (1/16 microstep) | Pan-axis rotation |
| Limit switch | Homing reference |
| SSD1306 OLED (128×64, I²C) | Live status |
| 18650 cells + TP4056, buck/boost converter | Portable regulated power |

Pin connections: [wiring.md](wiring.md)

## Validation
1. **Bench test:** check homing, sweep and monitoring separately before running the full flow.
2. **Indoor RF test:** check that the two units converge on each other.
3. **Radiation pattern check:** a manual 360° sweep records ground-truth RSSI against angle, which is then compared with the automated result.
4. **Disturbance recovery:** knock the antenna off target and measure the time to recover.
5. **Field trial:** a line-of-sight test on campus.

## Results
Testing is in progress. Measurements will be added here once they are done:

| Metric | Result |
|---|---|
| Alignment error vs manual ground truth | pending |
| Time to align from cold start | pending |
| Recovery time after disturbance | pending |
| RSSI at aligned peak vs worst angle | pending |
| Field trial distance (line of sight) | pending |

### Test data
Measurement logs will be added as CSV files, for example:

- `manual_sweep.csv`: angle_deg, rssi_dbm (ground-truth radiation pattern)
- `auto_alignment_runs.csv`: run, final_angle_deg, peak_rssi_dbm, time_to_align_s
- `recovery_tests.csv`: run, disturbance_deg, recovery_time_s

The aligner's Serial output (`sweep: ... deg -> ... dBm`) can be copied straight into a CSV.

## Known limitations
- Wi-Fi RSSI is a proxy for real microwave link quality and is not calibrated against a certified RF reference.
- Pan axis only; tilt is future work.
- Rotation is limited to ±150° to protect the antenna cable (no slip ring).
- A single 20-packet average triggers re-alignment, so a short burst of interference can cause an unnecessary sweep.
- Sensitive to multipath and to other 2.4 GHz traffic.

## Future work
- **4G LTE extension:** the same sweep-and-hold logic can align a 4G antenna (for example a 1800–2700 MHz Yagi) by reading the LTE router's **RSRP**, **SINR** and **cell ID** instead of ESP-NOW RSSI. The unit could then choose the best tower by signal quality, not only signal strength, which suits rural sites with weak coverage.
- Tilt axis, and a slip ring for continuous rotation.
- Hysteresis or repeated confirmation before re-aligning.
- A learned angle-prediction model to reduce search time.

## Repository structure
```
├── aligner_receiver.ino   # ESP32-WROOM-32U: homing, sweep, monitoring
├── reference_tx.ino       # fixed ESP32 ESP-NOW beacon
└── wiring.md              # pin connections
```

## How to run
1. Install the Arduino IDE with **ESP32 board support, core 3.x**. The receive callback uses the core 3.x `esp_now_recv_info_t` signature.
2. Install the libraries **AccelStepper** (Mike McCauley), **Adafruit SSD1306** and **Adafruit GFX**.
3. Flash `reference_tx.ino` to the fixed ESP32, and `aligner_receiver.ino` to the ESP32-WROOM-32U.
4. Set the same `WIFI_CHANNEL` in both sketches.
5. Open the Serial Monitor at **115200 baud** on the aligner to watch the sweep log.

## Acknowledgements
Group 14 teammates, and the EE356 course staff, Department of EEE, University of Peradeniya.
