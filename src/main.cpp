#include <Arduino.h>

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


void setup() {
    for (int i = 0; i < 4; i++) {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], LOW); // Start with all OFF
    }

    setPinsSequentially(relayPins, 4);
    
    for (int i = 0; i < 4; i++) {
        digitalWrite(relayPins[i], LOW); // Start with all OFF
    }
}

void loop() {
// Main code after setup
}
