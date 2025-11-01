#include <WiFi.h>
#include <EEPROM.h>
#include <ESPAsyncWebServer.h>

#define EEPROM_SIZE 100
#define LED_BUILTIN 2

// Structure to store WiFi credentials
struct WiFiConfig {
char ssid[32];
char password[32];
char ip[16];
char gateway[16];
} config;

AsyncWebServer server(80);

void saveConfig() {
EEPROM.put(0, config);
EEPROM.commit();
}

void loadConfig() {
EEPROM.get(0, config);
if (strlen(config.ssid) == 0) {
  strcpy(config.ssid, "Freebox-2074E2");
  strcpy(config.password, "tv733q46tfq7cz64qdw47z");
  strcpy(config.ip, "192.168.1.200");
  strcpy(config.gateway, "192.168.1.254");
}
}


const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 WiFi Manager</title>
</head>
<body>
  <h1>ESP32 WiFi Manager</h1>
  <h2>Network Configuration</h2>
  <form action='/save' method='GET'>
    <label>SSID:</label><input type='text' name='ssid' value='%SSID%'><br><br>
    <label>Password:</label><input type='password' name='password'><br><br>
    <label>Static IP:</label><input type='text' name='ip' value='%IP%'><br><br>
    <label>Gateway IP:</label><input type='text' name='gateway' value='%GATEWAY%'><br><br>
    
    <input type='submit' value='Save & Restart'>
  </form>
 </body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  loadConfig();
  Serial.println("Config Loaded");
  Serial.print("SSID: ");Serial.println(config.ssid);
  Serial.print("IP: ");Serial.println(config.ip);
  Serial.print("Gateway: ");Serial.println(config.gateway);
  Serial.print("Password: ");Serial.println(config.password);
  Serial.println();

  pinMode(LED_BUILTIN, OUTPUT);

  // Try to connect to WiFi for 20 seconds
  //WiFi.config(config.ip, config.gateway, config.ip); // commente cette ligne
  WiFi.begin(config.ssid, config.password);
  Serial.print("Connecting to WiFi");
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
    delay(500);
    Serial.print(".");
  }

if (WiFi.status() == WL_CONNECTED) {
  // Set static IP
  IPAddress local_IP, gateway;
  local_IP.fromString(config.ip);
  gateway.fromString(config.gateway);
  Serial.println("\nConnected! IP Address: " + WiFi.localIP().toString());
} else {
  // Switch to Access Point mode
  Serial.println("\nFailed to connect. Switching to AP mode...");
  WiFi.softAP("ESP32_Setup");
  Serial.println("AP Mode IP Address: 192.168.4.1");
}

server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
String page = index_html;
page.replace("%SSID%", config.ssid);
page.replace("%IP%", config.ip);
page.replace("%GATEWAY%", config.gateway);
request->send(200, "text/html", page);
});

server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
if (request->hasParam("ssid") && request->hasParam("password") && request->hasParam("ip") && request->hasParam("gateway")) {
strcpy(config.ssid, request->getParam("ssid")->value().c_str());
strcpy(config.password, request->getParam("password")->value().c_str());
strcpy(config.ip, request->getParam("ip")->value().c_str());
strcpy(config.gateway, request->getParam("gateway")->value().c_str());
saveConfig();
request->send(200, "text/html", "Settings Saved! Restarting...");
delay(2000);
ESP.restart();
} else {
request->send(400, "text/html", "Missing Parameters!");
}
});

server.begin();
Serial.println("HTTP Server Started");
}

void loop() {

}

