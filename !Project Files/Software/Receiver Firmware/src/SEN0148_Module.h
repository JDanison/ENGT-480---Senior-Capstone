/*
  Filename: SEN0148_Module.h
  SEN0148 Temperature/Humidity Sensor Module Header (DHT22/AM2302)
  Author: John Danison
  Date Created: 3/5/2026

  Description: Header file for DFRobot SEN0148 sensor functionality.
               This sensor uses DHT22 (AM2302) protocol, NOT I2C!
               Uses single-wire digital communication.
*/

#ifndef SEN0148_MODULE_H
#define SEN0148_MODULE_H

#include <Arduino.h>
#include <DHT.h>

class SEN0148_Module {
public:
    // Constructor - requires GPIO pin number
    SEN0148_Module(uint8_t dataPin);
    
    // Initialize the sensor
    bool begin();
    
    // Read temperature and humidity
    bool read();
    
    // Get last temperature reading (Celsius)
    float getTemperature();
    
    // Get last humidity reading (%)
    float getHumidity();
    
    // Check if sensor is responding
    bool isConnected();
    
private:
    DHT* _dht;
    uint8_t _dataPin;
    float _temperature;
    float _humidity;
    bool _initialized;
    unsigned long _lastReadTime;
    static const unsigned long READ_INTERVAL = 2000; // DHT22 needs 2 sec between reads
};

#endif
