/*
  Filename: main.h
  Heletec Receiver
  Authors: Alex Bolinger, John Danison, Grant Sylvester

  Last Updated: 12/6/2025
*/

#ifndef main_h
#define main_h

/* Include Generic Libraries */
#include <Arduino.h>    // Generic Arduino Library
#include "string.h"     // Specialized String Library
#include "heltec.h"     // OLED Heltec Library
#include <RadioLib.h>   // LoRa Library
#include <SPI.h>        // SPI Library
#include <SD.h>         // SD card library
#include <FS.h>         // File system library
#include <SPIFFS.h>     // SPIFFS storage library
#include <WiFi.h>       // WiFi Library
#include <Wire.h>       // I2C Library
//#include <chrono>       // Advanced Time Library - Commented out due to conflicts
//#include <Packet.h>     // Custom Packet Library

/* Include Custom Sensor Modules */
#include "OLEDDisplay_Module.h"
#include "SHT45_Module.h"
#include "LIS3DH_Module.h"


/**
 * Hardware Configuration Constants
 */
// I2C Pin Definitions
#define I2C_SENSOR_SDA_PIN  41      // Secondary I2C SDA pin for external sensors
#define I2C_SENSOR_SCL_PIN  42      // Secondary I2C SCL pin for external sensors
#define I2C_SENSOR_FREQ     400000  // I2C frequency: 400kHz
#define I2C_TIMEOUT         1000    // I2C timeout in milliseconds

// Sensor I2C Addresses
#define SHT45_I2C_ADDRESS   0x44    // SHT45 temperature/humidity sensor address
#define LIS3DH_I2C_ADDRESS  0x18    // LIS3DH accelerometer address

// SD Card SPI Pin Definitions
#define SDCARD_MOSI         34      // SD card MOSI pin (Brown wire)
#define SDCARD_MISO         33      // SD card MISO pin (Grey wire)
#define SDCARD_SCK          35      // SD card SCK pin (White wire)
#define SDCARD_CS           36      // SD card CS pin (Yellow wire)

// Serial Configuration
#define SERIAL_BAUD_RATE    115200  // Serial monitor baud rate

// Timing Configuration
#define SENSOR_READ_INTERVAL 2000   // Sensor reading interval in milliseconds


/**
 * Global Objects (External Declarations)
 */
extern TwoWire I2C_Sensors;              // Secondary I2C bus for external sensors
extern OLEDDisplay_Module oledDisplay;   // OLED display module
extern SHT45_Module sht45;               // SHT45 temperature/humidity sensor
extern LIS3DH_Module lis3dh;             // LIS3DH accelerometer


/**
 * Function Prototypes
 */
// System initialization and main loop
void setup();
void loop();

// SD Card functions
bool initSDCard();
bool writeToSDCard(const char* filename, const char* message);
String readFromSDCard(const char* filename);
void listSDCardFiles();

// Legacy function prototypes (to be implemented)
void decToHex(int decimal, char * hex);   // Conversion from Decimal to Hex
int hexToDec(const char * hex);           // Conversion from Hex to Decimal
void clearRSSIData();                     // Clear RSSI Data File


/**
 * Feature Flags - Comment out to disable features
 */
#define HELTEC_OLED_ENABLED     // Enable Heltec OLED Display
//#define ENABLE_SENSORS          // Enable I2C sensor reading (SHT45 & LIS3DH)
#define ENABLE_SDCARD           // Enable SD card functionality

#endif