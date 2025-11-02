#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wiegand.h>
#include "config.h"

// ===== OBJETS GLOBAUX =====
WIEGAND wg;
AsyncWebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;
WiFiManager wifiManager;

Config config;
AccessCode accessCodes[50];  // Max 50 codes
AccessLog accessLogs[100];   // Max 100 logs
int accessCodeCount = 0;
int logIndex = 0;

unsigned long relayStartTime = 0;
bool relayActive = false;
unsigned long lastMqttReconnect = 0;

// ===== PROTOTYPES =====
void loadConfig();
void saveConfig();
void loadAccessCodes();
void saveAccessCodes();
void addAccessLog(uint32_t code, bool granted, uint8_t type);
bool checkAccessCode(uint32_t code, uint8_t type);
void activateRelay(bool open);
void deactivateRelay();
void handleWiegandInput();

// Fonctions externes (définies dans d'autres fichiers)
void setupWebServer();
void setupMQTT();
void reconnectMQTT();
void publishMQTT(const char* topic, const char* payload);

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== ESP32 Roller Shutter Controller ===");
  Serial.println("Version 1.0 - With Wiegand, RFID & Fingerprint");
  
  // Configuration des pins
  pinMode(RELAY_OPEN, OUTPUT);
  pinMode(RELAY_CLOSE, OUTPUT);
  pinMode(PHOTO_BARRIER, INPUT_PULLUP);
  pinMode(STATUS_LED, OUTPUT);
  
  digitalWrite(RELAY_OPEN, LOW);
  digitalWrite(RELAY_CLOSE, LOW);
  digitalWrite(STATUS_LED, LOW);
  
  // Initialisation Wiegand
  wg.begin(WIEGAND_D0, WIEGAND_D1);
  Serial.println("✓ Wiegand initialized on pins 32 & 33");
  
  // Chargement de la configuration
  preferences.begin("roller", false);
  loadConfig();
  loadAccessCodes();
  
  // Configuration WiFi avec WiFiManager
  Serial.println("Starting WiFi configuration...");
  wifiManager.setConfigPortalTimeout(180);
  digitalWrite(STATUS_LED, HIGH);
  
  if (!wifiManager.autoConnect("ESP32-Roller-Setup")) {
    Serial.println("Failed to connect, restarting...");
    delay(3000);
    ESP.restart();
  }
  
  Serial.println("✓ WiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(STATUS_LED, LOW);
  
  // Configuration serveur web
  setupWebServer();
  
  // Configuration MQTT
  setupMQTT();
  
  // Démarrage du serveur
  server.begin();
  Serial.println("✓ Web server started");
  Serial.println("\n========================================");
  Serial.println("Access the web interface at:");
  Serial.print("http://");
  Serial.println(WiFi.localIP());
  Serial.println("========================================\n");
  
  // Clignotement de confirmation
  for(int i=0; i<3; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(200);
    digitalWrite(STATUS_LED, LOW);
    delay(200);
  }
}

// ===== LOOP =====
void loop() {
  // Gestion Wiegand
  handleWiegandInput();
  
  // Gestion relais avec temporisation
  if (relayActive && (millis() - relayStartTime >= config.relayDuration)) {
    deactivateRelay();
  }
  
  // Vérification barrière photoélectrique
  if (config.photoBarrierEnabled && relayActive) {
    if (digitalRead(PHOTO_BARRIER) == LOW) {  // Barrière coupée
      Serial.println("⚠ Photo barrier triggered! Stopping relay.");
      deactivateRelay();
      publishMQTT("status", "{\"event\":\"barrier_triggered\"}");
    }
  }
  
  // Reconnexion MQTT si nécessaire
  if (!mqttClient.connected() && millis() - lastMqttReconnect > 5000) {
    reconnectMQTT();
    lastMqttReconnect = millis();
  }
  
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  
  delay(10);
}

// ===== FONCTIONS CONFIGURATION =====
void loadConfig() {
  config.relayDuration = preferences.getULong("relayDur", 5000);
  config.photoBarrierEnabled = preferences.getBool("photoEn", true);
  config.mqttPort = preferences.getInt("mqttPort", 1883);
  
  preferences.getString("mqttSrv", config.mqttServer, sizeof(config.mqttServer));
  preferences.getString("mqttUser", config.mqttUser, sizeof(config.mqttUser));
  preferences.getString("mqttPass", config.mqttPassword, sizeof(config.mqttPassword));
  preferences.getString("mqttTop", config.mqttTopic, sizeof(config.mqttTopic));
  preferences.getString("adminPw", config.adminPassword, sizeof(config.adminPassword));
  
  if (strlen(config.mqttTopic) == 0) strcpy(config.mqttTopic, "roller");
  if (strlen(config.adminPassword) == 0) strcpy(config.adminPassword, "admin");
  
  config.initialized = preferences.getBool("init", false);
  
  Serial.printf("✓ Config loaded: Relay=%lums, MQTT=%s:%d\n", 
                config.relayDuration, config.mqttServer, config.mqttPort);
}

void saveConfig() {
  preferences.putULong("relayDur", config.relayDuration);
  preferences.putBool("photoEn", config.photoBarrierEnabled);
  preferences.putInt("mqttPort", config.mqttPort);
  preferences.putString("mqttSrv", config.mqttServer);
  preferences.putString("mqttUser", config.mqttUser);
  preferences.putString("mqttPass", config.mqttPassword);
  preferences.putString("mqttTop", config.mqttTopic);
  preferences.putString("adminPw", config.adminPassword);
  preferences.putBool("init", true);
  
  Serial.println("✓ Config saved to flash");
}

void loadAccessCodes() {
  accessCodeCount = preferences.getInt("codeCount", 0);
  if (accessCodeCount > 50) accessCodeCount = 0;
  
  for (int i = 0; i < accessCodeCount; i++) {
    String key = "code" + String(i);
    preferences.getBytes(key.c_str(), &accessCodes[i], sizeof(AccessCode));
  }
  
  Serial.printf("✓ Loaded %d access codes from flash\n", accessCodeCount);
}

void saveAccessCodes() {
  preferences.putInt("codeCount", accessCodeCount);
  
  for (int i = 0; i < accessCodeCount; i++) {
    String key = "code" + String(i);
    preferences.putBytes(key.c_str(), &accessCodes[i], sizeof(AccessCode));
  }
  
  Serial.printf("✓ Saved %d access codes to flash\n", accessCodeCount);
}

// ===== FONCTIONS GESTION ACCÈS =====
bool checkAccessCode(uint32_t code, uint8_t type) {
  for (int i = 0; i < accessCodeCount; i++) {
    if (accessCodes[i].active && 
        accessCodes[i].code == code && 
        accessCodes[i].type == type) {
      Serial.printf("✓ Code match found: %s (index %d)\n", accessCodes[i].name, i);
      return true;
    }
  }
  return false;
}

void addAccessLog(uint32_t code, bool granted, uint8_t type) {
  accessLogs[logIndex].timestamp = millis();
  accessLogs[logIndex].code = code;
  accessLogs[logIndex].granted = granted;
  accessLogs[logIndex].type = type;
  
  logIndex = (logIndex + 1) % 100;
  
  Serial.printf("Access log: code=%lu, granted=%d, type=%d\n", code, granted, type);
}

void handleWiegandInput() {
  if (wg.available()) {
    uint8_t type = wg.getWiegandType();
    uint32_t code = wg.getCode();
    
    Serial.printf("\n>>> Wiegand input detected: %u bits, code=%lu\n", type, code);
    
    bool granted = checkAccessCode(code, 0);  // Type 0 = Wiegand/Keypad
    addAccessLog(code, granted, 0);
    
    if (granted) {
      Serial.println("✓✓✓ Access GRANTED ✓✓✓");
      digitalWrite(STATUS_LED, HIGH);
      activateRelay(true);  // Ouverture
      
      // Publication MQTT
      char payload[128];
      snprintf(payload, sizeof(payload), 
               "{\"code\":%lu,\"granted\":true,\"type\":\"wiegand\",\"bits\":%u}", 
               code, type);
      publishMQTT("access", payload);
      
      delay(500);
      digitalWrite(STATUS_LED, LOW);
    } else {
      Serial.println("✗✗✗ Access DENIED ✗✗✗");
      // Clignotement rapide pour refus
      for(int i=0; i<3; i++) {
        digitalWrite(STATUS_LED, HIGH);
        delay(100);
        digitalWrite(STATUS_LED, LOW);
        delay(100);
      }
      
      char payload[128];
      snprintf(payload, sizeof(payload), 
               "{\"code\":%lu,\"granted\":false,\"type\":\"wiegand\",\"bits\":%u}", 
               code, type);
      publishMQTT("access", payload);
    }
    Serial.println();
  }
}

// ===== FONCTIONS RELAIS =====
void activateRelay(bool open) {
  if (relayActive) {
    deactivateRelay();  // Désactiver d'abord si déjà actif
    delay(100);
  }
  
  digitalWrite(open ? RELAY_OPEN : RELAY_CLOSE, HIGH);
  relayStartTime = millis();
  relayActive = true;
  
  Serial.printf("⚡ Relay activated: %s for %lums\n", 
                open ? "OPEN" : "CLOSE", config.relayDuration);
  
  char payload[128];
  snprintf(payload, sizeof(payload), 
           "{\"action\":\"%s\",\"duration\":%lu}", 
           open ? "open" : "close", config.relayDuration);
  publishMQTT("relay", payload);
}

void deactivateRelay() {
  digitalWrite(RELAY_OPEN, LOW);
  digitalWrite(RELAY_CLOSE, LOW);
  relayActive = false;
  
  Serial.println("⚡ Relay deactivated");
  publishMQTT("relay", "{\"action\":\"stopped\"}");
}