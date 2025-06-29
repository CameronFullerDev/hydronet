#include <WiFi.h>
#include <esp_now.h>
#include <DHT.h>

// Control unit MAC address
uint8_t controlMac[] = {0x3C, 0x8A, 0x1F, 0x5E, 0x6D, 0xC4};

constexpr int DHTPIN = 23;     // DHT data pin (GPIO23)
constexpr int DHTTYPE = DHT11; // DHT11 sensor type
constexpr int SOIL_PIN = 32;   // Soil moisture ADC pin (GPIO32)

DHT dht(DHTPIN, DHTTYPE);

typedef struct {
  float temperature;
  float humidity;
  int moisturePercent;
} SensorData;

SensorData data;

constexpr int SOIL_WET = 1800;  // example wet raw value (calibrate for your sensor)
constexpr int SOIL_DRY = 3000;  // example dry raw value

int getMoisturePercent(int raw) {
  raw = constrain(raw, SOIL_WET, SOIL_DRY);
  return map(raw, SOIL_DRY, SOIL_WET, 0, 100);
}

void setup() {
  Serial.begin(115200);
  delay(2000); // Wait for serial monitor to connect
  Serial.println("Starting sensor unit...");

  dht.begin();
  delay(1000);  // Give DHT sensor time to initialize

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(1); // Must match control unit channel

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true);
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, controlMac, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    while (true);
  }

  analogSetAttenuation(ADC_11db); // Use full ADC range for soil moisture sensor

  Serial.println("Setup complete.");
}

void loop() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  int rawMoisture = analogRead(SOIL_PIN);

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("DHT sensor error! Check wiring and pull-up resistor.");
    delay(2000);
    return;  // Skip sending data if sensor fails
  }

  int moisturePercent = getMoisturePercent(rawMoisture);

  data.temperature = temperature;
  data.humidity = humidity;
  data.moisturePercent = moisturePercent;

  esp_err_t res = esp_now_send(controlMac, (uint8_t *)&data, sizeof(data));

  if (res == ESP_OK) {
    Serial.printf("Sent - T: %.1f C, H: %.1f %%, M: %d%% (raw: %d)\n",
                  temperature, humidity, moisturePercent, rawMoisture);
  } else {
    Serial.println("Error sending data");
  }

  delay(2000);
}
