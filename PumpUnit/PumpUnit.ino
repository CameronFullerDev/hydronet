#include <WiFi.h>
#include <esp_now.h>

uint8_t controlMac[] = {0x3C, 0x8A, 0x1F, 0x5E, 0x6D, 0xC4};

typedef struct {
  bool pumpOn;
} PumpCommand;

typedef struct {
  bool pumpStatus;
} PumpStatus;

bool pumpIsOn = false;

void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  PumpCommand cmd;
  memcpy(&cmd, incomingData, sizeof(cmd));

  Serial.print("Pump Command received: ");
  Serial.println(cmd.pumpOn ? "ON" : "OFF");

  pumpIsOn = cmd.pumpOn;

  PumpStatus status = {pumpIsOn};
  esp_now_send(controlMac, (uint8_t *)&status, sizeof(status));
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, controlMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add Control Unit peer");
    return;
  }
}

void loop() {
  Serial.print("Pump is ");
  Serial.println(pumpIsOn ? "ON" : "OFF");
  delay(3000);
}
