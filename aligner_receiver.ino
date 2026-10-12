// =====================================================================
// AUTONOMOUS ANTENNA ALIGNER - RECEIVER / CONTROLLER
// Board: ESP32-WROOM-32U, directional Yagi antenna, A4988 + NEMA-17
// =====================================================================
// NOT compiled or tested on hardware by the author of this file.
// Check pin numbers against your wiring, and test each stage
// (homing, sweep, monitor) separately before trusting the full flow.
//
// Libraries needed (install via Arduino Library Manager):
//   - AccelStepper by Mike McCauley
//   - Adafruit SSD1306 + Adafruit GFX (if using the OLED)
//
// Pairs with: espnow_rssi_tx.ino running on a STANDARD ESP32 board,
// fixed in position, as the reference transmitter. Use the same
// CHANNEL value in both sketches.
// =====================================================================

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <AccelStepper.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- USER-ADJUSTABLE SETTINGS ----------------

const uint8_t  WIFI_CHANNEL   = 1;      // must match the transmitter

// Pin assignments - change to match your wiring
const int PIN_STEP   = 25;
const int PIN_DIR    = 26;
const int PIN_ENABLE = 27;
const int PIN_LIMIT  = 32;              // limit switch, wired to GND when pressed, INPUT_PULLUP

// Mechanical / motion settings
const float STEPS_PER_DEGREE   = 8.89;  // 3200 steps/rev (1/16 microstep) / 360. CHECK against your driver's microstep jumpers.
const float MAX_TRAVEL_DEG     = 150.0; // sweep only within +/- this, from home
const float MOTOR_MAX_SPEED    = 800;   // steps/sec
const float MOTOR_ACCEL        = 400;   // steps/sec^2
const float HOMING_SPEED       = 200;   // steps/sec, slow and gentle

// Sweep settings
const float COARSE_STEP_DEG    = 10.0;  // coarse sweep resolution
const float FINE_RANGE_DEG     = 15.0;  // fine sweep searches +/- this around coarse best
const float FINE_STEP_DEG      = 2.0;   // fine sweep resolution
const int   SAMPLES_PER_POINT  = 15;    // packets averaged at each test angle
const int   SETTLE_MS          = 150;   // wait after a move before sampling

// Monitoring / re-alignment settings
const float DROP_THRESHOLD_DB  = 6.0;   // re-sweep if avg RSSI falls this far below the recorded peak
const int   MONITOR_SAMPLES    = 20;    // packets averaged per monitoring check
const unsigned long MONITOR_PERIOD_MS = 2000; // how often to check while holding

// ---------------- OLED ----------------
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ---------------- Stepper ----------------
AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);

// ---------------- ESP-NOW / RSSI ----------------
struct __attribute__((packed)) Packet {
  uint32_t seq;
};

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile int32_t sumRssi = 0;
volatile uint32_t pktCount = 0;

void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < (int)sizeof(Packet)) return;
  int rssi = info->rx_ctrl->rssi;
  portENTER_CRITICAL(&mux);
  sumRssi += rssi;
  pktCount++;
  portEXIT_CRITICAL(&mux);
}

// Clears the running RSSI accumulator and returns the average once
// enough packets have been collected, or times out.
float collectAverageRssi(int numSamples, unsigned long timeoutMs = 2000) {
  portENTER_CRITICAL(&mux);
  sumRssi = 0;
  pktCount = 0;
  portEXIT_CRITICAL(&mux);

  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    uint32_t c;
    portENTER_CRITICAL(&mux);
    c = pktCount;
    portEXIT_CRITICAL(&mux);
    if (c >= (uint32_t)numSamples) break;
    delay(5);
  }

  int32_t s; uint32_t c;
  portENTER_CRITICAL(&mux);
  s = sumRssi; c = pktCount;
  portEXIT_CRITICAL(&mux);

  if (c == 0) return -127.0;            // no packets received - treat as very weak
  return (float)s / (float)c;
}

// ---------------- Motion helpers ----------------

void moveToDegree(float deg) {
  long targetSteps = (long)(deg * STEPS_PER_DEGREE);
  stepper.moveTo(targetSteps);
  while (stepper.distanceToGo() != 0) {
    stepper.run();
  }
}

float currentDegree() {
  return stepper.currentPosition() / STEPS_PER_DEGREE;
}

// ---------------- OLED helper ----------------

void showStatus(const char* state, float deg, float rssi) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(state);
  display.setCursor(0, 20);
  display.print("Angle: "); display.print(deg, 1); display.println(" deg");
  display.setCursor(0, 36);
  display.print("RSSI:  "); display.print(rssi, 1); display.println(" dBm");
  display.display();
}

// ---------------- Homing ----------------

void doHoming() {
  showStatus("HOMING", 0, 0);
  stepper.setMaxSpeed(HOMING_SPEED);
  stepper.setAcceleration(MOTOR_ACCEL);

  // Move toward the limit switch. Adjust sign if it homes the wrong way.
  stepper.moveTo(100000);
  while (digitalRead(PIN_LIMIT) == HIGH) {   // HIGH = not pressed (INPUT_PULLUP)
    stepper.setSpeed(HOMING_SPEED);
    stepper.runSpeed();
  }
  stepper.stop();
  stepper.setCurrentPosition(0);             // this is now angle 0 (home)

  stepper.setMaxSpeed(MOTOR_MAX_SPEED);
  stepper.setAcceleration(MOTOR_ACCEL);
  Serial.println("Homing complete, position zeroed.");
}

// ---------------- Sweep logic ----------------

// Sweeps from startDeg to endDeg in stepDeg increments, sampling RSSI at
// each stop. Returns the angle with the best average RSSI.
float sweepRange(float startDeg, float endDeg, float stepDeg, float &bestRssiOut) {
  float bestDeg = startDeg;
  float bestRssi = -200.0;

  for (float d = startDeg; d <= endDeg + 0.01; d += stepDeg) {
    moveToDegree(d);
    delay(SETTLE_MS);
    float avg = collectAverageRssi(SAMPLES_PER_POINT);
    Serial.printf("sweep: %.1f deg -> %.1f dBm\n", d, avg);
    showStatus("SWEEPING", d, avg);

    if (avg > bestRssi) {
      bestRssi = avg;
      bestDeg = d;
    }
  }
  bestRssiOut = bestRssi;
  return bestDeg;
}

float peakRssi = -127.0;   // RSSI recorded at the aligned position
float peakAngle = 0.0;

void runFullAlignment() {
  float coarseBestRssi;
  float coarseBest = sweepRange(-MAX_TRAVEL_DEG, MAX_TRAVEL_DEG, COARSE_STEP_DEG, coarseBestRssi);
  Serial.printf("Coarse best: %.1f deg at %.1f dBm\n", coarseBest, coarseBestRssi);

  float fineStart = max(-MAX_TRAVEL_DEG, coarseBest - FINE_RANGE_DEG);
  float fineEnd   = min(MAX_TRAVEL_DEG, coarseBest + FINE_RANGE_DEG);
  float fineBestRssi;
  float fineBest = sweepRange(fineStart, fineEnd, FINE_STEP_DEG, fineBestRssi);
  Serial.printf("Fine best: %.1f deg at %.1f dBm\n", fineBest, fineBestRssi);

  moveToDegree(fineBest);
  peakAngle = fineBest;
  peakRssi = fineBestRssi;

  showStatus("ALIGNED", peakAngle, peakRssi);
}

// ---------------- Setup / loop ----------------

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LIMIT, INPUT_PULLUP);
  pinMode(PIN_ENABLE, OUTPUT);
  digitalWrite(PIN_ENABLE, LOW);        // LOW = driver enabled on A4988

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed - check wiring/address");
  }
  showStatus("BOOTING", 0, 0);

  // Wi-Fi / ESP-NOW setup
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onRecv);

  // Stepper setup
  stepper.setMaxSpeed(MOTOR_MAX_SPEED);
  stepper.setAcceleration(MOTOR_ACCEL);

  doHoming();
  runFullAlignment();
}

void loop() {
  static unsigned long lastCheck = 0;

  if (millis() - lastCheck >= MONITOR_PERIOD_MS) {
    lastCheck = millis();

    float avg = collectAverageRssi(MONITOR_SAMPLES);
    Serial.printf("monitor: %.1f dBm (peak was %.1f at %.1f deg)\n", avg, peakRssi, peakAngle);
    showStatus("MONITORING", currentDegree(), avg);

    if (avg < peakRssi - DROP_THRESHOLD_DB) {
      Serial.println("Link degraded - re-aligning");
      showStatus("RE-ALIGNING", currentDegree(), avg);
      runFullAlignment();
    }
  }
}
