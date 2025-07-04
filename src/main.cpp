#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// Set password to "" for open networks.
//const char* ssid = "Freebox-2074E2";
//const char* pass = "tv733q46tfq7cz64qdw47z";
const char* ssid = "lolobotlab_xiaomi";
const char* pass = "eristofly01";

//Server
AsyncWebServer server(80);
// Set your Static IP address
IPAddress local_IP(192, 168, 1, 104);
// Set your Gateway IP address
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);

// put function declarations here:
int myFunction(int, int);

//Pin allocation
uint8_t pinK1 = 21;
uint8_t pinK2 = 19;
uint8_t pinK3 = 18;
uint8_t pinK4 = 5;
uint8_t pinredLed = 25;


// Define your pins
int relayPins[] = {21, 19, 18, 5};

// Example for ESP32 or Arduino
void setPinsSequentially(const int* pins, int numPins) {
    for (int i = 0; i < numPins; i++) {
        digitalWrite(pins[i], HIGH);
        delay(1000); // Wait 1 second
    }
}
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
  Serial.print("Stoped relais K");
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
          startrelais(relayPins, relayIndex);
          blinkRedLed();
          request->send(200, "text/plain", "Relay started: " + String(relayIndex));
        }

    } else {
        request->send(400, "text/plain", "Missing relay parameter");
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
          request->send(200, "text/plain", "All relays stoped");
        }
        else{
          int relayIndex = paramValue.toInt();
          stoprelais(relayPins, relayIndex);
          blinkRedLed();
          request->send(200, "text/plain", "Relay stoped: " + String(relayIndex));
        }

    } else {
        request->send(400, "text/plain", "Missing relay parameter");
    }
}



void setup() {

  //Switch red led on
  pinMode(pinredLed, OUTPUT);
  digitalWrite(pinredLed, HIGH);
  blinkRedLed();
  //--------Serial comm begin-----------
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.println("");
  //--------Wifi begin------------------
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  // Configures static IP address
  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
    blinkRedLed();
    blinkRedLed();
  }
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  blinkRedLed();
  //--------Set all to Low-------------
  for (int i = 0; i < 4; i++) {
      pinMode(relayPins[i], OUTPUT);
      blinkRedLed();
      digitalWrite(relayPins[i], LOW); // Start with all OFF
  }


  server.on("/start", HTTP_GET, handleStart);
  server.on("/stop", HTTP_GET,handleStop);

  server.begin();
}

void loop() {
// Main code after setup
}
