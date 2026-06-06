# ESP32 Meteo Garten SD - RRD Datenlogger

ESP32-basierter Wetterdaten-Logger mit Round-Robin-Database (RRD) auf SD-Karte.

## 📋 Übersicht

Dieses Projekt empfängt Wetterdaten über ESP-NOW vom ESP32_Meteo_aussen Sender und speichert 30-Minuten-Mittelwerte in einer RRD-Datenbank auf einer SD-Karte. Die Daten werden über einen Webserver mit interaktiven Charts visualisiert.

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
├── src/
│   └── main.cpp           # Hauptprogramm
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
