#include <Arduino.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <EEPROM.h>

#define SERIAL_BAUD_RATE      115200

// EEPROM storage for Wi-Fi profiles (MAX_WIFI_PROFILES * 100 bytes + headroom)
#define EEPROM_SIZE 2048
#define EEPROM_OFFSET_WIFI 0
#define EEPROM_SSID_SIZE 32
#define EEPROM_PASS_SIZE 64

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

#define SETUP_MASK_WIFI       (1 << 6)
#define SETUP_ACK_TIMEOUT_MS  1500UL
#define SETUP_ACK_RETRY_COUNT 4

SX1262 loraRadio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
volatile bool loraPacketReceived = false;
volatile bool g_setupAckReceived = false;

bool dataTransferActive = false;
unsigned long dataTransferStartMs = 0;
size_t dataTransferBytes = 0;
size_t dataTransferLines = 0;

#define MAX_WIFI_PROFILES 12
String t_wifiSsids[MAX_WIFI_PROFILES];
String t_wifiPasswords[MAX_WIFI_PROFILES];

// Pre-connect state: set when receiver broadcasts RSP:WIFI_CONNECTED:<ssid> via LoRa.
// This lets the transmitter start connecting immediately instead of waiting for RSP:WIFI_SERVER.
String g_receiverConnectedSsid = "";
bool   g_receiverWifiPreconnecting = false;

#define WIFI_CONNECT_TIMEOUT_SEC 20
#define WIFI_CONNECT_RETRY_COUNT 2
#define WIFI_SCAN_WAIT_SEC 12
#define WIFI_SCAN_POLL_SEC 2

// Allow extra headroom for receiver SD/Wi-Fi jitter before declaring transfer timeout.
#define WIFI_TCP_IDLE_TIMEOUT_MS 30000UL

#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setLoRaFlag(void) {
  loraPacketReceived = true;
}

void processLoRaPackets();

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

bool sendSetupPacketWithAck(const String& packet) {
  for (int attempt = 1; attempt <= SETUP_ACK_RETRY_COUNT; attempt++) {
    g_setupAckReceived = false;

    if (!sendLoRaPacket(packet)) {
      Serial.printf("[SETUP_RETRY] TX failed attempt %d/%d\n", attempt, SETUP_ACK_RETRY_COUNT);
      continue;
    }

    unsigned long startMs = millis();
    while ((millis() - startMs) < SETUP_ACK_TIMEOUT_MS) {
      processLoRaPackets();
      if (g_setupAckReceived) {
        return true;
      }
      delay(10);
    }

    Serial.printf("[SETUP_RETRY] No ACK attempt %d/%d\n", attempt, SETUP_ACK_RETRY_COUNT);
  }

  return false;
}

void saveWiFiProfilesToEEPROM() {
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    int16_t ssidLen = t_wifiSsids[i].length();
    int ssidAddr = EEPROM_OFFSET_WIFI + (i * (2 + EEPROM_SSID_SIZE + 2 + EEPROM_PASS_SIZE));
    
    EEPROM.writeShort(ssidAddr, ssidLen);
    for (size_t j = 0; j < EEPROM_SSID_SIZE; j++) {
      EEPROM.writeByte(ssidAddr + 2 + j, (j < t_wifiSsids[i].length()) ? t_wifiSsids[i][j] : 0);
    }
    
    int16_t passLen = t_wifiPasswords[i].length();
    int passAddr = ssidAddr + 2 + EEPROM_SSID_SIZE;
    EEPROM.writeShort(passAddr, passLen);
    for (size_t j = 0; j < EEPROM_PASS_SIZE; j++) {
      EEPROM.writeByte(passAddr + 2 + j, (j < t_wifiPasswords[i].length()) ? t_wifiPasswords[i][j] : 0);
    }
  }
  EEPROM.commit();
  Serial.println("[EEPROM] Wi-Fi profiles saved");
}

void loadWiFiProfilesFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    int ssidAddr = EEPROM_OFFSET_WIFI + (i * (2 + EEPROM_SSID_SIZE + 2 + EEPROM_PASS_SIZE));
    
    int16_t ssidLen = EEPROM.readShort(ssidAddr);
    t_wifiSsids[i] = "";
    if (ssidLen > 0 && ssidLen <= EEPROM_SSID_SIZE) {
      for (int j = 0; j < ssidLen; j++) {
        t_wifiSsids[i] += (char)EEPROM.readByte(ssidAddr + 2 + j);
      }
    }
    
    int16_t passLen = EEPROM.readShort(ssidAddr + 2 + EEPROM_SSID_SIZE);
    t_wifiPasswords[i] = "";
    if (passLen > 0 && passLen <= EEPROM_PASS_SIZE) {
      int passAddr = ssidAddr + 2 + EEPROM_SSID_SIZE;
      for (int j = 0; j < passLen; j++) {
        t_wifiPasswords[i] += (char)EEPROM.readByte(passAddr + 2 + j);
      }
    }
  }
  
  int configuredProfiles = 0;
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    if (t_wifiSsids[i].length() > 0) configuredProfiles++;
  }
  if (configuredProfiles > 0) {
    Serial.printf("[EEPROM] Loaded %d Wi-Fi profile(s)\n", configuredProfiles);
  }
}

static const char* wifiStatusToText(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED: return "CONNECTED";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    case WL_IDLE_STATUS: return "IDLE";
    default: return "UNKNOWN";
  }
}

static bool waitForVisibleSsid(const String& targetSsid, int waitSeconds) {
  unsigned long startMs = millis();
  unsigned long deadlineMs = startMs + (unsigned long)waitSeconds * 1000UL;
  int lastScanCount = -1;
  while (millis() < deadlineMs) {
    int networks = WiFi.scanNetworks(false, true);
    bool found = false;
    for (int i = 0; i < networks; i++) {
      if (WiFi.SSID(i) == targetSsid) {
        found = true;
        break;
      }
    }
    WiFi.scanDelete();
    if (found) {
      return true;
    }
    if (networks != lastScanCount) {
      Serial.printf("[WIFI_SCAN_COUNT] %d APs visible\n", networks);
      lastScanCount = networks;
    }
    unsigned long nowMs = millis();
    if (nowMs >= deadlineMs) {
      break;
    }
    unsigned long remainingMs = deadlineMs - nowMs;
    unsigned long sleepMs = WIFI_SCAN_POLL_SEC * 1000UL;
    if (sleepMs > remainingMs) {
      sleepMs = remainingMs;
    }
    delay(sleepMs);
  }
  return false;
}

static bool connectWifiProfile(const String& ssid, const String& password, wl_status_t* finalStatus) {
  wl_status_t status = WL_DISCONNECTED;
  for (int attempt = 1; attempt <= WIFI_CONNECT_RETRY_COUNT; attempt++) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    WiFi.disconnect(true, true);
    delay(250);

    if (!waitForVisibleSsid(ssid, WIFI_SCAN_WAIT_SEC)) {
      status = WL_NO_SSID_AVAIL;
      Serial.printf("[WIFI_FAIL] %s (NO_SSID scan timeout)\n", ssid.c_str());
      WiFi.mode(WIFI_OFF);
      delay(300);
      continue;
    }

    WiFi.begin(ssid.c_str(), password.c_str());
    int timeout = WIFI_CONNECT_TIMEOUT_SEC;
    while (timeout > 0) {
      status = WiFi.status();
      if (status == WL_CONNECTED) {
        if (finalStatus != nullptr) *finalStatus = status;
        return true;
      }
      if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED) {
        break;
      }
      delay(1000);
      timeout--;
    }

    status = WiFi.status();
    Serial.printf("[WIFI_FAIL] %s (%s) attempt %d/%d\n", ssid.c_str(), wifiStatusToText(status), attempt, WIFI_CONNECT_RETRY_COUNT);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(300);
  }

  if (finalStatus != nullptr) *finalStatus = status;
  return false;
}

static bool parseWifiPacketKey(const String& key, int& idx, char& kind) {
  if (key.length() < 3 || key.charAt(0) != 'w') {
    return false;
  }

  kind = key.charAt(key.length() - 1);
  if (kind != 's' && kind != 'p') {
    return false;
  }

  String idxPart = key.substring(1, key.length() - 1);
  if (idxPart.length() == 0) {
    return false;
  }

  for (int i = 0; i < idxPart.length(); i++) {
    if (!isDigit(idxPart.charAt(i))) {
      return false;
    }
  }

  idx = idxPart.toInt();
  return idx >= 0 && idx < MAX_WIFI_PROFILES;
}

static int findWifiProfileIndex(const String& ssid, String ssids[]) {
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    if (ssids[i] == ssid) {
      return i;
    }
  }
  return -1;
}

static int findFirstEmptyWifiSlot(String ssids[]) {
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    if (ssids[i].length() == 0) {
      return i;
    }
  }
  return -1;
}

static void compactWifiProfiles(String ssids[], String passwords[]) {
  int writeIdx = 0;
  for (int readIdx = 0; readIdx < MAX_WIFI_PROFILES; readIdx++) {
    if (ssids[readIdx].length() == 0) {
      continue;
    }
    if (writeIdx != readIdx) {
      ssids[writeIdx] = ssids[readIdx];
      passwords[writeIdx] = passwords[readIdx];
      ssids[readIdx] = "";
      passwords[readIdx] = "";
    }
    writeIdx++;
  }
}

void parseAndStoreWifiProfiles(const String& packet) {
  if (!packet.startsWith("SETUP:")) return;
  String data = packet.substring(6);
  String nextSsids[MAX_WIFI_PROFILES];
  String nextPasswords[MAX_WIFI_PROFILES];
  String wifiOp = "";
  String wifiOpSsid = "";
  String wifiOpPass = "";

  // Pre-scan for wop=setall so arrays are initialised empty before the main loop
  bool wifiSetAll = (data.indexOf("wop=setall") >= 0);
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    nextSsids[i] = wifiSetAll ? "" : t_wifiSsids[i];
    nextPasswords[i] = wifiSetAll ? "" : t_wifiPasswords[i];
  }

  bool maskProvided = false;
  unsigned int setupMask = 0;
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
      if (key == "m") {
        setupMask = (unsigned int)value.toInt();
        maskProvided = true;
      } else if (key == "wop") {
        wifiOp = value;
      } else if (key == "wssid") {
        wifiOpSsid = value;
      } else if (key == "wpass") {
        wifiOpPass = value;
      } else {
        int idx = -1;
        char kind = '\0';
        if (parseWifiPacketKey(key, idx, kind)) {
          if (kind == 's') nextSsids[idx] = value;
          else if (kind == 'p') nextPasswords[idx] = value;
        }
      }
    }
    start = sep + 1;
  }

  if (maskProvided && (setupMask & SETUP_MASK_WIFI) == 0) {
    return;
  }

  if (wifiOp == "add") {
    if (wifiOpSsid.length() == 0) {
      Serial.println("[WIFI_CFG] add requested without SSID");
      return;
    }
    int existingIdx = findWifiProfileIndex(wifiOpSsid, nextSsids);
    if (existingIdx >= 0) {
      nextPasswords[existingIdx] = wifiOpPass;
    } else {
      int emptyIdx = findFirstEmptyWifiSlot(nextSsids);
      if (emptyIdx < 0) {
        Serial.printf("[WIFI_CFG] profile list full (%d max)\n", MAX_WIFI_PROFILES);
        return;
      }
      nextSsids[emptyIdx] = wifiOpSsid;
      nextPasswords[emptyIdx] = wifiOpPass;
    }
  } else if (wifiOp == "remove") {
    if (wifiOpSsid.length() == 0) {
      Serial.println("[WIFI_CFG] remove requested without SSID");
      return;
    }
    int removeIdx = findWifiProfileIndex(wifiOpSsid, nextSsids);
    if (removeIdx >= 0) {
      nextSsids[removeIdx] = "";
      nextPasswords[removeIdx] = "";
      compactWifiProfiles(nextSsids, nextPasswords);
    }
  } else if (wifiOp == "clear") {
    for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
      nextSsids[i] = "";
      nextPasswords[i] = "";
    }
  } else if (wifiOp == "setall") {
    // Arrays were pre-cleared and filled by indexed w#s/w#p keys during token parsing.
    compactWifiProfiles(nextSsids, nextPasswords);
  }

  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    t_wifiSsids[i] = nextSsids[i];
    t_wifiPasswords[i] = nextPasswords[i];
  }
  
  // Persist the new profiles to EEPROM
  saveWiFiProfilesToEEPROM();
}

bool connectTransmitterWiFi() {
  int configuredProfiles = 0;
  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    if (t_wifiSsids[i].length() > 0) configuredProfiles++;
  }
  if (configuredProfiles == 0) {
    Serial.println("[WIFI_TX_FAIL] No WiFi profiles loaded on transmitter. Send setup with Wi-Fi selected.");
    return false;
  }

  for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
    if (t_wifiSsids[i].length() == 0) continue;
    Serial.printf("[WIFI_TRY] %s\n", t_wifiSsids[i].c_str());
    wl_status_t finalStatus = WL_DISCONNECTED;
    if (connectWifiProfile(t_wifiSsids[i], t_wifiPasswords[i], &finalStatus)) {
      Serial.printf("[WIFI_CONNECTED] %s\n", t_wifiSsids[i].c_str());
      return true;
    }
    Serial.printf("[WIFI_FAIL] %s (%s)\n", t_wifiSsids[i].c_str(), wifiStatusToText(finalStatus));
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Serial.println("[WIFI_HINT] Use 2.4GHz hotspot WPA2; WPA3/5GHz may fail on ESP32");
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

  // If RSP:WIFI_CONNECTED already kicked off a pre-connect, wait for it to complete
  // (up to 15 s) before falling back to the full sequential scan.
  bool alreadyConnected = false;
  if (g_receiverWifiPreconnecting) {
    Serial.println("[WIFI_PRECONNECT] Waiting for pre-connect to complete...");
    unsigned long waitStart = millis();
    while (millis() - waitStart < 15000UL) {
      wl_status_t s = WiFi.status();
      if (s == WL_CONNECTED) {
        alreadyConnected = true;
        break;
      }
      if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL) {
        break;
      }
      delay(300);
    }
    g_receiverWifiPreconnecting = false;
    if (alreadyConnected) {
      Serial.printf("[WIFI_CONNECTED] %s (pre-connected)\n", WiFi.SSID().c_str());
    } else {
      Serial.println("[WIFI_PRECONNECT] Pre-connect failed, falling back to full scan");
      WiFi.disconnect(true, true);
      WiFi.mode(WIFI_OFF);
      delay(150);
    }
  }

  if (!alreadyConnected && !connectTransmitterWiFi()) {
    Serial.println("[WIFI_TX_FAIL] No WiFi profiles or connection failed after all attempts");
    return;
  }

  WiFiClient client;
  bool tcpConnected = false;
  for (int attempt = 1; attempt <= 12 && !tcpConnected; attempt++) {
    if (client.connect(ip.c_str(), port)) {
      tcpConnected = true;
      break;
    }
    Serial.printf("[WIFI_TX_WAIT] TCP attempt %d/12 failed\n", attempt);
    delay(1000);
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
  while (client.connected() && (millis() - lastActivity) < WIFI_TCP_IDLE_TIMEOUT_MS) {
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
    g_setupAckReceived = true;
    Serial.printf("[SETUP_ACK] Setup echoed from receiver: %s\n", packet.c_str());
    return;
  }

  if (packet == "RSP:SETUP_OK") {
    g_setupAckReceived = true;
    Serial.println("[RSP:SETUP_OK]");
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

  if (packet.startsWith("RSP:WIFI_CONNECTED:")) {
    g_receiverConnectedSsid = packet.substring(19);
    g_receiverConnectedSsid.trim();
    Serial.printf("[RSP:WIFI_CONNECTED:%s]\n", g_receiverConnectedSsid.c_str());
    // Start connecting to the receiver's network NOW so we are ready when RSP:WIFI_SERVER arrives.
    for (int i = 0; i < MAX_WIFI_PROFILES; i++) {
      if (t_wifiSsids[i] == g_receiverConnectedSsid && t_wifiSsids[i].length() > 0) {
        WiFi.persistent(false);
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.setSleep(false);
        WiFi.disconnect(true, true);
        delay(50);  // minimal settle; radio re-armed faster
        WiFi.begin(t_wifiSsids[i].c_str(), t_wifiPasswords[i].c_str());
        g_receiverWifiPreconnecting = true;
        Serial.printf("[WIFI_PRECONNECT] Initiated connection to %s\n", t_wifiSsids[i].c_str());
        break;
      }
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

  if (line == "SCAN") {
    sendLoRaPacket("CMD:n");
    return;
  }

  if (line.startsWith("SETUP:")) {
    parseAndStoreWifiProfiles(line);  // cache Wi-Fi profiles for TCP client step
    if (!sendSetupPacketWithAck(line)) {
      Serial.println("[SETUP_TX_FAIL] Receiver did not acknowledge setup packet");
    } else {
      Serial.println("[SETUP_TX_OK]");
    }
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
  
  // Load saved Wi-Fi profiles from EEPROM
  loadWiFiProfilesFromEEPROM();

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