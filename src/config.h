#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ===== CONFIGURATION PINS =====
#define WIEGAND_D0        32
#define WIEGAND_D1        33
#define RELAY_OPEN        25
#define RELAY_CLOSE       26
#define PHOTO_BARRIER     27
#define STATUS_LED        2

// ===== STRUCTURES =====
struct AccessCode {
  uint32_t code;
  uint8_t type;  // 0=Wiegand/Keypad, 1=RFID, 2=Fingerprint
  char name[32];
  bool active;
};

struct Config {
  unsigned long relayDuration;
  bool photoBarrierEnabled;
  char mqttServer[64];
  int mqttPort;
  char mqttUser[32];
  char mqttPassword[32];
  char mqttTopic[64];
  char adminPassword[32];
  bool initialized;
};

struct AccessLog {
  unsigned long timestamp;
  uint32_t code;
  bool granted;
  uint8_t type;
};

#endif
