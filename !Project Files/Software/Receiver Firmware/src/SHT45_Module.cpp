/*
  Filename: SHT45_Module.cpp
  SHT45 Temperature/Humidity Sensor Module Implementation
  Author: John Danison
  Date Created: 12/6/2025

  Description: Implementation file for SHT45 sensor functionality
*/

#include "SHT45_Module.h"

// SHT45 Commands
#define SHT45_CMD_MEASURE_HIGH_PRECISION 0xFD
#define SHT45_CMD_READ_SERIAL 0x89
#define SHT45_CMD_SOFT_RESET 0x94

SHT45_Module::SHT45_Module(TwoWire* wire, uint8_t address) 
    : _wire(wire), _address(address), _temperature(0.0), _humidity(0.0), _initialized(false) {
}

bool SHT45_Module::begin() {
    if (!isConnected()) {
        Serial.println("SHT45: Sensor not found!");
        return false;
    }
    
    // Soft reset
    _wire->beginTransmission(_address);
    _wire->write(SHT45_CMD_SOFT_RESET);
    if (_wire->endTransmission() != 0) {
        Serial.println("SHT45: Reset failed!");
        return false;
    }
    
    delay(10); // Wait for reset
    
    _initialized = true;
    Serial.println("SHT45: Initialized successfully!");
    return true;
}

bool SHT45_Module::read() {
    if (!_initialized) {
        Serial.println("SHT45: Not initialized!");
        return false;
    }
    
    // Send measurement command
    _wire->beginTransmission(_address);
    _wire->write(SHT45_CMD_MEASURE_HIGH_PRECISION);
    if (_wire->endTransmission() != 0) {
        Serial.println("SHT45: Failed to send measurement command!");
        return false;
    }
    
    // Wait for measurement to complete
    delay(10);
    
    // Read 6 bytes: temp MSB, temp LSB, temp CRC, humidity MSB, humidity LSB, humidity CRC
    uint8_t data[6];
    _wire->requestFrom(_address, (uint8_t)6);
    
    if (_wire->available() != 6) {
        Serial.println("SHT45: Failed to read data!");
        return false;
    }
    
    for (int i = 0; i < 6; i++) {
        data[i] = _wire->read();
    }
    
    // Verify CRC for temperature
    uint8_t tempData[2] = {data[0], data[1]};
    if (calculateCRC(tempData, 2) != data[2]) {
        Serial.println("SHT45: Temperature CRC error!");
        return false;
    }
    
    // Verify CRC for humidity
    uint8_t humData[2] = {data[3], data[4]};
    if (calculateCRC(humData, 2) != data[5]) {
        Serial.println("SHT45: Humidity CRC error!");
        return false;
    }
    
    // Convert raw values to temperature and humidity
    uint16_t rawTemp = (data[0] << 8) | data[1];
    uint16_t rawHum = (data[3] << 8) | data[4];
    
    // Temperature conversion: -45 + 175 * (raw / 65535)
    _temperature = -45.0 + 175.0 * ((float)rawTemp / 65535.0);
    
    // Humidity conversion: 100 * (raw / 65535)
    _humidity = 100.0 * ((float)rawHum / 65535.0);
    
    return true;
}

float SHT45_Module::getTemperature() {
    return _temperature;
}

float SHT45_Module::getHumidity() {
    return _humidity;
}

bool SHT45_Module::isConnected() {
    _wire->beginTransmission(_address);
    return (_wire->endTransmission() == 0);
}

uint8_t SHT45_Module::calculateCRC(uint8_t data[], uint8_t len) {
    // CRC-8 polynomial: x^8 + x^5 + x^4 + 1 (0x31)
    uint8_t crc = 0xFF;
    
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    
    return crc;
}
