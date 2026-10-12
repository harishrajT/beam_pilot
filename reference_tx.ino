// =====================================================================
// AUTONOMOUS ANTENNA ALIGNER - REFERENCE TRANSMITTER
// Board: standard ESP32 (fixed position, acts as the "far end" of the link)
// =====================================================================
// Broadcasts a small ESP-NOW packet at a steady rate. The aligner
// measures the RSSI of these packets to find the best antenna angle.
//
// WIFI_CHANNEL must match the value in aligner_receiver.ino.
// Requires ESP32 Arduino core 3.x.
// =====================================================================

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

const uint8_t WIFI_CHANNEL = 1;          // must match the receiver
const unsigned long SEND_PERIOD_MS = 10; // ~100 packets per second

struct __attribute__((packed)) Packet {
  uint32_t seq;
};

uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
Packet pkt = {0};

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true) delay(1000);
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastAddr, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
    while (true) delay(1000);
  }
  Serial.println("Reference transmitter running");
}

void loop() {
  pkt.seq++;
  esp_now_send(broadcastAddr, (uint8_t *)&pkt, sizeof(pkt));
  delay(SEND_PERIOD_MS);
}
