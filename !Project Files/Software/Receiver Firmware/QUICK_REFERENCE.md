# Quick Reference - I2C Scanner & Dual Temperature Display

## New Serial Commands Added

### 🔍 **`i` - I2C Scanner**
Scans all I2C addresses (0x00 to 0x7F) and reports connected devices.

**Usage:**
1. Open Serial Monitor (115200 baud)
2. Press **`i`** and hit Enter
3. View scan results showing all detected devices

**Example Output:**
```
=== I2C SCANNER ===
Scanning I2C bus (SDA=GPIO41, SCL=GPIO42)...

     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
00: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- 18 -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- 2A -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- 44 -- -- -- 48 -- -- -- -- -- -- -- 
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- -- 

--- Scan Complete ---
Found 4 device(s)

--- Known Devices ---
0x44 - SHT45 Temperature/Humidity Sensor
0x48 - SEN0148 Temperature Sensor
0x18 - LIS3DH Accelerometer
0x2A - NAU7802 ADC (Strain Gauge)
===================
```

---

### 🌡️ **`e` - Display Temperature Sensors**
Shows current readings from both temperature sensors separately.

**Usage:**
1. Open Serial Monitor
2. Press **`e`** and hit Enter
3. View readings from SHT45 and SEN0148

**Example Output (Both Sensors):**
```
=== TEMPERATURE SENSOR READINGS ===
SHT45 Sensor:
  Temperature: 22.45 °C (72.41 °F)
  Humidity:    45.60 %

SEN0148 Sensor:
  Temperature: 22.38 °C (72.28 °F)

Comparison:
  Average Temperature: 22.42 °C
  Sensor Difference:   0.07 °C
  ✓ Sensors agree within tolerance
===================================
```

**Example Output (SHT45 Only):**
```
=== TEMPERATURE SENSOR READINGS ===
SHT45 Sensor:
  Temperature: 22.45 °C (72.41 °F)
  Humidity:    45.60 %
===================================
```

---

## Separate Temperature Display in Events

Events now show **individual readings** from each sensor:

**Before:**
```
=== EVENT 5 ===
Timestamp: 2026-03-05 14:30:45 EST
Temperature: 22.50 C
Humidity: 45.00 %
```

**After (with both sensors):**
```
=== EVENT 5 ===
Timestamp: 2026-03-05 14:30:45 EST
Temperature (SHT45): 22.45 C
Humidity (SHT45): 45.60 %
Temperature (SEN0148): 22.38 C
Temperature (Average): 22.42 C
Temperature Difference: 0.07 C
```

---

## How to Enable/Disable Sensors

Edit **`src/main.h`** (around line 26):

```cpp
// Current configuration (SHT45 only):
#define USE_SHT45       // Uncommented = enabled
//#define USE_SEN0148   // Commented = disabled

// To enable BOTH sensors:
#define USE_SHT45
#define USE_SEN0148

// To use ONLY SEN0148:
//#define USE_SHT45
#define USE_SEN0148
```

---

## Troubleshooting with I2C Scanner

### SEN0148 Not Found?
1. **Check Yellow wire goes to GPIO 40** (NOT GPIO 41!)
2. Verify power is connected (Red to 3.3V or 5V)
3. Blue wire should be disconnected
4. DHT22 needs 2 seconds to stabilize on power-up

### No Devices Found?
- ✅ Check I2C wire connections (GPIO 41=SDA, 42=SCL)
- ✅ Verify I2C sensor power (3.3V)
- ✅ **Disconnect SEN0148** - it blocks I2C if connected wrong!
- ✅ Ensure pull-up resistors (usually built-in)

### Wrong Temperature Readings?
1. Press **`e`** to read sensors separately
2. DHT22 accuracy: ±0.5°C (normal)
3. Check sensor datasheet
4. Allow 2 seconds between readings (DHT22 requirement)

---

## All Serial Commands Summary

| Key | Command |
|-----|---------|
| **i** | **I2C Scanner** - Scan for connected I2C devices |
| **e** | **Environment** - Display temperature sensor readings |
| s | Sync time via WiFi |
| t | Display current time |
| d | Display all stored events |
| c | Clear all events from SD card |
| o | Offload data (playback, resync, clear) |
| g | Read strain gauge sample |
| z | Tare/zero strain gauge |
| r | Restart NAU7802 conversions |
| m | Monitor strain continuously |
| b | Bridge balance and sensitivity test |
| 1-4 | Test gain settings |

---

## Next Steps

1. **Disconnect SEN0148 from I2C pins** if currently connected there
2. **Rewire SEN0148**: Yellow wire to GPIO 40 (NOT GPIO 41!)
3. **Upload the firmware** to your device
4. **Open Serial Monitor** (115200 baud)
5. **Press `i`** to verify I2C sensors are working (SHT45, LIS3DH, NAU7802)
6. **Uncomment `#define USE_SEN0148`** in `main.h` to enable it
7. **Press `e`** to verify both temperature sensors are reading correctly

Good luck! 🚀
