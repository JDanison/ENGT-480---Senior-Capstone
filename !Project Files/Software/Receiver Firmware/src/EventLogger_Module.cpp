/*
  Filename: EventLogger_Module.cpp
  Event Logger Module Implementation

  Description: Handles event CSV formatting and saving to SD card.
*/

#include "EventLogger_Module.h"

EventLogger_Module::EventLogger_Module(SDCard_Module* sdCard)
  : _sdCard(sdCard) {}

String EventLogger_Module::buildCsvDataRow(const EventSample* samples,
                                           int sampleCount,
                                           float temp,
                                           float humidity,
                                           const String& timestamp) const {
  String eventData;
  eventData.reserve(256 + (sampleCount * 64));

  String safeTimestamp = timestamp;
  safeTimestamp.replace("\"", "");

  eventData += "\"" + safeTimestamp + "\"";
  eventData += "," + String(temp, 2);
  eventData += "," + String(humidity, 2);

  char sampleValue[64];
  for (int i = 0; i < sampleCount; i++) {
    snprintf(sampleValue, sizeof(sampleValue), ",%.3f,%.3f,%.3f,%.2f",
             samples[i].x,
             samples[i].y,
             samples[i].z,
             samples[i].strainMicro);
    eventData += sampleValue;
  }
  eventData += "\n";

  return eventData;
}

bool EventLogger_Module::saveEventCsv(const EventSample* samples,
                                      int sampleCount,
                                      float temp,
                                      float humidity,
                                      const String& timestamp,
                                      int* outEventNumber,
                                      String* outFilename) const {
  if (_sdCard == nullptr) {
    return false;
  }

  int eventNumber = _sdCard->getNextEventNumber("/events", "event ");

  char filename[32];
  snprintf(filename, sizeof(filename), "/events/event %d.csv", eventNumber);

  String eventData = buildCsvDataRow(samples, sampleCount, temp, humidity, timestamp);
  bool writeOk = _sdCard->writeFile(filename, eventData.c_str(), false);

  if (outEventNumber != nullptr) {
    *outEventNumber = eventNumber;
  }
  if (outFilename != nullptr) {
    *outFilename = String(filename);
  }

  return writeOk;
}
