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

// Bouton pour reset WiFi (bouton BOOT sur ESP32)
#define RESET_WIFI_BUTTON 0

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

// Variables pour accumulation des codes numériques
String keypadBuffer = "";
unsigned long lastKeypadInput = 0;
const unsigned long KEYPAD_TIMEOUT = 10000;  // 10 secondes

// Variables pour mode apprentissage (learning mode)
bool learningMode = false;
unsigned long learningModeStart = 0;
const unsigned long LEARNING_TIMEOUT = 60000;  // 60 secondes
uint8_t learningType = 0;  // 0=Keypad, 1=RFID, 2=Fingerprint
String learningName = "";

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
bool checkTriplePress();
void blinkReaderLED(bool success);
void processKeypadCode();
bool addNewAccessCode(uint32_t code, uint8_t type, const char* name);
bool removeAccessCode(uint32_t code, uint8_t type);
void startLearningMode(uint8_t type, const char* name);
void stopLearningMode();

// Fonctions externes (définies dans d'autres fichiers)
void setupWebServer();
void setupMQTT();
void reconnectMQTT();
void publishMQTT(const char* topic, const char* payload);

// ===== FONCTION RESET WiFi =====
// Fonction pour détecter 3 appuis sur le bouton BOOT
bool checkTriplePress() {
  int pressCount = 0;
  unsigned long startTime = millis();
  unsigned long lastPressTime = 0;
  bool lastState = HIGH;
  
  Serial.println("\n⏱ WiFi Reset Check (10 seconds window)...");
  Serial.println("Press BOOT button 3 times to reset WiFi credentials");
  
  while (millis() - startTime < 10000) {  // 10 secondes
    bool currentState = digitalRead(RESET_WIFI_BUTTON);
    
    // Détection front descendant (appui)
    if (lastState == HIGH && currentState == LOW) {
      pressCount++;
      lastPressTime = millis();
      Serial.printf("✓ Press %d/3 detected\n", pressCount);
      
      if (pressCount >= 3) {
        Serial.println("\n🔥 Triple press detected!");
        return true;
      }
      
      delay(50);  // Anti-rebond
    }
    
    lastState = currentState;
    delay(10);
  }
  
  if (pressCount > 0) {
    Serial.printf("Only %d press(es) detected. Reset cancelled.\n", pressCount);
  }
  Serial.println("No reset requested. Continuing...\n");
  return false;
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(1000);  // Attendre la stabilisation du port série
  
  Serial.println("\n\n=== ESP32 Roller Shutter Controller ===");
  Serial.println("Version 1.0 - With Wiegand, RFID & Fingerprint");
  Serial.println("Chip ID: " + String((uint32_t)ESP.getEfuseMac(), HEX));
  Serial.println("SDK Version: " + String(ESP.getSdkVersion()));
  
  // Configuration des pins
  pinMode(RELAY_OPEN, OUTPUT);
  pinMode(RELAY_CLOSE, OUTPUT);
  pinMode(PHOTO_BARRIER, INPUT_PULLUP);
  pinMode(STATUS_LED, OUTPUT);
  pinMode(RESET_WIFI_BUTTON, INPUT_PULLUP);
  pinMode(READER_LED_RED, OUTPUT);
  pinMode(READER_LED_GREEN, OUTPUT);
  
  digitalWrite(RELAY_OPEN, LOW);
  digitalWrite(RELAY_CLOSE, LOW);
  digitalWrite(STATUS_LED, LOW);
  digitalWrite(READER_LED_RED, LOW);
  digitalWrite(READER_LED_GREEN, LOW);
  
  // ===== CONFIGURATION WiFi EN PREMIER =====
  // Configuration WiFiManager (AVANT les paramètres WiFi)
  wifiManager.setConfigPortalTimeout(180);  // 3 minutes pour configurer
  wifiManager.setConnectTimeout(30);        // 30 secondes pour se connecter
  wifiManager.setConnectRetries(3);         // 3 tentatives de connexion
  wifiManager.setDebugOutput(true);         // Activer le debug
  
  // Vérifier triple appui pour reset WiFi
  if (checkTriplePress()) {
    Serial.println("\n⚠⚠⚠ RESETTING WiFi credentials ⚠⚠⚠");
    wifiManager.resetSettings();
    delay(1000);
    Serial.println("Credentials erased. Restarting...");
    delay(2000);
    ESP.restart();
  }
  
  // Tentative de connexion WiFi
  Serial.println("\n⏱ Starting WiFi configuration...");
  Serial.println("If no saved credentials, access point will start:");
  Serial.println("SSID: ESP32-Roller-Setup");
  Serial.println("No password required");
  Serial.println("Connect and configure WiFi at: http://192.168.4.1\n");
  
  // Configuration WiFi pour compatibilité Freebox (juste avant autoConnect)
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Réduire la puissance pour éviter les timeouts
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  
  digitalWrite(STATUS_LED, HIGH);
  
  if (!wifiManager.autoConnect("ESP32-Roller-Setup")) {
    Serial.println("\n✗✗✗ WiFiManager failed to connect ✗✗✗");
    Serial.println("Restarting in 5 seconds...");
    digitalWrite(STATUS_LED, LOW);
    delay(5000);
    ESP.restart();
  }
  
  // Connexion réussie
  Serial.println("\n✓✓✓ WiFi CONNECTED ✓✓✓");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  digitalWrite(STATUS_LED, LOW);
  
  // Arrêter le serveur de configuration WiFiManager pour libérer le port 80
  wifiManager.stopConfigPortal();
  delay(500);  // Attendre la libération du port
  
  // ===== INITIALISATION DES AUTRES COMPOSANTS =====
  // Initialisation Wiegand
  wg.begin(WIEGAND_D0, WIEGAND_D1);
  Serial.println("✓ Wiegand initialized on pins 32 & 33");
  
  // Chargement de la configuration
  preferences.begin("roller", false);
  loadConfig();
  loadAccessCodes();
  
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
  // Vérification connexion WiFi
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) {  // Toutes les 30 secondes
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠ WiFi disconnected! Reconnecting...");
      WiFi.reconnect();
    }
  }
  
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
  // Vérifier timeout du mode apprentissage
  if (learningMode && (millis() - learningModeStart > LEARNING_TIMEOUT)) {
    Serial.println("⏱ Learning mode timeout");
    stopLearningMode();
  }
  
  // Vérifier timeout du buffer keypad
  if (keypadBuffer.length() > 0 && (millis() - lastKeypadInput > KEYPAD_TIMEOUT)) {
    Serial.println("⏱ Keypad timeout - buffer cleared");
    keypadBuffer = "";
  }
  
  if (wg.available()) {
    uint8_t bitCount = wg.getWiegandType();
    uint32_t code = wg.getCode();
    
    Serial.printf("\n>>> Wiegand input: %u bits, raw code=%lu (0x%X)\n", bitCount, code, code);
    
    // ===== GESTION SELON LE TYPE =====
    
    // 1. CODES NUMÉRIQUES (4 bits = 1 chiffre)
    if (bitCount == 4) {
      lastKeypadInput = millis();
      
      // Touche # = validation (code 13)
      if (code == 13) {
        Serial.printf("✓ # pressed - Validating code: %s\n", keypadBuffer.c_str());
        processKeypadCode();
        keypadBuffer = "";
      }
      // Touche * = annulation (code 14)
      else if (code == 14) {
        Serial.println("✗ * pressed - Clearing buffer");
        keypadBuffer = "";
        blinkReaderLED(false);
      }
      // Chiffres 0-9
      else if (code <= 9) {
        keypadBuffer += String(code);
        Serial.printf("Keypad buffer: %s\n", keypadBuffer.c_str());
        
        // Limite à 10 chiffres
        if (keypadBuffer.length() > 10) {
          keypadBuffer = keypadBuffer.substring(1);
        }
      }
      else {
        Serial.printf("⚠ Unknown keypad code: %lu\n", code);
      }
    }
    
    // 2. DONNÉES 26 BITS (Empreinte OU RFID selon la valeur)
    else if (bitCount == 26) {
      // Si le code est simple (< 100), c'est une EMPREINTE validée par le lecteur
      if (code < 100) {
        Serial.printf("👆 FINGERPRINT #%lu validated by reader\n", code);
        
        // MODE APPRENTISSAGE pour empreinte
        if (learningMode && learningType == 2) {
          addNewAccessCode(code, 2, learningName.c_str());
          stopLearningMode();
          blinkReaderLED(true);
          return;
        }
        
        // Vérifier si ce numéro d'empreinte est autorisé dans notre système
        bool granted = checkAccessCode(code, 2);  // Type 2 = Fingerprint
        addAccessLog(code, granted, 2);
        
        if (granted) {
          Serial.println("✓✓✓ Fingerprint GRANTED ✓✓✓");
          blinkReaderLED(true);
          activateRelay(true);
          
          char payload[128];
          snprintf(payload, sizeof(payload), 
                   "{\"code\":%lu,\"granted\":true,\"type\":\"fingerprint\",\"bits\":%u}", 
                   code, bitCount);
          publishMQTT("access", payload);
        } else {
          Serial.println("✗✗✗ Fingerprint DENIED (not authorized in system) ✗✗✗");
          blinkReaderLED(false);
          
          char payload[128];
          snprintf(payload, sizeof(payload), 
                   "{\"code\":%lu,\"granted\":false,\"type\":\"fingerprint\",\"reason\":\"not_authorized\",\"bits\":%u}", 
                   code, bitCount);
          publishMQTT("access", payload);
        }
      }
      // Sinon (≥ 100), c'est un BADGE RFID 26 bits
      else {
        Serial.printf("🔖 RFID badge (26-bit) detected: %lu (0x%06X)\n", code, code);
        
        // MODE APPRENTISSAGE pour RFID
        if (learningMode && learningType == 1) {
          addNewAccessCode(code, 1, learningName.c_str());
          stopLearningMode();
          blinkReaderLED(true);
          return;
        }
        
        bool granted = checkAccessCode(code, 1);  // Type 1 = RFID
        addAccessLog(code, granted, 1);
        
        if (granted) {
          Serial.println("✓✓✓ RFID GRANTED ✓✓✓");
          blinkReaderLED(true);
          activateRelay(true);
          
          char payload[128];
          snprintf(payload, sizeof(payload), 
                   "{\"code\":%lu,\"granted\":true,\"type\":\"rfid\",\"bits\":%u}", 
                   code, bitCount);
          publishMQTT("access", payload);
        } else {
          Serial.println("✗✗✗ RFID DENIED ✗✗✗");
          blinkReaderLED(false);
          
          char payload[128];
          snprintf(payload, sizeof(payload), 
                   "{\"code\":%lu,\"granted\":false,\"type\":\"rfid\",\"bits\":%u}", 
                   code, bitCount);
          publishMQTT("access", payload);
        }
      }
    }
    
    // 3. BADGE RFID (généralement 34-35 bits, parfois 32 bits)
    else if (bitCount >= 32) {
      Serial.printf("🔖 RFID badge detected: %lu (0x%X) - %u bits\n", code, code, bitCount);
      
      // MODE APPRENTISSAGE pour RFID
      if (learningMode && learningType == 1) {
        addNewAccessCode(code, 1, learningName.c_str());
        stopLearningMode();
        blinkReaderLED(true);
        return;
      }
      
      bool granted = checkAccessCode(code, 1);  // Type 1 = RFID
      addAccessLog(code, granted, 1);
      
      if (granted) {
        Serial.println("✓✓✓ RFID GRANTED ✓✓✓");
        blinkReaderLED(true);
        activateRelay(true);
        
        char payload[128];
        snprintf(payload, sizeof(payload), 
                 "{\"code\":%lu,\"granted\":true,\"type\":\"rfid\",\"bits\":%u}", 
                 code, bitCount);
        publishMQTT("access", payload);
      } else {
        Serial.println("✗✗✗ RFID DENIED ✗✗✗");
        blinkReaderLED(false);
        
        char payload[128];
        snprintf(payload, sizeof(payload), 
                 "{\"code\":%lu,\"granted\":false,\"type\":\"rfid\",\"bits\":%u}", 
                 code, bitCount);
        publishMQTT("access", payload);
      }
    }
    
    // 4. AUTRE (format inconnu - probablement 8 ou 24 bits)
    else {
      Serial.printf("❓ Unknown Wiegand format: %u bits, code=%lu (0x%X)\n", bitCount, code, code);
    }
    
    Serial.println();
  }
}

// Fonction pour traiter le code du clavier
void processKeypadCode() {
  if (keypadBuffer.length() == 0) {
    Serial.println("⚠ Empty keypad buffer");
    return;
  }
  
  uint32_t code = keypadBuffer.toInt();
  Serial.printf("🔢 Processing keypad code: %lu\n", code);
  
  bool granted = checkAccessCode(code, 0);  // Type 0 = Keypad
  addAccessLog(code, granted, 0);
  
  if (granted) {
    Serial.println("✓✓✓ Keypad code GRANTED ✓✓✓");
    blinkReaderLED(true);
    activateRelay(true);
    
    char payload[128];
    snprintf(payload, sizeof(payload), 
             "{\"code\":%lu,\"granted\":true,\"type\":\"keypad\"}", 
             code);
    publishMQTT("access", payload);
  } else {
    Serial.println("✗✗✗ Keypad code DENIED ✗✗✗");
    blinkReaderLED(false);
    
    char payload[128];
    snprintf(payload, sizeof(payload), 
             "{\"code\":%lu,\"granted\":false,\"type\":\"keypad\"}", 
             code);
    publishMQTT("access", payload);
  }
}

// Fonction pour faire clignoter les LEDs du lecteur
void blinkReaderLED(bool success) {
  if (success) {
    // LED verte - 2 clignotements
    for(int i=0; i<2; i++) {
      digitalWrite(READER_LED_GREEN, HIGH);
      delay(200);
      digitalWrite(READER_LED_GREEN, LOW);
      delay(200);
    }
  } else {
    // LED rouge - 3 clignotements rapides
    for(int i=0; i<3; i++) {
      digitalWrite(READER_LED_RED, HIGH);
      delay(100);
      digitalWrite(READER_LED_RED, LOW);
      delay(100);
    }
  }
}

// ===== FONCTIONS RELAIS =====
void activateRelay(bool open) {
  // SÉCURITÉ 1: Ne jamais activer les 2 relais simultanément !
  digitalWrite(RELAY_OPEN, LOW);
  digitalWrite(RELAY_CLOSE, LOW);
  delay(100);  // Pause de sécurité
  
  // SÉCURITÉ 2: Vérifier que l'autre relais est bien OFF
  if (open) {
    if (digitalRead(RELAY_CLOSE) == HIGH) {
      Serial.println("⚠ ERREUR: RELAY_CLOSE encore actif!");
      return;
    }
  } else {
    if (digitalRead(RELAY_OPEN) == HIGH) {
      Serial.println("⚠ ERREUR: RELAY_OPEN encore actif!");
      return;
    }
  }
  
  if (relayActive) {
    deactivateRelay();
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

// ===== FONCTIONS GESTION CODES D'ACCÈS =====
bool addNewAccessCode(uint32_t code, uint8_t type, const char* name) {
  // Vérifier si le code existe déjà
  for (int i = 0; i < accessCodeCount; i++) {
    if (accessCodes[i].code == code && accessCodes[i].type == type) {
      Serial.printf("⚠ Code already exists: %lu (type %d)\n", code, type);
      return false;
    }
  }
  
  // Vérifier si on a de la place
  if (accessCodeCount >= 50) {
    Serial.println("✗ Access codes list full (max 50)");
    return false;
  }
  
  // Ajouter le nouveau code
  accessCodes[accessCodeCount].code = code;
  accessCodes[accessCodeCount].type = type;
  strncpy(accessCodes[accessCodeCount].name, name, sizeof(accessCodes[accessCodeCount].name) - 1);
  accessCodes[accessCodeCount].name[sizeof(accessCodes[accessCodeCount].name) - 1] = '\0';
  accessCodes[accessCodeCount].active = true;
  
  accessCodeCount++;
  saveAccessCodes();
  
  Serial.printf("✓ New access code added: %s (code=%lu, type=%d)\n", name, code, type);
  
  // Publication MQTT
  char payload[256];
  snprintf(payload, sizeof(payload), 
           "{\"action\":\"added\",\"code\":%lu,\"type\":%d,\"name\":\"%s\",\"total\":%d}", 
           code, type, name, accessCodeCount);
  publishMQTT("codes", payload);
  
  return true;
}

bool removeAccessCode(uint32_t code, uint8_t type) {
  // Chercher le code
  int foundIndex = -1;
  for (int i = 0; i < accessCodeCount; i++) {
    if (accessCodes[i].code == code && accessCodes[i].type == type) {
      foundIndex = i;
      break;
    }
  }
  
  if (foundIndex == -1) {
    Serial.printf("⚠ Code not found: %lu (type %d)\n", code, type);
    return false;
  }
  
  // Sauvegarder le nom pour le log
  char removedName[32];
  strncpy(removedName, accessCodes[foundIndex].name, sizeof(removedName));
  
  // Décaler tous les codes suivants
  for (int i = foundIndex; i < accessCodeCount - 1; i++) {
    accessCodes[i] = accessCodes[i + 1];
  }
  
  accessCodeCount--;
  saveAccessCodes();
  
  Serial.printf("✓ Access code removed: %s (code=%lu, type=%d)\n", removedName, code, type);
  
  // Publication MQTT
  char payload[256];
  snprintf(payload, sizeof(payload), 
           "{\"action\":\"removed\",\"code\":%lu,\"type\":%d,\"name\":\"%s\",\"total\":%d}", 
           code, type, removedName, accessCodeCount);
  publishMQTT("codes", payload);
  
  return true;
}

void startLearningMode(uint8_t type, const char* name) {
  learningMode = true;
  learningModeStart = millis();
  learningType = type;
  learningName = String(name);
  
  const char* typeNames[] = {"Keypad", "RFID", "Fingerprint"};
  Serial.printf("\n🎓 LEARNING MODE activated for %s\n", typeNames[type]);
  Serial.printf("Name: %s\n", name);
  Serial.println("Waiting for input... (60 seconds)");
  
  // Clignoter la LED pour indiquer le mode apprentissage
  for(int i=0; i<5; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(100);
    digitalWrite(STATUS_LED, LOW);
    delay(100);
  }
  
  // Publication MQTT
  char payload[256];
  snprintf(payload, sizeof(payload), 
           "{\"learning\":true,\"type\":%d,\"name\":\"%s\",\"timeout\":60}", 
           type, name);
  publishMQTT("status", payload);
}

void stopLearningMode() {
  if (learningMode) {
    learningMode = false;
    Serial.println("🎓 LEARNING MODE deactivated\n");
    
    // Publication MQTT
    publishMQTT("status", "{\"learning\":false}");
  }
}