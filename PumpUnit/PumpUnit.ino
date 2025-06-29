#include <WiFi.h>
#include <esp_now.h>

// === Define MAC addresses ===
uint8_t controlMac[] = {0x3C, 0x8A, 0x1F, 0x5E, 0x6D, 0xC4};
uint8_t sensorMac[]  = {0x20, 0x43, 0xA8, 0x64, 0xF9, 0x64};
uint8_t pumpMac[]    = {0x3C, 0x8A, 0x1F, 0x5E, 0x8D, 0x50}; // This device's MAC (optional for ID only)

// === Pump Relay Setup ===
#define RELAY_PIN 25  // Make sure this GPIO is safe to use
bool pumpIsOn = false;

// === ESP-NOW Structures ===
typedef struct {
  bool pumpOn;
} PumpCommand;

typedef struct {
  bool pumpStatus;
} PumpStatus;

// === Send pump status back to control unit ===
void sendStatus(const uint8_t *mac) {
  PumpStatus status = {pumpIsOn};
  esp_err_t result = esp_now_send(mac, (uint8_t *)&status, sizeof(status));

  Serial.print("Sent pump status to ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }

  Serial.print(" = ");
  Serial.println(result == ESP_OK ? "SUCCESS" : "FAIL");
}

// === On receive command from control unit ===
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len != sizeof(PumpCommand)) {
    Serial.println("Invalid data size received");
    return;
  }

  PumpCommand cmd;
  memcpy(&cmd, incomingData, sizeof(cmd));

  // === Dynamically register sender (control unit) as peer if needed ===
  if (!esp_now_is_peer_exist(info->src_addr)) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, info->src_addr, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add peer");
      return;
    }
  }

  // === Control the relay ===
  if (cmd.pumpOn) {
    digitalWrite(RELAY_PIN, HIGH);  // HIGH for ON (high-level trigger)
    pumpIsOn = true;
    Serial.println("Pump turned ON");
  } else {
    digitalWrite(RELAY_PIN, LOW);   // LOW for OFF
    pumpIsOn = false;
    Serial.println("Pump turned OFF");
  }

  // === Respond with current pump status ===
  sendStatus(info->src_addr);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Pump unit starting...");

  // === Setup relay control pin ===
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Default OFF

  // === WiFi + ESP-NOW ===
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(1); // Optional: Match control/sensor if needed

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    while (true);  // Halt
  }

  // === Register receive callback ===
  esp_now_register_recv_cb(onDataRecv);

  Serial.println("Pump unit ready.");
}

void loop() {
  // Nothing to do continuously
}
