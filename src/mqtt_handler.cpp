#include <Arduino.h>
#include <PubSubClient.h>
#include "config.h"

extern Config config;
extern PubSubClient mqttClient;
extern void activateRelay(bool open);
extern void deactivateRelay();

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT message received on topic: ");
  Serial.println(topic);
  
  // Conversion du payload en string
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  String topicStr = String(topic);
  String baseTopicCmd = String(config.mqttTopic) + "/cmd";
  
  if (topicStr == baseTopicCmd) {
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
      String cmdTopic = String(config.mqttTopic) + "/cmd";
      mqttClient.subscribe(cmdTopic.c_str());
      
      // Publication du statut de connexion
      String statusTopic = String(config.mqttTopic) + "/status";
      mqttClient.publish(statusTopic.c_str(), "{\"state\":\"online\"}");
      
      Serial.printf("Subscribed to: %s\n", cmdTopic.c_str());
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
