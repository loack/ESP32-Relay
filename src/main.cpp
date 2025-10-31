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
// Déclaration globale
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
int relayPins[] = {pinK1, pinK2, pinK3, pinK4};

void setup() {
  Serial.begin(115200);
  Serial.println("Test minimal");
  pinMode(16, OUTPUT);
  pinMode(17, OUTPUT);

  // Initialisation après les objets server et dns
  wifiManager = new ESPAsync_WiFiManager(&server, &dns);
}


//pin 16 et 17
void loop() {
  Serial.println("Looping...");
  digitalWrite(16, HIGH);
  delay(200);
  digitalWrite(17, LOW);
  delay(200);
  digitalWrite(16, LOW);
  delay(200);
  digitalWrite(17, HIGH);
  delay(200);
}