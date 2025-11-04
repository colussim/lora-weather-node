// Version M5Stack ENV - SHT30 + QMP6988
// Use the official M5UnitENV library

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

// I2C pins
#define SDA_PIN 21
#define SCL_PIN 22

// Solar voltage ADC pin (A0 = GPIO 36)
#define SOLAR_PIN 36

// Deep Sleep Configuration
#define SLEEP_MINUTES 15          // 15 min entre transmissions
#define uS_TO_S_FACTOR 1000000    // Conversion µs vers s
#define SLEEP_TIME_S (SLEEP_MINUTES * 60)

// OTAA credentials
const char APP_EUI[] = "9182d1372de0164c";
const char APP_KEY[] = "";

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

void sendTemperature();
void goToSleep();

// --------- Solar voltage measurement ---------
const float ADC_MAX = 4095.0f;
const float VREF = 3.3f;
const int   ADC_SAMPLES = 12;
const float SOLAR_DIVIDER = 5.0f * 1.204f; // Calibrated divider

void setupADC() {
    analogReadResolution(12);
    analogSetPinAttenuation(SOLAR_PIN, ADC_11db);
}

// --------- MAX17043 SIMPLIFIÉ ---------
bool fuelGaugePresent = false;

bool initFuelGauge() {
    Serial.println("🔋 Initializing MAX17043...");
    
    Wire.beginTransmission(0x36);
    if (Wire.endTransmission() == 0) {
        FuelGauge.begin();
        fuelGaugePresent = true;
        
        // Initial configuration of the MAX17043
        Serial.println("🔧 Configuring MAX17043...");

        // 1. Reset the fuel gauge
        FuelGauge.reset();
        delay(500);  // Wait for reset

        // 2. Quick-start to recalibrate
        FuelGauge.quickstart();
        delay(1000);  // Wait for calibration

        // 3. Initial reading
        float voltage = FuelGauge.voltage();
        float percent = FuelGauge.percent();
        
        Serial.print("📊 Initial reading - Battery: "); 
        Serial.print(percent, 1); Serial.print("% (");
        Serial.print(voltage, 3); Serial.println("V)");

        // 4. If the percentage is still 0 with voltage > 3.7V, force an estimate
        if (percent < 1.0 && voltage > 3.7) {
            Serial.println("⚠️ MAX17043 needs manual calibration...");
            
            // Estimate based on standard LiPo voltage
            float estimatedPercent = estimateBatteryPercent(voltage);
            Serial.print("💡 Estimated percentage: ");
            Serial.print(estimatedPercent, 1);
            Serial.println("%");
            
            // Configure the alert threshold to 10%
            FuelGauge.setThreshold(10);
        }
        
        // 5. Final reading after configuration
        delay(500);
        voltage = FuelGauge.voltage();
        percent = FuelGauge.percent();
        
        Serial.print("✅ Final reading - Battery: "); 
        Serial.print(percent, 1); Serial.print("% (");
        Serial.print(voltage, 3); Serial.println("V)");
        
        Serial.print("🔧 fuelGaugePresent = ");
        Serial.println(fuelGaugePresent ? "true" : "false");
        
        return true;
    } else {
        Serial.println("❌ MAX17043 not detected");
        fuelGaugePresent = false;
        return false;
    }
// Function for manual estimation based on voltage
float estimateBatteryPercent(float voltage) {
    // Courbe de décharge LiPo 3.7V typique
    if (voltage >= 4.15) return 100.0;
    if (voltage >= 4.00) return 90.0 + (voltage - 4.00) * 66.7;  // 90-100%
    if (voltage >= 3.90) return 70.0 + (voltage - 3.90) * 200.0; // 70-90%
    if (voltage >= 3.80) return 40.0 + (voltage - 3.80) * 300.0; // 40-70%
    if (voltage >= 3.70) return 20.0 + (voltage - 3.70) * 200.0; // 20-40%
    if (voltage >= 3.50) return 5.0 + (voltage - 3.50) * 75.0;   // 5-20%
    if (voltage >= 3.30) return 1.0 + (voltage - 3.30) * 20.0;   // 1-5%
    return 0.0; // < 3.3V = vide
}

float readBatteryVoltage() {
    if (!fuelGaugePresent) return 0.0;
    return FuelGauge.voltage();
}

uint8_t readBatteryPercent() {
    // Always try to read the MAX17043, even if fuelGaugePresent is false.
    float percent = 0.0;
    float voltage = 0.0;

    // Test communication with the MAX17043
    Wire.beginTransmission(0x36);
    if (Wire.endTransmission() == 0) {
        // MAX17043 responds, try to read data
        percent = FuelGauge.percent();
        voltage = FuelGauge.voltage();
        
        Serial.print("🔋 MAX17043 raw: "); Serial.print(percent, 1);
        Serial.print("% ("); Serial.print(voltage, 3); Serial.println("V)");
    } else {
        Serial.println("❌ MAX17043 communication failed");
        return 0; // No sensor available
    }

    // Check if the MAX17043 reading is consistent
    bool percentValid = (percent >= 0 && percent <= 100 && !isnan(percent));
    bool voltageRealistic = (voltage > 3.0 && voltage < 4.5);
    
    if (!percentValid || !voltageRealistic) {
        Serial.println("⚠️ MAX17043 invalid reading, using voltage estimation");
        return (uint8_t)round(estimateBatteryPercent(voltage));
    }

    // If the MAX17043 shows 0% but voltage > 3.7V, use the estimation
    if (percent < 1.0 && voltage > 3.7) {
        Serial.print("🔄 MAX17043 shows "); Serial.print(percent, 1);
        Serial.print("% but voltage is "); Serial.print(voltage, 3);
        Serial.println("V - using estimation");
        
        float estimated = estimateBatteryPercent(voltage);
        return (uint8_t)round(estimated);
    }
    
    Serial.print("✅ Using MAX17043: "); Serial.print(percent, 1); Serial.println("%");
    return (uint8_t)round(percent);
}

float readSolarVoltage() {
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

// --------- Diagnostic MAX17043 ---------
void diagnosticMAX17043() {
    if (!fuelGaugePresent) {
        Serial.println("❌ MAX17043 not present for diagnostic");
        return;
    }
    
    Serial.println("\n🔍 MAX17043 DIAGNOSTIC");
    Serial.println("======================");
    
    float voltage = FuelGauge.voltage();
    float percent = FuelGauge.percent();
    float estimated = estimateBatteryPercent(voltage);
    
    Serial.print("📊 Raw voltage: "); Serial.print(voltage, 3); Serial.println("V");
    Serial.print("📊 Raw percent: "); Serial.print(percent, 2); Serial.println("%");
    Serial.print("💡 Estimated percent: "); Serial.print(estimated, 1); Serial.println("%");
    
    // Test internal registers
    uint16_t version = FuelGauge.version();
    Serial.print("🔧 IC Version: 0x"); Serial.println(version, HEX);
    
    // Status and configuration
    Serial.print("⚡ Alert threshold: "); Serial.print(FuelGauge.getThreshold()); Serial.println("%");
    Serial.print("🚨 Alert status: "); Serial.println(FuelGauge.alertIsActive() ? "ACTIVE" : "OK");
    
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

bool initENVUnit() {
    Serial.println("🌡️ Initializing M5Stack ENV Unit...");
    
    // Initialisation SHT30 (température/humidité)
    bool sht30_ok = sht30.begin(&Wire, SHT3X_I2C_ADDR, SDA_PIN, SCL_PIN, 400000U);
    if (!sht30_ok) {
        Serial.println("❌ SHT30 not detected");
    } else {
        Serial.println("✓ SHT30 ready");
    }
    
    // Initialisation QMP6988 (pression/altitude)
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
void goToSleep() {
    Serial.println("🌙 Entering deep sleep...");
    Serial.print("💤 Sleep duration: "); Serial.print(SLEEP_MINUTES); Serial.println(" minutes");
    
    esp_sleep_enable_timer_wakeup(SLEEP_TIME_S * uS_TO_S_FACTOR);
    
    Serial.println("💤 Good night...");
    Serial.flush();
    
    esp_deep_sleep_start();
}

bool shouldSleep() {
    uint8_t batteryPercent = readBatteryPercent();
    float solarVoltage = readSolarVoltage();
    
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

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    ++bootCount;
    Serial.println("\n=== ESP32 WAKE UP ===");
    Serial.print("Boot number: "); Serial.println(bootCount);
    
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason) {
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
    
    Wire.begin(SDA_PIN, SCL_PIN);
    setupADC();
    
    // Init capteurs
    initFuelGauge();
    
    // Diagnostic MAX17043 si premier boot ou problème détecté
    if (bootCount <= 2) {
        diagnosticMAX17043();
    }
    
    initENVUnit();
    
    // Init LoRa
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
    
    // Intelligent JOIN management
    bool needsJoin = true;
    uint32_t currentTime = millis() / 1000; // seconds since boot
    
    // Check if we have a recent valid session (< 24h)
    if (hasValidSession && (currentTime - lastJoinTime) < 86400) {
        Serial.println("🔄 Trying to reuse existing session...");
        // Try to send directly without re-JOIN
        needsJoin = false;
    }
    
    if (needsJoin || !hasValidSession) {
        Serial.println("🔗 Attempting OTAA join...");
        node.join();
        
        unsigned long joinStart = millis();
        while (!node.isJoined() && millis() - joinStart < 60000) {
            delay(1000);
            Serial.print(".");
        }
        
        if (node.isJoined()) {
            Serial.println("\n✅ Joined successfully!");
            wasJoined = true;
            hasValidSession = true;
            lastJoinTime = currentTime;
            frameCounter = 0;
            
        } else {
            Serial.println("\n❌ Join failed");
            wasJoined = false;
            hasValidSession = false;
        }
    } else {
        // Assume session is still valid
        wasJoined = true;
        Serial.println("🔄 Using cached session");
    }
    
    if (wasJoined) {
        sendTemperature();
        delay(2000);
    }
    
    if (shouldSleep()) {
        goToSleep();
    } else {
        Serial.println("ℹ️ Staying awake (debug or critical battery)");
    }
}

void sendTemperature() {
    if (!envUnitPresent) {
        Serial.println("❌ ENV Unit not available");
        return;
    }
    
    // Lecture des données
    bool sht30_updated = sht30.update();
    bool qmp_updated = qmp.update();
    
    if (!sht30_updated) {
        Serial.println("❌ SHT30 reading failed");
        return;
    }
    
    // Data SHT30
    float tempC = sht30.cTemp;
    float hum = sht30.humidity;
    
    // Data battery and solar
    float vbat = readBatteryVoltage();
    uint8_t battPct = readBatteryPercent();
    float vsolar = readSolarVoltage();
    
    if (qmp_updated && qmp.pressure > 80000 && qmp.pressure < 120000) {
        // Extended format with pressure (0x05)
        float pressure = qmp.pressure / 100.0; // Conversion from Pa to hPa
        float altitude = qmp.altitude;
        
        int16_t t100 = (int16_t)round(tempC * 100.0);
        uint16_t h100 = (uint16_t)round(hum * 100.0);
        uint16_t p10 = (uint16_t)round(pressure * 10.0);
        int16_t alt = (int16_t)round(altitude);
        uint16_t solarMv = (uint16_t)round(vsolar * 1000.0);
        
        uint8_t payload[12];
        payload[0] = 0x05; // Extended format
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
        
        Serial.println("\n📊 EXTENDED SENSOR DATA:");
        Serial.print("  Temperature: "); Serial.print(tempC, 1); Serial.println("°C");
        Serial.print("  Humidity: "); Serial.print(hum, 1); Serial.println("%");
        Serial.print("  Pressure: "); Serial.print(pressure, 1); Serial.println(" hPa");
        Serial.print("  Altitude: "); Serial.print(altitude, 0); Serial.println(" m");
        Serial.print("  Battery: "); Serial.print(battPct); Serial.print("% ("); 
        Serial.print(vbat, 3); Serial.println("V)");
        Serial.print("  Solar: "); Serial.print(vsolar, 3); Serial.println("V");
        
        bool sent = node.sendPacket(payload, sizeof(payload));
        if (sent) {
            Serial.println("✅ Extended data transmission completed");
            frameCounter++;
        } else {
            Serial.println("⚠️ Extended data transmission requested");
            // If sending fails, mark the session as invalid
            hasValidSession = false;
        }
        
    } else {
        // Basic format without pressure (0x04)
        int16_t t100 = (int16_t)round(tempC * 100.0);
        uint16_t h100 = (uint16_t)round(hum * 100.0);
        uint16_t solarMv = (uint16_t)round(vsolar * 1000.0);
        
        uint8_t payload[8];
        payload[0] = 0x04; // Basic format
        payload[1] = (t100 >> 8) & 0xFF;
        payload[2] = t100 & 0xFF;
        payload[3] = (h100 >> 8) & 0xFF;
        payload[4] = h100 & 0xFF;
        payload[5] = battPct;
        payload[6] = (solarMv >> 8) & 0xFF;
        payload[7] = solarMv & 0xFF;
        
        Serial.println("\n📊 BASIC SENSOR DATA:");
        Serial.print("  Temperature: "); Serial.print(tempC, 1); Serial.println("°C");
        Serial.print("  Humidity: "); Serial.print(hum, 1); Serial.println("%");
        Serial.print("  Battery: "); Serial.print(battPct); Serial.print("% ("); 
        Serial.print(vbat, 3); Serial.println("V)");
        Serial.print("  Solar: "); Serial.print(vsolar, 3); Serial.println("V");
        
        if (!qmp_updated) {
            Serial.println("  Pressure: Not available (QMP6988 failed)");
        }
        
        bool sent = node.sendPacket(payload, sizeof(payload));
        if (sent) {
            Serial.println("✅ Basic data transmission completed");
            frameCounter++;
        } else {
            Serial.println("⚠️ Basic data transmission requested");
            // If sending fails, mark the session as invalid
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
        goToSleep();
    }
}