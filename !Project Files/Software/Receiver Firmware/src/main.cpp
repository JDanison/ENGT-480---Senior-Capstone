/* 
  Filename: main.cpp
  Heletec Receiver
  Author: John Danison
  Date Created: 12/6/2025

  Description: Main program file for Capstone Receiver with sensor modules.
*/

#include "main.h"

/**
 * Global Object Definitions
 */
#ifdef ENABLE_SENSORS
TwoWire I2C_Sensors = TwoWire(1);                           // Secondary I2C bus instance
OLEDDisplay_Module oledDisplay;                             // OLED display instance
SHT45_Module sht45(&I2C_Sensors, SHT45_I2C_ADDRESS);        // SHT45 sensor instance
LIS3DH_Module lis3dh(&I2C_Sensors, LIS3DH_I2C_ADDRESS);     // LIS3DH sensor instance
#endif

/**
 * SD Card Functions
 */
#ifdef ENABLE_SDCARD
SPIClass spiSD(HSPI);  // Use HSPI bus for SD card

bool initSDCard() {
  Serial.println("\n--- Initializing SD Card ---");
  
  // Initialize SPI with custom pins
  spiSD.begin(SDCARD_SCK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);
  
  // Initialize SD card
  if (!SD.begin(SDCARD_CS, spiSD)) {
    Serial.println("SD Card: FAILED");
    Serial.println("Check wiring and card insertion");
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return false;
  }
  
  Serial.print("SD Card Type: ");
  switch(cardType) {
    case CARD_MMC:  Serial.println("MMC"); break;
    case CARD_SD:   Serial.println("SDSC"); break;
    case CARD_SDHC: Serial.println("SDHC"); break;
    default:        Serial.println("UNKNOWN"); break;
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  Serial.println("SD Card: OK");
  
  return true;
}

bool writeToSDCard(const char* filename, const char* message) {
  Serial.printf("\nWriting to file: %s\n", filename);
  
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return false;
  }
  
  if (file.println(message)) {
    Serial.println("Write successful");
  } else {
    Serial.println("Write failed");
    file.close();
    return false;
  }
  
  file.close();
  return true;
}

String readFromSDCard(const char* filename) {
  Serial.printf("\nReading from file: %s\n", filename);
  
  File file = SD.open(filename);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return "";
  }
  
  String content = "";
  Serial.println("--- File Content ---");
  while (file.available()) {
    String line = file.readStringUntil('\n');
    Serial.println(line);
    content += line + "\n";
  }
  Serial.println("--- End of File ---");
  
  file.close();
  return content;
}

void listSDCardFiles() {
  Serial.println("\n--- SD Card Files ---");
  
  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
    return;
  }
  
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      Serial.print("FILE: ");
      Serial.print(file.name());
      Serial.print("\t\tSIZE: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    file = root.openNextFile();
  }
  Serial.println("--- End of List ---");
}
#endif

void setup() {
  // Initialize Serial
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);
  Serial.println("\n\n=== Heltec Capstone Receiver Starting ===\n");

#ifdef ENABLE_SENSORS
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
#endif

#ifdef ENABLE_SDCARD
  // Initialize SD Card
  if (initSDCard()) {
    // List existing files
    listSDCardFiles();
    
    // Write test data to SD card in /data folder
    Serial.println("\n--- Writing Test Data ---");
    writeToSDCard("/data/test.txt", "Hello from Heltec!");
    writeToSDCard("/data/test.txt", "This is line 2");
    writeToSDCard("/data/test.txt", "Testing SD card write functionality");
    
    // Read back the file
    String content = readFromSDCard("/data/test.txt");
    
    // List files again to see the new file
    listSDCardFiles();
  }
#endif
  
  Serial.println("\n=== Setup Complete ===\n");
  delay(2000);
}

void loop() {
#ifdef ENABLE_SENSORS
  // Read SHT45 sensor
  if (sht45.read()) {
    float temp = sht45.getTemperature();
    float humidity = sht45.getHumidity();
    
    Serial.println("--- SHT45 Data ---");
    Serial.print("Temperature: ");
    Serial.print(temp, 2);
    Serial.println(" °C");
    Serial.print("Humidity: ");
    Serial.print(humidity, 2);
    Serial.println(" %");
  } else {
    Serial.println("Failed to read SHT45!");
  }
  
  Serial.println();
  
  // Read LIS3DH sensor
  if (lis3dh.read()) {
    float accelX = lis3dh.getX();
    float accelY = lis3dh.getY();
    float accelZ = lis3dh.getZ();
    
    Serial.println("--- LIS3DH Data ---");
    Serial.print("Accel X: ");
    Serial.print(accelX, 3);
    Serial.println(" g");
    Serial.print("Accel Y: ");
    Serial.print(accelY, 3);
    Serial.println(" g");
    Serial.print("Accel Z: ");
    Serial.print(accelZ, 3);
    Serial.println(" g");
    
    // Display all sensor data on OLED
    oledDisplay.displaySensorData(
      sht45.getTemperature(),
      sht45.getHumidity(),
      accelX, accelY, accelZ
    );
  } else {
    Serial.println("Failed to read LIS3DH!");
  }
  
  Serial.println("\n================================\n");
  
  // Read sensors at configured interval
  delay(SENSOR_READ_INTERVAL);
#endif

#ifdef ENABLE_SDCARD
  // SD Card testing - runs once per loop with delay
  Serial.println("SD Card is ready. Add your code here.");
  delay(5000);
#endif

#if !defined(ENABLE_SENSORS) && !defined(ENABLE_SDCARD)
  Serial.println("No features enabled. Check main.h feature flags.");
  delay(5000);
#endif
}