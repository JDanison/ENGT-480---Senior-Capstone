/*
  Filename: NAU7802_Module.cpp
  NAU7802 24-bit ADC for Strain Gauge Module Implementation
  Author: John Danison
  Date Created: 1/16/2026

  Description: Implementation file for NAU7802 ADC functionality
*/

#include "NAU7802_Module.h"

NAU7802_Module::NAU7802_Module(TwoWire* wire, uint8_t address) 
    : _wire(wire), _address(address), _initialized(false), _zeroOffset(0), _currentGain(NAU7802_GAIN_32) {
}

bool NAU7802_Module::begin() {
    if (!isConnected()) {
        Serial.println("NAU7802: Sensor not found!");
        return false;
    }
    
    Serial.println("NAU7802: Device detected, starting initialization...");
    
    // Reset all registers to default values
    bool result = setBit(NAU7802_PU_CTRL, 0); // RR bit
    if (!result) {
        Serial.println("NAU7802: Reset failed!");
        return false;
    }
    delay(10);
    
    // Clear reset bit
    result = clearBit(NAU7802_PU_CTRL, 0);
    if (!result) {
        Serial.println("NAU7802: Clear reset failed!");
        return false;
    }
    delay(10);
    
    // Power up digital and analog circuits
    result = setBit(NAU7802_PU_CTRL, 1); // PUD bit
    if (!result) {
        Serial.println("NAU7802: Power up digital failed!");
        return false;
    }
    
    result = setBit(NAU7802_PU_CTRL, 2); // PUA bit
    if (!result) {
        Serial.println("NAU7802: Power up analog failed!");
        return false;
    }
    
    // Wait for power up to complete
    delay(200);
    
    // Verify power up
    uint8_t puCtrl = readRegister(NAU7802_PU_CTRL);
    Serial.printf("NAU7802: PU_CTRL = 0x%02X\n", puCtrl);
    if (!(puCtrl & 0x04)) {
        Serial.println("NAU7802: Analog power not ready!");
    }
    if (!(puCtrl & 0x02)) {
        Serial.println("NAU7802: Digital power not ready!");
    }
    
    // Enable LDO (3.3V output for strain gauge excitation)
    Serial.println("NAU7802: Enabling LDO...");
    uint8_t powerReg = readRegister(NAU7802_POWER_REG);
    powerReg |= 0x80; // Set PGA_LDOMODE bit (use internal LDO)
    writeRegister(NAU7802_POWER_REG, powerReg);
    
    // Set LDO voltage to 3.3V
    uint8_t ctrlReg = readRegister(NAU7802_CTRL1);
    ctrlReg |= 0xC0; // Set VLDO bits to 11 for 3.3V
    writeRegister(NAU7802_CTRL1, ctrlReg);
    
    delay(100); // Allow LDO to stabilize
    
    // Set default gain (32x for strain gauges with imbalanced bridge)
    // Note: 128x causes saturation with 365Ω resistors vs 350Ω gauge
    if (!setGain(NAU7802_GAIN_32)) {
        Serial.println("NAU7802: Failed to set gain!");
        return false;
    }
    
    // Set sample rate to 10 SPS for better noise rejection (slower but cleaner)
    if (!setSampleRate(NAU7802_SPS_10)) {
        Serial.println("NAU7802: Failed to set sample rate!");
        return false;
    }
    
    // Calibrate AFE (Analog Front End)
    if (!calibrateAFE()) {
        Serial.println("NAU7802: Calibration failed!");
        return false;
    }
    
    Serial.println("NAU7802: Starting conversions...");
    if (!setBit(NAU7802_PU_CTRL, 4)) {
        Serial.println("NAU7802: Failed to start conversions!");
        return false;
    }
    
    delay(100); // Allow first conversion to complete
    
    // Verify CS bit is set and check for CR bit
    uint8_t puCtrlAfterCS = readRegister(NAU7802_PU_CTRL);
    Serial.printf("NAU7802: PU_CTRL after CS = 0x%02X\n", puCtrlAfterCS);
    Serial.printf("NAU7802: CS bit (4) = %d\n", (puCtrlAfterCS >> 4) & 0x01);
    Serial.printf("NAU7802: CR bit (5) = %d\n", (puCtrlAfterCS >> 5) & 0x01);
    
    _initialized = true;
    Serial.println("NAU7802: Initialized successfully!");
    return true;
}

bool NAU7802_Module::isConnected() {
    _wire->beginTransmission(_address);
    return (_wire->endTransmission() == 0);
}

bool NAU7802_Module::isDataReady() {
    return getBit(NAU7802_PU_CTRL, 5); // Check CR (Conversion Ready) bit
}

int32_t NAU7802_Module::readRaw() {
    if (!_initialized) {
        Serial.println("NAU7802: Not initialized!");
        return 0;
    }
    
    // Wait for data to be ready (longer timeout for slow sample rates)
    int timeout = 500; // 500ms timeout (covers 10 SPS = 100ms per sample)
    while (!isDataReady() && timeout > 0) {
        delay(1);
        timeout--;
    }
    
    if (timeout == 0) {
        Serial.println("NAU7802: Data timeout!");
        
        // Diagnostic info
        uint8_t puCtrl = readRegister(NAU7802_PU_CTRL);
        Serial.printf("  PU_CTRL = 0x%02X", puCtrl);
        Serial.printf(" (CS=%d, CR=%d, PUA=%d, PUD=%d)\n",
                     (puCtrl >> 4) & 1, (puCtrl >> 5) & 1, 
                     (puCtrl >> 2) & 1, (puCtrl >> 1) & 1);
        
        // Try to restart conversions
        Serial.println("  Attempting to restart conversions...");
        if (setBit(NAU7802_PU_CTRL, 4)) {
            delay(100);
            if (isDataReady()) {
                Serial.println("  ✓ Conversions restarted!");
            } else {
                Serial.println("  ✗ Still no data ready");
            }
        }
        return 0;
    }
    
    // Read 3 bytes of ADC data
    uint8_t b2 = readRegister(NAU7802_ADCO_B2);
    uint8_t b1 = readRegister(NAU7802_ADCO_B1);
    uint8_t b0 = readRegister(NAU7802_ADCO_B0);
    
    // Combine into 24-bit signed value
    int32_t value = ((int32_t)b2 << 16) | ((int32_t)b1 << 8) | b0;
    
    // Sign extend 24-bit to 32-bit
    if (value & 0x800000) {
        value |= 0xFF000000;
    }
    
    // CRITICAL: Wait for CR bit to clear after reading data registers
    // This ensures the next call waits for a NEW conversion, not stale data
    // The CR bit should auto-clear after reading, but verify it happened
    delay(2); // Brief delay to allow CR bit to update
    
    // Now wait for CR to go LOW (new conversion started), then HIGH again (new data ready)
    // This ensures subsequent readRaw() calls get fresh data
    timeout = 150; // 150ms should cover one full 10 SPS cycle (100ms) plus margin
    bool crWentLow = false;
    while (timeout > 0) {
        if (!isDataReady()) {
            crWentLow = true; // New conversion has started
            break;
        }
        delay(1);
        timeout--;
    }
    
    // If CR never went low, it means conversions might be stalled or too fast
    // This is expected if sample rate is very high (320 SPS) or if called infrequently
    
    return value;
}

int32_t NAU7802_Module::readAverage(uint8_t samples) {
    if (samples > 50) samples = 50; // Limit max samples
    int64_t sum = 0;
    for (uint8_t i = 0; i < samples; i++) {
        sum += readRaw();
    }
    return (int32_t)(sum / samples);
}

int32_t NAU7802_Module::readMedian(uint8_t samples) {
    // Read multiple samples
    if (samples < 3) samples = 3;
    if (samples > 25) samples = 25;
    
    int32_t readings[25];
    for (uint8_t i = 0; i < samples; i++) {
        readings[i] = readRaw();
    }
    
    // Simple bubble sort
    for (uint8_t i = 0; i < samples - 1; i++) {
        for (uint8_t j = 0; j < samples - i - 1; j++) {
            if (readings[j] > readings[j + 1]) {
                int32_t temp = readings[j];
                readings[j] = readings[j + 1];
                readings[j + 1] = temp;
            }
        }
    }
    
    // Return median value
    return readings[samples / 2];
}

int32_t NAU7802_Module::readFiltered(uint8_t samples) {
    // Read samples and reject outliers, then average
    if (samples < 5) samples = 5;
    if (samples > 50) samples = 50;
    
    int32_t readings[50];
    int64_t sum = 0;
    int32_t minVal = 2147483647;
    int32_t maxVal = -2147483648;
    
    // Collect samples
    for (uint8_t i = 0; i < samples; i++) {
        readings[i] = readRaw();
        sum += readings[i];
        if (readings[i] < minVal) minVal = readings[i];
        if (readings[i] > maxVal) maxVal = readings[i];
    }
    
    // Remove min and max (outliers), average the rest
    sum -= minVal;
    sum -= maxVal;
    return (int32_t)(sum / (samples - 2));
}

bool NAU7802_Module::setGain(NAU7802_Gain gain) {
    _currentGain = gain;
    
    // Clear gain bits (0-2) and set new gain
    uint8_t value = readRegister(NAU7802_CTRL1);
    value &= 0b11111000; // Clear bits 0-2
    value |= (gain & 0x07); // Set gain bits
    
    bool result = writeRegister(NAU7802_CTRL1, value);
    if (result) {
        delay(50); // Allow gain change to stabilize
    }
    return result;
}

bool NAU7802_Module::setSampleRate(NAU7802_SampleRate sps) {
    // Clear SPS bits (4-6) and set new rate
    uint8_t value = readRegister(NAU7802_CTRL2);
    value &= 0b10001111; // Clear bits 4-6
    value |= (sps << 4); // Set SPS bits
    
    return writeRegister(NAU7802_CTRL2, value);
}

bool NAU7802_Module::calibrateAFE() {
    // Begin calibration
    bool result = setBit(NAU7802_CTRL2, 2); // CALS bit
    if (!result) {
        return false;
    }
    
    // Wait for calibration to complete (check CAL_ERR bit)
    delay(500); // Calibration typically takes ~350ms
    
    // Check if calibration succeeded
    if (getBit(NAU7802_CTRL2, 3)) { // CAL_ERR bit
        Serial.println("NAU7802: Calibration error!");
        return false;
    }
    
    return true;
}

float NAU7802_Module::calculateVoltage(int32_t rawValue, float referenceVoltage) {
    // NAU7802 is 24-bit ADC with full scale range of ±2^23
    float fullScale = 8388608.0; // 2^23
    
    // Calculate voltage considering current gain
    int gainValue = 1 << _currentGain; // 2^gain
    
    return (rawValue / fullScale) * (referenceVoltage / gainValue);
}

bool NAU7802_Module::tare(uint8_t samples) {
    if (!_initialized) {
        Serial.println("NAU7802: Not initialized!");
        return false;
    }
    
    Serial.print("NAU7802: Taring with ");
    Serial.print(samples);
    Serial.println(" samples (outliers removed)...");
    
    // Use readFiltered instead of readAverage to reject outlier noise spikes
    _zeroOffset = readFiltered(samples);
    
    Serial.print("NAU7802: Zero offset set to ");
    Serial.println(_zeroOffset);
    
    return true;
}

int32_t NAU7802_Module::getReading() {
    return readRaw() - _zeroOffset;
}

float NAU7802_Module::calculateStrain(int32_t rawValue, float gaugeExcitation, float gaugeFactor) {
    // Calculate output voltage from ADC reading
    float vOut = calculateVoltage(rawValue);
    
    // Calculate strain using Wheatstone bridge equation (quarter bridge)
    // ε = (Vout / Vex) / (GF * (1/4))
    // Simplified: ε = 4 * Vout / (Vex * GF)
    
    float strain = 4.0 * vOut / (gaugeExcitation * gaugeFactor);
    
    return strain; // Returns strain as a decimal (e.g., 0.001 = 1000 microstrain)
}

bool NAU7802_Module::restartConversions() {
    Serial.println("NAU7802: Checking conversion status...");
    
    uint8_t puCtrl = readRegister(NAU7802_PU_CTRL);
    Serial.printf("  PU_CTRL = 0x%02X\n", puCtrl);
    
    bool cs = (puCtrl >> 4) & 1;  // Cycle Start bit
    bool cr = (puCtrl >> 5) & 1;  // Conversion Ready bit
    bool pua = (puCtrl >> 2) & 1; // Power Up Analog
    bool pud = (puCtrl >> 1) & 1; // Power Up Digital
    
    Serial.printf("  CS (Cycle Start): %d\n", cs);
    Serial.printf("  CR (Conv Ready):  %d\n", cr);
    Serial.printf("  PUA (Analog Pwr): %d\n", pua);
    Serial.printf("  PUD (Digital Pwr): %d\n", pud);
    
    // If conversions aren't running, restart them
    if (!cs) {
        Serial.println("  CS bit not set - starting conversions...");
        if (!setBit(NAU7802_PU_CTRL, 4)) {
            Serial.println("  Failed to set CS bit!");
            return false;
        }
        delay(100);
    }
    
    // Check if data is ready now
    if (isDataReady()) {
        Serial.println("  ✓ Data ready!");
        return true;
    } else {
        Serial.println("  ✗ Still no data ready");
        return false;
    }
}

// Private helper methods
bool NAU7802_Module::writeRegister(uint8_t reg, uint8_t value) {
    _wire->beginTransmission(_address);
    _wire->write(reg);
    _wire->write(value);
    return (_wire->endTransmission() == 0);
}

uint8_t NAU7802_Module::readRegister(uint8_t reg) {
    _wire->beginTransmission(_address);
    _wire->write(reg);
    _wire->endTransmission(false); // Send restart
    
    _wire->requestFrom(_address, (uint8_t)1);
    if (_wire->available()) {
        return _wire->read();
    }
    return 0;
}

bool NAU7802_Module::setBit(uint8_t reg, uint8_t bit) {
    uint8_t value = readRegister(reg);
    value |= (1 << bit);
    return writeRegister(reg, value);
}

bool NAU7802_Module::clearBit(uint8_t reg, uint8_t bit) {
    uint8_t value = readRegister(reg);
    value &= ~(1 << bit);
    return writeRegister(reg, value);
}

bool NAU7802_Module::getBit(uint8_t reg, uint8_t bit) {
    uint8_t value = readRegister(reg);
    return (value & (1 << bit)) != 0;
}
