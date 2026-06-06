# SD-Karte Verkabelung

## Pinbelegung ESP32 → SD-Karte Modul

| ESP32 GPIO | Funktion | SD-Modul Pin | Beschreibung |
|------------|----------|--------------|--------------|
| GPIO23     | MOSI     | DI           | Data In (Master Out Slave In) |
| GPIO19     | MISO     | DO           | Data Out (Master In Slave Out) |
| GPIO18     | SCK      | SCLK         | Serial Clock |
| GPIO5      | CS       | CS           | Chip Select |
| 3.3V       | VCC      | VCC          | Stromversorgung |
| GND        | GND      | GND          | Masse |

## ⚠️ Wichtige Hinweise

### Spannung
- **WICHTIG**: SD-Karte Module benötigen **3.3V**, NICHT 5V!
- ESP32 arbeitet mit 3.3V Logik
- Niemals 5V an das SD-Modul anlegen!

### SD-Karte
- Formatierung: **FAT32**
- Empfohlene Größe: 2GB - 32GB
- SDHC Karten werden unterstützt

### SPI Bus
- Der ESP32 hat mehrere SPI Busse (VSPI, HSPI)
- Dieses Projekt verwendet den VSPI Bus
- Standard VSPI Pins werden im Code konfiguriert

## Schaltplan (Textversion)

```
ESP32 DevKit v1                    SD-Karte Modul
+----------------+                 +----------------+
|                |                 |                |
|  GPIO23 (MOSI) |---------------->| DI (MOSI)      |
|  GPIO19 (MISO) |<----------------| DO (MISO)      |
|  GPIO18 (SCK)  |---------------->| SCLK           |
|  GPIO5  (CS)   |---------------->| CS             |
|  3.3V          |---------------->| VCC            |
|  GND           |---------------->| GND            |
|                |                 |                |
+----------------+                 +----------------+
```

## Code-Konfiguration

In `src/main.cpp`:
```cpp
// SD-Karte SPI Pins
#define SD_CS    5    // Chip Select
#define SD_MOSI  23   // Data In
#define SD_MISO  19   // Data Out
#define SD_SCK   18   // Clock

// SPI Bus initialisieren
SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

// SD-Karte starten
SD.begin(SD_CS);
```

## Troubleshooting

### Problem: SD-Karte wird nicht erkannt
**Mögliche Ursachen:**
1. Falsche Verkabelung → Pins prüfen
2. SD-Karte nicht formatiert → FAT32 Format
3. Defekte SD-Karte → Andere Karte testen
4. Falsche Spannung → 3.3V prüfen!
5. Lockere Verbindungen → Kabel prüfen

### Problem: Daten können nicht geschrieben werden
**Mögliche Ursachen:**
1. SD-Karte voll → Speicherplatz prüfen
2. Schreibschutz aktiv → Schalter an SD-Karte prüfen
3. Dateisystem beschädigt → Neu formatieren

## Alternative Pin-Konfiguration

Falls andere Pins verwendet werden sollen, in `main.cpp` ändern:
```cpp
// Beispiel: Andere Pins verwenden
#define SD_CS    15   // GPIO15 als CS
#define SD_MOSI  13   // GPIO13 als MOSI
#define SD_MISO  12   // GPIO12 als MISO
#define SD_SCK   14   // GPIO14 als SCK
```

## Empfohlene SD-Karten Module

- Standard SD-Karten Adapter Module mit 3.3V Regler
- Micro SD-Karten Adapter (günstiger)
- SPI SD-Karten Module mit Level Shifter

## Stromverbrauch

- SD-Karte im Standby: ~10mA
- SD-Karte beim Schreiben: ~50-100mA
- Empfehlung: Stabiles 3.3V Netzteil verwenden
