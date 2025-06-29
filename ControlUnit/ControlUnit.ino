#include <WiFi.h>
#include <esp_now.h>
#include <LiquidCrystal.h>

// LCD pins: RS, E, D4, D5, D6, D7
LiquidCrystal lcd(13, 12, 14, 27, 26, 25);

// MAC addresses of remote devices
uint8_t sensorMac[6] = {0x20, 0x43, 0xA8, 0x64, 0xF9, 0x64};
uint8_t pumpMac[6]   = {0x3C, 0x8A, 0x1F, 0x5E, 0x8D, 0x50};

// Data structures for communication
struct SensorData {
  float temperature;
  float humidity;
  int moisturePercent;
};

struct PumpCommand {
  bool pumpOn;
};

struct PumpStatus {
  bool pumpStatus;
};

// Last received sensor values
float lastTemperature = 0;
float lastHumidity = 0;
int lastMoisturePercent = 0;

// Pump control flags and timing
bool pumpIsOn = false;
bool pumpCommandActive = false;
bool sensorDataReceived = false;
bool pumpTurnedOnManually = false;

const float TEMP_THRESHOLD = 18.0f;          // °C threshold to start pump
const int MOISTURE_THRESHOLD = 40;           // % soil dryness threshold
const unsigned long PUMP_ON_TIME_MS = 60 * 1000;         // Pump ON duration (1 minute)
const unsigned long PUMP_CHECK_INTERVAL_MS = 10 * 60 * 1000;  // Check interval (10 minutes)

unsigned long lastPumpCheckTime = 0;
unsigned long pumpStartTime = 0;

// Helper: Print MAC address to Serial in human-readable form
void printMac(const uint8_t* mac) {
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }
}

// Update the LCD with current sensor and pump states
void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.printf("T:%dC H:%d%%", (int)lastTemperature, (int)lastHumidity);

  lcd.setCursor(0, 1);
  lcd.printf("M:%d%% P:%s", lastMoisturePercent, pumpIsOn ? "ON " : "OFF");
}

// Sends a pump command to turn pump ON or OFF
// isManual indicates whether the command came from manual serial input or auto logic
void sendPumpCommand(bool turnOn, bool isManual = false) {
  PumpCommand cmd = { turnOn };
  esp_now_send(pumpMac, (uint8_t*)&cmd, sizeof(cmd));
  pumpCommandActive = turnOn;

  pumpTurnedOnManually = turnOn && isManual;

  Serial.printf("Sent pump command: %s (%s)\n", turnOn ? "ON" : "OFF", isManual ? "manual" : "auto");
}

// ESP-NOW callback: Handles incoming data packets
void onDataReceived(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
  const uint8_t* senderMac = info->src_addr;

  if (length == sizeof(SensorData)) {
    SensorData sensor;
    memcpy(&sensor, data, sizeof(sensor));

    lastTemperature = sensor.temperature;
    lastHumidity = sensor.humidity;
    lastMoisturePercent = sensor.moisturePercent;
    sensorDataReceived = true;

    Serial.print("Sensor data from ");
    printMac(senderMac);
    Serial.printf(" - Temp: %.1fC Humidity: %.1f%% Moisture: %d%%\n",
                  sensor.temperature, sensor.humidity, sensor.moisturePercent);

    // Auto-activate pump on startup if conditions met
    if (!pumpCommandActive &&
        lastTemperature > TEMP_THRESHOLD &&
        lastMoisturePercent < MOISTURE_THRESHOLD) {

      sendPumpCommand(true, false);
      pumpStartTime = millis();
      lastPumpCheckTime = millis();

      Serial.println("Auto: Pump turned ON (startup condition)");
    }

  } else if (length == sizeof(PumpStatus)) {
    PumpStatus status;
    memcpy(&status, data, sizeof(status));
    pumpIsOn = status.pumpStatus;

    Serial.print("Pump status from ");
    printMac(senderMac);
    Serial.println(pumpIsOn ? "ON" : "OFF");
  }

  updateLCD();
}

// Handles manual pump control commands from Serial input
void handleSerialCommands() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd.equalsIgnoreCase("on")) {
    sendPumpCommand(true, true);
    pumpStartTime = millis();
  }
  else if (cmd.equalsIgnoreCase("off")) {
    sendPumpCommand(false, true);
    pumpTurnedOnManually = false; // manual OFF disables manual mode
  }
  else {
    Serial.println("Unknown command. Use 'on' or 'off'.");
  }
}

// Checks sensor data and activates pump automatically if conditions met
void checkAutoPumpActivation(unsigned long now) {
  if (!sensorDataReceived || pumpCommandActive) return;

  if (now - lastPumpCheckTime >= PUMP_CHECK_INTERVAL_MS) {
    lastPumpCheckTime = now;

    if (lastTemperature > TEMP_THRESHOLD &&
        lastMoisturePercent < MOISTURE_THRESHOLD) {
      sendPumpCommand(true, false);
      pumpStartTime = now;
      Serial.println("Auto: Pump turned ON");
    }
  }
}

// Turns pump off after predefined duration if it was auto activated
void checkAutoPumpDeactivation(unsigned long now) {
  if (!pumpCommandActive) return;

  // Only auto-turn off if pump was auto-activated
  if (!pumpTurnedOnManually && (now - pumpStartTime >= PUMP_ON_TIME_MS)) {
    sendPumpCommand(false, false);
    Serial.println("Auto: Pump turned OFF (timeout elapsed)");
  }
}

void setup() {
  Serial.begin(115200);

  lcd.begin(16, 2);
  lcd.print("Starting...");
  delay(2000);
  lcd.clear();

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(1); // Must match sensor channel

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    lcd.print("ESP-NOW failed");
    return;
  }

  esp_now_register_recv_cb(onDataReceived);

  esp_now_peer_info_t peer = {};
  peer.channel = 1;
  peer.encrypt = false;

  // Add sensor peer
  memcpy(peer.peer_addr, sensorMac, 6);
  esp_now_add_peer(&peer);

  // Add pump peer
  memcpy(peer.peer_addr, pumpMac, 6);
  esp_now_add_peer(&peer);

  Serial.println("Ready. Use 'on' or 'off' commands to control pump.");
  lcd.print("Control Ready");
  delay(2000);
  lcd.clear();
}

void loop() {
  unsigned long now = millis();

  handleSerialCommands();
  checkAutoPumpActivation(now);
  checkAutoPumpDeactivation(now);
}
