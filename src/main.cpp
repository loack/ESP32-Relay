#include <Arduino.h>
// ESP32 + Lecteur TF886 (RFID/Clavier/Empreinte) - Test Wiegand minimal
// D0 -> GPIO 32, D1 -> GPIO 33
// Affiche : longueur, hex, binaire, tentative d'interprétation (clavier 4/8-bit, 26/34-bit)

#include <Wiegand.h>
WIEGAND wg;

void setup() {
  Serial.begin(115200);
  wg.begin(32, 33); // D0, D1
}

void loop() {
  if (wg.available()) {
    uint8_t type = wg.getWiegandType();
    uint32_t code = wg.getCode();
    Serial.printf("Wiegand %u bits, code=%lu\n", type, (unsigned long)code);
  }
}