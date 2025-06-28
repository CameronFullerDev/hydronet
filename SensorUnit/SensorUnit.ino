#include <WiFi.h>
#include <esp_now.h>
#include <DHT.h>

uint8_t controlMac[] = {0x3C, 0x8A, 0x1F, 0x5E, 0x6D, 0xC4};

#define DHTPIN 13
#define DHTTYPE DHT11
#define SOIL_PIN 27

#define SOIL_WET 300   // Adjust after testing your sensor readings
#define SOIL_DRY 800

DHT dht(DHTPIN, DHTTYPE);

typedef struct {
  float temperature;
  float humidity;
  int moisturePercent;
} SensorData;

SensorData data;

int getMoisturePercent(int rawValue) {
  rawValue = constrain(rawValue, SOIL_WET, SOIL_DRY);
  // Map so that dry (high raw) is 0% and wet (low raw) is 100%
  return map(rawValue, SOIL_DRY, SOIL_WET, 0, 100);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  dht.begin();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

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
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  int rawMoisture = analogRead(SOIL_PIN);
  int moisturePercent = getMoisturePercent(rawMoisture);

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
  } else {
    data.temperature = temperature;
    data.humidity = humidity;
    data.moisturePercent = moisturePercent;

    esp_err_t result = esp_now_send(controlMac, (uint8_t *)&data, sizeof(data));
    if (result == ESP_OK) {
      Serial.printf("Sent - T: %.1f C, H: %.1f %%, M: %d %% (raw:%d)\n", temperature, humidity, moisturePercent, rawMoisture);
    } else {
      Serial.println("Error sending sensor data");
    }
  }

  delay(2000);
}
