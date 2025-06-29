#include <WiFi.h>
#include <esp_now.h>
#include <LiquidCrystal.h>

// === LCD Wiring: RS, E, D4, D5, D6, D7 ===
LiquidCrystal lcd(13, 12, 14, 27, 26, 25);

// === MAC Addresses ===
uint8_t sensorMac[] = {0x20, 0x43, 0xA8, 0x64, 0xF9, 0x64};
uint8_t pumpMac[]   = {0x3C, 0x8A, 0x1F, 0x5E, 0x8D, 0x50};

// === Data Structures ===
typedef struct {
  float temperature;
  float humidity;
  int moisturePercent;
} SensorData;

typedef struct {
  bool pumpOn;
} PumpCommand;

typedef struct {
  bool pumpStatus;
} PumpStatus;

// === Sensor State ===
float lastTemp = 0;
float lastHumidity = 0;
int lastMoisture = 0;
bool pumpIsOn = false;
bool sensorDataReceived = false;

// === Auto-Pump Logic ===
const float TEMP_HIGH_THRESHOLD = 18.0;      // °C
const int SOIL_DRY_THRESHOLD = 40;           // %
const unsigned long PUMP_ON_DURATION = 60 * 1000;       // 1 minute
const unsigned long PUMP_INTERVAL = 10UL * 60 * 1000;   // 10 minutes

unsigned long lastPumpCheck = 0;
unsigned long pumpStartTime = 0;
bool pumpRequestedOn = false;

// === LCD Update ===
void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print((int)lastTemp);
  lcd.print("C H:");
  lcd.print((int)lastHumidity);
  lcd.print("%");

  lcd.setCursor(0, 1);
  lcd.print("M:");
  lcd.print(lastMoisture);
  lcd.print("% P:");
  lcd.print(pumpIsOn ? "ON " : "OFF");
}

// === MAC Print Helper ===
void printMac(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }
}

// === ESP-NOW Receive Callback ===
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  const uint8_t *mac = recv_info->src_addr;

  if (len == sizeof(SensorData)) {
    SensorData data;
    memcpy(&data, incomingData, sizeof(data));
    lastTemp = data.temperature;
    lastHumidity = data.humidity;
    lastMoisture = data.moisturePercent;

    sensorDataReceived = true;

    Serial.print("Sensor data from ");
    printMac(mac);
    Serial.printf(" - Temp: %.1f C, Humidity: %.1f%%, Moisture: %d%%\n",
                  data.temperature, data.humidity, data.moisturePercent);

    // Immediate auto-pump check on first data
    if (!pumpRequestedOn && lastTemp > TEMP_HIGH_THRESHOLD && lastMoisture < SOIL_DRY_THRESHOLD) {
      PumpCommand autoCmd = {true};
      esp_now_send(pumpMac, (uint8_t *)&autoCmd, sizeof(autoCmd));
      pumpRequestedOn = true;
      pumpStartTime = millis();
      lastPumpCheck = millis();  // Reset interval
      Serial.println("Auto: Sent pump ON (startup trigger)");
    }

  } else if (len == sizeof(PumpStatus)) {
    PumpStatus status;
    memcpy(&status, incomingData, sizeof(status));
    pumpIsOn = status.pumpStatus;

    Serial.print("Pump status from ");
    printMac(mac);
    Serial.println(pumpIsOn ? "ON" : "OFF");
  }

  updateLCD();
}

void setup() {
  Serial.begin(115200);
  lcd.begin(16, 2);
  lcd.print("Starting...");
  delay(2000);
  lcd.clear();

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(1); // Must match sensor unit channel

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    lcd.print("ESP-NOW failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  // Add Sensor Unit
  memcpy(peerInfo.peer_addr, sensorMac, 6);
  esp_now_add_peer(&peerInfo);

  // Add Pump Unit
  memcpy(peerInfo.peer_addr, pumpMac, 6);
  esp_now_add_peer(&peerInfo);

  Serial.println("Ready. Type 'on' or 'off' to control pump.");
  lcd.print("Control Ready");
  delay(2000);
  lcd.clear();
}

void loop() {
  unsigned long now = millis();

  // === Manual Serial Control ===
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    PumpCommand command;

    if (cmd.equalsIgnoreCase("on")) {
      command.pumpOn = true;
      pumpRequestedOn = true;
      pumpStartTime = now;
    } else if (cmd.equalsIgnoreCase("off")) {
      command.pumpOn = false;
      pumpRequestedOn = false;
    } else {
      Serial.println("Unknown command. Use 'on' or 'off'");
      return;
    }

    esp_now_send(pumpMac, (uint8_t *)&command, sizeof(command));
    Serial.print("Sent pump command: ");
    Serial.println(command.pumpOn ? "ON" : "OFF");
  }

  // === Auto-Pump Periodic Activation ===
  if (sensorDataReceived && !pumpRequestedOn && now - lastPumpCheck >= PUMP_INTERVAL) {
    lastPumpCheck = now;

    if (lastTemp > TEMP_HIGH_THRESHOLD && lastMoisture < SOIL_DRY_THRESHOLD) {
      PumpCommand autoCmd = {true};
      esp_now_send(pumpMac, (uint8_t *)&autoCmd, sizeof(autoCmd));
      pumpRequestedOn = true;
      pumpStartTime = now;
      Serial.println("Auto: Sent pump ON");
    }
  }

  // === Auto-Pump Deactivation ===
  if (pumpRequestedOn && now - pumpStartTime >= PUMP_ON_DURATION) {
    PumpCommand autoCmd = {false};
    esp_now_send(pumpMac, (uint8_t *)&autoCmd, sizeof(autoCmd));
    pumpRequestedOn = false;
    Serial.println("Auto: Sent pump OFF (1 min elapsed)");
  }
}
