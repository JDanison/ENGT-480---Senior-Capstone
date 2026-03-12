/*
  Filename: SEN0148_Module.cpp
  SEN0148 Temperature/Humidity Sensor Module Implementation (DHT22/AM2302)
  Author: John Danison
  Date Created: 3/5/2026

  Description: Implementation file for DFRobot SEN0148 sensor functionality.
               This sensor uses DHT22 (AM2302) protocol with single-wire communication.
*/

#include "SEN0148_Module.h"

SEN0148_Module::SEN0148_Module(uint8_t dataPin) 
    : _dataPin(dataPin), _temperature(0.0), _humidity(0.0), 
      _initialized(false), _lastReadTime(0) {
    _dht = new DHT(_dataPin, DHT22);
}

bool SEN0148_Module::begin() {
    Serial.printf("SEN0148: Initializing DHT22 on GPIO %d...\n", _dataPin);
    
    _dht->begin();
    delay(2000); // DHT22 needs time to stabilize
    
    // Try to read once to verify sensor is working
    float testTemp = _dht->readTemperature();
    float testHum = _dht->readHumidity();
    
    if (isnan(testTemp) || isnan(testHum)) {
        Serial.println("SEN0148: Sensor not responding!");
        Serial.println("SEN0148: Check wiring - Yellow wire must go to GPIO pin, NOT I2C!");
        return false;
    }
    
    _temperature = testTemp;
    _humidity = testHum;
    _initialized = true;
    _lastReadTime = millis();
    
    Serial.println("SEN0148: Initialized successfully!");
    Serial.printf("SEN0148: Initial reading - Temp: %.1f°C, Humidity: %.1f%%\n", 
                  _temperature, _humidity);
    return true;
}

bool SEN0148_Module::read() {
    if (!_initialized) {
        Serial.println("SEN0148: Not initialized!");
        return false;
    }
    
    // DHT22 can only be read every 2 seconds
    unsigned long currentTime = millis();
    if (currentTime - _lastReadTime < READ_INTERVAL) {
        // Too soon, return cached values
        return true;
    }
    
    float temp = _dht->readTemperature();
    float hum = _dht->readHumidity();
    
    // Check if read failed
    if (isnan(temp) || isnan(hum)) {
        Serial.println("SEN0148: Read failed!");
        return false;
    }
    
    _temperature = temp;
    _humidity = hum;
    _lastReadTime = currentTime;
    
    return true;
}

float SEN0148_Module::getTemperature() {
    return _temperature;
}

float SEN0148_Module::getHumidity() {
    return _humidity;
}

bool SEN0148_Module::isConnected() {
    if (!_initialized) {
        return false;
    }
    
    // Try a quick read to check if sensor responds
    float temp = _dht->readTemperature();
    return !isnan(temp);
}
