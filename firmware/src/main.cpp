#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h> 
#include <PubSubClient.h>
#include <Wire.h>
#include "RTClib.h"
#include "Adafruit_SHTC3.h"
#include <ESP32Servo.h>
#include <Preferences.h> 

// Pins used
#define PIN_I2C_SDA      8
#define PIN_I2C_SCL      9
#define PIN_SERVO        7
#define PIN_WATER_TRIG   5
#define PIN_WATER_ECHO   6
#define PIN_FEED_TRIG    20
#define PIN_FEED_ECHO    21
#define PIN_RELAY        10
#define PIN_LED_WIFI     4
#define PIN_LED_MQTT     3

// Default settings
const int WATER_EMPTY_CM = 20; 
const int WATER_FULL_CM  = 5;  
const int FEED_EMPTY_CM  = 21; 
const int FEED_FULL_CM   = 5;  

const int SERVO_REST_ANGLE = 15;
const int SERVO_OPEN_ANGLE = 95;
const int FEED_DURATION_MS = 2000; 

const int FEED_HOUR_MORNING = 8; // 8AM
const int FEED_HOUR_EVENING = 17; // 5 PM
const int RELAY_ON_HOUR  = 19;    // 7 PM
const int RELAY_OFF_HOUR = 6;     // 6 AM


// Global object and State variables
WiFiClientSecure espClient; 
PubSubClient mqttClient(espClient);
RTC_DS3231 rtc;
Adafruit_SHTC3 shtc3 = Adafruit_SHTC3();
Servo feedServo;
Preferences flashMemory; 

// Timers
unsigned long lastSensorPublish = 0;
unsigned long feedStartTime = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastWiFiReconnectAttempt = 0;

// State Tracking
bool isFeeding = false;
float currentTemp = 0.0, currentHum = 0.0;
int waterLevelPct = 0, feedLevelPct = 0;
bool relayState = false;

// Override Tracking (0 = AUTO, 1 = FORCE ON, 2 = FORCE OFF)
int bulbOverrideState = 0; 

// Persistent Data (Loaded from Flash on Boot)
int lastFedDay = -1;
int lastFedHour = -1;
String lastFedTimeStr = "Not fed yet";


// FUNCTION DECLARATIONS
void setupWiFi();
void reconnectMQTT();
void readUltrasonicLevels();
void handleFeedingLogic(DateTime now);
void handleRelayLogic(DateTime now);
float getDistance(int trigPin, int echoPin);
void mqttCallback(char* topic, byte* payload, unsigned int length);


void setup() {
  Serial.begin(115200);
  delay(2000);

  //Initialize flash memeory
  flashMemory.begin("poultry", false); 
  lastFedDay = flashMemory.getInt("fedDay", -1); 
  lastFedHour = flashMemory.getInt("fedHour", -1); 
  lastFedTimeStr = flashMemory.getString("fedTimeStr", "Not fed yet");

  Serial.println("Booting... Loaded Last Fed Time from Flash: " + lastFedTimeStr);

  // Init Pins
  pinMode(PIN_LED_WIFI, OUTPUT);
  pinMode(PIN_LED_MQTT, OUTPUT);
  
  // High impedance- Force the relay OFF immediately on boot
  pinMode(PIN_RELAY, INPUT); 
  
  pinMode(PIN_WATER_TRIG, OUTPUT);
  pinMode(PIN_WATER_ECHO, INPUT);
  pinMode(PIN_FEED_TRIG, OUTPUT);
  pinMode(PIN_FEED_ECHO, INPUT);
  
  digitalWrite(PIN_LED_WIFI, LOW);
  digitalWrite(PIN_LED_MQTT, LOW);

  // Init Servo
  feedServo.setPeriodHertz(50);
  feedServo.attach(PIN_SERVO, 500, 2400);
  feedServo.write(SERVO_REST_ANGLE);

  // Init I2C
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // Init RTC ---
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
  } 
  
  if (rtc.lostPower()) {
    Serial.println("WARNING: RTC Lost Power Flag Triggered! Check your battery.");
  }

  // Init SHTC3
  if (!shtc3.begin()) {
    Serial.println("Couldn't find SHTC3");
  }

  // Setup Network & Secure MQTT
  setupWiFi();
  espClient.setInsecure(); 
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback); 
}

void loop() {
  unsigned long currentMillis = millis();
  DateTime now = rtc.now();

  // Maintain WiFi and MQTT 
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(PIN_LED_WIFI, HIGH);
    
    // MQTT Reconnection Logic
    if (!mqttClient.connected()) {
      digitalWrite(PIN_LED_MQTT, LOW);
      if (currentMillis - lastMqttReconnectAttempt > 5000) {
        lastMqttReconnectAttempt = currentMillis;
        reconnectMQTT();
      }
    } else {
      digitalWrite(PIN_LED_MQTT, HIGH);
      mqttClient.loop();
    }
    
  } else {
    digitalWrite(PIN_LED_WIFI, LOW);
    digitalWrite(PIN_LED_MQTT, LOW);
    
    // Wi-Fi to Reconnect every 10 seconds
    if (currentMillis - lastWiFiReconnectAttempt > 10000) {
      Serial.println("[WIFI] Disconnected. Attempting to reconnect...");
      WiFi.disconnect(); 
      WiFi.begin(ssid, password);
      lastWiFiReconnectAttempt = currentMillis;
    }
  }

  // Publish Data & Print Debug Info every 3 seconds
  if (currentMillis - lastSensorPublish >= 3000) {
    lastSensorPublish = currentMillis;

    // Read SHTC3
    sensors_event_t hum, temp;
    shtc3.getEvent(&hum, &temp);
    currentTemp = temp.temperature;
    currentHum = hum.relative_humidity;

    // Read Levels
    readUltrasonicLevels();

    // Generate formatted time string
    char timeBuffer[10];
    snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

    // --- SERIAL DEBUG BLOCK ---
    Serial.println("\n--- System Status ---");
    Serial.print("Time       : "); Serial.println(timeBuffer);
    Serial.print("Temperature: "); Serial.print(currentTemp); Serial.println(" °C");
    Serial.print("Humidity   : "); Serial.print(currentHum); Serial.println(" %");
    Serial.print("Water Level: "); Serial.print(waterLevelPct); Serial.println(" %");
    Serial.print("Feed Level : "); Serial.print(feedLevelPct); Serial.println(" %");
    Serial.print("Last Fed   : "); Serial.println(lastFedTimeStr);
    
    Serial.print("Bulb State : "); 
    if(bulbOverrideState == 1) Serial.println("ON (MANUAL OVERRIDE)");
    else if(bulbOverrideState == 2) Serial.println("OFF (MANUAL OVERRIDE)");
    else Serial.println(relayState ? "ON (AUTO)" : "OFF (AUTO)");
    Serial.println("---------------------\n");

    // Publish to MQTT
    if (mqttClient.connected()) {
      mqttClient.publish("poultry/temp", String(currentTemp).c_str());
      mqttClient.publish("poultry/hum", String(currentHum).c_str());
      mqttClient.publish("poultry/waterLevel", String(waterLevelPct).c_str());
      mqttClient.publish("poultry/feedLevel", String(feedLevelPct).c_str());
      mqttClient.publish("poultry/currentTime", timeBuffer); 
    }
  }

  // 3. Handle Local Logic
  handleFeedingLogic(now);
  handleRelayLogic(now);
}

//Helper Functions
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("MQTT Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(" | Payload: ");
  Serial.println(message);

  // Manual feed Logic(For overide)
  if (String(topic) == "poultry/cmd/feed") {
    if (message == "TRIGGER" && !isFeeding) {
      Serial.println("*** MANUAL OVERRIDE: FEEDING TRIGGERED ***");
      isFeeding = true;
      feedStartTime = millis();
      feedServo.write(SERVO_OPEN_ANGLE);
    }
  }

  // Bulb Logic for AUTO,ON,OFF
  if (String(topic) == "poultry/cmd/bulb") {
    if (message == "ON") {
      bulbOverrideState = 1;
      Serial.println("*** MANUAL OVERRIDE: BULB FORCED ON ***");
    } else if (message == "OFF") {
      bulbOverrideState = 2;
      Serial.println("*** MANUAL OVERRIDE: BULB FORCED OFF ***");
    } else if (message == "AUTO") {
      bulbOverrideState = 0;
      Serial.println("*** MANUAL OVERRIDE: BULB RETURNED TO AUTO ***");
    }
  }
}

//Wifi connection
void setupWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[SUCCESS] WiFi Connected!");
  } else {
    Serial.println("\n[ERROR] Failed to connect to WiFi. Check SSID/Password.");
  }
}

// MQTT connection
void reconnectMQTT() {
  Serial.print("Attempting MQTT connection...");
  String clientId = "ESP32C3-Poultry-" + String(random(0xffff), HEX);
  
  if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass, "poultry/status", 0, true, "Offline")) {
    Serial.println("connected");
    mqttClient.publish("poultry/status", "Online", true);
    
    //Overide Topics
    mqttClient.subscribe("poultry/cmd/feed");
    mqttClient.subscribe("poultry/cmd/bulb");
  } else {
    Serial.print("failed, rc=");
    Serial.println(mqttClient.state());
  }
}

float getDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000); 
  if (duration == 0) return -1;
  return (duration * 0.0343) / 2.0;
}

void readUltrasonicLevels() {
  // Read Water
  float waterDist = getDistance(PIN_WATER_TRIG, PIN_WATER_ECHO);
  if (waterDist != -1) {
    waterDist = constrain(waterDist, WATER_FULL_CM, WATER_EMPTY_CM);
    waterLevelPct = map(waterDist, WATER_EMPTY_CM, WATER_FULL_CM, 0, 100);
  }

  delay(50); 

  // Read Feed
  float feedDist = getDistance(PIN_FEED_TRIG, PIN_FEED_ECHO);
  if (feedDist != -1) {
    feedDist = constrain(feedDist, FEED_FULL_CM, FEED_EMPTY_CM);
    feedLevelPct = map(feedDist, FEED_EMPTY_CM, FEED_FULL_CM, 0, 100);
  }
}

void handleFeedingLogic(DateTime now) {
  bool isFeedingHour = (now.hour() == FEED_HOUR_MORNING || now.hour() == FEED_HOUR_EVENING);
  bool alreadyFedThisSlot = (lastFedDay == now.day() && lastFedHour == now.hour());

  // Normal Auto-Trigger
  if (isFeedingHour && !alreadyFedThisSlot && !isFeeding) {
    isFeeding = true;
    feedStartTime = millis();
    feedServo.write(SERVO_OPEN_ANGLE);
    Serial.println("\n*** Servo OPEN: Auto Feeding started ***\n");
  }

  // Close the servo after the duration passes - Auto and Manual override triggers
  if (isFeeding && (millis() - feedStartTime >= FEED_DURATION_MS)) {
    feedServo.write(SERVO_REST_ANGLE);
    isFeeding = false;
    
    char timeBuffer[10];
    snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    lastFedTimeStr = String(timeBuffer);
    
    lastFedDay = now.day();
    lastFedHour = now.hour();

    flashMemory.putInt("fedDay", lastFedDay);
    flashMemory.putInt("fedHour", lastFedHour);
    flashMemory.putString("fedTimeStr", lastFedTimeStr);
    
    Serial.println("\n*** Servo CLOSED: Feeding ended. Saved to Flash: " + lastFedTimeStr + " ***\n");
    
    if (mqttClient.connected()) {
      mqttClient.publish("poultry/lastFed", lastFedTimeStr.c_str());
    }
  }
}

//Handle light bulb logic (AUTO,OFF,ON)
void handleRelayLogic(DateTime now) {
  // 1. Manual force On
  if (bulbOverrideState == 1) { 
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);
    relayState = true;
  } 
  // 2. Manual force off
  else if (bulbOverrideState == 2) { 
    pinMode(PIN_RELAY, INPUT);
    relayState = false;
  } 
  // 3. Auto- uses time schedule
  else { 
    if (now.hour() >= RELAY_ON_HOUR || now.hour() < RELAY_OFF_HOUR) {
      pinMode(PIN_RELAY, OUTPUT);
      digitalWrite(PIN_RELAY, LOW);
      relayState = true;
    } else {
      pinMode(PIN_RELAY, INPUT);
      relayState = false;
    }
  }
}