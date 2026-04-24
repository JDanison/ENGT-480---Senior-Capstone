/*
  Filename: EventLogger_Module.h
  Event Logger Module Header

  Description: Handles event CSV formatting and saving to SD card.
*/

#ifndef EVENTLOGGER_MODULE_H
#define EVENTLOGGER_MODULE_H

#include <Arduino.h>
#include "SDCard_Module.h"

class EventLogger_Module {
  public:
    struct EventSample {
      float x;
      float y;
      float z;
      float strainMicro;
    };

    explicit EventLogger_Module(SDCard_Module* sdCard);

    String buildCsvDataRow(const EventSample* samples,
                           int sampleCount,
                           float temp,
                           float humidity,
                           const String& timestamp) const;

    bool saveEventCsv(const EventSample* samples,
                      int sampleCount,
                      float temp,
                      float humidity,
                      const String& timestamp,
                      int* outEventNumber = nullptr,
                      String* outFilename = nullptr) const;

  private:
    SDCard_Module* _sdCard;
};

#endif
