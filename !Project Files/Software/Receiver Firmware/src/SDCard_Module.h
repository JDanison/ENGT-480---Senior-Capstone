/*
  Filename: SDCard_Module.h
  SD Card Module Header
  Author: John Danison
  Date Created: 12/9/2025

  Description: SD card interface for data logging and storage.
*/

#ifndef SDCARD_MODULE_H
#define SDCARD_MODULE_H

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <FS.h>

class SDCard_Module {
  private:
    SPIClass* spiSD;
    uint8_t csPin;
    bool initialized;
    
  public:
    /**
     * Constructor
     */
    SDCard_Module(SPIClass* spi, uint8_t cs);
    
    /**
     * Initialize SD card
     * @return true if successful, false otherwise
     */
    bool begin();
    
    /**
     * Write data to SD card file (creates directory if needed)
     * @param filename Path to file (e.g., "/data/event1.txt")
     * @param message Data to write
     * @param append If true, append to file; if false, overwrite
     * @return true if successful, false otherwise
     */
    bool writeFile(const char* filename, const char* message, bool append = true);
    
    /**
     * Read entire file from SD card
     * @param filename Path to file
     * @return File content as String, empty if failed
     */
    String readFile(const char* filename);
    
    /**
     * List all files in a directory
     * @param dirname Directory path (default is root "/")
     */
    void listFiles(const char* dirname = "/");
    
    /**
     * Check if file exists
     * @param filename Path to file
     * @return true if exists, false otherwise
     */
    bool fileExists(const char* filename);
    
    /**
     * Delete a file
     * @param filename Path to file
     * @return true if successful, false otherwise
     */
    bool deleteFile(const char* filename);
    
    /**
     * Get next available event number (for sequential naming)
     * @param directory Directory to search (e.g., "/events")
     * @param prefix Filename prefix (e.g., "event")
     * @return Next available event number
     */
    int getNextEventNumber(const char* directory, const char* prefix);
    
    /**
     * Check if SD card is initialized
     * @return true if initialized, false otherwise
     */
    bool isInitialized() { return initialized; }
};

#endif
