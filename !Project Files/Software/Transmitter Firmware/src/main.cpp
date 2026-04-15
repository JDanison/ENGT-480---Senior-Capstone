#include <Arduino.h>
#include <RadioLib.h>
#include <WiFi.h>

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

#define MAX_WIFI_PROFILES 3
String t_wifiSsids[MAX_WIFI_PROFILES];
String t_wifiPasswords[MAX_WIFI_PROFILES];

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

void parseAndStoreWifiProfiles(const String& packet) {
  if (!packet.startsWith("SETUP:")) return;
  String data = packet.substring(6);
  int start = 0;
  while (start < (int)data.length()) {
    int sep = data.indexOf(';', start);
    if (sep < 0) sep = (int)data.length();
    String token = data.substring(start, sep);
    int eq = token.indexOf('=');
    if (eq > 0) {
      String key = token.substring(0, eq);
      String value = token.substring(eq + 1);
      key.trim(); value.trim();
      // Key format: w0s, w0p, w1s, w1p, w2s, w2p
      if (key.length() == 3 && key.charAt(0) == 'w' && isDigit(key.charAt(1))) {
        int idx = key.charAt(1) - '0';
        if (idx >= 0 && idx < MAX_WIFI_PROFILES) {
          if (key.charAt(2) == 's') t_wifiSsids[idx] = value;
          else if (key.charAt(2) == 'p') t_wifiPasswords[idx] = value;
        }
      }
    }
    start = sep + 1;
  }
}

bool connectTransmitterWiFi() {
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    if (t_wifiSsids[i].length() == 0) continue;
    Serial.printf("[WIFI_TRY] %s\n", t_wifiSsids[i].c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(t_wifiSsids[i].c_str(), t_wifiPasswords[i].c_str());
    int timeout = 8;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
      delay(1000);
      timeout--;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WIFI_CONNECTED] %s\n", t_wifiSsids[i].c_str());
      return true;
    }
    Serial.printf("[WIFI_FAIL] %s\n", t_wifiSsids[i].c_str());
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  return false;
}

void handleWifiServerMessage(const String& packet) {
  // Packet format: RSP:WIFI_SERVER:<IP>:<PORT>
  String payload = packet.substring(16);  // strip "RSP:WIFI_SERVER:"
  int colonPos = payload.lastIndexOf(':');
  if (colonPos < 0) {
    Serial.println("[WIFI_SERVER] Malformed packet");
    return;
  }
  String ip = payload.substring(0, colonPos);
  int port = payload.substring(colonPos + 1).toInt();

  if (!connectTransmitterWiFi()) {
    Serial.println("[WIFI_TX_FAIL] No WiFi connection available");
    return;
  }

  WiFiClient client;
  bool tcpConnected = false;
  for (int attempt = 0; attempt < 3 && !tcpConnected; attempt++) {
    if (client.connect(ip.c_str(), port)) {
      tcpConnected = true;
    } else {
      delay(2000);
    }
  }
  if (!tcpConnected) {
    Serial.println("[WIFI_TX_FAIL] TCP connect failed");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  Serial.printf("[WIFI_TX_CONNECTED] %s:%d\n", ip.c_str(), port);
  unsigned long startMs = millis();
  dataTransferBytes = 0;
  dataTransferLines = 0;

  unsigned long lastActivity = millis();
  while (client.connected() && (millis() - lastActivity) < 10000UL) {
    if (!client.available()) continue;
    String line = client.readStringUntil('\n');
    line.replace("\r", "");
    line.trim();
    if (line.length() == 0) continue;
    lastActivity = millis();

    if (line.startsWith("DATA:")) {
      String payload = line.substring(5);
      dataTransferBytes += payload.length();
      dataTransferLines++;
      Serial.println(payload);
    } else if (line.startsWith("DATC:")) {
      String payload = line.substring(5);
      dataTransferBytes += payload.length();
      Serial.print(payload);
    } else if (line == "END:D") {
      client.stop();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      unsigned long elapsedMs = millis() - startMs;
      float elapsedSec = elapsedMs / 1000.0f;
      float rate = (elapsedSec > 0.0f) ? (dataTransferBytes / elapsedSec) : 0.0f;
      Serial.println("END:D");
      Serial.printf("[TRANSFER] duration=%lums lines=%u bytes=%u rate=%.1f B/s\n",
                    elapsedMs, (unsigned int)dataTransferLines,
                    (unsigned int)dataTransferBytes, rate);
      return;
    } else {
      Serial.printf("[WIFI_RX] %s\n", line.c_str());
    }
  }

  // Timeout or connection closed before END:D
  client.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[WIFI_TX_TIMEOUT] Transfer ended without END:D");
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
    if (packet == "END:D" && dataTransferActive) {
      unsigned long elapsedMs = millis() - dataTransferStartMs;
      float elapsedSec = elapsedMs / 1000.0f;
      float bytesPerSec = (elapsedSec > 0.0f) ? (dataTransferBytes / elapsedSec) : 0.0f;
      Serial.println("END:D");  // bare END:D so UI session_events counter fires
      Serial.printf("[TRANSFER] duration=%lums lines=%u bytes=%u rate=%.1f B/s\n",
                    elapsedMs,
                    (unsigned int)dataTransferLines,
                    (unsigned int)dataTransferBytes,
                    bytesPerSec);
      dataTransferActive = false;
    } else {
      Serial.printf("[%s]\n", packet.c_str());
    }
    return;
  }

  if (packet.startsWith("RSP:WIFI_SERVER:")) {
    handleWifiServerMessage(packet);
    return;
  }

  if (packet.startsWith("RSP:ID:")) {
    String truckId = packet.substring(7);
    truckId.trim();
    Serial.printf("[SCAN_RESULT]:%s\n", truckId.c_str());
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

  if (line == "SCAN") {
    sendLoRaPacket("CMD:n");
    return;
  }

  if (line.startsWith("SETUP:")) {
    parseAndStoreWifiProfiles(line);  // cache Wi-Fi profiles for TCP client step
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