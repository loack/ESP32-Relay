#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <AsyncElegantOTA.h>

// Set password to "" for open networks.
const char* ssid = "SFR_787F";
const char* pass = "x889xnr8ez5xm2yaz58h";

//Server
AsyncWebServer server(80);

/*define pin for ESP32*/
//Actuators
uint8_t pinHeater1 = 12; // heater 1 - gris 12 
uint8_t pinHeater2 = 14; // heater 2  - gris 14 
uint8_t pinBubbles = 25; // bubbles -orange  25
uint8_t pinTransfo = 26; // trasnformateur - jaune 26
//bool status
bool statusHeater1 = LOW;
bool statusHeater2 = LOW;
bool statusBubbles = LOW;
bool statusTransfo = LOW;

//Sensors
uint8_t pinFlow = 34; //vert / FLOW
uint8_t pinTemp1 = 32; //bleu temperature sensor blanc / p12 valeur autour de 2v
uint8_t pinTemp2 = 35; //marron - temperature sensor noir / p17  valeur autour de 2v
//vals
float valueFlow = 0;
float valueTemp1 = 0;
float valueTemp2 = 0;

//timer
unsigned long startMillis;  //some global variables available anywhere in the program
unsigned long currentMillis;
const unsigned long period = 1000; 

// Set your Static IP address
IPAddress local_IP(192, 168, 1, 200);
// Set your Gateway IP address
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);

//config capteurs
//info base : 2350 - pour temperature normale environ 26°C
//chauffage on T1 - 2288
//t2 - 2353

//1943 - après 3h de chauffe
//Routine d'activation
int minValueTemp = 1850;
int maxTemp = 50;
int minFlow = 500;

void set2status(){
  if(statusHeater1)
    {digitalWrite(pinHeater1, HIGH);}
    else
    {digitalWrite(pinHeater1, LOW);}
  if(statusHeater2)
    {digitalWrite(pinHeater2, HIGH);}
    else
    {digitalWrite(pinHeater2, LOW);}
  

  if(statusBubbles)
    {digitalWrite(pinBubbles, HIGH);}
    else
    {digitalWrite(pinBubbles, LOW);}
  if(statusTransfo)
    {digitalWrite(pinTransfo, HIGH);}
    else
    {digitalWrite(pinTransfo, LOW);}
}

void updateActuators(){
  //cut heat if too low
  if((valueTemp1>maxTemp) || (valueTemp2>maxTemp)){
    digitalWrite(pinHeater1, LOW);
    digitalWrite(pinHeater2, LOW);
    statusHeater1 = LOW;
    statusHeater2 = LOW;
  }

  //cut all if flow too low
  if  (valueFlow>minFlow){
    digitalWrite(pinHeater1, LOW);
    digitalWrite(pinHeater2, LOW);
    digitalWrite(pinTransfo, LOW);
    digitalWrite(pinBubbles, LOW);
    statusHeater1 = LOW;
    statusHeater2 = LOW;
    statusTransfo = LOW;
    statusBubbles = LOW;
  }
}

void activation(String button, String command){
  if(button == "tempButton"){
    if(command=="on"){
      digitalWrite(pinHeater1, HIGH);
      digitalWrite(pinHeater2, HIGH);
      digitalWrite(pinTransfo, HIGH);
      statusHeater1 = HIGH;
      statusHeater2 = HIGH;  
      statusTransfo = HIGH; 
      }
    if(command=="off"){
      digitalWrite(pinHeater1, LOW);
      digitalWrite(pinHeater2, LOW);
      statusHeater1 = LOW;
      statusHeater2 = LOW;  
    }
  }

  if(button == "bubblesButton"){
    if(command=="on"){
      digitalWrite(pinBubbles, HIGH);
      statusBubbles = HIGH;
    }
    if(command=="off"){
      digitalWrite(pinBubbles, LOW);
      statusBubbles = LOW;
    }
  }

  if(button == "flowButton"){
    if(command=="on"){
      digitalWrite(pinTransfo, HIGH);
      statusTransfo = HIGH;
    }
    if(command=="off"){
      digitalWrite(pinTransfo, LOW);
      statusTransfo = LOW;
      if (statusHeater1 || statusHeater2){
        digitalWrite(pinHeater1, LOW);
        digitalWrite(pinHeater2, LOW);
        statusHeater1 = LOW;
        statusHeater2 = LOW;  
      }
    }
  }
}

float getVoltage(uint8_t pin){
  // convert the value you read:
  float value = analogRead(pin)/4095*3.3;
  //float temperature = (700 - value)/7.83;
  Serial.println("pin"+pin);
  Serial.print(" :");
  Serial.print(value);

  return value;
}

float getTemperature(uint8_t pin){
  // convert the analog read to temperature:
  float value = -61.69*std::log(analogRead(pin))+514.19;
  return value;
}
//update and formatData of capteurs
String IOData(){
  String dataJson = "";
  dataJson += "{\"statusHeater1\":"+String(statusHeater1);
  dataJson += ",\"statusHeater2\":"+String(statusHeater2);
  dataJson += ",\"statusBubbles\":"+String(statusBubbles);
  dataJson += ",\"statusTransfo\":"+String(statusTransfo);
  dataJson += ",\"valueFlow\":"+String(valueFlow);
  dataJson += ",\"valueTemp1\":"+String(valueTemp1);
  dataJson += ",\"valueTemp2\":"+String(valueTemp2);
  dataJson += "}";
  return dataJson;
};
void getSensorValues(){
  valueTemp1 = getTemperature(pinTemp1); // Gets the values of the temperature
  valueTemp2 = getTemperature(pinTemp2);
  valueFlow = analogRead(pinFlow);
  Serial.println(IOData());
  //valueTemp2 = getVoltage(pinTemp2); // Gets the values of the temperature
  //valueFlow  = getVoltage(pinFlow); //Get flow value
}





/* Setup*/
void setup(void) {

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
  }
  Serial.println(WiFi.localIP());

// ------ GPIO -----------------------
//Configuration of pins
pinMode(pinHeater1, OUTPUT);
pinMode(pinHeater2, OUTPUT);
pinMode(pinBubbles, OUTPUT);
pinMode(pinTransfo, OUTPUT);
pinMode(pinFlow,INPUT);
pinMode(pinTemp1,INPUT);
pinMode(pinTemp2,INPUT);

//--------SPIFFS launch&list----------
if(!SPIFFS.begin(true)){
  Serial.println("Erreur SPIFFS");
  return;
}
File root = SPIFFS.open("/");
File file =root.openNextFile();
while(file){
  Serial.print("File:");
  Serial.println(file.name());
  file.close();
  file = root.openNextFile();
}

//-----Server config----------------

// Route for root / web page
server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(SPIFFS, "/index.html", String(), false);
});

// Route to load style.css file
server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(SPIFFS, "/style.css", "text/css");
});
// Route to load style.css file
server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(SPIFFS, "/script.js", "text/javascript");
});

//Load data from server
server.on("/all.min.css", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(SPIFFS, "/all.min.css", "text/css");
});
server.on("/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(SPIFFS, "/bootstrap.min.css", "text/css");
});
server.on("/Draggable.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(SPIFFS, "/Draggable.min.js", "text/javascript");
});
server.on("/gsap.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(SPIFFS, "/gsap.min.js", "text/javascript");
});
server.on("/jquery.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(SPIFFS, "/jquery.min.js", "text/javascript");
});

// Lire des données des capteurs et envoyer à l'interface web

server.on("/getData", HTTP_GET, [](AsyncWebServerRequest *request){
  //Serial.println(dataJson);
  request->send(200, "text/plain", IOData());
});



// Route to set GPIO to HIGH
server.on("/activate", HTTP_GET, [](AsyncWebServerRequest *request){

  int paramsNr = request->params();
  AsyncWebParameter* p = request->getParam(0);
  //activate from button
  activation(p->name(), p->value());

  request->send(200, "text/plain", "message received");
});



//--------After configuration Server begin + OTA----------
  AsyncElegantOTA.begin(&server);
  server.begin();
  Serial.println("HTTP server started");

//timer for sensor actualisation
startMillis = millis();  //initial start time
}

void loop(void) {
currentMillis = millis();  //get the current "time" (actually the number of milliseconds since the program started)
if (currentMillis - startMillis >= period)  //test whether the period has elapsed
{
  getSensorValues();
  updateActuators();
  startMillis = currentMillis;  //IMPORTANT to save the start time of the current LED state.
}

}