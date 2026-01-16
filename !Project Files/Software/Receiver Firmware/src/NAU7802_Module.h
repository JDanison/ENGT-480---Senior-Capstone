/*
  Filename: NAU7802_Module.h
  NAU7802 24-bit ADC for Strain Gauge Module Header
  Author: John Danison
  Date Created: 1/16/2026

  Description: Header file for NAU7802 ADC functionality for strain gauge measurements
*/

#ifndef NAU7802_MODULE_H
#define NAU7802_MODULE_H

#include <Arduino.h>
#include <Wire.h>

// NAU7802 Register Addresses
#define NAU7802_PU_CTRL         0x00
#define NAU7802_CTRL1           0x01
#define NAU7802_CTRL2           0x02
#define NAU7802_ADCO_B2         0x12
#define NAU7802_ADCO_B1         0x13
#define NAU7802_ADCO_B0         0x14
#define NAU7802_ADC_REG         0x15
#define NAU7802_PGA_REG         0x1B
#define NAU7802_POWER_REG       0x1C

// NAU7802 Gain Settings
enum NAU7802_Gain {
    NAU7802_GAIN_1   = 0,
    NAU7802_GAIN_2   = 1,
    NAU7802_GAIN_4   = 2,
    NAU7802_GAIN_8   = 3,
    NAU7802_GAIN_16  = 4,
    NAU7802_GAIN_32  = 5,
    NAU7802_GAIN_64  = 6,
    NAU7802_GAIN_128 = 7
};

// NAU7802 Sample Rate Settings
enum NAU7802_SampleRate {
    NAU7802_SPS_10  = 0,
    NAU7802_SPS_20  = 1,
    NAU7802_SPS_40  = 2,
    NAU7802_SPS_80  = 3,
    NAU7802_SPS_320 = 7
};

class NAU7802_Module {
public:
    // Constructor
    NAU7802_Module(TwoWire* wire = &Wire, uint8_t address = 0x2A);
    
    // Initialize the sensor
    bool begin();
    
    // Check if sensor is connected
    bool isConnected();
    
    // Check if data is ready
    bool isDataReady();
    
    // Read raw 24-bit ADC value (signed)
    int32_t readRaw();
    
    // Read average of multiple samples
    int32_t readAverage(uint8_t samples = 10);
    
    // Set gain (1, 2, 4, 8, 16, 32, 64, 128)
    bool setGain(NAU7802_Gain gain);
    
    // Set sample rate (10, 20, 40, 80, 320 SPS)
    bool setSampleRate(NAU7802_SampleRate sps);
    
    // Calibrate internal offset
    bool calibrateAFE();
    
    // Calculate voltage from raw reading
    float calculateVoltage(int32_t rawValue, float referenceVoltage = 3.3);
    
    // Zero/tare the scale (store offset)
    bool tare(uint8_t samples = 10);
    
    // Get reading with offset removed
    int32_t getReading();
    
    // Convert raw value to strain (requires calibration)
    float calculateStrain(int32_t rawValue, float gaugeExcitation, float gaugeFactor = 2.0);
    
private:
    TwoWire* _wire;
    uint8_t _address;
    bool _initialized;
    int32_t _zeroOffset;
    NAU7802_Gain _currentGain;
    
    // Register read/write helpers
    bool writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    bool setBit(uint8_t reg, uint8_t bit);
    bool clearBit(uint8_t reg, uint8_t bit);
    bool getBit(uint8_t reg, uint8_t bit);
};

#endif
