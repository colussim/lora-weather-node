/* 
*
* Solar Powered LoRaWAN Weather Station 
* https://github.com/colussim/lora-weather-node
*
* Version : 1.0
* Version M5Stack ENV - SHT30 + QMP6988
*  Use the official M5UnitENV library
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <DFRobot_LWNode.h>
#include "MAX17043.h"  
#include "M5UnitENV.h"  
#include "esp_sleep.h"  

// LoRaWAN definitions
#define REGION EU868
#define DATARATE DR5
#define DBM16 16

// I2C pins for ESP32-C6-DevKitC-1
#define SDA_PIN 6     // Pin 5 -> GPIO6 (I2C_SDA)
#define SCL_PIN 7     // Pin 6 -> GPIO7 (I2C_SCL)

// Solar voltage ADC pin for ESP32-C6-DevKitC-1
#define SOLAR_PIN 0   // Pin 7 -> GPIO0 (ADC1_CH0)

// Deep Sleep Configuration
#define SLEEP_MINUTES 15          // 15 minutes between transmissions
#define uS_TO_S_FACTOR 1000000    // Conversion from µs to s
#define SLEEP_TIME_S (SLEEP_MINUTES * 60)

// OTAA credentials
const char APP_EUI[] = "xxxxxxxx";
const char APP_KEY[] = "xxxxxxxx";

// Instances M5Stack ENV
DFRobot_LWNode_IIC node(APP_EUI, APP_KEY);
SHT3X sht30;    // Temperature/humidity sensor
QMP6988 qmp;    // Pressure/altitude sensor

// RTC Memory to retain status between wake-ups
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool wasJoined = false;

// LoRaWAN session persistence to avoid re-JOIN
RTC_DATA_ATTR uint32_t lastJoinTime = 0;
RTC_DATA_ATTR bool hasValidSession = false;
RTC_DATA_ATTR uint16_t frameCounter = 0;

void send_temperature();
void go_to_sleep();
float estimate_battery_percent(float voltage);

// --------- Solar voltage measurement ---------
const float ADC_MAX = 4095.0f;
const float VREF = 3.3f;
const int   ADC_SAMPLES = 12;
const float SOLAR_DIVIDER = 5.0f * 1.204f; // Calibrated divider

void setup_adc() {
    analogReadResolution(12);
    analogSetPinAttenuation(SOLAR_PIN, ADC_11db);
}

// --------- MAX17043 SIMPLIFIÉ ---------
bool fuelGaugePresent = false;

bool check_fuel_gauge_connection() {
    return Wire.endTransmission() == 0;
}

void print_fuel_gauge_status(float percent, float voltage, bool present) {
    Serial.print("✅ Final reading - Battery: "); 
    Serial.print(percent, 1); 
    Serial.print("% (");
    Serial.print(voltage, 3); 
    Serial.println("V)");
    Serial.print("🔧 fuelGaugePresent = ");
    Serial.println(present ? "true" : "false");
}

bool init_fuel_gauge() {
    float percent = 0.0;
    float voltage = 0.0;

    if (check_fuel_gauge_connection()) {
        percent = FuelGauge.percent();
        voltage = FuelGauge.voltage() / 1000.0;
        fuelGaugePresent = true;
        print_fuel_gauge_status(percent, voltage, fuelGaugePresent);
        return true;
    } else {
        Serial.println("❌ MAX17043 not detected");
        fuelGaugePresent = false;
        return false;
    }
}


// Function for manual estimation based on voltage
float estimate_battery_percent(float voltage) {
    // Typical LiPo 3.7V discharge curve
    if (voltage >= 4.15) 
        return 100.0;
    if (voltage >= 4.00) 
        return 90.0 + (voltage - 4.00) * 66.7;  // 90-100%
    if (voltage >= 3.90) 
        return 70.0 + (voltage - 3.90) * 200.0; // 70-90%
    if (voltage >= 3.80) 
        return 40.0 + (voltage - 3.80) * 300.0; // 40-70%
    if (voltage >= 3.70) 
        return 20.0 + (voltage - 3.70) * 200.0; // 20-40%
    if (voltage >= 3.50) 
        return 5.0 + (voltage - 3.50) * 75.0;   // 5-20%
    if (voltage >= 3.30) 
        return 1.0 + (voltage - 3.30) * 20.0;   // 1-5%
    return 0.0; // < 3.3V = vide
}


float read_solar_voltage() {
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; ++i) {
        sum += analogRead(SOLAR_PIN);
        delay(3);
    }
    float raw = (float)sum / (float)ADC_SAMPLES;
    float v_adc = (raw / ADC_MAX) * VREF;
    float v_solar = v_adc * SOLAR_DIVIDER;
    
    return v_solar;
}

float read_battery_voltage() {
    if (!fuelGaugePresent) 
        return 0.0;
    // The library returns millivolts — convert to volts for callers
    return FuelGauge.voltage() / 1000.0;
}

// Fonctions helper 
bool is_voltage_realistic(float voltage) {
    return voltage > 3.0 && voltage < 4.5;
}

bool should_use_voltage_estimation(float percentConstrained, float estimated, float voltage) {
    // Too great a difference
    if (abs(percentConstrained - estimated) > 15.0) {
        return true;
    }
    
    // 0% with high voltage = sensor problem
    if (percentConstrained < 1.0 && voltage > 3.8) {
        return true;
    }
    
    // Very low % but good voltage
    if (percentConstrained < 5.0 && voltage > 3.7) {
        return true;
    }
    
    // 100% but low voltage
    if (percentConstrained > 95.0 && voltage < 4.0) {
        return true;
    }
    
    return false;
}



uint8_t read_battery_percent() {
    float percent = 0.0;
    float percentConstrained = 0.0;
    float voltage = 0.0;

    // Test communication with MAX17043
    Wire.beginTransmission(0x36);
    if (Wire.endTransmission() != 0) {
        Serial.println("❌ MAX17043 communication failed");
        return 0;
    }

    // Read data
    percent = FuelGauge.percent();
    percentConstrained = FuelGauge.percent(true);
    voltage = FuelGauge.voltage() / 1000.0;
    
    Serial.print("🔋 MAX17043 raw: "); 
    Serial.print(percent, 2);
    Serial.print("% (constrained: "); 
    Serial.print(percentConstrained, 2);
    Serial.print("%) voltage: "); 
    Serial.print(voltage, 3); 
    Serial.println(" V");

    // Check if voltage is realistic
    if (!is_voltage_realistic(voltage)) {
        Serial.println("⚠️ MAX17043 voltage unrealistic, using estimation");
        float estimated = estimate_battery_percent(voltage);
        return (uint8_t)round(estimated);
    }

    // Estimation based on voltage
    float estimated = estimate_battery_percent(voltage);
    
    // DDecide if we should use the estimation
    if (should_use_voltage_estimation(percentConstrained, estimated, voltage)) {
        Serial.print("⚠️ Using voltage-based estimation: "); 
        Serial.print(estimated, 1);
        Serial.println("%");
        return (uint8_t)round(estimated);
    }

    // MAX17043 OK, use its value
    Serial.print("✅ Using MAX17043: ");
    Serial.print(percentConstrained, 1); 
    Serial.print("% (estimated: "); 
    Serial.print(estimated, 1); 
    Serial.println("%)");
    return (uint8_t)round(percentConstrained);
}


// --------- Diagnostic MAX17043 ---------
void diagnostic_max17043() {
    if (!fuelGaugePresent) {
        Serial.println("❌ MAX17043 not present for diagnostic");
        return;
    }
    
    Serial.println("\n🔍 MAX17043 DIAGNOSTIC");
    Serial.println("======================");
    
    float voltage = FuelGauge.voltage() / 1000.0; // convert mV -> V
    float percent = FuelGauge.percent();
    float estimated = estimate_battery_percent(voltage);
    
    Serial.print("📊 Raw voltage: ");
    Serial.print(voltage, 3); 
    Serial.println(" V");
    Serial.print("📊 Raw percent: "); 
    Serial.print(percent, 2); 
    Serial.println("%");
    Serial.print("💡 Estimated percent: "); 
    Serial.print(estimated, 1); 
    Serial.println("%");
    
    // Test internal registers
    uint16_t version = FuelGauge.version();
    Serial.print("🔧 IC Version: 0x"); 
    Serial.println(version, HEX);
    
    // Status and configuration
    Serial.print("⚡ Alert threshold: "); 
    Serial.print(FuelGauge.getThreshold()); 
    Serial.println("%");
    Serial.print("🚨 Alert status: ");
    Serial.println(FuelGauge.alertIsActive() ? "ACTIVE" : "OK");
    
    // Recommendations
    Serial.println("\n💡 RECOMMENDATIONS:");
    if (percent < 1.0 && voltage > 3.7) {
        Serial.println("  → MAX17043 needs calibration or replacement");
        Serial.println("  → Using voltage-based estimation");
    } else if (abs(percent - estimated) > 20) {
        Serial.println("  → Large difference between MAX17043 and estimation");
        Serial.println("  → Consider recalibration cycle");
    } else {
        Serial.println("  → MAX17043 readings appear reasonable");
    }
    
    Serial.println("======================\n");
}

// --------- M5Stack ENV Unit ---------
bool envUnitPresent = false;

bool init_env_unit() {
    Serial.println("🌡️ Initializing M5Stack ENV Unit...");
    
    // Init SHT30 (temperature/humidity)
    bool sht30_ok = sht30.begin(&Wire, SHT3X_I2C_ADDR, SDA_PIN, SCL_PIN, 400000U);
    if (!sht30_ok) {
        Serial.println("❌ SHT30 not detected");
    } else {
        Serial.println("✓ SHT30 ready");
    }
    
    // Init QMP6988 (pressure/altitude)
    bool qmp_ok = qmp.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, SDA_PIN, SCL_PIN, 400000U);
    if (!qmp_ok) {
        Serial.println("❌ QMP6988 not detected");
    } else {
        Serial.println("✓ QMP6988 ready");
    }
    
    envUnitPresent = sht30_ok && qmp_ok;
    
    if (envUnitPresent) {
        Serial.println("✅ M5Stack ENV Unit fully operational");
    } else if (sht30_ok) {
        Serial.println("⚠️ Partial ENV Unit (SHT30 only)");
        envUnitPresent = true; 
    } else {
        Serial.println("❌ ENV Unit initialization failed");
    }
    
    return envUnitPresent;
}

// --------- Deep Sleep Management ---------
void go_to_sleep() {
    Serial.println("🌙 Entering deep sleep...");
    Serial.print("💤 Sleep duration: "); 
    Serial.print(SLEEP_MINUTES); 
    Serial.println(" minutes");
    
    esp_sleep_enable_timer_wakeup(SLEEP_TIME_S * uS_TO_S_FACTOR);
    
    Serial.println("💤 Good night...");
    Serial.flush();
    
    esp_deep_sleep_start();
}

bool should_sleep() {
    uint8_t batteryPercent = read_battery_percent();
    float solarVoltage = read_solar_voltage();
    
    if (batteryPercent < 15 && solarVoltage < 3.5) {
        Serial.println("⚠️ Battery critical + no solar - staying awake");
        return false;
    }
    
    if (bootCount == 0) {
        Serial.println("ℹ️ First boot - staying awake for debug");
        return false;
    }
    
    return true;
}

void init_serial_and_boot() {
    Serial.begin(115200);
    delay(1000);

    ++bootCount;
    Serial.println("\n=== ESP32 WAKE UP ===");
    Serial.print("Boot number: ");
    Serial.println(bootCount);
}

void log_wakeup_reason() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Wakeup: Timer");
            break;

        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("Wakeup: External signal");
            break;

        default:
            Serial.println("Wakeup: Power on or reset");
            break;
    }
}

void init_hardware() {
    Wire.begin(SDA_PIN, SCL_PIN);
    setup_adc();
}

void init_sensors() {
    // Init capteurs
    init_fuel_gauge();

    // MAX17043 diagnostics if first boot or problem detected
    if (bootCount <= 2) {
        diagnostic_max17043();
    }

    init_env_unit();
}

void init_lora() {
    Serial.println("🌐 Initializing LoRa...");
    node.begin(&Wire, &Serial);
    node.setRegion(REGION);
    node.setAppEUI(APP_EUI);
    node.setAppKEY(APP_KEY);
    node.setDevType(CLASS_A);
    node.setDataRate(DATARATE);
    node.setEIRP(DBM16);
    node.enableADR(false);
    node.setPacketType(UNCONFIRMED_PACKET);
}

bool should_reuse_lora_session(uint32_t currentTime) {
    return hasValidSession && ((currentTime - lastJoinTime) < 86400);
}

bool try_lora_join(uint32_t currentTime) {
    Serial.println("🔗 Attempting OTAA join...");
    node.join();

    unsigned long joinStart = millis();
    while (!node.isJoined() && millis() - joinStart < 60000) {
        delay(1000);
        Serial.print(".");
    }

    if (node.isJoined()) {
        Serial.println("\n✅ Joined successfully!");
        hasValidSession = true;
        lastJoinTime = currentTime;
        frameCounter = 0;
        return true;
    }

    Serial.println("\n❌ Join failed");
    hasValidSession = false;
    return false;
}

bool ensure_lora_session() {
    uint32_t currentTime = millis() / 1000; // seconds since boot

    if (should_reuse_lora_session(currentTime)) {
        Serial.println("🔄 Trying to reuse existing session...");
        // Try to send directly without re-JOIN
        Serial.println("🔄 Using cached session");
        return true;
    }

    return try_lora_join(currentTime);
}

void send_initial_data_if_joined() {
    if (wasJoined) {
        send_temperature();
        delay(2000);
    }
}

void handle_sleep() {
    if (should_sleep()) {
        go_to_sleep();
    } else {
        Serial.println("ℹ️ Staying awake (debug or critical battery)");
    }
}

void setup() {
    init_serial_and_boot();
    log_wakeup_reason();

    init_hardware();
    init_sensors();

    init_lora();

    wasJoined = ensure_lora_session();

    send_initial_data_if_joined();
    handle_sleep();
}

bool is_basic_data_valid(float temp, float hum) {
    return temp > -40 && temp < 85 && hum >= 0 && hum <= 100;
}

bool is_pressure_valid(float pressure) {
    return pressure > 80000 && pressure < 120000;
}

void print_basic_sensor_data(float tempC, float hum, float vbat, uint8_t battPct, float vsolar) {
    Serial.println("\n📊 BASIC SENSOR DATA:");
    Serial.print("  Temperature: "); 
    Serial.print(tempC, 1); 
    Serial.println("°C");
    Serial.print("  Humidity: ");
    Serial.print(hum, 1); 
    Serial.println("%");
    Serial.print("  Battery: "); 
    Serial.print(battPct); 
    Serial.print("% ("); 
    Serial.print(vbat, 3);
    Serial.println("V)");
    Serial.print("  Solar: "); 
    Serial.print(vsolar, 3); 
    Serial.println("V");
}

void print_extended_sensor_data(float tempC, float hum, float pressure, float altitude, float vbat, uint8_t battPct, float vsolar) {
    Serial.println("\n📊 EXTENDED SENSOR DATA:");
    Serial.print("  Temperature: "); 
    Serial.print(tempC, 1); 
    Serial.println("°C");
    Serial.print("  Humidity: "); 
    Serial.print(hum, 1); 
    Serial.println("%");
    Serial.print("  Pressure: "); 
    Serial.print(pressure, 1); 
    Serial.println(" hPa");
    Serial.print("  Altitude: "); 
    Serial.print(altitude, 0); 
    Serial.println(" m");
    Serial.print("  Battery: "); 
    Serial.print(battPct); 
    Serial.print("% ("); 
    Serial.print(vbat, 3); 
    Serial.println("V)");
    Serial.print("  Solar: "); 
    Serial.print(vsolar, 3); 
    Serial.println("V");
}

void print_payload_hex(const uint8_t* payload, size_t size) {
    Serial.print("🔁 Payload (hex): ");
    for (size_t i = 0; i < size; ++i) {
        if (i) Serial.print(' ');
        if (payload[i] < 16) Serial.print('0');
        Serial.print(payload[i], HEX);
    }
    Serial.println();
}

void send_temperature() {
    if (!envUnitPresent) {
        Serial.println("❌ ENV Unit not available");
        return;
    }

    bool sht30_updated = sht30.update();
    bool qmp_updated = qmp.update();

    if (!sht30_updated) {
        Serial.println("❌ SHT30 reading failed");
        return;
    }

    float tempC = sht30.cTemp;
    float hum = sht30.humidity;
    float vbat = read_battery_voltage();
    uint8_t battPct = read_battery_percent();
    float vsolar = read_solar_voltage();

    if (qmp_updated && is_pressure_valid(qmp.pressure)) {
        float pressure = qmp.pressure / 100.0; // Pa -> hPa
        float altitude = qmp.altitude;

        int16_t t100 = (int16_t)round(tempC * 100.0);
        uint16_t h100 = (uint16_t)round(hum * 100.0);
        uint16_t p10 = (uint16_t)round(pressure * 10.0);
        int16_t alt = (int16_t)round(altitude);
        uint16_t solarMv = (uint16_t)round(vsolar * 1000.0);

        uint8_t payload[12];
        payload[0] = 0x05;
        payload[1] = (t100 >> 8) & 0xFF;
        payload[2] = t100 & 0xFF;
        payload[3] = (h100 >> 8) & 0xFF;
        payload[4] = h100 & 0xFF;
        payload[5] = (p10 >> 8) & 0xFF;
        payload[6] = p10 & 0xFF;
        payload[7] = (alt >> 8) & 0xFF;
        payload[8] = alt & 0xFF;
        payload[9] = battPct;
        payload[10] = (solarMv >> 8) & 0xFF;
        payload[11] = solarMv & 0xFF;

        print_extended_sensor_data(tempC, hum, pressure, altitude, vbat, battPct, vsolar);
        print_payload_hex(payload, sizeof(payload));

        bool sent = node.sendPacket(payload, sizeof(payload));
        if (sent) {
            Serial.println("✅ Extended data transmission completed");
            frameCounter++;
        } else {
            Serial.println("⚠️ Extended data transmission requested");
            hasValidSession = false;
        }
    } else {
        int16_t t100 = (int16_t)round(tempC * 100.0);
        uint16_t h100 = (uint16_t)round(hum * 100.0);
        uint16_t solarMv = (uint16_t)round(vsolar * 1000.0);

        uint8_t payload[8];
        payload[0] = 0x04;
        payload[1] = (t100 >> 8) & 0xFF;
        payload[2] = t100 & 0xFF;
        payload[3] = (h100 >> 8) & 0xFF;
        payload[4] = h100 & 0xFF;
        payload[5] = battPct;
        payload[6] = (solarMv >> 8) & 0xFF;
        payload[7] = solarMv & 0xFF;

        print_basic_sensor_data(tempC, hum, vbat, battPct, vsolar);
        if (!qmp_updated) {
            Serial.println("  Pressure: Not available (QMP6988 failed)");
        }
        print_payload_hex(payload, sizeof(payload));

        bool sent = node.sendPacket(payload, sizeof(payload));
        if (sent) {
            Serial.println("✅ Basic data transmission completed");
            frameCounter++;
        } else {
            Serial.println("⚠️ Basic data transmission requested");
            hasValidSession = false;
        }
    }
}

void loop() {
    Serial.println("💤 Debug mode - sleeping in loop for 30s...");
    delay(30000);
    
    static int debugCycles = 0;
    debugCycles++;
    if (debugCycles >= 10) {
        Serial.println("🌙 Debug timeout - forcing sleep");
        go_to_sleep();
    }
}
