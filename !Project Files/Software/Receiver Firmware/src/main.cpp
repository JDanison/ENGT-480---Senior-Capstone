/* 
  Filename: main.cpp
  Heletec Receiver
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

// SD Card - Initialize SPI on HSPI bus
SPIClass spiSD(HSPI);
SDCard_Module sdCard(&spiSD, SDCARD_CS);

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
  if (sdCard.fileExists("/events")) {
    File root = SD.open("/events");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        if (!file.isDirectory()) {
          String filename = String(file.name());
          String fullPath = "/events/" + filename;
          file.close();
          sdCard.deleteFile(fullPath.c_str());
        } else {
          file.close();
        }
        file = root.openNextFile();
      }
      root.close();
      Serial.println("All event files deleted.");
    }
  } else {
    Serial.println("No events directory found.");
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
 * FAST: Captures 20 samples immediately, THEN formats and saves
 */
void captureEvent(float triggerX, float triggerY, float triggerZ) {
  static int eventNumber = 0;
  
  unsigned long captureStart = millis();
  
  // Create temporary array to store samples during fast capture
  AccelSample eventSamples[EVENT_SAMPLE_COUNT];
  
  // Store trigger sample as first sample
  eventSamples[0].x = triggerX;
  eventSamples[0].y = triggerY;
  eventSamples[0].z = triggerZ;
  eventSamples[0].timestamp = millis();
  
  Serial.printf("\n!!! EVENT TRIGGERED !!! Capturing...");
  
  // FAST CAPTURE: Read the next 19 samples as quickly as possible
  for (int i = 1; i < EVENT_SAMPLE_COUNT; i++) {
    delay(10); // 10ms for 100Hz sampling
    if (lis3dh.read()) {
      eventSamples[i].x = lis3dh.getX();
      eventSamples[i].y = lis3dh.getY();
      eventSamples[i].z = lis3dh.getZ();
      eventSamples[i].timestamp = millis();
      Serial.print(".");
    }
  }
  
  unsigned long captureTime = millis() - captureStart;
  Serial.printf(" Done! (%lums)\n", captureTime);
  
  // NOW do the slow operations (SD card, formatting, etc.)
  Serial.println("Saving to SD card...");
  unsigned long saveStart = millis();
  
  // Get next event number from SD card
  eventNumber = sdCard.getNextEventNumber("/events", "event ");
  
  // Create filename
  char filename[32];
  snprintf(filename, sizeof(filename), "/events/event %d.txt", eventNumber);
  
  // Read temperature and humidity
  float temp = 0.0, humidity = 0.0;
  if (sht45.read()) {
    temp = sht45.getTemperature();
    humidity = sht45.getHumidity();
  }
  
  // Pre-allocate large string buffer
  String eventData;
  eventData.reserve(1024);
  
  // Build event header
  eventData = "=== EVENT " + String(eventNumber) + " ===\n";
  eventData += "Timestamp: " + getFormattedTime() + "\n";
  eventData += "Temperature: " + String(temp, 2) + " C\n";
  eventData += "Humidity: " + String(humidity, 2) + " %\n";
  eventData += "\nAccelerometer Samples (20):\n";
  eventData += "Sample, X(g), Y(g), Z(g)\n";
  
  // Format all captured samples
  char sampleLine[64];
  for (int i = 0; i < EVENT_SAMPLE_COUNT; i++) {
    snprintf(sampleLine, sizeof(sampleLine), "%d, %.3f, %.3f, %.3f\n", 
             i+1, 
             eventSamples[i].x, 
             eventSamples[i].y, 
             eventSamples[i].z);
    eventData += sampleLine;
  }
  
  // Write to SD card
  sdCard.writeFile(filename, eventData.c_str(), false);
  
  unsigned long saveTime = millis() - saveStart;
  unsigned long totalTime = millis() - captureStart;
  
  Serial.printf("Saved to: %s\n", filename);
  Serial.printf("Capture: %lums, Save: %lums, Total: %lums\n\n", captureTime, saveTime, totalTime);
}

/**
 * Playback all saved events from SD card
 * Called during setup to show previous events
 */
void playbackEvents() {
  Serial.println("\n======================================");
  Serial.println("      PREVIOUS EVENTS PLAYBACK");
  Serial.println("======================================\n");
  
  // Check if events directory exists
  if (!sdCard.fileExists("/events")) {
    Serial.println("No events directory found. No previous events.\n");
    return;
  }
  
  // List all files in events directory
  File root = SD.open("/events");
  if (!root || !root.isDirectory()) {
    Serial.println("Failed to open events directory\n");
    return;
  }
  
  bool foundEvents = false;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String filename = String(file.name());
      if (filename.startsWith("event ")) {
        foundEvents = true;
        Serial.println("--------------------------------------");
        Serial.printf("Reading: %s\n", file.name());
        Serial.println("--------------------------------------");
        
        // Read and print file contents
        String fullPath = "/events/" + filename;
        String content = sdCard.readFile(fullPath.c_str());
        Serial.println(content);
        Serial.println();
      }
    }
    file = root.openNextFile();
  }
  
  if (!foundEvents) {
    Serial.println("No previous events found.\n");
  }
  
  Serial.println("======================================");
  Serial.println("      END OF PLAYBACK");
  Serial.println("======================================\n");
}

void setup() {
  // Initialize Serial
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);
  Serial.println("\n\n=== Heltec Capstone Receiver Starting ===\n");

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
      
    default:
      // Ignore unknown commands
      break;
  }
}

void loop() {
  // Check for serial commands
  if (Serial.available() > 0) {
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