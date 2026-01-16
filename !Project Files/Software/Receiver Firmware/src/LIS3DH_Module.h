/*
  Filename: LIS3DH_Module.h
  LIS3DH Accelerometer Module Header
  Author: John Danison
  Date Created: 12/6/2025

  Description: Header file for LIS3DH accelerometer functionality
*/

#ifndef LIS3DH_MODULE_H
#define LIS3DH_MODULE_H

#include <Arduino.h>
#include <Wire.h>

class LIS3DH_Module {
public:
    // Constructor
    LIS3DH_Module(TwoWire* wire = &Wire, uint8_t address = 0x18);
    
    // Initialize the sensor
    bool begin();
    
    // Read acceleration data
    bool read();
    
    // Get X-axis acceleration (g)
    float getX();
    
    // Get Y-axis acceleration (g)
    float getY();
    
    // Get Z-axis acceleration (g)
    float getZ();
    
    // Check if sensor is connected
    bool isConnected();
    
private:
    TwoWire* _wire;
    uint8_t _address;
    float _accelX;
    float _accelY;
    float _accelZ;
    bool _initialized;
    
    // Write to register
    void writeRegister(uint8_t reg, uint8_t value);
    
    // Read from register
    uint8_t readRegister(uint8_t reg);
    
    // Read multiple registers
    void readRegisters(uint8_t reg, uint8_t* buffer, uint8_t len);
};

#endif
