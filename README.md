# 🌤️ ESP32 Meteo Garten SD - RRD Datenlogger

ESP32-basierter Wetterdaten-Logger mit Round-Robin-Database (RRD) auf SD-Karte und webbasierter Visualisierung.

[![Platform](https://img.shields.io/badge/Platform-ESP32-blue)](https://www.espressif.com/en/products/socs/esp32)
[![Framework](https://img.shields.io/badge/Framework-Arduino-cyan)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

## 📋 Übersicht

Dieses Projekt empfängt Wetterdaten über **ESP-NOW** vom ESP32_Meteo_aussen Sender und speichert 30-Minuten-Mittelwerte in einer **Round-Robin-Database (RRD)** auf SD-Karte. Die Daten werden über einen integrierten **Webserver** mit interaktiven Charts visualisiert.

### ✨ Highlights
- 📶 **ESP-NOW** Kommunikation (keine WiFi-Router-Verbindung zum Sender nötig)
- 💾 **SD-Karte Datenspeicherung** (1 Woche Historie, 336 Datenpunkte)
- 📊 **Interaktive Web-Charts** (Chart.js mit 6 Sensorwerten)
- ⚡ **Echtzeit-Updates** via WebSocket
- 🔄 **Ring-Buffer** (automatisches Überschreiben alter Daten)
- 🌐 **Statische IP** (192.168.100.181)

## 🔌 Hardware

### ESP32 DevKit v1
- Microcontroller mit WiFi & ESP-NOW

### SD-Karte Modul
**Pinbelegung:**
- **GPIO23**: DI (MOSI) - Data In
- **GPIO05**: CS - Chip Select
- **GPIO18**: SCLK - Serial Clock
- **GPIO19**: DO (MISO) - Data Out

### Anforderungen
- SD-Karte (FAT32 formatiert)
- ESP32_Meteo_aussen Sender (auf ESP-NOW Kanal 1)

## 📊 Funktionen

### ESP-NOW Datenempfang
- Empfängt Sensordaten alle 2 Sekunden
- Kompatibel mit `struct_message` Format
- Automatische Kanal-Synchronisation mit WiFi-Router

### Datensammlung & Mittelwerte
- Sammelt 900 Samples (30 Minuten bei 2 Sek. Takt)
- Berechnet alle 30 Minuten Mittelwerte für:
  - Temperatur (°C)
  - Luftfeuchtigkeit (%)
  - Luftdruck (hPa)
  - Windgeschwindigkeit (km/h)
  - Niederschlag (mm)

### RRD-Datenbank
- **Speicherort**: SD-Karte (`/rrd_data.bin`)
- **Kapazität**: 336 Datenpunkte (1 Woche)
- **Ring-Buffer**: Älteste Daten werden automatisch überschrieben
- **Format**: Binärdatei für schnellen Zugriff

### Webserver & Visualisierung
- **IP-Adresse**: 192.168.100.181
- **Port**: 80
- Interaktive Charts (Chart.js) für alle Sensoren
- Echtzeit-Updates via WebSocket
- 7-Tage Verlaufsansicht

## 🚀 Installation

### 1. PlatformIO Projekt einrichten
```bash
cd f:\ESP32_Projekte\Meteo_Garten_SD
pio run
```

### 2. WiFi Konfiguration anpassen
Editiere in `src/main.cpp`:
```cpp
const char* ssid = "DeinWiFiName";
const char* password = "DeinWiFiPasswort";
```

### 3. SD-Karte vorbereiten
- SD-Karte mit FAT32 formatieren
- In das SD-Karten Modul einlegen
- Verkabelung prüfen

### 4. Hochladen
```bash
pio run --target upload
```

## 📡 Nutzung

### Serieller Monitor
```bash
pio device monitor
```

**Erwartete Ausgabe:**
```
========================================
ESP32 Meteo Garten SD - RRD Logger
========================================

Verbinde mit WiFi: lenovo
WiFi verbunden!
IP-Adresse: 192.168.100.181
WiFi-Kanal: 1

Initialisiere SD-Karte...
SD-Karte Typ: SDHC
SD-Karte Größe: 16384MB

ESP-NOW erfolgreich initialisiert
ESP-NOW Kanal: 1

System bereit!
Warte auf ESP-NOW Daten...

Paket #1 | Samples: 1/900 | Temp: 23.5°C
```

### Webinterface
Öffne im Browser:
```
http://192.168.100.181
```

**Features:**
- Echtzeit-Anzeige aktueller Werte
- 5 interaktive Charts (Temperatur, Luftfeuchtigkeit, Luftdruck, Wind, Regen)
- 7-Tage Verlaufsansicht
- Automatische Aktualisierung
- Sample-Counter (0-900)

### API Endpunkte

#### Aktuelle Daten
```
GET http://192.168.100.181/api/current
```
**Response:**
```json
{
  "temperature": "23.5",
  "humidity": "55",
  "pressure": "1013",
  "windSpeed": 12,
  "windDir": 180,
  "rainfall": "0.0",
  "sampleCount": 450,
  "totalPackets": 12500,
  "senderMAC": "AA:BB:CC:DD:EE:FF",
  "sdCard": "OK"
}
```

#### RRD Verlaufsdaten
```
GET http://192.168.100.181/api/rrd
```
**Response:**
```json
{
  "totalEntries": 336,
  "lastUpdate": 1717340400,
  "timestamps": [1717254000, 1717255800, ...],
  "temperatures": ["22.3", "23.1", ...],
  "humidities": ["58", "56", ...],
  "pressures": ["1015", "1014", ...],
  "windSpeeds": ["8.5", "12.3", ...],
  "rainfalls": ["0.0", "1.2", ...]
}
```

## 🔧 Konfiguration

### Zeitintervalle
In `src/main.cpp`:
```cpp
const unsigned long AVERAGE_INTERVAL = 1800000; // 30 Minuten
```

### RRD-Größe
```cpp
const int RRD_SIZE = 336;  // 7 Tage * 48 Werte (alle 30 Min)
```

### IP-Adresse
```cpp
IPAddress local_IP(192, 168, 100, 181);
```

## 📂 Projektstruktur

```
Meteo_Garten_SD/
├── platformio.ini          # PlatformIO Konfiguration
├── README.md               # Diese Datei
├── WIRING.md               # Verkabelungsanleitung
├── src/
│   └── main.cpp            # Hauptprogramm (1800+ Zeilen)
├── data/                   # Web-Dateien (optional)
├── include/                # Header-Dateien
├── lib/                    # Bibliotheken
└── test/                   # Unit-Tests
```

## 🏗️ Architektur

### Datenfluss
```
ESP32_Meteo_aussen (Sender)
         │ ESP-NOW (alle 2 Sek)
         ▼
ESP32_Meteo_Garten_SD (Empfänger)
         │
         ├─► Sample-Array sammeln (900 Samples = 30 Min)
         │
         ├─► Mittelwert berechnen (alle 30 Min)
         │
         ├─► RRD auf SD-Karte schreiben (Ring-Buffer)
         │
         └─► Webserver visualisiert Daten
               │
               ├─► WebSocket (Echtzeit-Updates)
               │
               └─► REST API (Verlaufsdaten)
```

### RRD-Dateiformat

**Dateistruktur** (`/rrd_data.bin`):
```
┌─────────────────────────────────┐
│ RRDHeader (12 Bytes)            │  ← Offset 0
│  - writeIndex (int, 4 Bytes)    │
│  - lastUpdate (time_t, 4 Bytes) │
│  - totalEntries (int, 4 Bytes)  │
├─────────────────────────────────┤
│ RRDData[0] (28 Bytes)           │  ← Offset 12
│  - timestamp (4 Bytes)          │
│  - temperature (4 Bytes)        │
│  - humidity (4 Bytes)           │
│  - pressure (4 Bytes)           │
│  - windSpeed (4 Bytes)          │
│  - rainfall (4 Bytes)           │
│  - rainfallTotal (4 Bytes)      │
├─────────────────────────────────┤
│ RRDData[1] (28 Bytes)           │
├─────────────────────────────────┤
│ ...                             │
├─────────────────────────────────┤
│ RRDData[335] (28 Bytes)         │  ← Max 336 Einträge
└─────────────────────────────────┘

Gesamtgröße: 12 + (336 × 28) = 9420 Bytes (~9 KB)
```

### ESP-NOW Datenstruktur
```cpp
struct struct_message {
  float temperature;     // BME680 Temperatur
  float humidity;        // BME680 Luftfeuchtigkeit
  float pressure;        // BME680 Luftdruck
  float gasResistance;   // BME680 Gas-Widerstand
  int16_t wind_speed;    // Windgeschwindigkeit
  uint16_t wind_dir;     // Windrichtung
  uint8_t sensorNummer;  // Sensor-ID
  
  float std_PM1;         // PM1.0 Standard
  float std_PM2_5;       // PM2.5 Standard
  float std_PM10;        // PM10 Standard
  
  float atm_PM1;         // PM1.0 Atmosphärisch
  float atm_PM2_5;       // PM2.5 Atmosphärisch
  float atm_PM10;        // PM10 Atmosphärisch
  
  float rainFall;        // Niederschlag aktuell
  float rainfall1;       // Niederschlag kumuliert
  float workingTime;     // Betriebszeit
  float rawData;         // Rohdaten
  
  uint16_t eco2;         // CO2-Äquivalent
  uint16_t tvoc;         // TVOC
};
```

## 🛠️ Erweiterte API

### Debug-Informationen
```
GET http://192.168.100.181/api/debug
```
**Response:**
```json
{
  "uptime_minutes": 1440,
  "sample_count": 450,
  "samples_max": 900,
  "packets_received": 25920,
  "packets_rejected": 0,
  "packets_total": 25920,
  "accept_rate_percent": "100.0",
  "time_since_last_avg_sec": 900,
  "avg_interval_sec": 1800,
  "sender_mac": "AA:BB:CC:DD:EE:FF",
  "sd_card": "OK",
  "last_packet_ago_sec": 2,
  "data_available": true,
  "struct_size": 76
}
```

### RRD zurücksetzen
```
GET http://192.168.100.181/api/reset_rrd
```
Löscht die RRD-Datei und erstellt sie neu (leerer Zustand).

### Manuelles Speichern (Debug)
```
GET http://192.168.100.181/api/force_save
```
Erzwingt sofortige Mittelwertberechnung und Speicherung (nützlich für Tests).

## 🐛 Troubleshooting

### Problem: SD-Karte wird nicht erkannt
**Symptom:**
```
SD-Karte Initialisierung fehlgeschlagen!
```

**Lösung:**
1. Verkabelung prüfen (siehe WIRING.md)
2. SD-Karte mit FAT32 formatieren (nicht exFAT!)
3. 3.3V Versorgung prüfen (NICHT 5V!)
4. Andere SD-Karte testen
5. Pull-Up Widerstände an SPI-Leitungen (10kΩ optional)

### Problem: Keine ESP-NOW Pakete empfangen
**Symptom:**
```
⚠️ WARNUNG: 30 Minuten vergangen, aber KEINE Samples empfangen!
```

**Lösung:**
1. **WiFi-Kanal prüfen:** Sender und Empfänger müssen auf gleichem Kanal sein
   ```
   WiFi-Kanal: 1  ← Sollte identisch sein
   ESP-NOW Kanal: 1
   ```
2. **Sender läuft:** ESP32_Meteo_aussen muss aktiv sein
3. **struct_message identisch:** Beide Systeme müssen gleiche Datenstruktur haben
4. **Reichweite:** Maximale ESP-NOW Reichweite ~200m (Freifeld)

### Problem: Pakete werden verworfen
**Symptom:**
```
⚠️⚠️⚠️ ACHTUNG: Viele Pakete werden verworfen!
Akzeptanzrate: 45.2%
```

**Lösung:**
1. **struct_message Größe prüfen:**
   ```cpp
   Serial.printf("struct_message Größe: %d Bytes\n", sizeof(struct_message));
   ```
   Sender und Empfänger MÜSSEN identische Größe haben!
2. **Padding/Packing:** `__attribute__((packed))` verwenden oder entfernen (konsistent!)
3. **Compiler-Einstellungen:** Gleiche Plattform/Compiler für beide Systeme

### Problem: RRD-Daten korrupt
**Symptom:**
```
❌ Korrupte RRD-Datei! totalEntries=9999 (max: 336)
```

**Lösung:**
1. **RRD zurücksetzen:** `http://192.168.100.181/api/reset_rrd` aufrufen
2. **SD-Karte Hardware-Test:**
   ```cpp
   const bool FILL_TEST_DATA_ON_STARTUP = false;  // In main.cpp
   ```
   Aktiviere Test, dann neu hochladen
3. **SD-Karte ersetzen:** Möglicherweise defekt

### Problem: Webserver nicht erreichbar
**Symptom:**
```
Browser: "Verbindung fehlgeschlagen"
```

**Lösung:**
1. **IP-Adresse prüfen:** Serieller Monitor zeigt `IP-Adresse: 192.168.100.181`
2. **Gleiche Netzwerk:** PC/Smartphone muss im selben WiFi sein
3. **Firewall:** Windows Firewall deaktivieren (Test)
4. **Ping-Test:** `ping 192.168.100.181` in CMD
5. **Browser-Cache:** STRG + F5 für Hard-Refresh

### Problem: Zeit nicht synchronisiert
**Symptom:**
```
⚠️ WARNUNG: Zeit nicht synchronisiert!
Timestamp: 12345 (sollte > 1000000 sein)
```

**Lösung:**
1. **NTP-Server erreichbar:** Internetzugang erforderlich
2. **Firewall:** UDP Port 123 freigeben
3. **NTP-Server ändern:**
   ```cpp
   const char* ntpServer = "de.pool.ntp.org";  // Deutscher NTP-Server
   ```
4. **Manuell warten:** Nach ~10 Sekunden sollte Zeit synchronisiert sein

## 📊 Performance

| Metrik | Wert |
|--------|------|
| Sample-Rate | 0.5 Hz (alle 2 Sek) |
| Samples pro Periode | 900 (30 Min) |
| RRD-Speicherplatz | ~9 KB |
| WebSocket Latenz | < 100 ms |
| Speicherverbrauch (RAM) | ~60 KB |
| Paket-Akzeptanzrate | > 99% (bei korrekter Konfiguration) |

## 🔐 Sicherheitshinweise

⚠️ **Achtung:**
- WiFi-Passwort ist im Code **NICHT verschlüsselt**
- Webserver hat **KEINE Authentifizierung**
- Nur in **vertrauenswürdigen Netzwerken** betreiben
- Für Produktivumgebungen: HTTPS & Passwortschutz implementieren

## 📜 Lizenz

MIT License - siehe [LICENSE](LICENSE) Datei

## 🤝 Beiträge

Contributions willkommen! Bitte:
1. Fork erstellen
2. Feature-Branch erstellen (`git checkout -b feature/AmazingFeature`)
3. Änderungen committen (`git commit -m 'Add AmazingFeature'`)
4. Branch pushen (`git push origin feature/AmazingFeature`)
5. Pull Request öffnen

## 👨‍💻 Autor

**doctorPit2**
- GitHub: [@doctorPit2](https://github.com/doctorPit2)

## 🙏 Danksagungen

- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
- [Chart.js](https://www.chartjs.org/)
- [ArduinoJson](https://arduinojson.org/)

## 📚 Weitere Dokumentation

- [WIRING.md](WIRING.md) - Detaillierte Verkabelungsanleitung
- [api_rrd.txt](api_rrd.txt) - RRD API-Spezifikation
├── include/               # Header-Dateien
├── lib/                   # Zusätzliche Libraries
└── test/                  # Test-Dateien
```

## 🧪 Troubleshooting

### SD-Karte wird nicht erkannt
```
SD-Karte Initialisierung fehlgeschlagen!
```
**Lösungen:**
- Prüfe Verkabelung (MOSI, MISO, SCK, CS)
- SD-Karte mit FAT32 formatieren
- Andere SD-Karte testen
- Spannung prüfen (3.3V!)

### Keine ESP-NOW Daten empfangen
```
Warte auf ESP-NOW Daten... (keine Pakete)
```
**Lösungen:**
- Sender einschalten und prüfen
- WiFi-Kanal prüfen (sollte Kanal 1 sein)
- ESP-NOW auf Sender korrekt konfiguriert?
- Entfernung zwischen Sender/Empfänger reduzieren

### WebSocket verbindet nicht
```
Browser: Getrennt
```
**Lösungen:**
- IP-Adresse im Browser prüfen
- Firewall-Einstellungen prüfen
- Seite neu laden (Strg+F5)

## 📈 RRD Datenbank Details

### Struktur
```cpp
struct RRDData {
  unsigned long timestamp;  // Unix Timestamp
  float temperature;        // Temperatur in °C
  float humidity;          // Luftfeuchtigkeit in %
  float pressure;          // Luftdruck in hPa
  float windSpeed;         // Windgeschwindigkeit in km/h
  float rainfall;          // Niederschlag in mm
};
```

### Dateigröße
- Header: 12 Bytes
- Pro Datensatz: 24 Bytes
- Gesamt: 12 + (336 × 24) = **8.076 Bytes**

### Ring-Buffer Prinzip
Wenn die RRD voll ist (336 Einträge), werden die ältesten Daten überschrieben:
```
Index 0 → Index 1 → ... → Index 335 → Index 0 (Loop)
```

## 🔄 Datenfluss

```
ESP32_Meteo_aussen (Sender)
    ↓ ESP-NOW (alle 2 Sek)
ESP32_Meteo_Garten_SD (Empfänger)
    ↓ Sammlung (900 Samples)
Mittelwertberechnung (alle 30 Min)
    ↓ Speicherung
RRD auf SD-Karte (336 Einträge = 1 Woche)
    ↓ Visualisierung
Webserver → Charts im Browser
```

## 📝 Lizenz

Projekt für persönliche Nutzung.

## 👤 Autor

ESP32 Meteo Projekt

## 📅 Version

- **v1.0** - Initiale Version (Juni 2026)
  - ESP-NOW Empfang
  - SD-Karte Integration
  - RRD-Datenbank
  - Webserver mit Charts
