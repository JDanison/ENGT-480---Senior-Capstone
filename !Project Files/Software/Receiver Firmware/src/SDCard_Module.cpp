/*
  Filename: SDCard_Module.cpp
  SD Card Module Implementation
  Author: John Danison
  Date Created: 12/9/2025

  Description: SD card interface for data logging and storage.
*/

#include "SDCard_Module.h"

SDCard_Module::SDCard_Module(SPIClass* spi, uint8_t cs) {
  this->spiSD = spi;
  this->csPin = cs;
  this->initialized = false;
}

bool SDCard_Module::begin() {
  Serial.println("\n--- Initializing SD Card ---");
  
  // Initialize SD card
  if (!SD.begin(csPin, *spiSD, 80000000, "/sd", 5, false)) {
    Serial.println("SD Card: FAILED");
    Serial.println("Check wiring and card insertion");
    initialized = false;
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    initialized = false;
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
  
  initialized = true;
  return true;
}

bool SDCard_Module::writeFile(const char* filename, const char* message, bool append) {
  if (!initialized) {
    Serial.println("SD Card not initialized");
    return false;
  }
  
  // Extract directory path and create if it doesn't exist
  String path = String(filename);
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash > 0) {
    String dirPath = path.substring(0, lastSlash);
    if (!SD.exists(dirPath.c_str())) {
      Serial.printf("Creating directory: %s\n", dirPath.c_str());
      if (!SD.mkdir(dirPath.c_str())) {
        Serial.println("Failed to create directory");
        return false;
      }
    }
  }
  
  // Open file in append or write mode
  File file;
  if (append) {
    file = SD.open(filename, FILE_APPEND);
  } else {
    file = SD.open(filename, FILE_WRITE);
  }
  
  if (!file) {
    Serial.printf("Failed to open file: %s\n", filename);
    return false;
  }
  
  if (!file.println(message)) {
    Serial.println("Write failed");
    file.close();
    return false;
  }
  
  file.close();
  return true;
}

String SDCard_Module::readFile(const char* filename) {
  if (!initialized) {
    Serial.println("SD Card not initialized");
    return "";
  }
  
  File file = SD.open(filename);
  if (!file) {
    Serial.printf("Failed to open file: %s\n", filename);
    return "";
  }
  
  String content = "";
  while (file.available()) {
    content += (char)file.read();
  }
  
  file.close();
  return content;
}

void SDCard_Module::listFiles(const char* dirname) {
  if (!initialized) {
    Serial.println("SD Card not initialized");
    return;
  }
  
  Serial.printf("\n--- Files in %s ---\n", dirname);
  
  File root = SD.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }
  
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("DIR:  ");
      Serial.println(file.name());
    } else {
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

bool SDCard_Module::fileExists(const char* filename) {
  if (!initialized) {
    return false;
  }
  return SD.exists(filename);
}

bool SDCard_Module::deleteFile(const char* filename) {
  if (!initialized) {
    Serial.println("SD Card not initialized");
    return false;
  }
  
  if (SD.remove(filename)) {
    Serial.printf("Deleted: %s\n", filename);
    return true;
  } else {
    Serial.printf("Failed to delete: %s\n", filename);
    return false;
  }
}

int SDCard_Module::getNextEventNumber(const char* directory, const char* prefix) {
  if (!initialized) {
    return 1;
  }
  
  int maxNum = 0;
  File root = SD.open(directory);
  if (!root || !root.isDirectory()) {
    return 1;
  }
  
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String filename = String(file.name());
      // Check if filename starts with prefix
      if (filename.startsWith(prefix)) {
        // Extract number from filename like "event 1.txt"
        int startIdx = strlen(prefix);
        int endIdx = filename.indexOf('.');
        if (endIdx > startIdx) {
          String numStr = filename.substring(startIdx, endIdx);
          numStr.trim();
          int num = numStr.toInt();
          if (num > maxNum) {
            maxNum = num;
          }
        }
      }
    }
    file = root.openNextFile();
  }
  
  return maxNum + 1;
}
