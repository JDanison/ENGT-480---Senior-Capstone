# Temperature Sensor Setup Guide

## Sensor Selection System

Your firmware now supports **two temperature sensors** with flexible configuration:
- **SHT45**: Temperature + Humidity sensor (currently active)
- **SEN0148**: Temperature only sensor (currently disabled)

### How to Toggle Sensors

Edit the defines at the top of **`src/main.h`** (around line 26-32):

```cpp
// Enable ONE or BOTH sensors:
#define USE_SHT45       // ← Uncomment to use SHT45
//#define USE_SEN0148   // ← Uncomment to use SEN0148
```

**Configuration Options:**
1. **SHT45 only** (default):
   ```cpp
   #define USE_SHT45
   //#define USE_SEN0148
   ```

2. **SEN0148 only**:
   ```cpp
   //#define USE_SHT45
   #define USE_SEN0148
   ```

3. **Both sensors** (temperature averaged):
   ```cpp
   #define USE_SHT45
   #define USE_SEN0148
   ```

When both sensors are enabled, the firmware will **average** the temperature readings from both sensors for redundancy.

---

## I2C Wiring Guide

### Standard I2C Wire Colors

Most I2C sensors use these wire colors:

| Wire Color | Function | Connect To |
|------------|----------|------------|
| **Red** | VCC (Power) | 3.3V or 5V |
| **Black** | GND (Ground) | GND |
| **Yellow/White** | SDA (Data) | GPIO 41 |
| **Green/Blue** | SCL (Clock) | GPIO 42 |

### SHT45 Wiring
- **VCC** → 3.3V (Red wire)
- **GND** → Ground (Black wire)
- **SDA** → GPIO 41 (Yellow/White wire)
- **SCL** → GPIO 42 (Green/Blue wire)
- **I2C Address**: 0x44 (default)

### SEN0148 Wiring

**⚠️ IMPORTANT: The SEN0148 is NOT an I2C sensor!**

The **DFRobot SEN0148** uses a **DHT22 (AM2302)** chip with **single-wire digital communication**.

**Correct Wiring:**
- **Red wire** → 3.3V or 5V (Power)
- **Black wire** → GND (Ground)
- **Yellow wire** → GPIO 40 (DATA line) - **NOT I2C SDA!**
- **Blue wire** → Leave disconnected (not used)

**DO NOT connect to I2C pins!** The Yellow wire must go to GPIO 40, not GPIO 41 (SDA).

**Current GPIO Pin:** GPIO 40 (defined in `main.h`)

To change the GPIO pin, edit **`src/main.h`** around line 58:
```cpp
#define SEN0148_DATA_PIN    40      // Change to any free GPIO pin
```

---

## ⚠️ Critical: SEN0148 is NOT I2C!

If you connect the SEN0148 to I2C pins (GPIO 41/42), it will **block the entire I2C bus** and prevent all other sensors (SHT45, LIS3DH, NAU7802) from working!

---

## Changing the GPIO Pin for SEN0148

The SEN0148 uses GPIO 40 by default. To change it, edit **`src/main.h`** around line 58:

```cpp
#define SEN0148_DATA_PIN    40    // ← Change this to any free GPIO pin
```

**Available GPIO pins on Heltec WiFi LoRa 32 V3:**
- GPIO 37, 38, 39, 40, 45, 46, 47, 48 (recommended)
- Avoid pins used for I2C, SPI, or other peripherals

### How to Find Your Sensor's I2C Address

This section applies to **I2C sensors only** (SHT45, LIS3DH, NAU7802).

**The SEN0148 is NOT an I2C sensor** and will not appear in I2C scanner results.

Press **`i`** in the serial monitor to scan for I2C devices.
```

---

## Modifying SEN0148_Module for Different Sensors

The **SEN0148 module is specifically designed for DFRobot SEN0148 (DHT22)**.

If you have a different temperature sensor:
- For OneWire sensors (DS18B20): Use Dallas Temperature library
- For other I2C sensors: Create a new module based on sensor's datasheet
- For SPI sensors: Use SPI communication library

The DHT22 library is already included in `platformio.ini`.

---

## Testing Your Sensor

After wiring and configuration:

1. **Build and upload** the firmware
2. **Open Serial Monitor** (115200 baud)
3. Look for initialization messages:
   ```
   Initializing SHT45 Sensor...
   SHT45: OK
   
   Initializing SEN0148 Sensor...
   SEN0148: OK
   ```

4. If you see **"FAILED"**:
   - Check wiring connections
   - Verify I2C address
   - Check power supply (3.3V vs 5V)
   - Try I2C scanner to detect the sensor

---   SEN0148: Initializing DHT22 on GPIO 40...
   SEN0148: Initialized successfully!
   SEN0148: Initial reading - Temp: 22.5°C, Humidity: 45.0%
## Troubleshooting
5. If you see **"FAILED"**:
   - Verify Yellow wire goes to GPIO 40 (not GPIO 41!)
   - Check power supply (3.3V or 5V)
   - Wait 2 seconds and try again (DHT22 needs time)
   - Use I2C scanner (`i`) to verify other sensors work* (NOT GPIO 41/SDA!)
- ✅ Check wire connections
- ✅ Ensure sensor is powered (3.3V or 5V)
- ✅ DHT22 sensors need 2 seconds between reads
- ✅ Try a different GPIO pin if needed
- ✅ DHT22 sensors have ±0.5°C accuracy
- ✅ Allow 2 seconds between readings
- ✅ Check if sensor probe is properly sealed
- ✅ Verify power supply is stable

### Both Sensors Giving Different Readings
- This is normal - some variance is expected
- If both are enabled, firmware averages them
- Check calibration if difference is significant (>2°C)

---

## Need More Help?

1. Check your sensor's **datasheet** for:
   - I2C address
   - Register map
   - Temperature conversion formula
   - Power requirements (3.3V or 5V)

2. Share the sensor's **datasheet or part number** for specific implementation help

3. Use the **I2C scanner** to verify the sensor is detected
the **DFRobot SEN0148 datasheet** for:
   - DHT22/AM2302 specifications
   - Operating voltage (3.3V to 5.5V)
   - Accuracy: ±0.5°C temperature, ±2% RH humidity
   - Response time: 2 seconds minimum between reads

2. **Common DHT22 Issues:**
   - Reading "NaN" = wiring issue or dead sensor
   - Slow response = normal, DHT22 needs 2 seconds
   - Different readings = humidity affects temperature measurement

3. **Hardware Issues:**
   - Verify Yellow wire is NOT on GPIO 41 (I2C SDA)
   - Check for loose connections
   - Ensure stable power supply