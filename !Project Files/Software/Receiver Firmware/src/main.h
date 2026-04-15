/*
  Filename: main.h
  Heltec Receiver
  Author: John Danison

  Last Updated: 12/16/2025
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
#include <time.h>       // Time library for NTP
//#include <chrono>       // Advanced Time Library - Commented out due to conflicts
//#include <Packet.h>     // Custom Packet Library

/* Include Custom Sensor Modules */
#include "OLEDDisplay_Module.h"
#include "SHT45_Module.h"
#include "LIS3DH_Module.h"
#include "SDCard_Module.h"
#include "NAU7802_Module.h"
#include "EventLogger_Module.h"


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
#define NAU7802_I2C_ADDRESS 0x2A    // NAU7802 ADC address (default)

// SD Card SPI Pin Definitions
#define SDCARD_MOSI         34      // SD card MOSI pin (Brown wire)
#define SDCARD_MISO         33      // SD card MISO pin (Grey wire)
#define SDCARD_SCK          35      // SD card SCK pin (White wire)
#define SDCARD_CS           36      // SD card CS pin (Yellow wire)

// LoRa (SX1262) Pin Definitions for Heltec WiFi LoRa 32 V3
#define LORA_NSS            8
#define LORA_DIO1           14
#define LORA_RST            12
#define LORA_BUSY           13

// LoRa Radio Link Configuration
#define LORA_FREQUENCY_MHZ  915.0
#define LORA_BANDWIDTH_KHZ  125.0
#define LORA_SPREADING_FACTOR 9
#define LORA_CODING_RATE    7
#define LORA_SYNC_WORD      0x34
#define LORA_TX_POWER_DBM   14
#define LORA_PREAMBLE_LEN   8
#define LORA_DATA_CHUNK_SIZE 180

// Serial Configuration
#define SERIAL_BAUD_RATE    115200  // Serial monitor baud rate

// ===== CONFIGURABLE RUNTIME PARAMETERS (can be updated via CFG packets) =====
// These are declared as extern globals and defined in main.cpp
extern unsigned long SENSOR_READ_INTERVAL;      // Sensor reading interval in milliseconds
extern float ACCEL_THRESHOLD;                   // Accelerometer threshold in g's
extern unsigned long EVENT_CAPTURE_DURATION_MS; // Event capture window in milliseconds
extern unsigned int LAB_TEST_SAMPLE_RATE_HZ;    // Lab test sampling rate (10 or 20 Hz)
// ======================================================================

// Timing Configuration (non-configurable)
#define EVENT_MAX_SAMPLES      80      // Safety cap for paired accel+strain samples in one event

// WiFi Configuration (for time sync)
// NOTE: Update these with your WiFi credentials before deploying
#define WIFI_SSID_PRIMARY       "NetHouse"              // Primary WiFi network
#define WIFI_PASSWORD_PRIMARY   "@AAMBN3Ts4G0od$&"      // Primary WiFi password
#define WIFI_SSID_BACKUP        "PAL3.0"        // Backup WiFi network (replace with your backup)
#define WIFI_PASSWORD_BACKUP    "Pu&$rl)u3ePu&"    // Backup WiFi password
#define NTP_SERVER              "pool.ntp.org"          // NTP server for time sync
#define GMT_OFFSET_SEC          -18000                  // EST = GMT-5 (5 hours * 3600 seconds)
#define DAYLIGHT_OFFSET_SEC     3600                    // Daylight saving time offset (1 hour)

// WiFi peer-to-peer offload profile storage
#define MAX_WIFI_PROFILES        3
#define WIFI_PROFILE_FILE        "/wifi/profiles.txt"
#define WIFI_CONNECT_TIMEOUT_SEC 8
#define WIFI_SERVER_PORT         8080
#define WIFI_CLIENT_TIMEOUT_SEC  35   // Seconds receiver waits for transmitter TCP connection


/**
 * Global Objects (External Declarations)
 */
extern TwoWire I2C_Sensors;              // Secondary I2C bus for external sensors
extern OLEDDisplay_Module oledDisplay;   // OLED display module
extern SHT45_Module sht45;               // SHT45 temperature/humidity sensor
extern LIS3DH_Module lis3dh;             // LIS3DH accelerometer
extern SDCard_Module sdCard;             // SD card module
extern NAU7802_Module nau7802;           // NAU7802 ADC for strain gauges


/**
 * Function Prototypes
 */
// System initialization and main loop
void setup();
void loop();

// Event capture functions
void captureEvent(float triggerX, float triggerY, float triggerZ);
void playbackEvents();

// Time sync functions
bool syncTime();
String getFormattedTime();
void offloadData();
bool startWifiLocalOffload();
void loadWiFiProfilesFromSd();
bool saveWiFiProfilesToSd();

// Configuration functions
bool parseSetupPacket(const String& packet);
bool saveTruckInfoToSd(const String& truckId, const String& description, bool includeTruckId, bool includeDescription);
void applyConfiguration();

// Legacy function prototypes (to be implemented)
void decToHex(int decimal, char * hex);   // Conversion from Decimal to Hex
int hexToDec(const char * hex);           // Conversion from Hex to Decimal
void clearRSSIData();                     // Clear RSSI Data File

#endif