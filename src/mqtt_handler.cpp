#include <Arduino.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

extern Config config;
extern PubSubClient mqttClient;
extern void activateRelay(bool open);
extern void deactivateRelay();
extern bool addNewAccessCode(uint32_t code, uint8_t type, const char* name);
extern bool removeAccessCode(uint32_t code, uint8_t type);
extern void startLearningMode(uint8_t type, const char* name);
extern void stopLearningMode();

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT message received on topic: ");
  Serial.println(topic);
  
  // Conversion du payload en string
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  String topicStr = String(topic);
  String baseTopic = String(config.mqttTopic);
  
  // Topic: roller/cmd - Commandes relais
  if (topicStr == baseTopic + "/cmd") {
    String cmd = String(message);
    
    if (cmd == "open") {
      Serial.println("MQTT command: OPEN");
      activateRelay(true);
    } else if (cmd == "close") {
      Serial.println("MQTT command: CLOSE");
      activateRelay(false);
    } else if (cmd == "stop") {
      Serial.println("MQTT command: STOP");
      deactivateRelay();
    } else {
      Serial.print("Unknown MQTT command: ");
      Serial.println(cmd);
    }
  }
  
  // Topic: roller/codes/add - Ajouter un code
  else if (topicStr == baseTopic + "/codes/add") {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      return;
    }
    
    if (doc["code"].is<uint32_t>() && doc["type"].is<uint8_t>() && doc["name"].is<const char*>()) {
      uint32_t code = doc["code"];
      uint8_t type = doc["type"];
      const char* name = doc["name"];
      
      Serial.printf("MQTT: Add code %lu, type %d, name %s\n", code, type, name);
      addNewAccessCode(code, type, name);
    } else {
      Serial.println("MQTT: Invalid add code format. Expected: {\"code\":123,\"type\":0,\"name\":\"Name\"}");
    }
  }
  
  // Topic: roller/codes/remove - Supprimer un code
  else if (topicStr == baseTopic + "/codes/remove") {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      return;
    }
    
    if (doc["code"].is<uint32_t>() && doc["type"].is<uint8_t>()) {
      uint32_t code = doc["code"];
      uint8_t type = doc["type"];
      
      Serial.printf("MQTT: Remove code %lu, type %d\n", code, type);
      removeAccessCode(code, type);
    } else {
      Serial.println("MQTT: Invalid remove code format. Expected: {\"code\":123,\"type\":0}");
    }
  }
  
  // Topic: roller/learn - Activer mode apprentissage
  else if (topicStr == baseTopic + "/learn") {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      return;
    }
    
    if (doc["type"].is<uint8_t>() && doc["name"].is<const char*>()) {
      uint8_t type = doc["type"];
      const char* name = doc["name"];
      
      Serial.printf("MQTT: Start learning mode - type %d, name %s\n", type, name);
      startLearningMode(type, name);
    } else {
      Serial.println("MQTT: Invalid learn format. Expected: {\"type\":1,\"name\":\"BadgeName\"}");
      Serial.println("Types: 0=Keypad, 1=RFID, 2=Fingerprint");
    }
  }
  
  // Topic: roller/learn/stop - Arrêter mode apprentissage
  else if (topicStr == baseTopic + "/learn/stop") {
    Serial.println("MQTT: Stop learning mode");
    stopLearningMode();
  }
}

void setupMQTT() {
  if (strlen(config.mqttServer) > 0) {
    mqttClient.setServer(config.mqttServer, config.mqttPort);
    mqttClient.setCallback(mqttCallback);
    Serial.printf("MQTT configured: %s:%d\n", config.mqttServer, config.mqttPort);
  } else {
    Serial.println("MQTT not configured");
  }
}

void reconnectMQTT() {
  if (strlen(config.mqttServer) == 0) return;
  
  if (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    String clientId = "ESP32-Roller-" + String(random(0xffff), HEX);
    
    bool connected = false;
    if (strlen(config.mqttUser) > 0) {
      connected = mqttClient.connect(clientId.c_str(), 
                                     config.mqttUser, 
                                     config.mqttPassword);
    } else {
      connected = mqttClient.connect(clientId.c_str());
    }
    
    if (connected) {
      Serial.println(" connected!");
      
      // Souscription aux topics de commande
      String baseTopic = String(config.mqttTopic);
      
      mqttClient.subscribe((baseTopic + "/cmd").c_str());
      mqttClient.subscribe((baseTopic + "/codes/add").c_str());
      mqttClient.subscribe((baseTopic + "/codes/remove").c_str());
      mqttClient.subscribe((baseTopic + "/learn").c_str());
      mqttClient.subscribe((baseTopic + "/learn/stop").c_str());
      
      // Publication du statut de connexion
      mqttClient.publish((baseTopic + "/status").c_str(), "{\"state\":\"online\"}");
      
      Serial.println("Subscribed to MQTT topics:");
      Serial.printf("  - %s/cmd\n", baseTopic.c_str());
      Serial.printf("  - %s/codes/add\n", baseTopic.c_str());
      Serial.printf("  - %s/codes/remove\n", baseTopic.c_str());
      Serial.printf("  - %s/learn\n", baseTopic.c_str());
      Serial.printf("  - %s/learn/stop\n", baseTopic.c_str());
    } else {
      Serial.print(" failed, rc=");
      Serial.println(mqttClient.state());
    }
  }
}

void publishMQTT(const char* subtopic, const char* payload) {
  if (!mqttClient.connected()) return;
  
  String fullTopic = String(config.mqttTopic) + "/" + String(subtopic);
  
  if (mqttClient.publish(fullTopic.c_str(), payload)) {
    Serial.printf("MQTT published to %s: %s\n", fullTopic.c_str(), payload);
  } else {
    Serial.println("MQTT publish failed");
  }
}
