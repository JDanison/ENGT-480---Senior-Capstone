/*
  Filename: LIS3DH_Module.cpp
  LIS3DH Accelerometer Module Implementation
  Author: John Danison
  Date Created: 12/6/2025

  Description: Implementation file for LIS3DH accelerometer functionality
*/

#include "LIS3DH_Module.h"

// LIS3DH Register Addresses
#define LIS3DH_REG_WHO_AM_I 0x0F
#define LIS3DH_REG_CTRL_REG1 0x20
#define LIS3DH_REG_CTRL_REG4 0x23
#define LIS3DH_REG_OUT_X_L 0x28
#define LIS3DH_REG_OUT_X_H 0x29
#define LIS3DH_REG_OUT_Y_L 0x2A
#define LIS3DH_REG_OUT_Y_H 0x2B
#define LIS3DH_REG_OUT_Z_L 0x2C
#define LIS3DH_REG_OUT_Z_H 0x2D

#define LIS3DH_WHO_AM_I_VALUE 0x33

LIS3DH_Module::LIS3DH_Module(TwoWire* wire, uint8_t address)
    : _wire(wire), _address(address), _accelX(0.0), _accelY(0.0), _accelZ(0.0), _initialized(false) {
}

bool LIS3DH_Module::begin() {
    if (!isConnected()) {
        Serial.println("LIS3DH: Sensor not found!");
        return false;
    }
    
    // Check WHO_AM_I register
    uint8_t whoAmI = readRegister(LIS3DH_REG_WHO_AM_I);
    if (whoAmI != LIS3DH_WHO_AM_I_VALUE) {
        Serial.print("LIS3DH: Wrong WHO_AM_I value: 0x");
        Serial.println(whoAmI, HEX);
        return false;
    }
    
    // Configure sensor
    // CTRL_REG1: ODR=100Hz, normal mode, enable all axes
    writeRegister(LIS3DH_REG_CTRL_REG1, 0x57);
    
    // CTRL_REG4: ±2g scale, high resolution mode
    writeRegister(LIS3DH_REG_CTRL_REG4, 0x08);
    
    delay(10);
    
    _initialized = true;
    Serial.println("LIS3DH: Initialized successfully!");
    return true;
}

bool LIS3DH_Module::read() {
    if (!_initialized) {
        Serial.println("LIS3DH: Not initialized!");
        return false;
    }
    
    // Read acceleration data (6 bytes)
    uint8_t data[6];
    readRegisters(LIS3DH_REG_OUT_X_L | 0x80, data, 6); // 0x80 for auto-increment
    
    // Combine high and low bytes (data is left-aligned in 16-bit format)
    int16_t rawX = (int16_t)(data[1] << 8 | data[0]);
    int16_t rawY = (int16_t)(data[3] << 8 | data[2]);
    int16_t rawZ = (int16_t)(data[5] << 8 | data[4]);
    
    // Convert to g (±2g scale, 16-bit left-aligned output)
    // In high-resolution mode: sensitivity is 1mg/digit (from datasheet)
    // But data is 16-bit left-aligned, so we need to shift right by 4
    // Final sensitivity: approximately 0.001 g per LSB after shifting
    _accelX = (float)(rawX >> 4) * 0.001;
    _accelY = (float)(rawY >> 4) * 0.001;
    _accelZ = (float)(rawZ >> 4) * 0.001;
    
    return true;
}

float LIS3DH_Module::getX() {
    return _accelX;
}

float LIS3DH_Module::getY() {
    return _accelY;
}

float LIS3DH_Module::getZ() {
    return _accelZ;
}

bool LIS3DH_Module::isConnected() {
    _wire->beginTransmission(_address);
    return (_wire->endTransmission() == 0);
}

void LIS3DH_Module::writeRegister(uint8_t reg, uint8_t value) {
    _wire->beginTransmission(_address);
    _wire->write(reg);
    _wire->write(value);
    _wire->endTransmission();
}

uint8_t LIS3DH_Module::readRegister(uint8_t reg) {
    _wire->beginTransmission(_address);
    _wire->write(reg);
    _wire->endTransmission(false);
    
    _wire->requestFrom(_address, (uint8_t)1);
    if (_wire->available()) {
        return _wire->read();
    }
    return 0;
}

void LIS3DH_Module::readRegisters(uint8_t reg, uint8_t* buffer, uint8_t len) {
    _wire->beginTransmission(_address);
    _wire->write(reg);
    _wire->endTransmission(false);
    
    _wire->requestFrom(_address, len);
    for (uint8_t i = 0; i < len && _wire->available(); i++) {
        buffer[i] = _wire->read();
    }
}
