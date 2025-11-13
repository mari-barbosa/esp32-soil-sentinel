/*
 * ESP32 Soil Sentinel - Plant Monitoring System
 * 
 * This system monitors soil moisture, temperature, and humidity for multiple plants.
 * Each plant has its own calibration profile and ThingSpeak channel.
 * 
 * Features:
 * - Multi-plant profile support
 * - Calibration mode for soil sensors
 * - LCD display for real-time monitoring
 * - ThingSpeak cloud data logging
 * - Button navigation interface
 */

// ============================================================================
// LIBRARY INCLUDES
// ============================================================================
#include <Wire.h>                // I2C communication
#include <LiquidCrystal_I2C.h>   // LCD display control
#include <WiFi.h>                // ESP32 WiFi functionality
#include "DHT.h"                 // Temperature and humidity sensor
#include "ThingSpeak.h"          // IoT cloud platform

// ============================================================================
// NETWORK CONFIGURATION
// ============================================================================
const char* ssid = "";           // WiFi network name
const char* password = "";       // WiFi password

// ============================================================================
// HARDWARE PIN CONFIGURATION
// ============================================================================
#define LCD_ADDRESS 0x27         // I2C address for LCD (usually 0x27 or 0x3F)
#define SOIL_SENSOR_PIN 34       // Analog pin for capacitive soil moisture sensor
#define DHT_PIN 25               // Digital pin for DHT22 sensor
#define DHT_TYPE DHT22           // DHT sensor type (DHT11 or DHT22)
#define BUTTON_UP_PIN 13         // Navigation button - scroll up
#define BUTTON_DOWN_PIN 12       // Navigation button - scroll down
#define BUTTON_SELECT_PIN 14     // Navigation button - confirm selection

// ============================================================================
// HARDWARE OBJECTS
// ============================================================================
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);  // 16x2 LCD display
DHT dht(DHT_PIN, DHT_TYPE);                 // DHT temperature/humidity sensor
WiFiClient client;                          // WiFi client for ThingSpeak

// ============================================================================
// PLANT PROFILE STRUCTURE
// ============================================================================
struct PlantProfile {
  String name;                   // Plant name for identification
  int drySoilMoisture;           // Sensor value when soil needs water (higher value)
  int wetSoilMoisture;           // Sensor value when soil is saturated (lower value)
  unsigned long channelID;       // ThingSpeak channel ID
  String apiKey;                 // ThingSpeak Write API Key
};

// TODO: Fill with your plant data and ThingSpeak credentials
PlantProfile plantProfiles[] = {
  {"",,,,""}, 
};

int numPlants = sizeof(plantProfiles) / sizeof(PlantProfile);

// ============================================================================
// CONTROL VARIABLES
// ============================================================================
int selectedPlantIndex = 0;      // Currently selected plant in menu
bool plantWasChosen = false;     // Flag: has user selected a plant?
bool calibrationMode = false;    // Flag: running in calibration mode?
long lastConnectionTime = 0;     // Timestamp of last ThingSpeak update
const long postingInterval = 30000;  // Update interval (30 seconds)

// ============================================================================
// SETUP - Runs once at startup
// ============================================================================
void setup() {
  // Initialize serial communication for debugging
  Serial.begin(115200);
  
  // Initialize LCD display
  lcd.init();
  lcd.backlight();
  
  // Configure button pins with internal pull-up resistors
  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT_PIN, INPUT_PULLUP);
  
  // Display startup message
  lcd.setCursor(0, 0);
  lcd.print("Starting...");
  delay(1000);

  // Check if SELECT button is held during startup for calibration mode
  if (digitalRead(BUTTON_SELECT_PIN) == LOW) {
    calibrationMode = true;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Calibration Mode");
  } else {
    // Normal operation mode
    calibrationMode = false;
    dht.begin();              // Initialize DHT sensor
    connectWiFi();            // Connect to WiFi network
    ThingSpeak.begin(client); // Initialize ThingSpeak client
    
    // Show plant selection prompt
    lcd.print("Select the");
    lcd.setCursor(0, 1);
    lcd.print("plant:");
    delay(2000);
  }
}

// ============================================================================
// MAIN LOOP - Runs continuously
// ============================================================================
void loop() {
  if (calibrationMode) {
    // Calibration mode: display raw sensor values for calibration
    runCalibrationMode();
  } else {
    if (!plantWasChosen) {
      // Show plant selection menu
      selectionScreen();
    } else {
      // Monitor and send data at regular intervals
      if (millis() - lastConnectionTime > postingInterval) {
        monitoringAndSendingScreen();
      }
      
      // Allow user to return to plant selection
      if (digitalRead(BUTTON_SELECT_PIN) == LOW) {
         plantWasChosen = false;
         lcd.clear();
         lcd.print("Select the");
         lcd.setCursor(0, 1);
         lcd.print("plant:");
         delay(500);
      }
    }
  }
}

// ============================================================================
// CALIBRATION MODE
// Continuously displays raw sensor values for calibration purposes
// Use this to determine drySoilMoisture and wetSoilMoisture values
// ============================================================================
void runCalibrationMode() {
  int sensorValue = analogRead(SOIL_SENSOR_PIN);
  
  // Display on LCD
  lcd.setCursor(0, 1);
  lcd.print("Value: ");
  lcd.print(sensorValue);
  lcd.print("   ");
  
  // Output to Serial Monitor for logging
  Serial.println("Calibration Value: " + String(sensorValue));
  delay(250);
}

// ============================================================================
// PLANT SELECTION SCREEN
// Navigate through available plant profiles using UP/DOWN buttons
// Confirm selection with SELECT button
// ============================================================================
void selectionScreen() {
  // Display current plant name
  lcd.setCursor(0, 0);
  lcd.print("Plant: " + plantProfiles[selectedPlantIndex].name + " ");
  lcd.setCursor(0, 1);
  lcd.print("< UP/DOWN >");

  // Handle UP button - cycle forward through plants
  if (digitalRead(BUTTON_UP_PIN) == LOW) {
    selectedPlantIndex = (selectedPlantIndex + 1) % numPlants;
    delay(200);  // Simple debounce
  }
  
  // Handle DOWN button - cycle backward through plants
  if (digitalRead(BUTTON_DOWN_PIN) == LOW) {
    selectedPlantIndex = (selectedPlantIndex - 1 + numPlants) % numPlants;
    delay(200);  // Simple debounce
  }
  
  // Handle SELECT button - confirm plant choice
  if (digitalRead(BUTTON_SELECT_PIN) == LOW) {
    plantWasChosen = true;
    lcd.clear();
    lcd.print("Monitoring:");
    lcd.setCursor(0, 1);
    lcd.print(plantProfiles[selectedPlantIndex].name);
    delay(2000);
    lastConnectionTime = -postingInterval;  // Trigger immediate first reading
  }
}

// ============================================================================
// MONITORING AND DATA TRANSMISSION
// Reads sensors, analyzes soil conditions, displays on LCD, and sends to ThingSpeak
// ============================================================================
void monitoringAndSendingScreen() {
  lastConnectionTime = millis();
  
  // Ensure WiFi connection is active
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  // Get the selected plant's calibration profile
  PlantProfile currentProfile = plantProfiles[selectedPlantIndex];

  // -------- Read Sensors --------
  int soilMoistureValue = analogRead(SOIL_SENSOR_PIN);
  float temperature = dht.readTemperature();  // Celsius
  float airHumidity = dht.readHumidity();     // Percentage

  // Validate DHT sensor readings
  if (isnan(temperature) || isnan(airHumidity)) {
    Serial.println("Error reading from DHT sensor!");
    return;
  }

  // -------- Analyze Soil Condition --------
  // Note: Higher analog value = drier soil (capacitive sensors work inversely)
  String soilStatus = "";
  if (soilMoistureValue > currentProfile.drySoilMoisture) { 
    soilStatus = "Water the soil"; 
  } 
  else if (soilMoistureValue < currentProfile.wetSoilMoisture) { 
    soilStatus = "Soil too wet"; 
  } 
  else { 
    soilStatus = "Ideal soil"; 
  }

  // -------- Display on LCD --------
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("T:" + String(temperature, 1) + "C H:" + String(airHumidity, 1) + "%");
  lcd.setCursor(0, 1);
  lcd.print(soilStatus);

  // -------- Send Data to ThingSpeak --------
  ThingSpeak.setField(1, temperature);         // Field 1: Temperature (°C)
  ThingSpeak.setField(2, soilMoistureValue);   // Field 2: Raw soil moisture value
  ThingSpeak.setField(3, airHumidity);         // Field 3: Air humidity (%)
  ThingSpeak.setStatus("Plant: " + currentProfile.name + " - " + soilStatus);
  
  // Transmit to plant-specific channel
  int httpCode = ThingSpeak.writeFields(currentProfile.channelID, currentProfile.apiKey.c_str());
  
  // Log transmission result
  if (httpCode == 200) {
    Serial.println("Data sent to channel: " + currentProfile.name);
  } else {
    Serial.println("Error sending data. HTTP Code: " + String(httpCode));
  }
}

// ============================================================================
// WiFi CONNECTION HANDLER
// Attempts to connect to WiFi with visual feedback on LCD
// Maximum 20 attempts (10 seconds) before timeout
// ============================================================================
void connectWiFi() {
  // Skip if already connected
  if (WiFi.status() == WL_CONNECTED) { return; }
  
  // Start connection attempt
  WiFi.begin(ssid, password);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  
  // Wait for connection with timeout
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    lcd.print(".");
    delay(500);
    attempts++;
  }
  
  // Display connection result
  lcd.clear();
  lcd.setCursor(0, 0);
  if(WiFi.status() == WL_CONNECTED) {
    lcd.print("Connected!");
  } else {
    lcd.print("Connection failed");
  }
  
  delay(1000);
  lcd.clear();
}