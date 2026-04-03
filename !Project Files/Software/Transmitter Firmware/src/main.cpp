#include <Arduino.h>
#include <RadioLib.h>

#define SERIAL_BAUD_RATE      115200

// LoRa (SX1262) Pin Definitions for Heltec WiFi LoRa 32 V3
#define LORA_NSS              8
#define LORA_DIO1             14
#define LORA_RST              12
#define LORA_BUSY             13

// LoRa Radio Link Configuration (must match receiver)
#define LORA_FREQUENCY_MHZ    915.0
#define LORA_BANDWIDTH_KHZ    125.0
#define LORA_SPREADING_FACTOR 9
#define LORA_CODING_RATE      7
#define LORA_SYNC_WORD        0x34
#define LORA_TX_POWER_DBM     14
#define LORA_PREAMBLE_LEN     8

SX1262 loraRadio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
volatile bool loraPacketReceived = false;

bool dataTransferActive = false;
unsigned long dataTransferStartMs = 0;
size_t dataTransferBytes = 0;
size_t dataTransferLines = 0;

#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setLoRaFlag(void) {
  loraPacketReceived = true;
}

void restartLoRaReceive() {
  int rxState = loraRadio.startReceive();
  if (rxState != RADIOLIB_ERR_NONE) {
    Serial.printf("LoRa RX start failed (%d)\n", rxState);
  }
}

bool sendLoRaPacket(const String& packet) {
  int txState = loraRadio.transmit(packet.c_str());
  if (txState != RADIOLIB_ERR_NONE) {
    Serial.printf("LoRa TX failed (%d)\n", txState);
    return false;
  }

  Serial.printf("[TX] %s\n", packet.c_str());
  restartLoRaReceive();
  return true;
}

bool sendLoRaCommand(char command) {
  String packet = "CMD:" + String(command);
  if (!sendLoRaPacket(packet)) {
    return false;
  }

  if (command == 'd' || command == 'D') {
    dataTransferActive = true;
    dataTransferStartMs = millis();
    dataTransferBytes = 0;
    dataTransferLines = 0;
  }
  return true;
}

void handleLoRaMessage(const String& packet) {
  if (packet.startsWith("SETUP:")) {
    Serial.printf("[SETUP_ACK] Setup echoed from receiver: %s\n", packet.c_str());
    return;
  }

  if (packet.startsWith("DATC:")) {
    String chunk = packet.substring(5);
    if (dataTransferActive) {
      dataTransferBytes += chunk.length();
    }
    Serial.print(chunk);
    return;
  }

  if (packet.startsWith("DATA:")) {
    String chunk = packet.substring(5);
    if (dataTransferActive) {
      dataTransferBytes += chunk.length();
      dataTransferLines++;
    }
    Serial.println(chunk);
    return;
  }

  if (packet.startsWith("END:")) {
    Serial.printf("[%s]\n", packet.c_str());

    if (packet == "END:D" && dataTransferActive) {
      unsigned long elapsedMs = millis() - dataTransferStartMs;
      float elapsedSec = elapsedMs / 1000.0f;
      float bytesPerSec = (elapsedSec > 0.0f) ? (dataTransferBytes / elapsedSec) : 0.0f;

      Serial.printf("[TRANSFER] duration=%lums lines=%u bytes=%u rate=%.1f B/s\n",
                    elapsedMs,
                    (unsigned int)dataTransferLines,
                    (unsigned int)dataTransferBytes,
                    bytesPerSec);

      dataTransferActive = false;
    }
    return;
  }

  if (packet.startsWith("RSP:")) {
    Serial.printf("[%s]\n", packet.c_str());
    return;
  }

  Serial.printf("[RX] %s\n", packet.c_str());
}

void processLoRaPackets() {
  if (!loraPacketReceived) {
    return;
  }

  loraPacketReceived = false;

  String packet;
  int rxState = loraRadio.readData(packet);
  if (rxState == RADIOLIB_ERR_NONE) {
    packet.trim();
    if (packet.length() > 0) {
      handleLoRaMessage(packet);
    }
  } else {
    Serial.printf("LoRa RX read failed (%d)\n", rxState);
  }

  restartLoRaReceive();
}

void processSerialInput() {
  if (Serial.available() <= 0) {
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) {
    return;
  }

  if (line.startsWith("SETUP:")) {
    sendLoRaPacket(line);
    return;
  }

  if (line.length() == 1) {
    sendLoRaCommand(line.charAt(0));
    return;
  }

  // Fallback: transmit arbitrary packet payload as-is.
  sendLoRaPacket(line);
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);
  Serial.println("\n=== Heltec LoRa Transmitter Bridge ===");
  Serial.println("Type a command character and press Enter.");
  Serial.println("Example: d  (request receiver event data)");

  int loraState = loraRadio.begin(LORA_FREQUENCY_MHZ,
                                  LORA_BANDWIDTH_KHZ,
                                  LORA_SPREADING_FACTOR,
                                  LORA_CODING_RATE,
                                  LORA_SYNC_WORD,
                                  LORA_TX_POWER_DBM,
                                  LORA_PREAMBLE_LEN);

  if (loraState == RADIOLIB_ERR_NONE) {
    loraRadio.setDio1Action(setLoRaFlag);
    restartLoRaReceive();
    Serial.println("LoRa: OK");
  } else {
    Serial.printf("LoRa: FAILED (%d)\n", loraState);
  }
}

void loop() {
  processSerialInput();
  processLoRaPackets();
}