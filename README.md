# 🌦️ Solar Powered LoRaWAN Weather Station

[![Arduino](https://img.shields.io/badge/Arduino-ESP32-blue.svg)](https://www.arduino.cc/)
[![LoRaWAN](https://img.shields.io/badge/LoRaWAN-ChirpStack-green.svg)](https://www.chirpstack.io/)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-Integration-orange.svg)](https://www.home-assistant.io/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A comprehensive solar-powered LoRaWAN weather station built with ESP32, featuring comprehensive sensor monitoring, intelligent battery management, and seamless Home Assistant integration.

## 📋 Table of Contents

- [Features](#-features)
- [Hardware Components](#-hardware-components)
- [System Architecture](#-system-architecture)
- [Sensor Reading Process](#-sensor-reading-process)
- [LoRaWAN Gateway Configuration](#-lorawan-gateway-configuration)
- [MQTT Integration](#-mqtt-integration)
- [Installation](#-installation)
- [Configuration](#-configuration)
- [Usage](#-usage)
- [Home Assistant Integration](#-home-assistant-integration)
- [Troubleshooting](#-troubleshooting)
- [Contributing](#-contributing)

## ✨ Features

- **🌡️ Environmental Monitoring**: Temperature, humidity, atmospheric pressure, and altitude
- **🔋 Smart Battery Management**: MAX17043 fuel gauge with voltage-based fallback estimation
- **☀️ Solar Power Integration**: DFRobot Solar Power Manager with automated charging
- **📡 LoRaWAN Connectivity**: ChirpStack v4 compatible with OTAA authentication
- **💤 Ultra-Low Power**: Deep sleep optimization with session persistence
- **🏠 Home Assistant Ready**: Complete MQTT integration with device discovery
- **🔧 Self-Diagnostic**: Comprehensive sensor validation and error reporting
- **📊 Real-time Monitoring**: Web dashboard with automated charging control

## 🛠️ Hardware Components

### Core Components
| Component | Model | Purpose | Connection |
|-----------|-------|---------|------------|
| **Microcontroller** | ESP32 FireBeetle | Main processing unit | - |
| **LoRa Module** | DFRobot DFR1115 | LoRaWAN communication | SPI (GPIO 25, 26, 27) |
| **Environmental Sensor** | M5Stack ENV Unit | Temperature, humidity, pressure | I2C (GPIO 21, 22) |
| **Battery Monitor** | DFRobot DFR0563 (MAX17043) | Fuel gauge and voltage monitoring | I2C (0x36) |
| **Solar Manager** | DFRobot DFR0559 | Solar charging and power management | Analog input |

### Environmental Sensors (M5Stack ENV Unit)
- **SHT30**: Temperature (-40°C to +125°C) and Humidity (0-100% RH)
- **QMP6988**: Atmospheric pressure (300-1100 hPa) and altitude calculation

### Power System
- **18650 LiPo Battery**: 3.7V primary power source
- **Solar Panel**: 6V input via DFR0559 charging controller
- **MAX17043**: Intelligent battery monitoring with calibration
- **Automated Charging**: Mystrom smart switch integration

## 🏗️ System Architecture

```
┌─────────────────┐    ┌──────────────┐    ┌─────────────────┐
│   Solar Panel   │───▶│ Solar Manager│───▶│  18650 Battery  │
│     (6V)        │    │   DFR0559    │    │     (3.7V)      │
└─────────────────┘    └──────────────┘    └─────────────────┘
                                                    │
┌─────────────────┐    ┌──────────────┐    ┌─────────────────┐
│  MAX17043 FG    │◀───┤ ESP32 FB2.0  │◀───┤  M5Stack ENV    │
│  (Battery %)    │    │ (Deep Sleep) │    │  (T°C/H%/hPa)   │
└─────────────────┘    └──────────────┘    └─────────────────┘
                               │
                       ┌──────────────┐
                       │ LoRa DFR1115 │
                       │ (868MHz EU)  │
                       └──────────────┘
                               │
                        ╔══════════════╗
                        ║ LoRaWAN GW   ║
                        ║ ChirpStack   ║
                        ╚══════════════╝
                               │
                    ┌─────────────────────┐
                    │   MQTT Broker       │
                    │ (ChirpStack Data)   │
                    └─────────────────────┘
                               │
                    ┌─────────────────────┐
                    │  Home Assistant     │
                    │ (Dashboard & Auto)  │
                    └─────────────────────┘
```

## 📊 Sensor Reading Process

### 1. **Wake-up Sequence**
```cpp
ESP32 Deep Sleep → RTC Timer Wake → Session Restore → Sensor Init
```

### 2. **Environmental Data Collection**
- **SHT30 Reading**: Temperature (±0.3°C) and Humidity (±2% RH)
- **QMP6988 Reading**: Pressure (±0.5 hPa) and calculated altitude
- **Data Validation**: Range checking and error detection
- **Weather State**: Automatic classification (Sunny, Cloudy, Rainy, Stormy)

### 3. **Battery Monitoring**
- **MAX17043 Query**: Voltage and state-of-charge percentage
- **Calibration Check**: Reset and QuickStart if needed
- **Fallback Estimation**: Voltage-based calculation if sensor fails
- **Solar Input**: ADC reading of charging voltage

### 4. **Data Processing**
```cpp
// 12-byte Extended Payload Format
[Temperature] [Humidity] [Pressure] [Battery%] [Weather] [Altitude] [Voltage]
    2 bytes     2 bytes    2 bytes    1 byte    1 byte    2 bytes   2 bytes
```

### 5. **LoRaWAN Transmission**
- **Session Management**: Persistent OTAA with RTC storage
- **Adaptive Data Rate**: Automatic SF optimization
- **Retry Logic**: Fallback transmission handling
- **Deep Sleep**: Immediate sleep after TX_COMPLETE

## 📡 LoRaWAN Gateway Configuration

### ChirpStack v4 Setup

#### 1. **Device Profile Configuration**
```json
{
  "name": "Weather_Station_Profile",
  "region": "EU868", 
  "macVersion": "1.0.3",
  "regParamsRevision": "A",
  "supportsOtaa": true,
  "supportsClassB": false,
  "supportsClassC": false,
  "maxEirp": 16,
  "payloadCodec": "CUSTOM_JS",
  "payloadEncoderScript": "",
  "payloadDecoderScript": "// See chirpstack_decoder_v4_simple.js"
}
```

#### 2. **Application Configuration**
```json
{
  "name": "Solar_Weather_Station",
  "description": "ESP32 LoRaWAN Weather Station with Solar Power",
  "integrations": [
    {
      "kind": "MQTT",
      "configuration": {
        "server": "tcp://localhost:1883",
        "username": "chirpstack",
        "password": "your_password",  
        "topicPrefix": "application/{{application_id}}/device/{{dev_eui}}/event/{{event}}",
        "retain": true,
        "qos": 1
      }
    }
  ]
}
```

#### 3. **Device Registration**
```bash
# Device EUI (LSB format)
DEVEUI: "X-X-X"

# Application Key (MSB format) 
APPKEY: "X-X-X-X-X"

# Join Server: Built-in
# Device Profile: Weather_Station_Profile
```

## 🔗 MQTT Integration

### ChirpStack to Home Assistant Bridge

#### 1. **MQTT Broker Configuration**
```yaml
# mosquitto.conf
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd

# Bridge configuration for Home Assistant
connection bridge-ha
address homeassistant.local:1883
topic application/+/device/+/event/up out 0 "" "homeassistant/sensor/"
username ha_bridge
password your_bridge_password
try_private false
```

#### 2. **ChirpStack MQTT Settings**
- **Topic Template**: `application/{{application_id}}/device/{{dev_eui}}/event/{{event}}`
- **Retain Messages**: `true` (Critical for deep sleep compatibility)
- **QoS Level**: `1` (Ensures delivery)
- **JSON Payload**: Decoded sensor data

#### 3. **Data Flow**
```
ChirpStack → Local MQTT → Bridge → Home Assistant MQTT → Sensors
```

### MQTT Message Format
```json
{
  "applicationID": "1",
  "applicationName": "Solar_Weather_Station", 
  "deviceName": "ESP32_Weather_01",
  "devEUI": "6745230189abcdef",
  "data": {
    "temperature": 22.5,
    "humidity": 65.2,
    "pressure": 1013.8,
    "altitude": 245,
    "battery_percent": 87,
    "battery_voltage": 4.02,
    "solar_voltage": 5.8,
    "weather_state": "Partiellement nuageux"
  },
  "fCnt": 1245,
  "fPort": 1
}
```

## 🚀 Installation

### Prerequisites
```bash
# Arduino IDE with ESP32 support
# Libraries required:
- MCCI LoRaWAN LMIC library (>= 4.1.1)
- M5Unit-ENV (>= 0.0.9) 
- MAX17043 library (>= 1.0.0)
- ArduinoJson (>= 6.19.0)
```

### Hardware Assembly
1. **Connect M5Stack ENV** to ESP32 I2C (GPIO 21/22)
2. **Wire MAX17043** to I2C bus (address 0x36)
3. **Connect LoRa DFR1115** via SPI (GPIO 25/26/27)
4. **Install Solar Manager** with 18650 battery
5. **Mount in weatherproof enclosure**

### Software Configuration
1. **Clone Repository**
```bash
git clone https://github.com/colussim/lora-weather-node.git
cd lora-weather-node
```

2. **Configure Credentials**
```cpp
// Update in main .ino file
static const u1_t PROGMEM DEVEUI[8] = { YOUR_DEVEUI };
static const u1_t PROGMEM APPKEY[16] = { YOUR_APPKEY };
```

3. **Upload Firmware**
```bash
# Open Loraesp32_m5stack.ino in Arduino IDE
# Select: ESP32 Dev Module
# Upload Speed: 921600
# Flash Mode: QIO
```

## ⚙️ Configuration

### Sleep Interval
```cpp
#define SLEEP_INTERVAL 600  // 10 minutes (adjustable)
```

### Battery Thresholds
```cpp
// Low battery alert
FuelGauge.setThreshold(10);  // 10% alert threshold

// Solar charging automation
if (battery_percent < 20) {
    // Trigger Mystrom charging switch
}
```

### Sensor Calibration
```cpp
// Sea level pressure calibration
#define SEA_LEVEL_PRESSURE 1013.25

// Temperature offset (if needed)
temperature += TEMP_OFFSET;
```

## 📱 Usage

### Serial Monitor Output
```
🚀 Boot #1
🔋 Initializing MAX17043...
✅ MAX17043 initialized with calibration
🌡️ Initializing M5Stack ENV Unit...
✅ M5Stack ENV initialized

📊 EXTENDED SENSOR DATA:
  Temperature: 22.3°C
  Humidity: 64.7%
  Pressure: 1013.2 hPa
  Altitude: 247 m
  Battery: 87% (3.805V)
  Solar: 5.8V

📡 Sending LoRaWAN data...
✅ Extended data transmission completed
😴 Going to sleep...
```

### LED Status Indicators
- **Blue Flash**: LoRaWAN joining
- **Green Flash**: Successful transmission
- **Red Flash**: Sensor error
- **Off**: Deep sleep mode

## 🏠 Home Assistant Integration

### Automatic Discovery
The system creates the following entities:
- `sensor.weather_station_temperature`
- `sensor.weather_station_humidity` 
- `sensor.weather_station_pressure`
- `sensor.weather_station_battery`
- `sensor.weather_station_solar`
- `sensor.weather_station_altitude`

### Dashboard Configuration
```yaml
# See home_assistant_charge_auto.yaml for complete config
type: entities
entities:
  - entity: sensor.weather_station_temperature
    icon: mdi:thermometer
  - entity: sensor.weather_station_humidity
    icon: mdi:water-percent
  - entity: sensor.weather_station_pressure
    icon: mdi:gauge
  - entity: sensor.weather_station_battery
    icon: mdi:battery
```

### Automation Example
```yaml
# Automated charging when battery low
automation:
  - alias: "Weather Station Auto Charge"
    trigger:
      platform: numeric_state
      entity_id: sensor.weather_station_battery
      below: 20
    action:
      service: switch.turn_on
      entity_id: switch.mystrom_charger
```

## 🔧 Troubleshooting

### Common Issues

#### MAX17043 Shows 0% Battery
```cpp
// Solution: Use voltage-based estimation
uint8_t estimatedPercent = estimateBatteryPercent(voltage);
```

#### LoRaWAN Join Failed
- Check DEVEUI/APPKEY configuration
- Verify gateway coverage
- Ensure correct frequency plan (EU868)

#### Sensor Reading Errors
- Verify I2C connections (GPIO 21/22)
- Check 3.3V power supply stability
- Monitor serial output for diagnostic info

#### Deep Sleep Issues  
- Disable WiFi: `WiFi.mode(WIFI_OFF)`
- Ensure proper session persistence
- Check RTC memory usage

### Debug Mode
```cpp
#define DEBUG_MODE 1  // Enable verbose logging
```

### Reset Procedures
```cpp
// Factory reset (hold BOOT button during power-on)
void factoryReset() {
    rtcData = {0};  // Clear RTC memory
    FuelGauge.reset();  // Reset fuel gauge
    LMIC_reset();  // Reset LoRaWAN stack
}
```

## 📊 Performance Metrics

- **Battery Life**: 2-4 weeks (depending on transmission interval)
- **Solar Charging**: Full charge in 6-8 hours direct sunlight
- **Transmission Range**: 2-15 km (line of sight)
- **Sensor Accuracy**: ±0.3°C, ±2% RH, ±0.5 hPa
- **Current Consumption**: 
  - Active: ~150mA
  - Deep Sleep: <1mA

## 📝 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🤝 Contributing

1. Fork the repository
2. Create feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)
5. Open Pull Request

---

## Ressources

---
