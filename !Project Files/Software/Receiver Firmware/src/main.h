/*
  Filename: main.h
  Receiver Firmware Header File
  Project: ENGT 480 Senior Capstone - Purdue University
  Author: John Danison
  Date Created: 12/4/2025
*/

#ifndef main_h
#define main_h

/* Include Generic Libraries */
#include <Arduino.h>    // Generic Arduino Library
#include "string.h"     // Specialized String Library
#include "heltec.h"     // OLED Heltec Library
#include <RadioLib.h>   // LoRa Library
#include <SPI.h>        // SPI Library - Not sure why its needed but it is...
#include <SPIFFS.h>     // Use this library for storage read/write. Helful tutorial: https://randomnerdtutorials.com/esp32-vs-code-platformio-spiffs/
#include <WiFi.h>       // WiFi Library
#include <chrono>       // Advanced Time Library

#endif