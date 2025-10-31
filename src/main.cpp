#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncDNSServer.h>
#include <ESPAsync_WiFiManager.h>
#include <ElegantOTA.h>
#include <Preferences.h>

// Objets globaux
AsyncWebServer server(80);
AsyncDNSServer dns;
ESPAsync_WiFiManager* wifiManager;
Preferences preferences;

// Configuration réseau
IPAddress local_IP(192, 168, 1, 104);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);

// Pins relais et LED
uint8_t pinK1 = 21;
uint8_t pinK2 = 19;
uint8_t pinK3 = 18;
uint8_t pinK4 = 5;
uint8_t pinredLed = 25;
int relayPins[] = {21, 19, 18, 5};

// Fonctions utilitaires
void blinkRedLed() {
  digitalWrite(pinredLed, LOW);
  delay(100);
  digitalWrite(pinredLed, HIGH);
  delay(100);
}

void startrelais(const int* pins, int pin_nb){
  digitalWrite(pins[pin_nb-1], HIGH);
  Serial.print("Started relais K");
  Serial.println(pin_nb);
}

void stoprelais(const int* pins, int pin_nb){
  digitalWrite(pins[pin_nb-1], LOW);
  Serial.print("Stopped relais K");
  Serial.println(pin_nb);
}

void handleStart(AsyncWebServerRequest *request) {
  if (request->hasParam("relay")) {
    String paramValue = request->getParam("relay")->value();
    if (paramValue=="all"){
      for (int i=1; i<5;i++){
        startrelais(relayPins, i);
        blinkRedLed();
      }
      request->send(200, "text/plain", "All relays started");
    }
    else{
      int relayIndex = paramValue.toInt();
      if (relayIndex >= 1 && relayIndex <= 4) {
        startrelais(relayPins, relayIndex);
        blinkRedLed();
        request->send(200, "text/plain", "Relay started: " + String(relayIndex));
      } else {
        request->send(400, "text/plain", "Invalid relay index");
        Serial.println("Invalid relay index in /start");
      }
    }
  } else {
    request->send(400, "text/plain", "Missing relay parameter");
    Serial.println("Missing relay parameter in /start");
  }
}

void handleStop(AsyncWebServerRequest *request) {
  if (request->hasParam("relay")) {
    String paramValue = request->getParam("relay")->value();
    if (paramValue=="all"){
      for (int i=1; i<5;i++){
        stoprelais(relayPins, i);
        blinkRedLed();
      }
      request->send(200, "text/plain", "All relays stopped");
    }
    else{
      int relayIndex = paramValue.toInt();
      if (relayIndex >= 1 && relayIndex <= 4) {
        stoprelais(relayPins, relayIndex);
        blinkRedLed();
        request->send(200, "text/plain", "Relay stopped: " + String(relayIndex));
      } else {
        request->send(400, "text/plain", "Invalid relay index");
        Serial.println("Invalid relay index in /stop");
      }
    }
  } else {
    request->send(400, "text/plain", "Missing relay parameter");
    Serial.println("Missing relay parameter in /stop");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting ESP32 Relay...");
  pinMode(pinredLed, OUTPUT);
  digitalWrite(pinredLed, HIGH);
  blinkRedLed();

  wifiManager = new ESPAsync_WiFiManager(&server, &dns);

  WiFi.mode(WIFI_STA);
  wifiManager->resetSettings();
  Serial.println("WiFi settings erased.");
  if (!wifiManager->startConfigPortal("ESP32-Relay-Setup")) {
    Serial.println("Failed to connect or setup WiFi. Restarting...");
    delay(3000);
    ESP.restart();
  }
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  blinkRedLed();

  // Initialisation Preferences
  if (!preferences.begin("relay", false)) {
    Serial.println("Failed to open preferences!");
  }

  for (int i = 0; i < 4; i++) {
    int pin = preferences.getInt((String("pin") + String(i+1)).c_str(), relayPins[i]);
    relayPins[i] = pin;
    Serial.printf("Relay %d pin loaded: %d\n", i+1, pin);
  }

  for (int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
    blinkRedLed();
    digitalWrite(relayPins[i], LOW);
  }

  // Routes serveur : à placer APRÈS la connexion WiFi
  server.on("/start", HTTP_GET, handleStart);
  server.on("/stop", HTTP_GET, handleStop);

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<html><head><title>Config Relais</title></head><body>";
    html += "<h2>Configuration des pins relais</h2>";
    html += "<form method='POST' action='/config'>";
    for (int i=0; i<4; i++) {
      html += "Pin Relais " + String(i+1) + ": <input type='number' name='pin" + String(i+1) + "' value='" + String(relayPins[i]) + "'><br>";
    }
    html += "<input type='submit' value='Enregistrer'>";
    html += "</form></body></html>";
    request->send(200, "text/html", html);
    Serial.println("Served /config GET");
  });

  server.on("/config", HTTP_POST, [](AsyncWebServerRequest *request){
    for (int i=0; i<4; i++) {
      if (request->hasParam("pin" + String(i+1), true)) {
        int newPin = request->getParam("pin" + String(i+1), true)->value().toInt();
        relayPins[i] = newPin;
        preferences.putInt((String("pin") + String(i+1)).c_str(), newPin);
        Serial.printf("Relay %d pin updated: %d\n", i+1, newPin);
      }
    }
    request->send(200, "text/html", "<html><body><h2>Configuration enregistrée !</h2><a href='/config'>Retour</a></body></html>");
    Serial.println("Served /config POST");
  });

  ElegantOTA.begin(&server);
  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  // Boucle principale, rien à faire ici pour le serveur asynchrone
}