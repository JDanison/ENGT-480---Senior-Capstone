/*
  Filename: OLEDDisplay_Module.cpp
  OLED Display Module Implementation
  Authors: Alex Bolinger, John Danison, Grant Sylvester
  Date Created: 12/6/2025

  Description: Implementation file for OLED display functionality
*/

#include "OLEDDisplay_Module.h"

OLEDDisplay_Module::OLEDDisplay_Module() : _initialized(false) {
}

bool OLEDDisplay_Module::begin() {
    // Initialize Heltec board with OLED display
    Heltec.begin(true /*DisplayEnable*/, false /*LoRa Disable*/, true /*Serial Enable*/);
    _initialized = true;
    
    // Set brightness and contrast to maximum
    Heltec.display->setBrightness(255);
    Heltec.display->setContrast(255);
    
    // Normal display mode (not inverted)
    Heltec.display->normalDisplay();
    
    clear();
    displayMessage("OLED Display", "Initialized");
    update();
    
    return true;
}

void OLEDDisplay_Module::clear() {
    if (_initialized) {
        Heltec.display->clear();
    }
}

void OLEDDisplay_Module::displaySensorData(float temperature, float humidity, float accelX, float accelY, float accelZ) {
    if (!_initialized) return;
    
    clear();
    Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
    Heltec.display->setFont(ArialMT_Plain_10);
    
    char buffer[32];
    
    // Display temperature
    snprintf(buffer, sizeof(buffer), "Temp: %.2f C", temperature);
    Heltec.display->drawString(0, 0, buffer);
    
    // Display humidity
    snprintf(buffer, sizeof(buffer), "Humidity: %.2f %%", humidity);
    Heltec.display->drawString(0, 12, buffer);
    
    // Display acceleration
    snprintf(buffer, sizeof(buffer), "X:%.2f Y:%.2f Z:%.2f", accelX, accelY, accelZ);
    Heltec.display->drawString(0, 24, buffer);
    
    update();
}

void OLEDDisplay_Module::displayMessage(const char* line1, const char* line2, const char* line3, const char* line4) {
    if (!_initialized) return;
    
    clear();
    Heltec.display->setTextAlignment(TEXT_ALIGN_LEFT);
    Heltec.display->setFont(ArialMT_Plain_10);
    
    if (line1) Heltec.display->drawString(0, 0, line1);
    if (line2) Heltec.display->drawString(0, 12, line2);
    if (line3) Heltec.display->drawString(0, 24, line3);
    if (line4) Heltec.display->drawString(0, 36, line4);
    
    update();
}

void OLEDDisplay_Module::update() {
    if (_initialized) {
        Heltec.display->display();
    }
}
