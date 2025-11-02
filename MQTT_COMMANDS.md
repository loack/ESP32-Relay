# 📡 Commandes MQTT - ESP32 Roller Shutter Controller

## Configuration MQTT

Topic de base : `roller` (configurable dans l'interface web)

---

## 🎮 Commandes de contrôle du volet

### Ouvrir le volet
```bash
mosquitto_pub -h localhost -t "roller/cmd" -m "open"
```

### Fermer le volet
```bash
mosquitto_pub -h localhost -t "roller/cmd" -m "close"
```

### Arrêter le volet
```bash
mosquitto_pub -h localhost -t "roller/cmd" -m "stop"
```

---

## 🔑 Gestion des codes d'accès

### Types de codes
- **0** = Code clavier (Keypad)
- **1** = Badge RFID
- **2** = Empreinte digitale

### Ajouter un code manuellement
```bash
# Ajouter un code clavier
mosquitto_pub -h localhost -t "roller/codes/add" -m '{"code":1234,"type":0,"name":"Code Admin"}'

# Ajouter un badge RFID
mosquitto_pub -h localhost -t "roller/codes/add" -m '{"code":5096968,"type":1,"name":"Badge Bleu"}'

# Ajouter une empreinte
mosquitto_pub -h localhost -t "roller/codes/add" -m '{"code":1,"type":2,"name":"Pouce Admin"}'
```

### Supprimer un code
```bash
# Supprimer un code clavier
mosquitto_pub -h localhost -t "roller/codes/remove" -m '{"code":1234,"type":0}'

# Supprimer un badge RFID
mosquitto_pub -h localhost -t "roller/codes/remove" -m '{"code":5096968,"type":1}'

# Supprimer une empreinte
mosquitto_pub -h localhost -t "roller/codes/remove" -m '{"code":1,"type":2}'
```

---

## 🎓 Mode apprentissage (Learning Mode)

### Activer le mode apprentissage
Le système attend pendant **60 secondes** qu'un nouveau code/badge/empreinte soit présenté.

```bash
# Apprendre un nouveau badge RFID
mosquitto_pub -h localhost -t "roller/learn" -m '{"type":1,"name":"Badge Rouge"}'

# Apprendre une nouvelle empreinte
mosquitto_pub -h localhost -t "roller/learn" -m '{"type":2,"name":"Index Utilisateur"}'
```

**Important** : Pour les codes clavier, utilisez plutôt l'ajout manuel car l'apprentissage automatique n'est pas pratique.

### Arrêter le mode apprentissage
```bash
mosquitto_pub -h localhost -t "roller/learn/stop" -m ""
```

### Fonctionnement du mode apprentissage
1. Envoyer la commande `roller/learn` avec le type et le nom
2. La LED ESP32 clignote 5 fois pour confirmer l'activation
3. Présenter le badge RFID ou scanner l'empreinte
4. Le code est automatiquement enregistré
5. La LED verte du lecteur clignote pour confirmer
6. Le mode apprentissage se désactive automatiquement

**Timeout** : 60 secondes (le mode se désactive automatiquement)

---

## 📊 Topics de publication (ESP32 → Broker)

### Statut du système
**Topic** : `roller/status`

```json
// Connexion
{"state":"online"}

// Mode apprentissage activé
{"learning":true,"type":1,"name":"Badge Rouge","timeout":60}

// Mode apprentissage désactivé
{"learning":false}

// Barrière photoélectrique déclenchée
{"event":"barrier_triggered"}
```

### État des relais
**Topic** : `roller/relay`

```json
// Relais activé
{"action":"open","duration":5000}
{"action":"close","duration":5000}

// Relais arrêté
{"action":"stopped"}
```

### Événements d'accès
**Topic** : `roller/access`

```json
// Accès accordé
{"code":1234,"granted":true,"type":"keypad"}
{"code":5096968,"granted":true,"type":"rfid","bits":26}
{"code":1,"granted":true,"type":"fingerprint","bits":26}

// Accès refusé
{"code":9999,"granted":false,"type":"keypad"}
{"code":12345678,"granted":false,"type":"rfid","bits":34}
{"code":5,"granted":false,"type":"fingerprint","reason":"not_authorized","bits":26}
```

### Gestion des codes
**Topic** : `roller/codes`

```json
// Code ajouté
{"action":"added","code":1234,"type":0,"name":"Nouveau Code","total":5}

// Code supprimé
{"action":"removed","code":1234,"type":0,"name":"Ancien Code","total":4}
```

---

## 🔧 Exemples avec Node-RED

### Flow pour ajouter un badge RFID en mode apprentissage
```json
[
  {
    "type": "inject",
    "name": "Apprendre badge",
    "topic": "roller/learn",
    "payload": "{\"type\":1,\"name\":\"Badge Visiteur\"}",
    "payloadType": "str"
  },
  {
    "type": "mqtt out",
    "broker": "localhost:1883",
    "topic": "roller/learn"
  }
]
```

### Flow pour supprimer un code
```json
[
  {
    "type": "inject",
    "name": "Supprimer code 1234",
    "topic": "roller/codes/remove",
    "payload": "{\"code\":1234,\"type\":0}",
    "payloadType": "str"
  },
  {
    "type": "mqtt out",
    "broker": "localhost:1883",
    "topic": "roller/codes/remove"
  }
]
```

---

## 🏠 Intégration Home Assistant

### Configuration YAML

```yaml
mqtt:
  # Contrôle du volet
  cover:
    - name: "Volet Roulant Garage"
      command_topic: "roller/cmd"
      state_topic: "roller/relay"
      payload_open: "open"
      payload_close: "close"
      payload_stop: "stop"
      state_open: '{"action":"open"}'
      state_closed: '{"action":"close"}'
      optimistic: false
  
  # Capteur d'accès
  sensor:
    - name: "Dernier Accès Volet"
      state_topic: "roller/access"
      value_template: "{{ value_json.type }}"
      json_attributes_topic: "roller/access"
      json_attributes_template: "{{ value_json | tojson }}"
  
  # Bouton mode apprentissage RFID
  button:
    - name: "Apprendre Badge RFID"
      command_topic: "roller/learn"
      payload_press: '{"type":1,"name":"Nouveau Badge"}'
  
  # Bouton mode apprentissage empreinte
  button:
    - name: "Apprendre Empreinte"
      command_topic: "roller/learn"
      payload_press: '{"type":2,"name":"Nouvelle Empreinte"}'
```

---

## 📝 Notes importantes

1. **Sécurité** : Authentification MQTT recommandée (username/password)
2. **QoS** : Utiliser QoS 1 pour les commandes critiques
3. **Retain** : Ne pas utiliser `retain` pour les commandes
4. **Format JSON** : Toujours valider le JSON avant envoi
5. **Types** : Respecter les types de données (uint32, uint8, string)

---

## 🧪 Tests avec mosquitto_sub

### Écouter tous les événements
```bash
mosquitto_sub -h localhost -t "roller/#" -v
```

### Écouter uniquement les accès
```bash
mosquitto_sub -h localhost -t "roller/access" -v
```

### Écouter le statut
```bash
mosquitto_sub -h localhost -t "roller/status" -v
```

---

## 🆘 Dépannage

### Le code n'est pas enregistré en mode apprentissage
- Vérifier que le mode est actif : `mosquitto_sub -t "roller/status"`
- Le timeout est de 60 secondes
- Vérifier le type (0=Keypad, 1=RFID, 2=Fingerprint)

### Les commandes MQTT ne fonctionnent pas
- Vérifier la connexion MQTT dans les logs série
- Vérifier le topic de base configuré
- Vérifier l'authentification (user/password)

### Le mode apprentissage ne s'arrête pas
- Envoyer : `mosquitto_pub -t "roller/learn/stop" -m ""`
- Ou attendre 60 secondes (timeout automatique)
