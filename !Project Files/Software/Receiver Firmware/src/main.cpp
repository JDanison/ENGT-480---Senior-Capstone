/* 
  Filename: main.cpp
  Heltec Receiver
  Author: John Danison
  Date Created: 12/6/2025

  Description: Main program file for Capstone Receiver with sensor modules.
               Monitors accelerometer and captures events when threshold exceeded.
*/

#include "main.h"

/**
 * Global Object Definitions
 */
TwoWire I2C_Sensors = TwoWire(1);                           // Secondary I2C bus instance
OLEDDisplay_Module oledDisplay;                             // OLED display instance
SHT45_Module sht45(&I2C_Sensors, SHT45_I2C_ADDRESS);        // SHT45 sensor instance
LIS3DH_Module lis3dh(&I2C_Sensors, LIS3DH_I2C_ADDRESS);     // LIS3DH accelerometer instance
NAU7802_Module nau7802(&I2C_Sensors, NAU7802_I2C_ADDRESS);  // NAU7802 ADC for strain gauges

// SD Card - Initialize SPI on HSPI bus
SPIClass spiSD(HSPI);
SDCard_Module sdCard(&spiSD, SDCARD_CS);
EventLogger_Module eventLogger(&sdCard);
SX1262 loraRadio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);

volatile bool loraPacketReceived = false;

// ===== CONFIGURABLE RUNTIME PARAMETERS =====
unsigned long SENSOR_READ_INTERVAL = 100;       // Default: 100ms
float ACCEL_THRESHOLD = 2.0;                    // Default: 2.0g
unsigned long EVENT_CAPTURE_DURATION_MS = 2000; // Default: 2000ms
unsigned int LAB_TEST_SAMPLE_RATE_HZ = 20;      // Default: 20Hz
// ===========================================

void processSerialCommand(char command);

#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setLoRaFlag(void) {
  loraPacketReceived = true;
}

bool sendLoRaMessage(const String& payload) {
  int txState = loraRadio.transmit(payload.c_str());
  if (txState != RADIOLIB_ERR_NONE) {
    Serial.printf("LoRa TX failed (%d)\n", txState);
    return false;
  }
  return true;
}

void restartLoRaReceive() {
  int rxState = loraRadio.startReceive();
  if (rxState != RADIOLIB_ERR_NONE) {
    Serial.printf("LoRa RX start failed (%d)\n", rxState);
  }
}

void sendCsvLineOverLoRa(const String& line) {
  if (line.length() <= LORA_DATA_CHUNK_SIZE) {
    sendLoRaMessage("DATA:" + line);
    return;
  }

  int index = 0;
  while (index < line.length()) {
    int remaining = line.length() - index;
    int take = remaining > LORA_DATA_CHUNK_SIZE ? LORA_DATA_CHUNK_SIZE : remaining;
    String chunk = line.substring(index, index + take);
    bool isFinalChunk = (index + take) >= line.length();

    if (isFinalChunk) {
      sendLoRaMessage("DATA:" + chunk);
    } else {
      sendLoRaMessage("DATC:" + chunk);
    }

    index += take;
    delay(10);
  }
}

bool streamStoredEventsOverLoRa() {
  if (!sdCard.isInitialized() || !sdCard.fileExists("/events")) {
    return false;
  }

  File root = SD.open("/events");
  if (!root || !root.isDirectory()) {
    return false;
  }

  bool sentAnyLine = false;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String filename = String(file.name());
      String baseName = filename;
      int slashIdx = baseName.lastIndexOf('/');
      if (slashIdx >= 0 && slashIdx < (baseName.length() - 1)) {
        baseName = baseName.substring(slashIdx + 1);
      }

      if (baseName.startsWith("event ") && baseName.endsWith(".csv")) {
        while (file.available()) {
          String line = file.readStringUntil('\n');
          line.replace("\r", "");
          line.trim();
          if (line.length() == 0 || line.startsWith("timestamp,")) {
            continue;
          }

          sendCsvLineOverLoRa(line);
          sentAnyLine = true;
          delay(15);
        }
      }
      file.close();
    } else {
      file.close();
    }
    file = root.openNextFile();
  }

  root.close();
  return sentAnyLine;
}

bool saveTruckInfoToSd(const String& truckId, const String& description, bool includeTruckId, bool includeDescription) {
  if (!includeTruckId && !includeDescription) {
    return true;
  }

  if (!sdCard.isInitialized()) {
    Serial.println("Truck info not saved: SD card not initialized.");
    return false;
  }

  String content = "# Truck Info\n";
  content += "updated=" + getFormattedTime() + "\n";
  content += "include_truck_id=" + String(includeTruckId ? "1" : "0") + "\n";
  content += "include_description=" + String(includeDescription ? "1" : "0") + "\n";
  content += "truck_id=" + String(includeTruckId ? truckId : "") + "\n";
  content += "description=" + String(includeDescription ? description : "") + "\n";

  bool ok = sdCard.writeFile("/truck info/truck_id.txt", content.c_str(), false);
  if (ok) {
    Serial.println("Truck info saved: /truck info/truck_id.txt");
  } else {
    Serial.println("Truck info save failed.");
  }
  return ok;
}

bool parseSetupPacket(const String& packet) {
  if (!packet.startsWith("SETUP:")) {
    return false;
  }

  String data = packet.substring(6);
  data.trim();

  unsigned long nextInterval = SENSOR_READ_INTERVAL;
  float nextThreshold = ACCEL_THRESHOLD;
  unsigned int nextSampleRate = LAB_TEST_SAMPLE_RATE_HZ;
  unsigned long nextDuration = EVENT_CAPTURE_DURATION_MS;
  bool includeTruckId = false;
  bool includeDescription = false;
  String truckId = "";
  String description = "";

  int start = 0;
  while (start < data.length()) {
    int sep = data.indexOf(';', start);
    if (sep < 0) {
      sep = data.length();
    }

    String token = data.substring(start, sep);
    int eq = token.indexOf('=');
    if (eq > 0) {
      String key = token.substring(0, eq);
      String value = token.substring(eq + 1);
      key.trim();
      value.trim();

      if (key == "si") {
        unsigned long v = value.toInt();
        if (v < 1 || v > 10000) {
          Serial.println("ERROR: Sensor interval out of range (1-10000 ms)");
          return false;
        }
        nextInterval = v;
      } else if (key == "thr") {
        float v = value.toFloat();
        if (v <= 0.0f || v > 10.0f) {
          Serial.println("ERROR: Event trigger threshold out of range (0-10 g]");
          return false;
        }
        nextThreshold = v;
      } else if (key == "sr") {
        int v = value.toInt();
        if (v != 10 && v != 20) {
          Serial.println("ERROR: Sample rate must be 10 or 20 Hz");
          return false;
        }
        nextSampleRate = (unsigned int)v;
      } else if (key == "dur") {
        unsigned long v = value.toInt();
        if (v < 1 || v > 10000) {
          Serial.println("ERROR: Event capture duration out of range (1-10000 ms)");
          return false;
        }
        nextDuration = v;
      } else if (key == "ti") {
        includeTruckId = (value == "1");
      } else if (key == "tid") {
        truckId = value;
      } else if (key == "di") {
        includeDescription = (value == "1");
      } else if (key == "desc") {
        description = value;
      }
    }

    start = sep + 1;
  }

  SENSOR_READ_INTERVAL = nextInterval;
  ACCEL_THRESHOLD = nextThreshold;
  LAB_TEST_SAMPLE_RATE_HZ = nextSampleRate;
  EVENT_CAPTURE_DURATION_MS = nextDuration;

  Serial.println("SETUP applied:");
  Serial.printf("  SENSOR_READ_INTERVAL: %lu ms\n", SENSOR_READ_INTERVAL);
  Serial.printf("  EVENT_TRIGGER_THRESHOLD: %.3f g\n", ACCEL_THRESHOLD);
  Serial.printf("  LAB_TEST_SAMPLE_RATE_HZ: %u Hz\n", LAB_TEST_SAMPLE_RATE_HZ);
  Serial.printf("  EVENT_CAPTURE_DURATION_MS: %lu ms\n", EVENT_CAPTURE_DURATION_MS);

  if (!saveTruckInfoToSd(truckId, description, includeTruckId, includeDescription)) {
    Serial.println("SETUP warning: truck info requested but not saved.");
  }

  return true;
}

/**
 * Apply configuration (called after successful parsing)
 * Can reconfigure I2C bus or other sensors here if needed
 */
void applyConfiguration() {
  Serial.println("\n✓ Configuration applied successfully!");
  Serial.println("Unit is now using new parameters.");
}

void handleLoRaCommandPacket(const String& packet) {
  if (!packet.startsWith("CMD:") || packet.length() != 5) {
    // Ignore malformed or unrelated packets to avoid serial spam.
    return;
  }

  char command = packet.charAt(4);
  Serial.printf("LoRa CMD received: %c\n", command);

  if (command == 'd' || command == 'D') {
    sendLoRaMessage("RSP:BEGIN_D");
    bool sentData = streamStoredEventsOverLoRa();
    if (!sentData) {
      sendLoRaMessage("RSP:NO_DATA");
    }
    sendLoRaMessage("END:D");
    return;
  }

  // Safety hardening: only allow remote data-dump command over LoRa.
  // All other commands stay local via receiver USB serial.
  sendLoRaMessage("RSP:ERR_UNSUPPORTED");
}

void processLoRaPackets() {
  if (!loraPacketReceived) {
    return;
  }

  loraPacketReceived = false;
  String packet;
  int rxState = loraRadio.readData(packet);
  if (rxState == RADIOLIB_ERR_NONE) {
    packet.trim();
    if (packet.startsWith("CMD:") && packet.length() == 5) {
      handleLoRaCommandPacket(packet);
    } else if (packet.startsWith("SETUP:")) {
      if (parseSetupPacket(packet)) {
        applyConfiguration();
        sendLoRaMessage("RSP:SETUP_OK");
      } else {
        sendLoRaMessage("RSP:SETUP_ERR");
      }
    }
  } else {
    Serial.printf("LoRa RX read failed (%d)\n", rxState);
  }

  restartLoRaReceive();
}

/**
 * Circular buffer for continuous accelerometer data capture
 * This allows us to capture data BEFORE and AFTER the threshold trigger
 */
struct AccelSample {
  float x;
  float y;
  float z;
  unsigned long timestamp;
};

#define BUFFER_SIZE 20
AccelSample accelBuffer[BUFFER_SIZE];
int bufferIndex = 0;
bool bufferFilled = false;

// WiFi connection timeouts
#define WIFI_CONNECT_TIMEOUT 10  // seconds
#define NTP_SYNC_TIMEOUT 10      // seconds

// Add sample to circular buffer
void addToBuffer(float x, float y, float z) {
  accelBuffer[bufferIndex].x = x;
  accelBuffer[bufferIndex].y = y;
  accelBuffer[bufferIndex].z = z;
  accelBuffer[bufferIndex].timestamp = millis();
  
  bufferIndex++;
  if (bufferIndex >= BUFFER_SIZE) {
    bufferIndex = 0;
    bufferFilled = true;
  }
}

/**
 * Connect to WiFi (try primary, then backup)
 * Returns true if connected, false otherwise
 */
bool connectToWiFi() {
  // Try primary WiFi first
  Serial.printf("Trying primary WiFi: %s\n", WIFI_SSID_PRIMARY);
  WiFi.begin(WIFI_SSID_PRIMARY, WIFI_PASSWORD_PRIMARY);
  
  int timeout = WIFI_CONNECT_TIMEOUT;
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(1000);
    Serial.print(".");
    timeout--;
  }
  Serial.println();
  
  // If primary failed, try backup
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Primary WiFi failed, trying backup...");
    Serial.printf("Connecting to backup WiFi: %s\n", WIFI_SSID_BACKUP);
    
    WiFi.disconnect();
    delay(100);
    WiFi.begin(WIFI_SSID_BACKUP, WIFI_PASSWORD_BACKUP);
    
    timeout = WIFI_CONNECT_TIMEOUT;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
      delay(1000);
      Serial.print(".");
      timeout--;
    }
    Serial.println();
  }
  
  // Check if either connection succeeded
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Both WiFi networks failed!");
    return false;
  }
  
  Serial.println("WiFi connected!");
  Serial.printf("Connected to: %s\n", WiFi.SSID().c_str());
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  return true;
}

/**
 * Sync time via WiFi using NTP
 */
bool syncTime() {
  Serial.println("\n=== TIME SYNC STARTING ===");
  
  if (!connectToWiFi()) {
    Serial.println("Time sync FAILED");
    return false;
  }
  
  // Configure time with NTP
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  Serial.println("Waiting for NTP time sync...");
  
  // Wait for time to be set
  struct tm timeinfo;
  int timeout = NTP_SYNC_TIMEOUT;
  while (!getLocalTime(&timeinfo) && timeout > 0) {
    delay(1000);
    Serial.print(".");
    timeout--;
  }
  Serial.println();
  
  if (timeout == 0) {
    Serial.println("Failed to obtain time from NTP");
    WiFi.disconnect(true);
    return false;
  }
  
  Serial.println("Time synced successfully!");
  Serial.print("Current time: ");
  Serial.println(getFormattedTime());
  
  // Disconnect WiFi to save power
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi disconnected to save power");
  Serial.println("=== TIME SYNC COMPLETE ===\n");
  
  return true;
}

/**
 * Set time manually from serial input
 * Format: YYYY-MM-DD HH:MM:SS
 * Example: setTime("2025-12-10 14:30:00")
 */
bool setTimeManually(const char* dateTimeStr) {
  struct tm timeinfo = {0};
  
  // Parse the date/time string
  int year, month, day, hour, minute, second;
  if (sscanf(dateTimeStr, "%d-%d-%d %d:%d:%d", 
             &year, &month, &day, &hour, &minute, &second) != 6) {
    Serial.println("Error: Invalid format. Use: YYYY-MM-DD HH:MM:SS");
    return false;
  }
  
  // Validate ranges
  if (year < 2000 || year > 2100 || month < 1 || month > 12 || 
      day < 1 || day > 31 || hour < 0 || hour > 23 || 
      minute < 0 || minute > 59 || second < 0 || second > 59) {
    Serial.println("Error: Date/time values out of range");
    return false;
  }
  
  // Fill tm structure
  timeinfo.tm_year = year - 1900;  // Years since 1900
  timeinfo.tm_mon = month - 1;      // Months since January (0-11)
  timeinfo.tm_mday = day;
  timeinfo.tm_hour = hour;
  timeinfo.tm_min = minute;
  timeinfo.tm_sec = second;
  
  // Convert to time_t
  time_t t = mktime(&timeinfo);
  
  // Set the system time
  struct timeval now = { .tv_sec = t };
  settimeofday(&now, NULL);
  
  Serial.println("Time set successfully!");
  Serial.print("Current time: ");
  Serial.println(getFormattedTime());
  
  return true;
}

/**
 * Get formatted timestamp string
 */
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time not set";
  }
  
  char buffer[64];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S EST", &timeinfo);
  return String(buffer);
}

/**
 * Delete all event files from SD card
 */
void deleteAllEventFiles() {
  if (!sdCard.isInitialized()) {
    Serial.println("SD card is not initialized. Cannot clear files.");
    return;
  }

  // Delete event files
  if (sdCard.fileExists("/events")) {
    if (sdCard.deleteAllFilesInDirectory("/events")) {
      Serial.println("All event files deleted.");
    } else {
      Serial.println("Some event files could not be deleted.");
    }
  } else {
    Serial.println("No events directory found.");
  }
  
  // Delete lab-testing files
  if (sdCard.fileExists("/lab-testing")) {
    if (sdCard.deleteAllFilesInDirectory("/lab-testing")) {
      Serial.println("All lab-testing files deleted.");
    } else {
      Serial.println("Some lab-testing files could not be deleted.");
    }
  } else {
    Serial.println("No lab-testing directory found.");
  }
}

/**
 * Offload data: Playback events, resync time, and clear SD card
 */
void offloadData() {
  Serial.println("\n");
  Serial.println("========================================");
  Serial.println("        DATA OFFLOAD INITIATED");
  Serial.println("========================================\n");
  
  // Step 1: Playback all events
  playbackEvents();
  
  // Step 2: Resync time
  Serial.println("\n--- Resyncing Time ---");
  syncTime();
  
  // Step 3: Clear SD card
  Serial.println("\n--- Clearing SD Card ---");
  deleteAllEventFiles();
  
  Serial.println("\n========================================");
  Serial.println("        DATA OFFLOAD COMPLETE");
  Serial.println("========================================\n");
}

/**
 * Event capture function
 * Called when accelerometer threshold is exceeded
 * FAST: Captures paired samples immediately, THEN formats and saves
 */
void captureEvent(float triggerX, float triggerY, float triggerZ) {
  unsigned long captureStart = millis();
  
  // Create temporary array to store samples during fast capture
  EventLogger_Module::EventSample eventSamples[EVENT_MAX_SAMPLES];
  int sampleCount = 1;
  
  // Store trigger sample as first sample
  eventSamples[0].x = triggerX;
  eventSamples[0].y = triggerY;
  eventSamples[0].z = triggerZ;
  int32_t triggerStrainRaw = nau7802.readRaw();
  int32_t triggerStrainZeroed = triggerStrainRaw - nau7802.getZeroOffset();
  eventSamples[0].strainMicro = nau7802.calculateStrain(triggerStrainZeroed, 3.3, 2.0) * 1000000.0;
  
  Serial.printf("\n!!! EVENT TRIGGERED !!! Capturing for %d ms...", EVENT_CAPTURE_DURATION_MS);
  
  // PAIRED CAPTURE: Collect accel + strain pairs for a fixed duration (1:1 pairing)
  while ((millis() - captureStart) < EVENT_CAPTURE_DURATION_MS && sampleCount < EVENT_MAX_SAMPLES) {
    bool accelOk = lis3dh.read();
    int i = sampleCount;

    if (accelOk) {
      eventSamples[i].x = lis3dh.getX();
      eventSamples[i].y = lis3dh.getY();
      eventSamples[i].z = lis3dh.getZ();
    } else {
      eventSamples[i].x = 0.0;
      eventSamples[i].y = 0.0;
      eventSamples[i].z = 0.0;
    }

    int32_t strainRaw = nau7802.readRaw();
    int32_t strainZeroed = strainRaw - nau7802.getZeroOffset();
    eventSamples[i].strainMicro = nau7802.calculateStrain(strainZeroed, 3.3, 2.0) * 1000000.0;

    sampleCount++;
    Serial.print(".");
  }

  if (sampleCount >= EVENT_MAX_SAMPLES) {
    Serial.print(" [MAX BUFFER REACHED]");
  }
  
  unsigned long captureTime = millis() - captureStart;
  Serial.printf(" Done! (%lums)\n", captureTime);
  
  // NOW do the slow operations (SD card, formatting, etc.)
  Serial.println("Saving to SD card...");
  unsigned long saveStart = millis();
  
  // Read temperature and humidity
  float temp = 0.0, humidity = 0.0;
  if (sht45.read()) {
    temp = sht45.getTemperature();
    humidity = sht45.getHumidity();
  }
  
  // Save CSV data row only (no header row)
  String savedFilename;
  bool writeOk = eventLogger.saveEventCsv(eventSamples,
                                          sampleCount,
                                          temp,
                                          humidity,
                                          getFormattedTime(),
                                          nullptr,
                                          &savedFilename);
  
  unsigned long saveTime = millis() - saveStart;
  unsigned long totalTime = millis() - captureStart;
  
  if (writeOk) {
    Serial.printf("Saved to: %s\n", savedFilename.c_str());
  } else {
    Serial.printf("Failed to save event file: %s\n", savedFilename.c_str());
  }
  Serial.printf("Capture: %lums, Save: %lums, Total: %lums\n\n", captureTime, saveTime, totalTime);
}

/**
 * Playback all saved events from SD card
 * Called during setup to show previous events
 */
void playbackEvents() {
  if (!sdCard.isInitialized()) {
    Serial.println("SD card is not initialized. Cannot playback events.\n");
    return;
  }
  
  // Check if events directory exists
  if (!sdCard.fileExists("/events")) {
    Serial.println("No events directory found. No previous events.\n");
    return;
  }
  
  bool foundEvents = sdCard.printCsvDataRows("/events", "event ");
  
  if (!foundEvents) {
    Serial.println("No previous events found.\n");
  }
}

void setup() {
  // Initialize Serial
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);
  Serial.println("\n\n=== Heltec Capstone Receiver Starting ===\n");

  Serial.println("Initializing LoRa radio...");
  int loraState = loraRadio.begin(LORA_FREQUENCY_MHZ,
                                  LORA_BANDWIDTH_KHZ,
                                  LORA_SPREADING_FACTOR,
                                  LORA_CODING_RATE,
                                  LORA_SYNC_WORD,
                                  LORA_TX_POWER_DBM,
                                  LORA_PREAMBLE_LEN);
  if (loraState == RADIOLIB_ERR_NONE) {
    loraRadio.setDio1Action(setLoRaFlag);
    restartLoRaReceive();
    Serial.println("LoRa: OK");
  } else {
    Serial.printf("LoRa: FAILED (%d)\n", loraState);
  }

  // Initialize OLED Display - DISABLED for performance
  /*
  Serial.println("Initializing OLED Display...");
  if (oledDisplay.begin()) {
    Serial.println("OLED: OK");
  } else {
    Serial.println("OLED: FAILED");
  }
  */
  
  // Initialize secondary I2C bus for external sensors
  Serial.printf("\nInitializing I2C Sensor Bus (GPIO %d/%d @ %dkHz)...\n", 
                I2C_SENSOR_SDA_PIN, I2C_SENSOR_SCL_PIN, I2C_SENSOR_FREQ/1000);
  I2C_Sensors.begin(I2C_SENSOR_SDA_PIN, I2C_SENSOR_SCL_PIN, I2C_SENSOR_FREQ);
  I2C_Sensors.setTimeout(I2C_TIMEOUT);
  
  // Initialize SHT45 Temperature/Humidity Sensor
  Serial.println("\nInitializing SHT45 Sensor...");
  if (sht45.begin()) {
    Serial.println("SHT45: OK");
  } else {
    Serial.println("SHT45: FAILED");
  }
  
  // Initialize LIS3DH Accelerometer
  Serial.println("\nInitializing LIS3DH Sensor...");
  if (lis3dh.begin()) {
    Serial.println("LIS3DH: OK");
  } else {
    Serial.println("LIS3DH: FAILED");
  }

  // Initialize NAU7802 ADC for Strain Gauges
  Serial.println("\nInitializing NAU7802 ADC...");
  if (nau7802.begin()) {
    Serial.println("NAU7802: OK");
    
    // Tare the ADC (zero it)
    Serial.println("Taring strain gauge ADC");
    nau7802.tare(200);
    Serial.println("NAU7802: Ready for measurements");
  } else {
    Serial.println("NAU7802: FAILED");
  }

  // Initialize SD Card
  Serial.println();
  spiSD.begin(SDCARD_SCK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);
  if (sdCard.begin()) {
    // Playback previous events
    playbackEvents();
  } else {
    Serial.println("SD Card initialization failed. Events will not be saved.");
  }
  
  Serial.println("\n=== Setup Complete ===");
  Serial.println("Monitoring accelerometer for threshold events...");
  Serial.printf("Threshold: %.1fg on any axis\n", ACCEL_THRESHOLD);
  Serial.println("\n--- Serial Commands ---");
  Serial.println("  s - Sync time via WiFi (requires WiFi credentials in main.h)");
  Serial.println("  t - Display current time");
  Serial.println("  d - Display all stored events");
  Serial.println("  c - Clear all events from SD card");
  Serial.println("  o - Offload data (playback events, resync time, clear SD)");
  Serial.println("  g - Read single strain gauge sample");
  Serial.println("  z - Tare/zero the strain gauge");
  Serial.println("  r - Restart NAU7802 conversions (if timeouts occur)");
  Serial.println("  m - Monitor strain continuously (press any key to stop)");
  Serial.println("  l - Lab test: Log strain readings to SD card (press any key to stop)");
  Serial.println("  b - Bridge balance and sensitivity test");
  Serial.println("  1-4 - Test with gain 1x, 2x, 4x, 8x (temporary)");
  Serial.println("-----------------------\n");
  delay(2000);
}

/**
 * Process serial commands
 */
void processSerialCommand(char command) {
  switch (command) {
    case 'c':
    case 'C':
      Serial.println("\n=== CLEARING SD CARD ===");
      deleteAllEventFiles();
      Serial.println("=== SD CARD CLEARED ===\n");
      break;
      
    case 's':
    case 'S':
      syncTime();
      break;
      
    case 'o':
    case 'O':
      offloadData();
      break;
      
    case 't':
    case 'T':
      Serial.print("Current time: ");
      Serial.println(getFormattedTime());
      break;
      
    case 'd':
    case 'D':
      playbackEvents();
      break;
      
    case 'g':
    case 'G':
      {
        Serial.println("\n=== STRAIN GAUGE READING ===");
        
        // Take multiple readings with different methods
        Serial.println("Raw single sample:");
        int32_t raw = nau7802.readRaw();
        Serial.printf("  Single:    %8ld\n", raw);
        
        Serial.println("\nFiltered readings (10 samples each):");
        int32_t avg = nau7802.readAverage(10);
        int32_t median = nau7802.readMedian(9);
        int32_t filtered = nau7802.readFiltered(10);
        
        Serial.printf("  Average:   %8ld\n", avg);
        Serial.printf("  Median:    %8ld\n", median);
        Serial.printf("  Filtered:  %8ld (outliers removed)\n", filtered);
        
        // Show zeroed readings
        int32_t reading = nau7802.getReading(); // Uses single raw read
        float voltage = nau7802.calculateVoltage(filtered);
        
        Serial.println("\nZeroed values:");
        Serial.printf("  Raw zeroed:      %8ld\n", reading);
        Serial.printf("  Filtered zeroed: %8ld\n", filtered - raw + reading);
        Serial.printf("  Offset applied:  %8ld\n", raw - reading);
        Serial.printf("  Output voltage:  %.6f V (%.3f mV)\n", voltage, voltage * 1000.0);
        
        // Check if offset looks suspicious
        if (abs(reading) > abs(raw)) {
          Serial.println("⚠️  WARNING: Zeroed reading larger than raw!");
          Serial.println("⚠️  You may need to tare the sensor (press 'z')");
        }
        
        // Example strain calculation (assuming 3.3V excitation and GF=2.0)
        float strain = nau7802.calculateStrain(filtered - raw + reading, 3.3, 2.0);
        float microstrain = strain * 1000000.0; // Convert to microstrain
        Serial.printf("\nEstimated Strain: %.2f με (microstrain)\n", microstrain);
        
        // Interpret the strain value
        if (abs(microstrain) < 100) {
          Serial.println("✅ Strain looks good (near zero, no load)");
        } else if (abs(microstrain) < 500) {
          Serial.println("⚠️  Moderate strain detected");
        } else {
          Serial.println("❌ High strain! Check tare or applied load");
        }
        
        Serial.println("==============================\n");
      }
      break;
      
    case 'z':
    case 'Z':
      {
        Serial.println("\n=== TARING STRAIN GAUGE ===");
        Serial.println("Taking 200 samples for tare...");
        if (nau7802.tare(200)) {
          Serial.println("Strain gauge zeroed successfully!");
        } else {
          Serial.println("Failed to zero strain gauge!");
        }
        Serial.println("===========================\n");
      }
      break;
      
    case 'r':
    case 'R':
      {
        Serial.println("\n=== RESTARTING NAU7802 ===");
        nau7802.restartConversions();
        Serial.println("===========================\n");
      }
      break;
      
    case '1':
    case '2':
    case '3':
    case '4':
      {
        // Test different gain settings
        NAU7802_Gain testGain;
        int gainValue = 1;
        
        switch(command) {
          case '1': testGain = NAU7802_GAIN_1; gainValue = 1; break;
          case '2': testGain = NAU7802_GAIN_2; gainValue = 2; break;
          case '3': testGain = NAU7802_GAIN_4; gainValue = 4; break;
          case '4': testGain = NAU7802_GAIN_8; gainValue = 8; break;
        }
        
        Serial.printf("\n=== TESTING GAIN %dx ===\n", gainValue);
        nau7802.setGain(testGain);
        delay(100);
        
        Serial.println("Taking 5 samples:");
        for (int i = 0; i < 5; i++) {
          int32_t raw = nau7802.readRaw();
          float percent = (raw / 8388608.0) * 100.0;
          Serial.printf("  Sample %d: %8ld (%.2f%% FS)", i+1, raw, percent);
          if (raw >= 8388600 || raw <= -8388600) {
            Serial.print(" ❌ SATURATED!");
          }
          Serial.println();
          delay(100);
        }
        
        // Restore gain to 128x
        nau7802.setGain(NAU7802_GAIN_128);
        Serial.println("\nGain restored to 128x");
        Serial.println("===========================\n");
      }
      break;
      
    case 'm':
    case 'M':
      {
        Serial.println("\n=== CONTINUOUS STRAIN MONITORING ===");
        Serial.println("[M_SESSION_START]");
        Serial.println("Monitoring strain in real-time...");
        Serial.println("Apply load to the strain gauge now!");
        Serial.println("Press any key to stop.\n");
        Serial.println("Time(s), SampleMs, Raw, Avg(20), Filtered(20), Zeroed, Strain(με)");
        Serial.println("---------------------------------------------------------------------------------");

        // Clear any pending serial bytes (e.g., newline after command input)
        while (Serial.available()) {
          Serial.read();
        }
        
        unsigned long startTime = millis();
        int sampleCount = 0;
        
        while (!Serial.available()) {
          unsigned long sampleStart = millis();

          // Use heavy averaging to reduce noise: 20 samples with outlier rejection
          int32_t raw = nau7802.readRaw();
          int32_t avg = nau7802.readAverage(20);      // Simple average
          int32_t filtered = nau7802.readFiltered(20); // Outlier rejection
          int32_t zeroed = filtered - nau7802.getZeroOffset(); // Apply tare offset
          float strain = nau7802.calculateStrain(zeroed, 3.3, 2.0);
          float microstrain = strain * 1000000.0;
          
          float elapsedTime = (millis() - startTime) / 1000.0;
          unsigned long sampleMs = millis() - sampleStart;
          
          Serial.printf("%.2f, %8lu, %8ld, %8ld, %8ld, %8ld, %9.2f", 
                       elapsedTime, sampleMs, raw, avg, filtered, zeroed, microstrain);
          
          // Add visual indicator for high strain
          if (abs(microstrain) > 50) {
            Serial.print(" ← STRAIN DETECTED!");
          }
          Serial.println();
          
          sampleCount++;
          delay(100); // Small delay between readings
        }
        
        // Clear the serial buffer
        while (Serial.available()) Serial.read();
        
        Serial.println("---------------------------------------------------------------------------------");
        Serial.printf("Monitoring stopped. Collected %d samples.\n", sampleCount);
        Serial.println("[M_SESSION_END]");
        Serial.println("===========================\n");
      }
      break;
      
    case 'b':
    case 'B':
      {
        Serial.println("\n=== BRIDGE BALANCE TEST ===");
        Serial.println("Testing Wheatstone bridge configuration...\n");
        
        // Take multiple readings
        Serial.println("Taking 10 raw ADC samples:");
        int64_t sum = 0;
        int32_t minVal = 2147483647;
        int32_t maxVal = -2147483648;
        
        for (int i = 0; i < 10; i++) {
          int32_t raw = nau7802.readRaw();
          Serial.printf("  Sample %d: %8ld\n", i+1, raw);
          sum += raw;
          if (raw < minVal) minVal = raw;
          if (raw > maxVal) maxVal = raw;
          delay(50);
        }
        
        int32_t avg = sum / 10;
        int32_t range = maxVal - minVal;
        float percentFS = (abs(avg) / 8388608.0) * 100.0;
        
        Serial.println("\n--- Analysis ---");
        Serial.printf("Average:    %ld\n", avg);
        Serial.printf("Min:        %ld\n", minVal);
        Serial.printf("Max:        %ld\n", maxVal);
        Serial.printf("Range:      %ld (noise)\n", range);
        Serial.printf("% Full Scale: %.2f%%\n", percentFS);
        
        Serial.println("\n--- Bridge Status ---");
        if (abs(avg) < 100000) {
          Serial.println("✓ Bridge is well balanced!");
        } else if (abs(avg) < 1000000) {
          Serial.println("⚠ Bridge has moderate offset (normal)");
        } else if (abs(avg) < 4000000) {
          Serial.println("⚠ Bridge has large offset (acceptable)");
        } else {
          Serial.println("❌ Bridge severely unbalanced or gain too high!");
        }
        
        if (range < 1000) {
          Serial.println("✓ Low noise - good signal quality");
        } else if (range < 10000) {
          Serial.println("⚠ Moderate noise");
        } else {
          Serial.println("❌ High noise - check connections!");
        }
        
        Serial.println("\n--- Sensitivity Test ---");
        Serial.println("Now apply a small load and watch for changes...");
        Serial.println("Monitoring for 5 seconds:");
        
        int32_t baseline = nau7802.readAverage(10);
        Serial.printf("Baseline (no load): %ld\n\n", baseline);
        
        for (int i = 0; i < 50; i++) {
          int32_t current = nau7802.readRaw();
          int32_t delta = current - baseline;
          Serial.printf("  t=%.1fs: %8ld (Δ=%+8ld)", i * 0.1, current, delta);
          
          if (abs(delta) > 1000) {
            Serial.print(" ← CHANGE DETECTED!");
          }
          Serial.println();
          delay(100);
        }
        
        Serial.println("\n===========================\n");
      }
      break;
      
    case 'l':
    case 'L':
      {
        Serial.println("\n=== LAB TEST: CONTINUOUS STRAIN LOGGING ===");
        Serial.printf("Sample Rate: %d Hz\n", LAB_TEST_SAMPLE_RATE_HZ);
        Serial.println("[LOG_START]");
        Serial.println("Recording raw strain gauge data...");
        Serial.println("Apply load now. Press any key to stop.\n");
        Serial.println("Time(s), Raw, Zeroed, Strain(με)");
        Serial.println("---------------------------------------");
        
        // Clear any pending serial bytes
        while (Serial.available()) {
          Serial.read();
        }
        
        // Allocate dynamic array for storing samples (max 10000 samples = ~16 minutes at 10Hz)
        const int MAX_SAMPLES = 10000;
        struct StrainSample {
          float time;
          int32_t raw;
          int32_t zeroed;
          float microstrain;
        };
        
        StrainSample* samples = new StrainSample[MAX_SAMPLES];
        
        unsigned long startTime = millis();
        int sampleCount = 0;
        int sampleDelay = 1000 / LAB_TEST_SAMPLE_RATE_HZ; // Calculate delay from sample rate
        
        // Fast data acquisition loop - NO SD card writes!
        while (!Serial.available() && sampleCount < MAX_SAMPLES) {
          // Read RAW value only - fastest method
          int32_t raw = nau7802.readRaw();
          int32_t zeroed = raw - nau7802.getZeroOffset();
          float strain = nau7802.calculateStrain(zeroed, 3.3, 2.0);
          float microstrain = strain * 1000000.0;
          float elapsedTime = (millis() - startTime) / 1000.0;
          
          // Store in memory
          samples[sampleCount].time = elapsedTime;
          samples[sampleCount].raw = raw;
          samples[sampleCount].zeroed = zeroed;
          samples[sampleCount].microstrain = microstrain;
          
          // Display to serial
          Serial.printf("%.2f, %8ld, %8ld, %9.2f\n", elapsedTime, raw, zeroed, microstrain);
          
          sampleCount++;
          delay(sampleDelay); // Delay based on LAB_TEST_SAMPLE_RATE_HZ
        }
        
        // Clear the serial buffer
        while (Serial.available()) Serial.read();
        
        Serial.println("---------------------------------------");
        Serial.printf("Monitoring stopped. Collected %d samples.\n", sampleCount);
        Serial.println("\nSaving to SD card...");
        
        // NOW save everything to SD card
        int logNumber = sdCard.getNextEventNumber("/lab-testing", "strain-log");
        char filename[64];
        snprintf(filename, sizeof(filename), "/lab-testing/strain-log%d.txt", logNumber);
        
        // Build complete file content
        String fileContent;
        fileContent.reserve(sampleCount * 50 + 512); // Pre-allocate memory
        
        // Header
        fileContent = "=== STRAIN GAUGE LAB TEST LOG " + String(logNumber) + " ===\n";
        fileContent += "Timestamp: " + getFormattedTime() + "\n";
        fileContent += "Sample Rate: " + String(LAB_TEST_SAMPLE_RATE_HZ) + " Hz\n";
        fileContent += "Gain: 32x\n";
        fileContent += "Samples: " + String(sampleCount) + "\n";
        fileContent += "Duration: " + String((millis() - startTime) / 1000.0) + " seconds\n";
        fileContent += "\nTime(s), Raw, Zeroed, Strain(με)\n";
        fileContent += "---------------------------------------\n";
        
        // All data samples
        for (int i = 0; i < sampleCount; i++) {
          char line[64];
          snprintf(line, sizeof(line), "%.2f, %ld, %ld, %.2f\n",
                   samples[i].time, samples[i].raw, samples[i].zeroed, samples[i].microstrain);
          fileContent += line;
        }
        
        // Footer
        fileContent += "---------------------------------------\n";
        fileContent += "[LOG_END]\n";
        
        // Write entire file at once
        sdCard.writeFile(filename, fileContent.c_str(), false);
        
        Serial.printf("Data saved to: %s\n", filename);
        Serial.println("[LOG_END]");
        Serial.println("===========================\n");
        
        // Free memory
        delete[] samples;
      }
      break;
      
    default:
      // Ignore unknown commands
      break;
  }
}

void loop() {
  // Handle incoming command packets from transmitter
  processLoRaPackets();

  // Check for serial commands and setup packets
  if (Serial.available() > 0) {
    if (Serial.peek() == 'S') {
      String setupLine = Serial.readStringUntil('\n');
      setupLine.trim();

      if (setupLine.startsWith("SETUP:")) {
        if (parseSetupPacket(setupLine)) {
          applyConfiguration();
          sendLoRaMessage("RSP:SETUP_OK");
        } else {
          Serial.println("SETUP parse error");
        }
        return;
      }

      if (setupLine.length() == 1) {
        processSerialCommand(setupLine.charAt(0));
        return;
      }
    }

    char command = Serial.read();
    processSerialCommand(command);
  }
  
  // Read temperature and humidity
  float temp = 0.0, humidity = 0.0;
  sht45.read(); // Read even if it fails, will use default values
  temp = sht45.getTemperature();
  humidity = sht45.getHumidity();
  
  // Read accelerometer
  if (lis3dh.read()) {
    float accelX = lis3dh.getX();
    float accelY = lis3dh.getY();
    float accelZ = lis3dh.getZ();
    
    // Add current reading to circular buffer
    addToBuffer(accelX, accelY, accelZ);
    
    // OLED update - DISABLED for performance
    /*
    oledDisplay.displaySensorData(
      temp,
      humidity,
      accelX, accelY, accelZ
    );
    */
    
    // Check if any axis exceeds threshold
    if (abs(accelX) > ACCEL_THRESHOLD || 
        abs(accelY) > ACCEL_THRESHOLD || 
        abs(accelZ) > ACCEL_THRESHOLD) {
      
      // Trigger event capture - will read from the buffer (contains recent history)
      captureEvent(accelX, accelY, accelZ);
    }
    
    // Delay for loop timing - gives display time to refresh
    delay(SENSOR_READ_INTERVAL);
  } else {
    Serial.println("Failed to read LIS3DH!");
    delay(SENSOR_READ_INTERVAL);
  }
}