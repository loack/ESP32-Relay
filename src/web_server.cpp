#include "web_server.h"
#include "config.h"
#include <ElegantOTA.h>
#include <PubSubClient.h>

extern Config config;
extern AccessCode accessCodes[];
extern AccessLog accessLogs[];
extern int accessCodeCount;
extern int logIndex;
extern PubSubClient mqttClient;

extern void saveConfig();
extern void saveAccessCodes();
extern void activateRelay(bool open);
extern void deactivateRelay();
extern bool deleteAccessCode(int index);

void setupWebServer() {
  // Page principale
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  
  // API - Statut système
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["mqtt"] = mqttClient.connected();
    doc["barrier"] = digitalRead(PHOTO_BARRIER);
    doc["wifi"] = WiFi.status() == WL_CONNECTED;
    doc["ip"] = WiFi.localIP().toString();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API - Contrôle relais
  server.on("/api/relay", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      deserializeJson(doc, (const char*)data);
      
      String action = doc["action"].as<String>();
      
      if (action == "open") {
        activateRelay(true);
        request->send(200, "application/json", "{\"message\":\"Ouverture en cours\"}");
      } else if (action == "close") {
        activateRelay(false);
        request->send(200, "application/json", "{\"message\":\"Fermeture en cours\"}");
      } else if (action == "stop") {
        deactivateRelay();
        request->send(200, "application/json", "{\"message\":\"Arrêt du relais\"}");
      } else {
        request->send(400, "application/json", "{\"error\":\"Action invalide\"}");
      }
    }
  );
  
  // API - Récupérer les codes
  server.on("/api/codes", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    JsonArray codes = doc["codes"].to<JsonArray>();
    
    for (int i = 0; i < accessCodeCount; i++) {
      JsonObject code = codes.add<JsonObject>();
      code["code"] = accessCodes[i].code;
      code["type"] = accessCodes[i].type;
      code["name"] = accessCodes[i].name;
      code["active"] = accessCodes[i].active;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API - Ajouter un code
  server.on("/api/codes", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      if (accessCodeCount >= 50) {
        request->send(400, "application/json", "{\"error\":\"Limite de codes atteinte\"}");
        return;
      }
      
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (const char*)data);
      
      if (error) {
        request->send(400, "application/json", "{\"error\":\"JSON invalide\"}");
        return;
      }
      
      // Validation des champs requis
      if (!doc["code"].is<uint32_t>() || !doc["type"].is<uint8_t>() || !doc["name"].is<const char*>()) {
        request->send(400, "application/json", "{\"error\":\"Champs manquants ou invalides\"}");
        return;
      }
      
      uint32_t code = doc["code"];
      uint8_t type = doc["type"];
      const char* name = doc["name"];
      
      // Validation des valeurs
      if (code == 0) {
        request->send(400, "application/json", "{\"error\":\"Code ne peut pas être 0\"}");
        return;
      }
      
      if (type > 2) {
        request->send(400, "application/json", "{\"error\":\"Type invalide (0-2)\"}");
        return;
      }
      
      if (strlen(name) == 0 || strlen(name) > 31) {
        request->send(400, "application/json", "{\"error\":\"Nom invalide (1-31 caractères)\"}");
        return;
      }
      
      // Vérifier si le code existe déjà
      for (int i = 0; i < accessCodeCount; i++) {
        if (accessCodes[i].code == code && accessCodes[i].type == type) {
          request->send(400, "application/json", "{\"error\":\"Ce code existe déjà\"}");
          return;
        }
      }
      
      // Ajouter le code
      accessCodes[accessCodeCount].code = code;
      accessCodes[accessCodeCount].type = type;
      strlcpy(accessCodes[accessCodeCount].name, name, 32);
      accessCodes[accessCodeCount].active = true;
      
      accessCodeCount++;
      saveAccessCodes();
      
      Serial.printf("✓ Code added via web: %s (code=%lu, type=%d)\n", name, code, type);
      
      request->send(200, "application/json", "{\"message\":\"Code ajouté\"}");
    }
  );
  
  // API - Supprimer un code (simple GET avec paramètre)
  server.on("/api/codes/delete", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("index")) {
      request->send(400, "application/json", "{\"error\":\"Paramètre index manquant\"}");
      return;
    }
    
    int idx = request->getParam("index")->value().toInt();
    
    if (deleteAccessCode(idx)) {
      request->send(200, "application/json", "{\"message\":\"Code supprimé\"}");
    } else {
      request->send(400, "application/json", "{\"error\":\"Index invalide ou erreur lors de la suppression\"}");
    }
  });
  
  // API - Récupérer les logs
  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    JsonArray logs = doc["logs"].to<JsonArray>();
    
    for (int i = 0; i < 100; i++) {
      if (accessLogs[i].timestamp > 0) {
        JsonObject log = logs.add<JsonObject>();
        log["timestamp"] = accessLogs[i].timestamp;
        log["code"] = accessLogs[i].code;
        log["granted"] = accessLogs[i].granted;
        log["type"] = accessLogs[i].type;
      }
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API - Récupérer la configuration
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["relayDuration"] = config.relayDuration;
    doc["photoEnabled"] = config.photoBarrierEnabled;
    doc["mqttServer"] = config.mqttServer;
    doc["mqttPort"] = config.mqttPort;
    doc["mqttUser"] = config.mqttUser;
    doc["mqttTopic"] = config.mqttTopic;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API - Enregistrer la configuration
  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      deserializeJson(doc, (const char*)data);
      
      config.relayDuration = doc["relayDuration"] | 5000;
      config.photoBarrierEnabled = doc["photoEnabled"] | true;
      config.mqttPort = doc["mqttPort"] | 1883;
      
      if (doc.containsKey("mqttServer")) 
        strlcpy(config.mqttServer, doc["mqttServer"], 64);
      if (doc.containsKey("mqttUser")) 
        strlcpy(config.mqttUser, doc["mqttUser"], 32);
      if (doc.containsKey("mqttPassword")) 
        strlcpy(config.mqttPassword, doc["mqttPassword"], 32);
      if (doc.containsKey("mqttTopic")) 
        strlcpy(config.mqttTopic, doc["mqttTopic"], 64);
      if (doc.containsKey("adminPassword")) 
        strlcpy(config.adminPassword, doc["adminPassword"], 32);
      
      saveConfig();
      
      request->send(200, "application/json", "{\"message\":\"Configuration enregistrée\"}");
    }
  );
  
  // ElegantOTA pour les mises à jour
  ElegantOTA.begin(&server);
  
  Serial.println("Web server routes configured");
}
