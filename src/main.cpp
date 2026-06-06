/*
 * ESP32 Meteo Garten SD - RRD Datenlogger mit SD-Karte
 * 
 * Empfängt ESP-NOW Daten vom ESP32_Meteo_aussen Sender (Kanal 1)
 * Sammelt Daten (alle 2 Sekunden) und speichert Mittelwerte (alle 30 Min)
 * in einer RRD (Round-Robin Database) auf SD-Karte
 * RRD-Länge: 1 Woche (336 Datenpunkte pro Sensor)
 * Zeigt Charts im Browser über Webserver an
 * 
 * SD-Karte Pinbelegung:
 * - GPIO23: DI (MOSI)
 * - GPIO05: CS
 * - GPIO18: SCLK
 * - GPIO19: DO (MISO)
 */

#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <time.h>

// ===== WiFi Konfiguration =====
const char* ssid = "lenovo";
const char* password = "lenovotablet";

// Statische IP-Konfiguration
IPAddress local_IP(192, 168, 100, 181);     // Feste IP-Adresse (andere als das Original)
IPAddress gateway(192, 168, 100, 1);        // Gateway (Router)
IPAddress subnet(255, 255, 255, 0);         // Subnetzmaske
IPAddress primaryDNS(192, 168, 100, 1);     // Primärer DNS
IPAddress secondaryDNS(8, 8, 8, 8);         // Sekundärer DNS (Google DNS)

// ===== SD-Karte SPI Pins =====
#define SD_CS    5    // Chip Select
#define SD_MOSI  23   // Data In
#define SD_MISO  19   // Data Out
#define SD_SCK   18   // Clock

// ===== Webserver & WebSocket =====
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ===== Datenstruktur - IDENTISCH mit Sender! =====
typedef struct struct_message {
  float temperature;
  float humidity;
  float pressure;
  float gasResistance;
  int16_t wind_speed;
  uint16_t wind_dir;
  uint8_t sensorNummer;
  
  float std_PM1;
  float std_PM2_5;
  float std_PM10;

  float atm_PM1;
  float atm_PM2_5;
  float atm_PM10;

  float rainFall;
  float rainfall1;
  float workingTime;
  float rawData;

  uint16_t eco2;
  uint16_t tvoc;
} struct_message;

// ===== RRD Konfiguration =====
const int RRD_SIZE = 336;  // 7 Tage * 48 Werte (alle 30 Min)
const char* RRD_FILE = "/rrd_data.bin";

// RRD Datenstruktur (PACKED - kein Alignment!)
struct __attribute__((packed)) RRDData {
  time_t timestamp;  // Unix Timestamp (4 Bytes)
  float temperature; // 4 Bytes
  float humidity;    // 4 Bytes
  float pressure;    // 4 Bytes
  float windSpeed;   // 4 Bytes
  float rainfall;    // 4 Bytes
  float rainfallTotal; // 4 Bytes
};
// Gesamt: 28 Bytes (7 × 4 Bytes, kein Padding)

// RRD Header (Metadaten) - PACKED
struct __attribute__((packed)) RRDHeader {
  int writeIndex;           // Aktueller Schreibindex (Ring-Buffer) - 4 Bytes
  time_t lastUpdate;        // Letztes Update Timestamp - 4 Bytes
  int totalEntries;         // Anzahl gespeicherter Einträge - 4 Bytes
};
// Gesamt: 12 Bytes (3 × 4 Bytes, kein Padding)

// ===== Globale Variablen =====
struct_message receivedData;
bool dataAvailable = false;
unsigned long lastPacketTime = 0;
String lastSenderMAC = "Noch kein Sender";
unsigned long totalPacketsReceived = 0;
unsigned long totalPacketsRejected = 0;  // Zähler für verworfene Pakete

// Arrays für Mittelwertberechnung (6 Hauptwerte)
const int SAMPLES_PER_PERIOD = 900;  // 30 Min / 2 Sek = 900 Samples
float tempSamples[SAMPLES_PER_PERIOD];
float pressSamples[SAMPLES_PER_PERIOD];
float humSamples[SAMPLES_PER_PERIOD];
float speedSamples[SAMPLES_PER_PERIOD];
float regenSamples[SAMPLES_PER_PERIOD];      // rainFall
float regenTotalSamples[SAMPLES_PER_PERIOD]; // rainfall1
int sampleCount = 0;
unsigned long lastAverageTime = 0;

// Zeitkonstanten
const unsigned long AVERAGE_INTERVAL = 1800000; // 30 Minuten (1800000ms)

// ===== DEBUG/TEST EINSTELLUNGEN =====
const bool FILL_TEST_DATA_ON_STARTUP = false;  // true = Test-Daten beim Start schreiben
// HINWEIS: Auf false setzen für normalen Betrieb

// NTP Zeit-Synchronisation
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;        // GMT+1
const int daylightOffset_sec = 3600;     // Sommerzeit

// SD-Karte Status
bool sdCardInitialized = false;

// ===== Hilfsfunktionen =====

// NTP-Zeit initialisieren
void initTime() {
  Serial.println("Synchronisiere Zeit mit NTP-Server...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 10) {
    Serial.print(".");
    delay(500);
    attempts++;
  }
  
  if (attempts < 10) {
    Serial.println("\nZeit synchronisiert!");
    Serial.println(&timeinfo, "%A, %d. %B %Y %H:%M:%S");
  } else {
    Serial.println("\nZeitsynchronisation fehlgeschlagen!");
  }
}

// WiFi verbinden
void connectWiFi() {
  Serial.println();
  Serial.print("Verbinde mit WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Statische IP-Konfiguration fehlgeschlagen!");
  }
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi verbunden!");
    Serial.print("IP-Adresse: ");
    Serial.println(WiFi.localIP());
    Serial.print("WiFi-Kanal: ");
    Serial.println(WiFi.channel());
  } else {
    Serial.println("\nWiFi Verbindung fehlgeschlagen!");
  }
}

// SD-Karte initialisieren
bool initSDCard() {
  Serial.println("Initialisiere SD-Karte...");
  
  // SPI mit benutzerdefinierten Pins initialisieren
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  if (!SD.begin(SD_CS)) {
    Serial.println("SD-Karte Initialisierung fehlgeschlagen!");
    Serial.println("Prüfe:");
    Serial.println("  - SD-Karte eingelegt?");
    Serial.println("  - Verkabelung korrekt?");
    Serial.println("  - SD-Karte formatiert (FAT32)?");
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("Keine SD-Karte gefunden!");
    return false;
  }
  
  Serial.print("SD-Karte Typ: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD-Karte Größe: %lluMB\n", cardSize);
  Serial.printf("Freier Speicher: %lluMB\n", SD.usedBytes() / (1024 * 1024));
  
  sdCardInitialized = true;
  return true;
}

// SD-Karte Hardware-Test (Schreiben & Lesen)
bool testSDCard() {
  if (!sdCardInitialized) {
    Serial.println("SD-Karte nicht initialisiert!");
    return false;
  }
  
  Serial.println("\n========================================");
  Serial.println("SD-KARTE HARDWARE-TEST");
  Serial.println("========================================");
  
  const char* testFile = "/sd_test.tmp";
  const int testDataSize = 1024;  // 1 KB Testdaten
  
  // Test 1: Schreiben
  Serial.println("\n[1] Schreibe Testdaten (1 KB)...");
  File file = SD.open(testFile, FILE_WRITE);
  if (!file) {
    Serial.println("❌ FEHLER: Kann Testdatei nicht erstellen!");
    return false;
  }
  
  // Testmuster erstellen (zählende Bytes 0-255 wiederholt)
  uint8_t writeBuffer[testDataSize];
  for (int i = 0; i < testDataSize; i++) {
    writeBuffer[i] = i % 256;
  }
  
  size_t bytesWritten = file.write(writeBuffer, testDataSize);
  file.close();
  
  if (bytesWritten != testDataSize) {
    Serial.printf("❌ FEHLER: Nur %d von %d Bytes geschrieben!\n", bytesWritten, testDataSize);
    return false;
  }
  Serial.printf("✓ %d Bytes erfolgreich geschrieben\n", bytesWritten);
  
  // Test 2: Lesen
  Serial.println("\n[2] Lese Testdaten zurück...");
  file = SD.open(testFile, FILE_READ);
  if (!file) {
    Serial.println("❌ FEHLER: Kann Testdatei nicht öffnen!");
    return false;
  }
  
  uint8_t readBuffer[testDataSize];
  size_t bytesRead = file.read(readBuffer, testDataSize);
  file.close();
  
  if (bytesRead != testDataSize) {
    Serial.printf("❌ FEHLER: Nur %d von %d Bytes gelesen!\n", bytesRead, testDataSize);
    return false;
  }
  Serial.printf("✓ %d Bytes erfolgreich gelesen\n", bytesRead);
  
  // Test 3: Datenvergleich
  Serial.println("\n[3] Vergleiche Daten...");
  int errors = 0;
  for (int i = 0; i < testDataSize; i++) {
    if (readBuffer[i] != writeBuffer[i]) {
      if (errors < 10) {  // Zeige nur erste 10 Fehler
        Serial.printf("❌ Byte %d: Geschrieben=%d, Gelesen=%d\n", i, writeBuffer[i], readBuffer[i]);
      }
      errors++;
    }
  }
  
  if (errors > 0) {
    Serial.printf("\n❌ FEHLER: %d von %d Bytes fehlerhaft!\n", errors, testDataSize);
    Serial.println("⚠️  HARDWARE-PROBLEM: SD-Karte defekt!");
    return false;
  }
  Serial.println("✓ Alle Daten korrekt!");
  
  // Test 4: Aufräumen
  Serial.println("\n[4] Lösche Testdatei...");
  if (SD.remove(testFile)) {
    Serial.println("✓ Testdatei gelöscht");
  } else {
    Serial.println("⚠️  Warnung: Testdatei konnte nicht gelöscht werden");
  }
  
  // Test 5: RRD-Header Test
  Serial.println("\n[5] Teste RRD-Header Struktur...");
  const char* headerTestFile = "/header_test.tmp";
  
  // Schreibe Test-Header
  RRDHeader testHeader;
  testHeader.writeIndex = 42;
  testHeader.lastUpdate = 1234567890;
  testHeader.totalEntries = 100;
  
  file = SD.open(headerTestFile, FILE_WRITE);
  if (file) {
    file.write((uint8_t*)&testHeader, sizeof(RRDHeader));
    file.close();
    
    // Lese Test-Header zurück
    file = SD.open(headerTestFile, FILE_READ);
    if (file) {
      RRDHeader readHeader;
      file.read((uint8_t*)&readHeader, sizeof(RRDHeader));
      file.close();
      
      bool headerOK = (readHeader.writeIndex == testHeader.writeIndex &&
                       readHeader.lastUpdate == testHeader.lastUpdate &&
                       readHeader.totalEntries == testHeader.totalEntries);
      
      if (headerOK) {
        Serial.println("✓ RRD-Header Struktur OK");
        Serial.printf("  writeIndex: %d, lastUpdate: %ld, totalEntries: %d\n",
                     readHeader.writeIndex, readHeader.lastUpdate, readHeader.totalEntries);
      } else {
        Serial.println("❌ FEHLER: RRD-Header Daten korrupt!");
        Serial.printf("  Erwartet: idx=%d, time=%ld, entries=%d\n",
                     testHeader.writeIndex, testHeader.lastUpdate, testHeader.totalEntries);
        Serial.printf("  Gelesen:  idx=%d, time=%ld, entries=%d\n",
                     readHeader.writeIndex, readHeader.lastUpdate, readHeader.totalEntries);
        return false;
      }
    }
    SD.remove(headerTestFile);
  }
  
  Serial.println("\n========================================");
  Serial.println("✅ SD-KARTE HARDWARE-TEST ERFOLGREICH!");
  Serial.println("========================================\n");
  
  return true;
}

// Forward-Deklaration
bool writeToRRD(float avgTemp, float avgHum, float avgPress, float avgSpeed, float avgRain, float avgRainTotal);

// RRD initialisieren oder laden
bool initRRD() {
  if (!sdCardInitialized) {
    Serial.println("SD-Karte nicht initialisiert!");
    return false;
  }
  
  // Prüfe ob RRD-Datei existiert
  if (SD.exists(RRD_FILE)) {
    Serial.println("RRD-Datei gefunden, lade Daten...");
    File file = SD.open(RRD_FILE, FILE_READ);
    if (file) {
      RRDHeader header;
      file.read((uint8_t*)&header, sizeof(RRDHeader));
      Serial.printf("RRD geladen: Index=%d, Einträge=%d\n", 
                    header.writeIndex, header.totalEntries);
      
      // Validierung: Prüfe auf korrupte Daten
      if (header.totalEntries > RRD_SIZE || header.totalEntries < 0 || 
          header.writeIndex >= RRD_SIZE || header.writeIndex < 0) {
        Serial.println("FEHLER: RRD-Datei korrupt! Erstelle neue Datei...");
        file.close();
        SD.remove(RRD_FILE);  // Korrupte Datei löschen
        // Weiter zur Neuerstellung (nächster Code-Block)
      } else {
        file.close();
        return true;
      }
    }
  }
  
  // Neue RRD-Datei erstellen
  Serial.println("Erstelle neue RRD-Datei...");
  File file = SD.open(RRD_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("Fehler beim Erstellen der RRD-Datei!");
    return false;
  }
  
  // Header initialisieren
  RRDHeader header;
  header.writeIndex = 0;
  header.lastUpdate = 0;
  header.totalEntries = 0;
  file.write((uint8_t*)&header, sizeof(RRDHeader));
  
  // Leere Dateneinträge schreiben
  RRDData emptyData = {0, 0, 0, 0, 0, 0};
  for (int i = 0; i < RRD_SIZE; i++) {
    file.write((uint8_t*)&emptyData, sizeof(RRDData));
  }
  
  file.close();
  Serial.println("RRD-Datei erstellt!");
  return true;
}

// Test-Daten in RRD schreiben (für Demo/Debug)
void fillRRDWithTestData() {
  if (!sdCardInitialized) {
    Serial.println("SD-Karte nicht verfügbar!");
    return;
  }
  
  Serial.println("\n========================================");
  Serial.println("Fülle RRD mit Test-Daten (letzte 24h)...");
  Serial.println("========================================");
  
  time_t now = time(nullptr);
  Serial.printf("Aktuelle Zeit (Unix): %ld\n", now);
  
  if (now < 100000) {
    Serial.println("⚠️  WARNUNG: Zeit nicht synchronisiert!");
    Serial.println("Warte auf NTP-Synchronisation...");
    delay(5000);  // Länger warten
    now = time(nullptr);
    Serial.printf("Neue Zeit (Unix): %ld\n", now);
    
    if (now < 100000) {
      Serial.println("❌ Zeit immer noch nicht synchronisiert!");
      Serial.println("Test-Daten werden NICHT geschrieben (ungültige Timestamps)");
      Serial.println("========================================\n");
      return;
    }
  }
  
  // Zeitstempel als Datum anzeigen
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  Serial.print("Referenzzeit: ");
  Serial.println(&timeinfo, "%d.%m.%Y %H:%M:%S");
  Serial.printf("Erstelle 48 Datenpunkte (je 30 Min zurück)...\n");
  
  // RRD-Datei öffnen
  File file = SD.open(RRD_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("Fehler beim Öffnen der RRD-Datei!");
    return;
  }
  
  // Header initialisieren
  RRDHeader header;
  header.writeIndex = 0;
  header.lastUpdate = now;
  header.totalEntries = 0;
  
  // Schreibe 48 Test-Einträge direkt
  float cumulativeRain = 0.0;
  for (int i = 0; i < 48; i++) {
    RRDData data;
    data.timestamp = now - (48 - i) * 1800;  // 30 Min zurück
    data.temperature = 20.0 + sin(i * 0.26) * 8.0;
    data.humidity = 60.0 + cos(i * 0.26) * 20.0;
    data.pressure = 1013.0 + sin(i * 0.13) * 10.0;
    data.windSpeed = 5.0 + fabs(sin(i * 0.4)) * 15.0;
    data.rainfall = (i % 6 == 0) ? 2.5 : 0.0;        // Regen alle 3h
    cumulativeRain += data.rainfall;
    data.rainfallTotal = cumulativeRain;              // Kumuliert
    
    int writePos = sizeof(RRDHeader) + (i * sizeof(RRDData));
    file.seek(writePos);
    file.write((uint8_t*)&data, sizeof(RRDData));
    
    header.totalEntries++;
    
    if (i == 0 || i == 47) {
      // Zeige ersten und letzten Eintrag
      struct tm t;
      localtime_r(&data.timestamp, &t);
      Serial.printf("Eintrag #%d: ", i+1);
      Serial.print(&t, "%d.%m %H:%M");
      Serial.printf(" - %.1f°C\n", data.temperature);
    } else if (i % 10 == 0) {
      Serial.printf("Geschrieben: %d/48\n", i+1);
      delay(10);  // Watchdog
    }
  }
  
  // Header schreiben
  header.writeIndex = 48 % RRD_SIZE;
  file.seek(0);
  file.write((uint8_t*)&header, sizeof(RRDHeader));
  
  file.close();
  
  Serial.println("\n✓ Test-Daten erfolgreich geschrieben!");
  Serial.printf("  Einträge: %d\n", header.totalEntries);
  Serial.printf("  Write-Index: %d\n", header.writeIndex);
  Serial.printf("  Letztes Update: %ld\n", header.lastUpdate);
  Serial.println("========================================\n");
}

// Mittelwert in RRD schreiben
bool writeToRRD(float avgTemp, float avgHum, float avgPress, float avgSpeed, float avgRain, float avgRainTotal) {
  if (!sdCardInitialized) {
    Serial.println("SD-Karte nicht verfügbar!");
    return false;
  }
  
  Serial.println("\n>>> writeToRRD() - Start");
  
  // SCHRITT 1: Header lesen (nur lesen!)
  File file = SD.open(RRD_FILE, FILE_READ);
  if (!file) {
    Serial.println("❌ Fehler beim Öffnen der RRD-Datei zum Lesen!");
    return false;
  }
  
  RRDHeader header;
  file.read((uint8_t*)&header, sizeof(RRDHeader));
  file.close();
  
  Serial.printf("Header gelesen: writeIndex=%d, totalEntries=%d, lastUpdate=%ld\n",
                header.writeIndex, header.totalEntries, header.lastUpdate);
  
  // Validierung
  if (header.writeIndex < 0 || header.writeIndex >= RRD_SIZE ||
      header.totalEntries < 0 || header.totalEntries > RRD_SIZE) {
    Serial.println("⚠️  WARNUNG: Header ungültig, initialisiere neu...");
    header.writeIndex = 0;
    header.lastUpdate = 0;
    header.totalEntries = 0;
  }
  
  // SCHRITT 2: Neuen Datensatz vorbereiten
  RRDData newData;
  newData.timestamp = time(nullptr);
  newData.temperature = avgTemp;
  newData.humidity = avgHum;
  newData.pressure = avgPress;
  newData.windSpeed = avgSpeed;
  newData.rainfall = avgRain;
  newData.rainfallTotal = avgRainTotal;
  
  Serial.printf("Neuer Datensatz: Timestamp=%ld, Temp=%.1f°C\n", 
                newData.timestamp, newData.temperature);
  
  int writePos = sizeof(RRDHeader) + (header.writeIndex * sizeof(RRDData));
  Serial.printf("Schreibe Daten an Position: %d (Index %d)\n", writePos, header.writeIndex);
  
  // SCHRITT 3: Daten schreiben (Modus "r+" = read/write ohne truncate!)
  file = SD.open(RRD_FILE, "r+");
  if (!file) {
    Serial.println("❌ Fehler beim Öffnen zum Schreiben (r+ Modus)!");
    return false;
  }
  
  size_t fileSizeBefore = file.size();
  Serial.printf("Datei geöffnet (r+ Modus), Größe: %d Bytes\n", fileSizeBefore);
  file.seek(writePos);
  size_t bytesWritten = file.write((uint8_t*)&newData, sizeof(RRDData));
  file.flush();
  size_t fileSizeAfter = file.size();
  file.close();
  
  Serial.printf("Daten geschrieben: %d Bytes, Datei: %d → %d Bytes (sollte gleich bleiben!)\n", 
                bytesWritten, fileSizeBefore, fileSizeAfter);
  
  // Sofort zurücklesen zur Verifikation
  file = SD.open(RRD_FILE, FILE_READ);
  if (file) {
    file.seek(writePos);
    RRDData verifyData;
    file.read((uint8_t*)&verifyData, sizeof(RRDData));
    file.close();
    
    struct tm t;
    localtime_r(&verifyData.timestamp, &t);
    Serial.printf("Daten-Verifikation: Timestamp=%ld (", verifyData.timestamp);
    Serial.print(&t, "%d.%m.%Y %H:%M");
    Serial.printf("), Temp=%.1f°C\n", verifyData.temperature);
    
    if (verifyData.timestamp != newData.timestamp) {
      Serial.println("❌❌❌ FEHLER: Geschriebene Daten stimmen nicht überein!");
      Serial.printf("Erwartet: %ld, Gelesen: %ld\n", newData.timestamp, verifyData.timestamp);
    } else {
      Serial.println("✓ Daten korrekt geschrieben und verifiziert");
    }
  }
  
  // SCHRITT 4: Header aktualisieren
  int oldWriteIndex = header.writeIndex;
  int oldTotalEntries = header.totalEntries;
  
  header.writeIndex = (header.writeIndex + 1) % RRD_SIZE;
  header.lastUpdate = newData.timestamp;
  if (header.totalEntries < RRD_SIZE) {
    header.totalEntries++;
  }
  
  Serial.printf("Header Update: writeIndex %d → %d, totalEntries %d → %d\n",
                oldWriteIndex, header.writeIndex, oldTotalEntries, header.totalEntries);
  
  // SCHRITT 5: Header schreiben (Modus "r+" = read/write ohne truncate!)
  file = SD.open(RRD_FILE, "r+");
  if (!file) {
    Serial.println("❌ Fehler beim Öffnen für Header (r+ Modus)!");
    return false;
  }
  
  size_t fileSizeBeforeHeader = file.size();
  Serial.printf("Datei für Header geöffnet (r+ Modus), Größe: %d Bytes\n", fileSizeBeforeHeader);
  file.seek(0);
  size_t headerWritten = file.write((uint8_t*)&header, sizeof(RRDHeader));
  file.flush();
  delay(10);
  size_t fileSizeAfterHeader = file.size();
  file.close();
  
  Serial.printf("Header geschrieben: %d Bytes, Datei: %d → %d Bytes (sollte gleich bleiben!)\n", 
                headerWritten, fileSizeBeforeHeader, fileSizeAfterHeader);
  
  // SCHRITT 6: Verifikation
  file = SD.open(RRD_FILE, FILE_READ);
  if (file) {
    RRDHeader verifyHeader;
    file.read((uint8_t*)&verifyHeader, sizeof(RRDHeader));
    file.close();
    
    Serial.printf("Verifikation: writeIndex=%d, totalEntries=%d\n",
                  verifyHeader.writeIndex, verifyHeader.totalEntries);
    
    if (verifyHeader.writeIndex != header.writeIndex) {
      Serial.println("❌❌❌ FEHLER: Header wurde nicht korrekt geschrieben!");
      return false;
    } else {
      Serial.println("✓ Header erfolgreich verifiziert");
    }
  }
  
  struct tm timeinfo;
  localtime_r(&newData.timestamp, &timeinfo);
  Serial.println("\n========================================");
  Serial.println("MITTELWERT IN RRD GESPEICHERT");
  Serial.println(&timeinfo, "%d.%m.%Y %H:%M:%S");
  Serial.printf("Temp: %.1f°C | Hum: %.0f%% | Press: %.0f hPa\n", 
                avgTemp, avgHum, avgPress);
  Serial.printf("Wind: %.1f km/h | Regen: %.1f mm\n", avgSpeed, avgRain);
  Serial.printf("RRD Index: %d/%d\n", header.writeIndex, RRD_SIZE);
  Serial.println("========================================");
  
  return true;
}

// RRD Daten lesen (für Charts)
String readRRDAsJSON() {
  Serial.println("\n>>> Lese RRD-Daten für Charts...");
  
  if (!sdCardInitialized) {
    Serial.println("❌ SD-Karte nicht verfügbar!");
    return "{\"error\":\"SD-Karte nicht verfügbar\"}";
  }
  
  File file = SD.open(RRD_FILE, FILE_READ);
  if (!file) {
    Serial.println("❌ RRD-Datei nicht gefunden!");
    return "{\"error\":\"RRD-Datei nicht gefunden\"}";
  }
  
  // Header lesen
  RRDHeader header;
  file.read((uint8_t*)&header, sizeof(RRDHeader));
  
  Serial.printf("RRD Header: totalEntries=%d, writeIndex=%d, lastUpdate=%ld\n",
                header.totalEntries, header.writeIndex, header.lastUpdate);
  
  // WICHTIG: Validierung! Verhindert Watchdog bei korrupten Daten
  if (header.totalEntries > RRD_SIZE || header.totalEntries < 0) {
    Serial.printf("❌ Korrupte RRD-Datei! totalEntries=%d (max: %d)\n", 
                  header.totalEntries, RRD_SIZE);
    file.close();
    return "{\"error\":\"Korrupte RRD-Datei - bitte löschen und neu starten\",\"totalEntries\":0}";
  }
  
  if (header.totalEntries == 0) {
    Serial.println("⚠️  Keine Daten in RRD vorhanden");
    file.close();
    return "{\"error\":\"Keine Daten - warte auf erste Messungen\",\"totalEntries\":0}";
  }
  
  // JSON aufbauen
  JsonDocument doc;
  doc["totalEntries"] = header.totalEntries;
  doc["lastUpdate"] = header.lastUpdate;
  
  JsonArray timestamps = doc["timestamps"].to<JsonArray>();
  JsonArray temperatures = doc["temperatures"].to<JsonArray>();
  JsonArray humidities = doc["humidities"].to<JsonArray>();
  JsonArray pressures = doc["pressures"].to<JsonArray>();
  JsonArray windSpeeds = doc["windSpeeds"].to<JsonArray>();
  JsonArray rainfalls = doc["rainfalls"].to<JsonArray>();
  JsonArray rainfallTotals = doc["rainfallTotals"].to<JsonArray>();
  
  // Daten in chronologischer Reihenfolge lesen
  int readIndex = (header.writeIndex - header.totalEntries + RRD_SIZE) % RRD_SIZE;
  int validEntries = 0;
  int skippedEntries = 0;
  
  Serial.printf("Lese %d Einträge ab Index %d...\n", header.totalEntries, readIndex);
  
  for (int i = 0; i < header.totalEntries; i++) {
    // Watchdog füttern bei vielen Einträgen
    if (i % 50 == 0) {
      delay(1);  // Gibt anderen Tasks Zeit
    }
    
    int pos = sizeof(RRDHeader) + (readIndex * sizeof(RRDData));
    file.seek(pos);
    
    RRDData data;
    file.read((uint8_t*)&data, sizeof(RRDData));
    
    // DEBUG: Ersten Eintrag anzeigen
    if (i == 0) {
      struct tm t;
      localtime_r(&data.timestamp, &t);
      Serial.printf("Erster Eintrag: Timestamp=%ld (", data.timestamp);
      Serial.print(&t, "%d.%m.%Y %H:%M");
      Serial.printf("), Temp=%.1f°C\n", data.temperature);
    }
    
    if (data.timestamp > 0) {  // Nur gültige Daten
      timestamps.add(data.timestamp);
      temperatures.add(serialized(String(data.temperature, 1)));
      humidities.add(serialized(String(data.humidity, 0)));
      pressures.add(serialized(String(data.pressure, 0)));
      windSpeeds.add(serialized(String(data.windSpeed, 1)));
      rainfalls.add(serialized(String(data.rainfall, 1)));
      rainfallTotals.add(serialized(String(data.rainfallTotal, 1)));
      validEntries++;
    } else {
      skippedEntries++;
    }
    
    readIndex = (readIndex + 1) % RRD_SIZE;
  }
  
  file.close();
  
  Serial.printf("✓ %d gültige Einträge gelesen", validEntries);
  if (skippedEntries > 0) {
    Serial.printf(" (%d übersprungen - Timestamp=0)", skippedEntries);
  }
  Serial.println();
  
  String output;
  serializeJson(doc, output);
  return output;
}

// Mittelwertberechnung und RRD-Speicherung
void calculateAndStoreAverages() {
  if (sampleCount == 0) {
    Serial.println("Keine Samples zum Mitteln vorhanden!");
    return;
  }
  
  Serial.printf("\n>>> Berechne Mittelwerte aus %d Samples...\n", sampleCount);
  
  // Mittelwerte berechnen
  float avgTemp = 0, avgHum = 0, avgPress = 0, avgSpeed = 0, avgRain = 0, avgRainTotal = 0;
  
  for (int i = 0; i < sampleCount; i++) {
    avgTemp += tempSamples[i];
    avgHum += humSamples[i];
    avgPress += pressSamples[i];
    avgSpeed += speedSamples[i];
    avgRain += regenSamples[i];
    avgRainTotal += regenTotalSamples[i];
    
    // Watchdog füttern bei vielen Samples
    if (i % 100 == 0) {
      delay(1);
    }
  }
  
  avgTemp /= sampleCount;
  avgHum /= sampleCount;
  avgPress /= sampleCount;
  avgSpeed /= sampleCount;
  avgRain /= sampleCount;
  avgRainTotal /= sampleCount;
  
  Serial.printf("Mittelwerte: T=%.1f°C, H=%.0f%%, P=%.0fhPa, W=%.1fkm/h, R=%.1fmm, RT=%.1fmm\n",
                avgTemp, avgHum, avgPress, avgSpeed, avgRain, avgRainTotal);
  
  // In RRD speichern
  if (writeToRRD(avgTemp, avgHum, avgPress, avgSpeed, avgRain, avgRainTotal)) {
    Serial.println("✓ Erfolgreich in RRD gespeichert");
  } else {
    Serial.println("✗ Fehler beim Speichern in RRD");
  }
  
  // Arrays zurücksetzen
  sampleCount = 0;
  lastAverageTime = millis();
  
  // WebSocket Update senden
  ws.textAll("{\"type\":\"rrd_updated\"}");
  
  Serial.println(">>> Bereit für neue Samples\n");
}

// ESP-NOW Callback für Datenempfang
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2], 
           mac_addr[3], mac_addr[4], mac_addr[5]);
  lastSenderMAC = String(macStr);
  
  // DEBUG: Erste 5 Pakete ausführlich loggen
  if (totalPacketsReceived < 5) {
    Serial.println("\n========== ESP-NOW PAKET ==========");
    Serial.printf("Paket #%lu von %s\n", totalPacketsReceived + 1, macStr);
    Serial.printf("Datenlänge: %d Bytes (erwartet: %d)\n", data_len, sizeof(struct_message));
  }
  
  if (data_len == sizeof(struct_message)) {
    memcpy(&receivedData, data, sizeof(struct_message));
    totalPacketsReceived++;
    lastPacketTime = millis();
    dataAvailable = true;
    
    // Beim ersten Sample nach Reset: Timer starten
    if (sampleCount == 0) {
      lastAverageTime = millis();
      Serial.println(">>> Timer für 30-Minuten-Intervall gestartet");
    }
    
    // Samples sammeln (wenn Platz vorhanden)
    if (sampleCount < SAMPLES_PER_PERIOD) {
      tempSamples[sampleCount] = receivedData.temperature;
      humSamples[sampleCount] = receivedData.humidity;
      pressSamples[sampleCount] = receivedData.pressure;
      speedSamples[sampleCount] = receivedData.wind_speed;
      regenSamples[sampleCount] = receivedData.rainFall;
      regenTotalSamples[sampleCount] = receivedData.rainfall1;
      sampleCount++;
      
      if (totalPacketsReceived < 5) {
        Serial.printf("✓ Sample #%d gespeichert\n", sampleCount);
        Serial.printf("  Temperatur: %.1f°C\n", receivedData.temperature);
        Serial.printf("  Luftfeuchtigkeit: %.0f%%\n", receivedData.humidity);
        Serial.printf("  Luftdruck: %.0f hPa\n", receivedData.pressure);
        Serial.println("===================================");
      }
    } else {
      Serial.println("⚠️  WARNUNG: Sample-Array voll! Trigger Mittelwertberechnung...");
    }
    
    // Kompakte Ausgabe für restliche Pakete
    if (totalPacketsReceived >= 5 && totalPacketsReceived % 10 == 0) {
      Serial.printf("Paket #%lu | Samples: %d/%d | Temp: %.1f°C\n",
                    totalPacketsReceived, sampleCount, SAMPLES_PER_PERIOD, 
                    receivedData.temperature);
    }
  } else {
    totalPacketsRejected++;
    
    // Zeige verworfene Pakete regelmäßig
    if (totalPacketsRejected <= 10 || totalPacketsRejected % 10 == 0) {
      Serial.println("\n⚠️  WARNUNG: Ungültige Paketgröße!");
      Serial.printf("Paket #%lu (verworfen #%lu)\n", 
                    totalPacketsReceived + totalPacketsRejected, totalPacketsRejected);
      Serial.printf("Erwartet: %d Bytes, Empfangen: %d Bytes\n", 
                    sizeof(struct_message), data_len);
      
      if (totalPacketsRejected == 1) {
        Serial.println("\nMögliche Ursachen:");
        Serial.println("  - Sender/Empfänger haben unterschiedliche struct_message Definitionen");
        Serial.println("  - Datenkorruption während Übertragung");
        Serial.printf("\nstruct_message Größe auf diesem System: %d Bytes\n", sizeof(struct_message));
        Serial.println("Bitte prüfen Sie die Sender-Definition!\n");
      }
    }
  }
}

// Aktuelle Daten als JSON
String getCurrentDataJSON() {
  JsonDocument doc;
  
  doc["temperature"] = serialized(String(receivedData.temperature, 1));
  doc["humidity"] = serialized(String(receivedData.humidity, 0));
  doc["pressure"] = serialized(String(receivedData.pressure, 0));
  doc["windSpeed"] = receivedData.wind_speed;
  doc["windDir"] = receivedData.wind_dir;
  doc["rainfall"] = serialized(String(receivedData.rainFall, 1));
  doc["rainfallTotal"] = serialized(String(receivedData.rainfall1, 1));
  doc["sampleCount"] = sampleCount;
  doc["totalPackets"] = totalPacketsReceived;
  doc["senderMAC"] = lastSenderMAC;
  doc["sdCard"] = sdCardInitialized ? "OK" : "ERROR";
  
  String output;
  serializeJson(doc, output);
  return output;
}

// WebSocket Event Handler
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket Client #%u verbunden\n", client->id());
    client->text(getCurrentDataJSON());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket Client #%u getrennt\n", client->id());
  }
}

// Webserver initialisieren
void initWebServer() {
  // WebSocket Setup
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  
  // Hauptseite
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Meteo Garten SD - RRD Datenlogger</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: #fff;
            padding: 20px;
        }
        .container {
            max-width: 1400px;
            margin: 0 auto;
        }
        h1 {
            text-align: center;
            margin-bottom: 10px;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
        }
        .status {
            background: rgba(255,255,255,0.1);
            padding: 10px;
            border-radius: 10px;
            margin-bottom: 15px;
            text-align: center;
            backdrop-filter: blur(10px);
        }
        .status-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
            gap: 8px;
            margin-top: 8px;
        }
        .status-item {
            background: rgba(255,255,255,0.2);
            padding: 8px;
            border-radius: 5px;
        }
        .status-value {
            font-size: 20px;
            font-weight: bold;
        }
        .data-status {
            background: rgba(255,255,255,0.9);
            padding: 15px;
            border-radius: 10px;
            margin-bottom: 15px;
            text-align: center;
            font-weight: bold;
            display: none;
        }
        .data-status.info {
            display: block;
            background: rgba(255, 193, 7, 0.9);
            color: #333;
        }
        .data-status.success {
            display: block;
            background: rgba(76, 175, 80, 0.9);
            color: white;
        }
        .data-status.error {
            display: block;
            background: rgba(244, 67, 54, 0.9);
            color: white;
        }
        .chart-container {
            background: rgba(255,255,255,0.95);
            padding: 10px 15px;
            border-radius: 10px;
            margin-bottom: 10px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.3);
        }
        .charts-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 10px;
            margin-bottom: 10px;
        }
        .chart-full {
            grid-column: 1 / -1;
        }
        canvas {
            max-height: 180px !important;
            height: 180px !important;
        }
        .chart-title {
            color: #333;
            font-size: 16px;
            font-weight: bold;
            margin-bottom: 5px;
            text-align: center;
        }
        @media (max-width: 768px) {
            .charts-grid {
                grid-template-columns: 1fr;
            }
        }
        .online { color: #4ade80; }
        .offline { color: #f87171; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌤️ Meteo Garten SD - RRD Datenlogger</h1>
        
        <div class="status">
            <div id="connection-status">Verbinde...</div>
            <div class="status-grid">
                <div class="status-item">
                    <div>Temperatur</div>
                    <div class="status-value" id="current-temp">--°C</div>
                </div>
                <div class="status-item">
                    <div>Luftfeuchtigkeit</div>
                    <div class="status-value" id="current-hum">--%</div>
                </div>
                <div class="status-item">
                    <div>Luftdruck</div>
                    <div class="status-value" id="current-press">-- hPa</div>
                </div>
                <div class="status-item">
                    <div>Wind</div>
                    <div class="status-value" id="current-wind">-- km/h</div>
                </div>
                <div class="status-item">
                    <div>Regen</div>
                    <div class="status-value" id="current-rain">-- mm</div>
                </div>
                <div class="status-item">
                    <div>Regen Total</div>
                    <div class="status-value" id="current-rain-total">-- mm</div>
                </div>
                <div class="status-item">
                    <div>Samples</div>
                    <div class="status-value" id="sample-count">0/900</div>
                </div>
            </div>
        </div>

        <!-- Daten-Status Banner -->
        <div id="data-status" class="data-status info">
            ⏳ Lade Daten...
        </div>

        <div class="charts-grid">
            <div class="chart-container">
                <div class="chart-title">Temperatur (1 Woche)</div>
                <canvas id="tempChart"></canvas>
            </div>

            <div class="chart-container">
                <div class="chart-title">Luftfeuchtigkeit (1 Woche)</div>
                <canvas id="humChart"></canvas>
            </div>

            <div class="chart-container">
                <div class="chart-title">Luftdruck (1 Woche)</div>
                <canvas id="pressChart"></canvas>
            </div>

            <div class="chart-container">
                <div class="chart-title">Windgeschwindigkeit (1 Woche)</div>
                <canvas id="windChart"></canvas>
            </div>
        </div>

        <div class="chart-container chart-full">
            <div class="chart-title">Niederschlag (1 Woche)</div>
            <canvas id="rainChart"></canvas>
        </div>

        <div class="chart-container chart-full">
            <div class="chart-title">Niederschlag Kumuliert (1 Woche)</div>
            <canvas id="rainTotalChart"></canvas>
        </div>
    </div>

    <script>
        let ws;
        let charts = {};

        // WebSocket verbinden
        function connectWebSocket() {
            ws = new WebSocket('ws://' + window.location.hostname + '/ws');
            
            ws.onopen = () => {
                document.getElementById('connection-status').innerHTML = 
                    '<span class="online">●</span> Verbunden';
                loadRRDData();
            };
            
            ws.onclose = () => {
                document.getElementById('connection-status').innerHTML = 
                    '<span class="offline">●</span> Getrennt';
                setTimeout(connectWebSocket, 3000);
            };
            
            ws.onmessage = (event) => {
                const data = JSON.parse(event.data);
                if (data.type === 'rrd_updated') {
                    loadRRDData();
                } else {
                    updateCurrentData(data);
                }
            };
        }

        // Aktuelle Daten aktualisieren
        function updateCurrentData(data) {
            document.getElementById('current-temp').textContent = data.temperature + '°C';
            document.getElementById('current-hum').textContent = data.humidity + '%';
            document.getElementById('current-press').textContent = data.pressure + ' hPa';
            document.getElementById('current-wind').textContent = data.windSpeed + ' km/h';
            document.getElementById('current-rain').textContent = data.rainfall + ' mm';
            document.getElementById('current-rain-total').textContent = data.rainfallTotal + ' mm';
            document.getElementById('sample-count').textContent = data.sampleCount + '/900';
        }

        // RRD Daten laden
        async function loadRRDData() {
            const statusEl = document.getElementById('data-status');
            try {
                const response = await fetch('/api/rrd');
                const data = await response.json();
                
                if (data.error) {
                    console.error('RRD Fehler:', data.error);
                    statusEl.className = 'data-status error';
                    statusEl.textContent = '❌ Fehler: ' + data.error;
                    return;
                }
                
                if (!data.timestamps || data.timestamps.length === 0) {
                    console.warn('Keine RRD-Daten verfügbar. Warte auf erste Messungen...');
                    statusEl.className = 'data-status info';
                    statusEl.textContent = '⏳ Keine Daten - Warte auf erste ESP-NOW Messungen (alle 30 Min)...';
                    return;
                }
                
                statusEl.className = 'data-status success';
                statusEl.textContent = '✓ ' + data.timestamps.length + ' Datenpunkte geladen';
                setTimeout(() => { statusEl.style.display = 'none'; }, 5000);
                
                updateCharts(data);
            } catch (error) {
                console.error('Fehler beim Laden der RRD-Daten:', error);
                statusEl.className = 'data-status error';
                statusEl.textContent = '❌ Ladefehler: ' + error.message;
            }
        }

        // Charts aktualisieren
        function updateCharts(data) {
            const labels = data.timestamps.map(ts => {
                const date = new Date(ts * 1000);
                return date.toLocaleString('de-DE', { 
                    day: '2-digit', 
                    month: '2-digit', 
                    hour: '2-digit', 
                    minute: '2-digit' 
                });
            });

            // Temperatur Chart
            updateChart('tempChart', 'Temperatur', labels, data.temperatures, 
                       'rgb(255, 99, 132)', '°C');
            
            // Luftfeuchtigkeit Chart
            updateChart('humChart', 'Luftfeuchtigkeit', labels, data.humidities, 
                       'rgb(54, 162, 235)', '%');
            
            // Luftdruck Chart
            updateChart('pressChart', 'Luftdruck', labels, data.pressures, 
                       'rgb(75, 192, 192)', 'hPa');
            
            // Wind Chart
            updateChart('windChart', 'Windgeschwindigkeit', labels, data.windSpeeds, 
                       'rgb(153, 102, 255)', 'km/h');
            
            // Regen Chart
            updateChart('rainChart', 'Niederschlag', labels, data.rainfalls, 
                       'rgb(255, 159, 64)', 'mm');
            
            // Regen Total Chart
            updateChart('rainTotalChart', 'Niederschlag Kumuliert', labels, data.rainfallTotals, 
                       'rgb(255, 206, 86)', 'mm');
        }

        // Einzelnen Chart aktualisieren/erstellen
        function updateChart(canvasId, label, labels, data, color, unit) {
            const ctx = document.getElementById(canvasId).getContext('2d');
            
            if (charts[canvasId]) {
                charts[canvasId].data.labels = labels;
                charts[canvasId].data.datasets[0].data = data;
                charts[canvasId].update();
            } else {
                charts[canvasId] = new Chart(ctx, {
                    type: 'line',
                    data: {
                        labels: labels,
                        datasets: [{
                            label: label,
                            data: data,
                            borderColor: color,
                            backgroundColor: color.replace('rgb', 'rgba').replace(')', ', 0.1)'),
                            tension: 0.4,
                            fill: true
                        }]
                    },
                    options: {
                        responsive: true,
                        maintainAspectRatio: false,
                        scales: {
                            y: {
                                beginAtZero: false,
                                grid: {
                                    display: true,
                                    color: 'rgba(0, 0, 0, 0.1)',
                                    drawBorder: true,
                                    drawOnChartArea: true,
                                    drawTicks: true
                                },
                                ticks: {
                                    callback: function(value) {
                                        return value + ' ' + unit;
                                    },
                                    font: {
                                        size: 10
                                    }
                                }
                            },
                            x: {
                                grid: {
                                    display: true,
                                    color: 'rgba(0, 0, 0, 0.1)',
                                    drawBorder: true,
                                    drawOnChartArea: true,
                                    drawTicks: true
                                },
                                ticks: {
                                    maxRotation: 45,
                                    minRotation: 45,
                                    font: {
                                        size: 9
                                    },
                                    maxTicksLimit: 8
                                }
                            }
                        },
                        plugins: {
                            legend: {
                                display: false
                            }
                        }
                    }
                });
            }
        }

        // Start
        connectWebSocket();
        loadRRDData(); // Initiale Daten laden
        setInterval(loadRRDData, 60000); // Aktualisiere Charts jede Minute
    </script>
</body>
</html>
)rawliteral");
  });
  
  // API Endpunkt für RRD-Daten
  server.on("/api/rrd", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = readRRDAsJSON();
    request->send(200, "application/json", json);
  });
  
  // API Endpunkt für aktuelle Daten
  server.on("/api/current", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = getCurrentDataJSON();
    request->send(200, "application/json", json);
  });
  
  // API Endpunkt zum Zurücksetzen der RRD-Datei
  server.on("/api/reset_rrd", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("\n>>> API: RRD Reset angefordert");
    if (sdCardInitialized) {
      if (SD.exists(RRD_FILE)) {
        SD.remove(RRD_FILE);
        Serial.println("✓ RRD-Datei gelöscht");
      }
      if (initRRD()) {
        Serial.println("✓ Neue RRD-Datei erstellt");
        request->send(200, "application/json", "{\"success\":true,\"message\":\"RRD zurückgesetzt\"}");
      } else {
        Serial.println("❌ Fehler beim Erstellen der RRD-Datei");
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Fehler beim Erstellen\"}");
      }
    } else {
      Serial.println("❌ SD-Karte nicht verfügbar");
      request->send(500, "application/json", "{\"success\":false,\"message\":\"SD-Karte nicht verfügbar\"}");
    }
  });
  
  // API Endpunkt zum manuellen Speichern (für Tests/Debug)
  server.on("/api/force_save", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("\n>>> API: Manuelles Speichern angefordert");
    if (sampleCount > 0) {
      calculateAndStoreAverages();
      request->send(200, "application/json", 
        "{\"success\":true,\"message\":\"Mittelwert gespeichert\",\"samples\":" + String(sampleCount) + "}");
    } else {
      Serial.println("⚠️  Keine Samples zum Speichern vorhanden");
      request->send(200, "application/json", 
        "{\"success\":false,\"message\":\"Keine Samples vorhanden\",\"samples\":0}");
    }
  });
  
  // API Endpunkt für Debug-Informationen
  server.on("/api/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    unsigned long totalPackets = totalPacketsReceived + totalPacketsRejected;
    float acceptRate = totalPackets > 0 ? (float)totalPacketsReceived / totalPackets * 100.0 : 0;
    
    doc["uptime_minutes"] = millis() / 60000;
    doc["sample_count"] = sampleCount;
    doc["samples_max"] = SAMPLES_PER_PERIOD;
    doc["packets_received"] = totalPacketsReceived;
    doc["packets_rejected"] = totalPacketsRejected;
    doc["packets_total"] = totalPackets;
    doc["accept_rate_percent"] = serialized(String(acceptRate, 1));
    doc["time_since_last_avg_sec"] = (millis() - lastAverageTime) / 1000;
    doc["avg_interval_sec"] = AVERAGE_INTERVAL / 1000;
    doc["sender_mac"] = lastSenderMAC;
    doc["sd_card"] = sdCardInitialized ? "OK" : "ERROR";
    doc["last_packet_ago_sec"] = (millis() - lastPacketTime) / 1000;
    doc["data_available"] = dataAvailable;
    doc["struct_size"] = sizeof(struct_message);
    
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });
  
  server.begin();
  Serial.println("Webserver gestartet!");
  Serial.print("URL: http://");
  Serial.println(WiFi.localIP());
}

// ESP-NOW initialisieren
void initESPNOW() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Initialisierung fehlgeschlagen!");
    return;
  }
  
  Serial.println("ESP-NOW erfolgreich initialisiert");
  
  // Callback registrieren
  esp_now_register_recv_cb(OnDataRecv);
  
  // WiFi-Kanal des Routers verwenden (sollte Kanal 1 sein)
  uint8_t currentChannel = WiFi.channel();
  Serial.printf("ESP-NOW Kanal: %d\n", currentChannel);
}

// Setup
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========================================");
  Serial.println("ESP32 Meteo Garten SD - RRD Logger");
  Serial.println("========================================\n");
  
  // Debug: Struktur-Größen anzeigen
  Serial.printf("Struktur-Größen:\n");
  Serial.printf("  sizeof(RRDHeader) = %d Bytes\n", sizeof(RRDHeader));
  Serial.printf("  sizeof(RRDData) = %d Bytes\n", sizeof(RRDData));
  Serial.printf("  sizeof(time_t) = %d Bytes\n", sizeof(time_t));
  Serial.printf("  sizeof(float) = %d Bytes\n\n", sizeof(float));
  
  // WiFi verbinden
  connectWiFi();
  
  // Zeit synchronisieren
  initTime();
  
  // SD-Karte initialisieren
  if (!initSDCard()) {
    Serial.println("WARNUNG: SD-Karte nicht verfügbar!");
    Serial.println("System läuft weiter, aber Daten werden NICHT gespeichert!");
  } else {
    // RRD initialisieren
    initRRD();
    
    // Optional: Test-Daten einfügen (für sofortige Visualisierung)
    if (FILL_TEST_DATA_ON_STARTUP) {
      fillRRDWithTestData();
    }
  }
  
  // ESP-NOW initialisieren
  initESPNOW();
  
  // Webserver starten
  initWebServer();
  
  // WICHTIG: lastAverageTime wird beim ersten empfangenen Paket gesetzt!
  // So wird sichergestellt, dass immer 30 Min ab dem ersten Sample gezählt werden
  
  Serial.println("\n========================================");
  Serial.println("System bereit!");
  Serial.println("Warte auf ESP-NOW Daten...");
  Serial.println("========================================\n");
}

// Loop
void loop() {
  // WebSocket aufräumen
  ws.cleanupClients();
  
  // DEBUG: Status-Ausgabe alle 60 Sekunden
  static unsigned long lastDebugOutput = 0;
  if (millis() - lastDebugOutput >= 60000) {
    unsigned long timeSinceLastAvg = (millis() - lastAverageTime) / 1000;
    unsigned long totalPackets = totalPacketsReceived + totalPacketsRejected;
    float acceptRate = totalPackets > 0 ? (float)totalPacketsReceived / totalPackets * 100.0 : 0;
    
    Serial.println("\n========== STATUS ==========");
    Serial.printf("Laufzeit: %lu Minuten\n", millis() / 60000);
    Serial.printf("Samples gesammelt: %d/%d\n", sampleCount, SAMPLES_PER_PERIOD);
    Serial.printf("Zeit seit letztem Mittelwert: %lu Sekunden (von %lu)\n", 
                  timeSinceLastAvg, AVERAGE_INTERVAL / 1000);
    Serial.printf("ESP-NOW Pakete empfangen: %lu (akzeptiert) + %lu (verworfen) = %lu gesamt\n", 
                  totalPacketsReceived, totalPacketsRejected, totalPackets);
    Serial.printf("Akzeptanzrate: %.1f%%\n", acceptRate);
    Serial.printf("Letztes Paket vor: %lu Sekunden\n", (millis() - lastPacketTime) / 1000);
    Serial.printf("Sender MAC: %s\n", lastSenderMAC.c_str());
    Serial.printf("SD-Karte: %s\n", sdCardInitialized ? "OK" : "FEHLER");
    
    if (totalPacketsRejected > 0 && acceptRate < 50.0) {
      Serial.println("\n⚠️⚠️⚠️  ACHTUNG: Viele Pakete werden verworfen!");
      Serial.println("Prüfen Sie die struct_message Definition auf Sender & Empfänger!");
    }
    
    Serial.println("============================\n");
    lastDebugOutput = millis();
  }
  
  // Prüfe ob 30 Minuten vergangen sind ODER das Array voll ist
  if (sampleCount >= SAMPLES_PER_PERIOD || 
      (millis() - lastAverageTime >= AVERAGE_INTERVAL && sampleCount > 0)) {
    Serial.println("\n>>> Berechne Mittelwerte und speichere in RRD...");
    calculateAndStoreAverages();
  } else if (millis() - lastAverageTime >= AVERAGE_INTERVAL && sampleCount == 0) {
    // WARNUNG: Zeit abgelaufen, aber keine Daten
    Serial.println("\n⚠️  WARNUNG: 30 Minuten vergangen, aber KEINE Samples empfangen!");
    Serial.println("    Prüfe:");
    Serial.println("    - Läuft der ESP32_Meteo_aussen Sender?");
    Serial.println("    - Ist der Sender auf Kanal 1?");
    Serial.println("    - Ist die Datenstruktur identisch?");
    Serial.printf("    - Pakete empfangen: %lu\n", totalPacketsReceived);
    lastAverageTime = millis();  // Timer zurücksetzen
  }
  
  // Aktuelle Daten per WebSocket senden (alle 5 Sekunden)
  static unsigned long lastWSUpdate = 0;
  if (millis() - lastWSUpdate >= 5000 && dataAvailable) {
    ws.textAll(getCurrentDataJSON());
    lastWSUpdate = millis();
  }
  
  delay(10);
}
