# 🔧 Troubleshooting: Weather Classification Problems

## 🚨 Observed Symptoms

- **Weather state displayed**: "Storm Risk" / "Forte perturbation"
- **Actual conditions**: Bright sunshine, clear blue sky
- **Duration**: Atmospheric pressure identical for 3+ days

## 🔍 Probable Cause

The **QMP6988** sensor (atmospheric pressure) returns a **stuck or erroneous value**, which completely distorts the ChirpStack codec's weather classification.

### Weather Classification by Pressure

| Pressure (hPa) | Weather State | Icon | Typical Conditions |
|----------------|---------------|------|--------------------|
| > 1030 | High Pressure | ☀️ sunny | Stable fair weather |
| 1020-1030 | Stable | 🌤️ partly-cloudy | Stable weather |
| 1013-1020 | Improving | 🌥️ partly-sunny | Improvement |
| 1005-1013 | Variable | ☁️ cloudy | Variable |
| 995-1005 | Deteriorating | 🌦️ partly-rainy | Deterioration |
| 985-995 | Unstable | 🌧️ rainy | Unstable |
| < 985 | **Storm Risk** | ⛈️ lightning-rainy | **Severe disturbance** |

**If your pressure is < 950 hPa or > 1050 hPa → sensor is defective!**

## 🛠️ Diagnostic Procedure

### Step 1: Check ESP32 Logs

Upload the diagnostic Arduino code and observe the **Serial Monitor**:

```
🔍 QMP6988 DIAGNOSTIC:
  Raw pressure: 98523 Pa
  Pressure: 985.2 hPa
  Altitude: 125.3 m
  Temperature: 22.1 °C
  ⚠️ WARNING: Pressure out of normal range (900-1100 hPa)
```

**Problem indicators:**
- Pressure < 900 hPa or > 1100 hPa
- Pressure **exactly identical** across multiple transmissions
- Message `⚠️ WARNING: Pressure stuck at XXX hPa for N readings`

### Step 2: Analyze MQTT Data in Home Assistant

Check your `sensor.atmospheric_pressure_lora` sensor:

```yaml
Developer Tools → States → sensor.atmospheric_pressure_lora
```

**Normal values for Switzerland:**
- Minimum: 980 hPa (major depression)
- Normal: 1000-1020 hPa
- Maximum: 1040 hPa (strong anticyclone)

**If you see:**
- **< 950 hPa** → Defective sensor
- **> 1050 hPa** → Defective sensor
- **Exactly identical value for 24h+** → Stuck sensor

### Step 3: Check ChirpStack Codec

The updated codec adds diagnostic fields in the decoded payload:

```json
{
  "pressure": 875.3,
  "pressure_warning": "Out of normal range (900-1100 hPa) - sensor may be faulty",
  "pressure_alert": "Extreme value detected",
  "weather_state": "Sensor Error",
  "weather_error": "Pressure 875.3 hPa is outside realistic range (950-1050)"
}
```

In ChirpStack:
1. Go to **Applications** → **Devices** → Your station
2. Click **LoRaWAN frames**
3. Observe decoded payload (`object`)
4. Look for `pressure_warning` or `weather_error` fields

## 🔧 Solutions in Priority Order

### Solution 1: QMP6988 Software Reset

Add this function to your Arduino code (after `initENVUnit()`):

```cpp
bool resetQMP6988() {
    Serial.println("🔄 Attempting QMP6988 reset...");
    
    // I2C reset
    Wire.end();
    delay(100);
    Wire.begin(SDA_PIN, SCL_PIN);
    delay(100);
    
    // QMP6988 reinitialization
    bool success = qmp.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, SDA_PIN, SCL_PIN, 400000U);
    
    if (success) {
        delay(200);
        if (qmp.update()) {
            float pressure = qmp.pressure / 100.0;
            Serial.print("✅ Reset successful - New pressure: ");
            Serial.print(pressure, 1);
            Serial.println(" hPa");
            return true;
        }
    }
    
    Serial.println("❌ Reset failed");
    return false;
}
```

Call this function in `setup()` if pressure is out of limits.

### Solution 2: Hardware Verification

1. **Check I2C connections**:
   - SDA (GPIO 6) properly connected
   - SCL (GPIO 7) properly connected
   - Stable 3.3V power supply

2. **Test sensor in isolation**:
```cpp
void loop() {
    if (qmp.update()) {
        Serial.print("Pressure: ");
        Serial.print(qmp.pressure / 100.0, 1);
        Serial.println(" hPa");
    }
    delay(5000);
}
```

### Solution 3: Sensor Replacement

If after software/hardware reset the pressure remains stuck or aberrant:
- The **QMP6988 is defective**
- Order a new **M5Stack ENV Unit** (SHT30 + QMP6988)
- Alternative: **BME280** (temperature + humidity + pressure)

### Solution 4: Degraded Mode (Without Pressure)

While waiting for replacement, modify the codec to ignore pressure:

```javascript
// In chirpstack_decoder_v4_simple.js
const pressureValid = decoded.pressure >= 950 && decoded.pressure <= 1050;

if (!pressureValid) {
    // Degraded mode: classification by humidity/temperature only
    if (decoded.humidity > 80) {
        weatherState = "Humid";
        weatherIcon = "rainy";
    } else if (decoded.temperature > 25) {
        weatherState = "Warm";
        weatherIcon = "sunny";
    } else {
        weatherState = "No Pressure Data";
        weatherIcon = "help";
    }
}
```

## 📊 Continuous Monitoring

Add this automation in Home Assistant for alerts:

```yaml
automation:
  - alias: "⚠️ Weather Station - Pressure Sensor Alert"
    trigger:
      - platform: numeric_state
        entity_id: sensor.atmospheric_pressure_lora
        below: 950
        for: "00:05:00"
      - platform: numeric_state
        entity_id: sensor.atmospheric_pressure_lora
        above: 1050
        for: "00:05:00"
    action:
      - service: notify.mobile_app_your_phone
        data:
          title: "⚠️ Pressure Sensor Error"
          message: >
            Pressure: {{ states('sensor.atmospheric_pressure_lora') }} hPa
            QMP6988 sensor may be faulty!
```

## 🎯 Diagnostic Checklist

- [ ] Upload new Arduino code with diagnostics
- [ ] Observe Serial Monitor logs (raw pressure Pa and converted hPa)
- [ ] Check pressure in Home Assistant for 1 hour
- [ ] Compare with local weather station (Météo France, Windy.com)
- [ ] Test QMP6988 software reset
- [ ] Verify physical I2C connections
- [ ] If necessary: order new M5Stack ENV Unit

## 📝 Reference Values

**Normal sea level pressure: 1013.25 hPa**

**Typical variations:**
- Winter depression: 980-995 hPa
- Variable weather: 1000-1013 hPa
- Anticyclone: 1020-1035 hPa
- Record low (storm): ~950 hPa
- Record high (exceptional anticyclone): ~1050 hPa

**If your station is at altitude, correct:**
- 450m → -50 hPa (measured pressure ~963 hPa = 1013 hPa at sea level)
- The codec automatically calculates `sea_level_pressure`

## 🔗 Useful Links

- [Météo France - Real-time pressure](https://meteofrance.com)
- [Windy - Pressure maps](https://www.windy.com)
- [QMP6988 Datasheet](https://datasheet.lcsc.com/lcsc/2012031830_QST-QMP6988_C457708.pdf)