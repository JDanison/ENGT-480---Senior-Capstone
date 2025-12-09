/*
  Filename: SHT45_Module.h
  SHT45 Temperature/Humidity Sensor Module Header
  Authors: Alex Bolinger, John Danison, Grant Sylvester
  Date Created: 12/6/2025

  Description: Header file for SHT45 sensor functionality
*/

#ifndef SHT45_MODULE_H
#define SHT45_MODULE_H

#include <Arduino.h>
#include <Wire.h>

class SHT45_Module {
public:
    // Constructor
    SHT45_Module(TwoWire* wire = &Wire, uint8_t address = 0x44);
    
    // Initialize the sensor
    bool begin();
    
    // Read temperature and humidity
    bool read();
    
    // Get last temperature reading (Celsius)
    float getTemperature();
    
    // Get last humidity reading (%)
    float getHumidity();
    
    // Check if sensor is connected
    bool isConnected();
    
private:
    TwoWire* _wire;
    uint8_t _address;
    float _temperature;
    float _humidity;
    bool _initialized;
    
    // CRC calculation for data validation
    uint8_t calculateCRC(uint8_t data[], uint8_t len);
};

#endif
