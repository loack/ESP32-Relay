#include <Arduino.h>
// ESP32 + Lecteur TF886 (RFID/Clavier/Empreinte) - Test Wiegand minimal
// D0 -> GPIO 32, D1 -> GPIO 33
// Affiche : longueur, hex, binaire, tentative d'interprétation (clavier 4/8-bit, 26/34-bit)

// --- Réglages ---
#define PIN_D0 32
#define PIN_D1 33
// Timeout pour considérer qu'une trame est terminée (en ms)
#define WIEGAND_GAP_MS 25
// Taille max bits par trame (suffisant pour 26/34/37 + marge)
#define MAX_BITS 128
#include <Arduino.h>
#include <atomic>

volatile uint8_t bitBuffer[MAX_BITS / 8] = {0};
volatile uint16_t bitCount = 0;
volatile uint32_t lastBitTimeMicros = 0;

// Pour éviter les conflits entre ISR/loop
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR handleD0() {
  // D0 = '0' -> on décale et ajoute un 0
  portENTER_CRITICAL_ISR(&mux);
  if (bitCount < MAX_BITS) {
    // Décalage : on insère un bit '0' (donc juste avancer l'index)
    uint16_t byteIndex = bitCount >> 3;
    uint8_t bitIndex = 7 - (bitCount & 0x7);
    // mettre 0 n'est pas nécessaire (buffer init), mais on efface par sécurité
    bitBuffer[byteIndex] &= ~(1 << bitIndex);
    bitCount++;
  }
  lastBitTimeMicros = micros();
  portEXIT_CRITICAL_ISR(&mux);
}

void IRAM_ATTR handleD1() {
  // D1 = '1' -> on décale et ajoute un 1
  portENTER_CRITICAL_ISR(&mux);
  if (bitCount < MAX_BITS) {
    uint16_t byteIndex = bitCount >> 3;
    uint8_t bitIndex = 7 - (bitCount & 0x7);
    bitBuffer[byteIndex] |= (1 << bitIndex);
    bitCount++;
  }
  lastBitTimeMicros = micros();
  portEXIT_CRITICAL_ISR(&mux);
}

String bitsToBinaryString(const uint8_t *buf, uint16_t nbits) {
  String s; s.reserve(nbits + (nbits / 4));
  for (uint16_t i = 0; i < nbits; i++) {
    uint16_t byteIndex = i >> 3;
    uint8_t bitIndex = 7 - (i & 0x7);
    bool b = (buf[byteIndex] >> bitIndex) & 1;
    s += b ? '1' : '0';
    if ((i % 4) == 3 && i != nbits - 1) s += ' ';
  }
  return s;
}

String bitsToHexString(const uint8_t *buf, uint16_t nbits) {
  uint16_t nbytes = (nbits + 7) / 8;
  String s; s.reserve(nbytes * 3);
  for (uint16_t i = 0; i < nbytes; i++) {
    if (i) s += ' ';
    char tmp[4];
    sprintf(tmp, "%02X", buf[i]);
    s += tmp;
  }
  return s;
}

// Extrait un entier 64-bit (max) des bits [start .. start+len-1]
uint64_t extractBits(const uint8_t *buf, uint16_t start, uint16_t len) {
  uint64_t v = 0;
  for (uint16_t i = 0; i < len; i++) {
    uint16_t pos = start + i;
    uint16_t byteIndex = pos >> 3;
    uint8_t bitIndex = 7 - (pos & 0x7);
    v <<= 1;
    v |= (buf[byteIndex] >> bitIndex) & 1;
  }
  return v;
}

// Tentative d'interprétation : clavier 4/8 bits, cartes 26/34 bits
void interpretAndPrint(const uint8_t *buf, uint16_t nbits) {
  Serial.printf("Bits: %u\n", nbits);
  Serial.printf("Hex : %s\n", bitsToHexString(buf, nbits).c_str());
  Serial.printf("Bin : %s\n", bitsToBinaryString(buf, nbits).c_str());

  // Clavier 4-bit (burst) très courant
  if (nbits == 4) {
    uint8_t code = (uint8_t)extractBits(buf, 0, 4);
    char key = '?';
    // Mapping courant (peut varier selon lecteurs)
    if (code <= 9) key = (code == 0) ? '0' : ('0' + code);
    else if (code == 0x0A) key = '*';
    else if (code == 0x0B) key = '#';
    // 0x0C, 0x0D parfois utilisés pour F1/F2 ou autres, on laisse brut
    Serial.printf("Type: CLAVIER 4-bit — code=0x%X, touche='%c'\n", code, key);
    return;
  }

  // Clavier 8-bit : souvent haut nibble = 0x0, bas nibble = touche 0..B
  if (nbits == 8) {
    uint8_t b = (uint8_t)extractBits(buf, 0, 8);
    uint8_t hi = (b >> 4) & 0x0F;
    uint8_t lo = b & 0x0F;
    char key = '?';
    if (lo <= 9) key = (lo == 0) ? '0' : ('0' + lo);
    else if (lo == 0x0A) key = '*';
    else if (lo == 0x0B) key = '#';
    Serial.printf("Type: CLAVIER 8-bit — byte=0x%02X (hi=0x%X lo=0x%X), touche='%c'\n", b, hi, lo, key);
    return;
  }

  // Wiegand-26 : P | FC(8) | ID(16) | P
  if (nbits == 26) {
    uint8_t fc = (uint8_t)extractBits(buf, 1, 8);
    uint16_t id = (uint16_t)extractBits(buf, 9, 16);
    Serial.printf("Type: RFID/EMP W26 — FC=%u, ID=%u (brut=%s)\n",
                  fc, id, bitsToHexString(buf, nbits).c_str());
    return;
  }

  // Wiegand-34 : P | FC(16) | ID(16) | P (varie selon modèles)
  if (nbits == 34) {
    uint16_t fc = (uint16_t)extractBits(buf, 1, 16);
    uint16_t id = (uint16_t)extractBits(buf, 17, 16);
    Serial.printf("Type: RFID/EMP W34 — FC=%u, ID=%u (brut=%s)\n",
                  fc, id, bitsToHexString(buf, nbits).c_str());
    return;
  }

  // Autres longueurs possibles (W32, W37, etc.)
  Serial.println("Type: Inconnu/Personnalisé — vérifie la doc du lecteur pour ce format.");
}

void resetBuffer() {
  memset((void*)bitBuffer, 0, sizeof(bitBuffer));
  bitCount = 0;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== Test Wiegand ESP32 — TF886 — D0=GPIO32, D1=GPIO33 ===");

  pinMode(PIN_D0, INPUT_PULLUP); // souvent open-collector -> pull-up utile
  pinMode(PIN_D1, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_D0), handleD0, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_D1), handleD1, FALLING);

  resetBuffer();
}

void loop() {
  static uint32_t lastCheck = 0;
  uint32_t now = millis();

  // Vérifie s'il y a une trame terminée
  bool ready = false;
  uint16_t nbits = 0;
  uint8_t localBuf[MAX_BITS / 8];

  portENTER_CRITICAL(&mux);
  if (bitCount > 0) {
    uint32_t gapMs = (micros() - lastBitTimeMicros) / 1000;
    if (gapMs > WIEGAND_GAP_MS) {
      nbits = bitCount;
      memcpy(localBuf, (const void*)bitBuffer, (nbits + 7) / 8);
      resetBuffer();
      ready = true;
    }
  }
  portEXIT_CRITICAL(&mux);

  if (ready) {
    Serial.println("------------------------------");
    interpretAndPrint(localBuf, nbits);
    Serial.println();
  }

  // Petit heartbeat
  if (now - lastCheck > 1000) {
    lastCheck = now;
    // Serial.println("..."); // décommenter pour voir que ça tourne
  }
}