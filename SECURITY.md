# 🔐 Guide de sécurité - ESP32 Roller Shutter Controller

## ⚠️ Vulnérabilités actuelles

### Critique
1. **Aucune authentification sur les commandes MQTT sensibles**
2. **Communications non chiffrées (HTTP, MQTT)**
3. **Mots de passe stockés en clair dans la flash**
4. **Pas de limite de tentatives d'accès**

### Moyennes
5. **Potentiel buffer overflow sur les noms**
6. **Injection JSON possible via MQTT**
7. **Pas de protection contre replay attacks**
8. **Pas de logs d'audit détaillés**

---

## 🛡️ Recommandations de sécurisation

### 1. Authentification MQTT renforcée

#### Ajouter un token d'administration
```cpp
// Dans config.h
struct Config {
    // ... existing fields ...
    char mqttAdminToken[64];  // Token secret pour commandes admin
};

// Dans mqtt_handler.cpp
bool verifyAdminToken(JsonDocument& doc) {
    if (!doc["token"].is<const char*>()) {
        Serial.println("⚠ Missing admin token");
        return false;
    }
    
    const char* token = doc["token"];
    if (strcmp(token, config.mqttAdminToken) != 0) {
        Serial.println("✗ Invalid admin token");
        return false;
    }
    
    return true;
}

// Utiliser dans les commandes sensibles
if (topicStr == baseTopic + "/codes/add") {
    JsonDocument doc;
    deserializeJson(doc, message);
    
    if (!verifyAdminToken(doc)) {
        publishMQTT("security", "{\"event\":\"unauthorized_add_attempt\"}");
        return;
    }
    
    // ... rest of code
}
```

#### Format de commande sécurisé
```json
{
    "token": "secret_admin_token_here",
    "code": 1234,
    "type": 0,
    "name": "Admin"
}
```

### 2. Limite de tentatives (Rate Limiting)

```cpp
// Variables globales
struct AccessAttempt {
    uint32_t code;
    unsigned long timestamp;
};

AccessAttempt failedAttempts[20];
int failedAttemptCount = 0;
unsigned long lockoutUntil = 0;

bool isLockedOut() {
    if (millis() < lockoutUntil) {
        Serial.printf("⚠ System locked for %lu more seconds\n", 
                     (lockoutUntil - millis()) / 1000);
        return true;
    }
    return false;
}

void recordFailedAttempt(uint32_t code) {
    if (isLockedOut()) return;
    
    // Ajouter tentative
    failedAttempts[failedAttemptCount % 20].code = code;
    failedAttempts[failedAttemptCount % 20].timestamp = millis();
    failedAttemptCount++;
    
    // Compter tentatives dans les 60 dernières secondes
    int recentFails = 0;
    for (int i = 0; i < min(failedAttemptCount, 20); i++) {
        if (millis() - failedAttempts[i].timestamp < 60000) {
            recentFails++;
        }
    }
    
    // Bloquer après 5 tentatives en 60 secondes
    if (recentFails >= 5) {
        lockoutUntil = millis() + 300000;  // 5 minutes
        Serial.println("🚨 TOO MANY FAILED ATTEMPTS - LOCKED FOR 5 MINUTES");
        publishMQTT("security", "{\"event\":\"lockout\",\"duration\":300}");
        
        // Désactiver les LEDs du lecteur
        digitalWrite(READER_LED_RED, HIGH);
    }
}

// Dans handleWiegandInput()
if (!granted) {
    recordFailedAttempt(code);
    // ... existing code
}
```

### 3. Validation et sanitization des entrées

```cpp
bool isValidName(const char* name) {
    if (name == nullptr || strlen(name) == 0) return false;
    if (strlen(name) > 31) return false;  // Max 31 + null terminator
    
    // Vérifier caractères autorisés (alphanumeric + espaces + - _)
    for (int i = 0; i < strlen(name); i++) {
        char c = name[i];
        if (!isalnum(c) && c != ' ' && c != '-' && c != '_') {
            return false;
        }
    }
    return true;
}

// Utiliser avant d'ajouter un code
if (!isValidName(name)) {
    Serial.println("✗ Invalid name format");
    return false;
}
```

### 4. Chiffrement MQTT (TLS/SSL)

```cpp
#include <WiFiClientSecure.h>

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

void setupMQTT() {
    // Certificat CA pour vérifier le serveur
    const char* ca_cert = \
    "-----BEGIN CERTIFICATE-----\n" \
    "MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQsFADBh\n" \
    // ... rest of certificate
    "-----END CERTIFICATE-----\n";
    
    secureClient.setCACert(ca_cert);
    
    mqttClient.setServer(config.mqttServer, 8883);  // Port TLS
    mqttClient.setCallback(mqttCallback);
}
```

### 5. Authentification Web (Basic Auth minimum)

```cpp
// Dans web_server.cpp
bool checkAuthentication(AsyncWebServerRequest *request) {
    if (!request->authenticate("admin", config.adminPassword)) {
        request->requestAuthentication();
        return false;
    }
    return true;
}

// Protéger les endpoints sensibles
server.on("/api/codes", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!checkAuthentication(request)) return;
    // ... rest of handler
});
```

### 6. Chiffrement des mots de passe (hashing)

```cpp
#include <mbedtls/md.h>

void hashPassword(const char* password, char* output) {
    byte hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char*)password, strlen(password));
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);
    
    // Convertir en hex
    for(int i = 0; i < 32; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[64] = 0;
}

// Stocker uniquement le hash
char hashedPassword[65];
hashPassword(config.adminPassword, hashedPassword);
preferences.putString("adminPwHash", hashedPassword);
```

### 7. Logs d'audit

```cpp
struct SecurityLog {
    unsigned long timestamp;
    char event[64];
    uint32_t relatedCode;
};

SecurityLog securityLogs[50];
int securityLogIndex = 0;

void logSecurityEvent(const char* event, uint32_t code = 0) {
    securityLogs[securityLogIndex].timestamp = millis();
    strncpy(securityLogs[securityLogIndex].event, event, 63);
    securityLogs[securityLogIndex].relatedCode = code;
    securityLogIndex = (securityLogIndex + 1) % 50;
    
    // Publier via MQTT
    char payload[128];
    snprintf(payload, sizeof(payload), 
             "{\"event\":\"%s\",\"code\":%lu,\"timestamp\":%lu}", 
             event, code, millis());
    publishMQTT("security", payload);
}

// Utiliser partout
logSecurityEvent("unauthorized_mqtt_add", code);
logSecurityEvent("failed_web_auth", 0);
logSecurityEvent("lockout_triggered", 0);
```

### 8. Protection physique

```cpp
// Désactiver JTAG pour éviter debug physique
#define DISABLE_JTAG_AT_BOOT

void setup() {
    // Désactiver le debug JTAG
    gpio_config_t io_conf;
    io_conf.pin_bit_mask = (1ULL << GPIO_NUM_12) | (1ULL << GPIO_NUM_13) | 
                           (1ULL << GPIO_NUM_14) | (1ULL << GPIO_NUM_15);
    io_conf.mode = GPIO_MODE_DISABLE;
    gpio_config(&io_conf);
}
```

### 9. Watchdog de sécurité

```cpp
#include <esp_task_wdt.h>

void setup() {
    // Watchdog 30 secondes
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);
}

void loop() {
    // Reset watchdog
    esp_task_wdt_reset();
    
    // Vérifier intégrité
    if (accessCodeCount > 50 || accessCodeCount < 0) {
        Serial.println("🚨 MEMORY CORRUPTION DETECTED");
        ESP.restart();
    }
}
```

---

## 📋 Checklist de sécurisation

### Niveau 1 - Basique (30 min)
- [ ] Activer authentification MQTT (username/password)
- [ ] Changer les mots de passe par défaut
- [ ] Désactiver le portail WiFi après configuration
- [ ] Limiter l'accès réseau (firewall, VLAN)

### Niveau 2 - Intermédiaire (2h)
- [ ] Ajouter token d'administration MQTT
- [ ] Implémenter rate limiting (5 tentatives/minute)
- [ ] Validation des entrées (noms, codes)
- [ ] Logs de sécurité

### Niveau 3 - Avancé (1 jour)
- [ ] MQTT over TLS (port 8883)
- [ ] HTTPS pour interface web
- [ ] Hashing des mots de passe
- [ ] Audit trail complet
- [ ] Désactivation JTAG

### Niveau 4 - Paranoia (3 jours)
- [ ] Chiffrement flash ESP32
- [ ] Secure boot
- [ ] Certificate pinning
- [ ] Intrusion detection
- [ ] Honeypot endpoints

---

## 🌐 Configuration réseau sécurisée

### VLAN isolation
```
Internet ──► Router ──┬──► VLAN 10 (trusted) ──► PC, Phone
                      │
                      ├──► VLAN 20 (IoT) ──► ESP32, autres IoT
                      │                       ↓
                      │                    Firewall rules:
                      │                    - Allow MQTT in
                      │                    - Block Internet out
                      │                    - Block VLAN 10 access
                      │
                      └──► VLAN 30 (DMZ) ──► MQTT Broker
                                             ↓
                                          Allow 20→30
                                          Block 30→10
```

### Règles firewall recommandées
```bash
# Autoriser uniquement MQTT depuis IoT VLAN
iptables -A INPUT -p tcp --dport 1883 -s 192.168.20.0/24 -j ACCEPT
iptables -A INPUT -p tcp --dport 1883 -j DROP

# Bloquer accès Internet sortant
iptables -A OUTPUT -s 192.168.20.0/24 -d 0.0.0.0/0 -j DROP

# Autoriser DNS/NTP uniquement
iptables -A OUTPUT -p udp --dport 53 -s 192.168.20.0/24 -j ACCEPT
iptables -A OUTPUT -p udp --dport 123 -s 192.168.20.0/24 -j ACCEPT
```

---

## 🔑 Gestion des secrets

### Bonnes pratiques
1. **Ne jamais hardcoder les secrets** dans le code
2. **Générer des tokens aléatoires** de 32+ caractères
3. **Rotation régulière** des mots de passe (tous les 90 jours)
4. **Stockage séparé** des credentials (Vault, AWS Secrets Manager)
5. **Logs sans secrets** (ne jamais logger les tokens/passwords)

### Génération de tokens sécurisés
```python
import secrets
import base64

# Token admin MQTT (256 bits)
admin_token = base64.b64encode(secrets.token_bytes(32)).decode()
print(f"MQTT Admin Token: {admin_token}")

# Password aléatoire
password = secrets.token_urlsafe(16)
print(f"Random Password: {password}")
```

---

## 📊 Monitoring de sécurité

### Métriques à surveiller
- Tentatives d'accès échouées (> 5/minute = alerte)
- Ajouts/suppressions de codes
- Changements de configuration
- Connexions MQTT non autorisées
- Redémarrages inattendus
- Changements dans le nombre de codes stockés

### Alertes recommandées
```yaml
# Home Assistant automation
automation:
  - alias: "Alert: Accès refusé multiple"
    trigger:
      - platform: mqtt
        topic: "roller/access"
    condition:
      - condition: template
        value_template: "{{ trigger.payload_json.granted == false }}"
    action:
      - service: notify.mobile_app
        data:
          message: "⚠️ Tentative d'accès refusée au volet"
```

---

## 🆘 Plan de réponse aux incidents

### En cas de compromission suspectée

1. **Isolation immédiate**
   - Débrancher physiquement l'ESP32
   - Bloquer l'IP sur le firewall
   - Désactiver le compte MQTT

2. **Investigation**
   - Examiner les logs d'accès
   - Vérifier les codes d'accès enregistrés
   - Analyser le trafic réseau

3. **Remediation**
   - Changer tous les mots de passe
   - Flasher nouveau firmware
   - Réenregistrer tous les codes d'accès
   - Mettre à jour certificats

4. **Prevention**
   - Implémenter les protections manquantes
   - Durcir la configuration réseau
   - Former les utilisateurs

---

## 📚 Références

- [OWASP IoT Security](https://owasp.org/www-project-internet-of-things/)
- [ESP32 Security Features](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/index.html)
- [MQTT Security Fundamentals](https://www.hivemq.com/mqtt-security-fundamentals/)
- [Arduino Security Best Practices](https://docs.arduino.cc/learn/programming/security)

---

## ⚖️ Disclaimer

Ce système **N'EST PAS** adapté pour :
- Environnements critiques (banques, hôpitaux)
- Conformité PCI-DSS / HIPAA
- Déploiements exposés sur Internet public

Pour ces cas, utilisez des solutions commerciales certifiées.
