#include <WiFi.h>
#include <esp_now.h>

#define RELAY_PIN 25 // Choose a suitable GPIO (not used for boot)

bool pumpIsOn = false;

typedef struct {
  bool pumpOn;
} PumpCommand;

typedef struct {
  bool pumpStatus;
} PumpStatus;

// === Send pump status back ===
void sendStatus(const uint8_t *mac) {
  PumpStatus status = {pumpIsOn};
  esp_now_send(mac, (uint8_t *)&status, sizeof(status));
}

// === Receive pump commands ===
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len != sizeof(PumpCommand)) return;

  PumpCommand cmd;
  memcpy(&cmd, incomingData, sizeof(cmd));

  if (cmd.pumpOn) {
    digitalWrite(RELAY_PIN, HIGH);  // LOW to turn ON (low-level trigger)
    pumpIsOn = true;
    Serial.println("Pump turned ON");
  } else {
    digitalWrite(RELAY_PIN, LOW); // HIGH to turn OFF
    pumpIsOn = false;
    Serial.println("Pump turned OFF");
  }

  sendStatus(info->src_addr);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // Start OFF (relay OFF)

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(1); // Match channel if needed

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    while (true);
  }

  esp_now_register_recv_cb(onDataRecv);

  Serial.println("Pump unit ready.");
}

void loop() {
  // Nothing in loop
}
