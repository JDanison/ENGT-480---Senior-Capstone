/*
  Filename: OLEDDisplay_Module.h
  OLED Display Module Header
  Authors: Alex Bolinger, John Danison, Grant Sylvester
  Date Created: 12/6/2025

  Description: Header file for OLED display functionality
*/

#ifndef OLEDDISPLAY_MODULE_H
#define OLEDDISPLAY_MODULE_H

#include <Arduino.h>
#include "heltec.h"

class OLEDDisplay_Module {
public:
    // Constructor
    OLEDDisplay_Module();
    
    // Initialize the OLED display
    bool begin();
    
    // Clear display
    void clear();
    
    // Display sensor data
    void displaySensorData(float temperature, float humidity, float accelX, float accelY, float accelZ);
    
    // Display text message
    void displayMessage(const char* line1, const char* line2 = nullptr, const char* line3 = nullptr, const char* line4 = nullptr);
    
    // Update display buffer to screen
    void update();
    
private:
    bool _initialized;
};

#endif
