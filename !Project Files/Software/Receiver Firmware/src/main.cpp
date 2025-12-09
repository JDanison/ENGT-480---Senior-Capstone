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
 * Sync time via WiFi using NTP
 */
bool syncTime() {
  Serial.println("\n=== TIME SYNC STARTING ===");
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int timeout = 20; // 20 second timeout
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(1000);
    Serial.print(".");
    timeout--;
  }
  Serial.println();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed!");
    Serial.println("Time sync FAILED");
    return false;
  }
  
  Serial.println("WiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Configure time with NTP
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  Serial.println("Waiting for NTP time sync...");
  
  // Wait for time to be set
  struct tm timeinfo;
  timeout = 10;
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
  
  Serial.println("\n========================================");
  Serial.println("        DATA OFFLOAD COMPLETE");
  Serial.println("========================================\n");
}

/**
 * Event capture function
 * Called when accelerometer threshold is exceeded
 */
void captureEvent() {
  static int eventNumber = 0;
  
  // Get next event number from SD card
  eventNumber = sdCard.getNextEventNumber("/events", "event ");
  
  // Create filename
  char filename[32];
  snprintf(filename, sizeof(filename), "/events/event %d.txt", eventNumber);
  
  Serial.printf("\n!!! EVENT %d TRIGGERED !!!\n", eventNumber);
  
  // Read temperature and humidity
  float temp = 0.0, humidity = 0.0;
  if (sht45.read()) {
    temp = sht45.getTemperature();
    humidity = sht45.getHumidity();
  }
  
  // Build event header
  String eventData = "=== EVENT " + String(eventNumber) + " ===\n";
  eventData += "Timestamp: " + getFormattedTime() + "\n";
  eventData += "Temperature: " + String(temp, 2) + " C\n";
  eventData += "Humidity: " + String(humidity, 2) + " %\n";
  eventData += "\nAccelerometer Samples (20):\n";
  eventData += "Sample, X(g), Y(g), Z(g)\n";
  
  // Write header to file
  sdCard.writeFile(filename, eventData.c_str(), false); // false = overwrite
  
  // Capture 20 accelerometer samples
  Serial.println("Capturing 20 samples...");
  for (int i = 0; i < EVENT_SAMPLE_COUNT; i++) {
    if (lis3dh.read()) {
      float x = lis3dh.getX();
      float y = lis3dh.getY();
      float z = lis3dh.getZ();
      
      String sample = String(i+1) + ", " + 
                     String(x, 3) + ", " + 
                     String(y, 3) + ", " + 
                     String(z, 3);
      
      sdCard.writeFile(filename, sample.c_str(), true); // true = append
      
      Serial.print(".");
      delay(10); // Small delay between samples
    }
  }
  
  Serial.println("\nEvent captured successfully!");
  Serial.printf("Saved to: %s\n\n", filename);
  
  // Give a moment before resuming normal monitoring
  delay(500);
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

  // Initialize OLED Display
  Serial.println("Initializing OLED Display...");
  if (oledDisplay.begin()) {
    Serial.println("OLED: OK");
  } else {
    Serial.println("OLED: FAILED");
  }
  
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

void loop() {
  // Check for serial commands
  if (Serial.available() > 0) {
    char command = Serial.read();
    
    if (command == 'c' || command == 'C') {
      // Clear SD card
      Serial.println("\n=== CLEARING SD CARD ===");
      
      // Delete all event files
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
      
      Serial.println("=== SD CARD CLEARED ===\n");
    }
    else if (command == 's' || command == 'S') {
      // Sync time
      syncTime();
    }
    else if (command == 'o' || command == 'O') {
      // Offload data (playback, resync, clear)
      offloadData();
    }
    else if (command == 't' || command == 'T') {
      // Display current time
      Serial.print("Current time: ");
      Serial.println(getFormattedTime());
    }
    else if (command == 'd' || command == 'D') {
      // Display all stored events
      playbackEvents();
    }
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
    
    // Always update OLED with current readings
    oledDisplay.displaySensorData(
      temp,
      humidity,
      accelX, accelY, accelZ
    );
    
    // Check if any axis exceeds threshold
    if (abs(accelX) > ACCEL_THRESHOLD || 
        abs(accelY) > ACCEL_THRESHOLD || 
        abs(accelZ) > ACCEL_THRESHOLD) {
      
      // Trigger event capture
      captureEvent();
    }
    
    // Delay for loop timing - gives display time to refresh
    delay(SENSOR_READ_INTERVAL);
  } else {
    Serial.println("Failed to read LIS3DH!");
    delay(SENSOR_READ_INTERVAL);
  }
}