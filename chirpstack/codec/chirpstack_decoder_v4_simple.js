function decodeUplink(input) {
    const bytes = input.bytes;
    const fPort = input.fPort;
    let decoded = {};
    
    if (bytes.length < 1) {
        return {
            data: { error: "Payload too short" }
        };
    }
    
    const type = bytes[0];
    
    if (type === 0x05 && bytes.length === 12) {
        
        
        // Temperature (°C, signed, x100)
        let temp_raw = (bytes[1] << 8) | bytes[2];
        if (temp_raw > 32767) temp_raw -= 65536;
        decoded.temperature = temp_raw / 100;
        
        // Humidity (%, x100)
        const hum_raw = (bytes[3] << 8) | bytes[4];
        decoded.humidity = hum_raw / 100;
        
        // Pressure (hPa, x10)
        const pressure_raw = (bytes[5] << 8) | bytes[6];
        decoded.pressure = pressure_raw / 10;
        
        // Local pressure validation (adjusted for Swiss altitude)
        if (decoded.pressure < 850 || decoded.pressure > 1100) {
            decoded.pressure_warning = "Out of normal range (850-1100 hPa) - sensor may be faulty";
        }
        if (decoded.pressure < 900 || decoded.pressure > 1050) {
            decoded.pressure_alert = "Extreme value detected";
        }
        
        // Altitude (m, signed)
        let alt_raw = (bytes[7] << 8) | bytes[8];
        if (alt_raw > 32767) alt_raw -= 65536;
        decoded.altitude = alt_raw;
        
        // Battery (%)
        decoded.battery_percent = bytes[9];
        
        // Solar voltage (V, mV)
        const solar_mv = (bytes[10] << 8) | bytes[11];
        decoded.solar_voltage = solar_mv / 1000;
        
        // Additional calculations
        
        // Sea level pressure with diagnostic info (CORRECTED FORMULA)
        if (decoded.altitude > 0) {
            // Formule barométrique internationale correcte
            const factor = Math.pow(1 - (0.0065 * decoded.altitude) / (decoded.temperature + 273.15), -5.257);
            decoded.sea_level_pressure = Math.round(decoded.pressure * factor * 10) / 10;  // MULTIPLY, don't divide!
            
            // Debug info for Swiss altitude correction
            decoded.altitude_correction = Math.round((decoded.sea_level_pressure - decoded.pressure) * 10) / 10;
            decoded.pressure_factor = Math.round(factor * 1000) / 1000;
        } else {
            decoded.sea_level_pressure = decoded.pressure;
            decoded.altitude_correction = 0;
        }
        
        // Comfort index
        if (decoded.temperature >= 18 && decoded.temperature <= 24 && 
            decoded.humidity >= 40 && decoded.humidity <= 60) {
            decoded.comfort_index = "Optimal";
        } else if (decoded.temperature >= 16 && decoded.temperature <= 26 && 
                   decoded.humidity >= 30 && decoded.humidity <= 70) {
            decoded.comfort_index = "Confortable";
        } else if (decoded.temperature >= 10 && decoded.temperature <= 30) {
            decoded.comfort_index = "Acceptable";
        } else {
            decoded.comfort_index = "Inconfortable";
        }
        
        // Advanced Weather State Classification
        // Based on sea level pressure (corrected for altitude) - Swiss/Alpine regions
        let weatherState = "Unknown";
        let weatherIcon = "unknown";
        
        // Use sea level pressure for weather classification (not local pressure)
        const weatherPressure = decoded.sea_level_pressure;
        
        // Validation: realistic pressure at sea level for weather (960-1080 hPa)
        const pressureValid = weatherPressure >= 960 && weatherPressure <= 1080;
        
        if (!pressureValid) {
            weatherState = "Sensor Error";
            weatherIcon = "alert";
            decoded.weather_error = `Sea level pressure ${weatherPressure} hPa is outside realistic range (970-1070)`;
        } else {
            // Weather classification based on sea level pressure (Switzerland/Alps)
            if (weatherPressure > 1030) {
                weatherState = "High Pressure";
                weatherIcon = "sunny";
            } else if (weatherPressure > 1020) {
                weatherState = "Stable";
                weatherIcon = "partly-cloudy";
            } else if (weatherPressure > 1013) {
                weatherState = "Improving";
                weatherIcon = "partly-sunny";
            } else if (weatherPressure > 1005) {
                weatherState = "Variable";
                weatherIcon = "cloudy";
            } else if (weatherPressure > 995) {
                weatherState = "Unsettled"; 
                weatherIcon = "cloudy";
            } else if (weatherPressure > 980) {
                weatherState = "Low Pressure";
                weatherIcon = "rainy";
            } else {
                weatherState = "Storm Risk";  // Seulement < 980 hPa niveau mer
                weatherIcon = "lightning-rainy";
            }
        }
        
        // Secondary classification by humidity (moisture content)
        let humidityState = "Normal";
        if (decoded.humidity > 85) {
            humidityState = "Very Humid";
        } else if (decoded.humidity > 70) {
            humidityState = "Humid";
        } else if (decoded.humidity > 60) {
            humidityState = "Moderate";
        } else if (decoded.humidity > 40) {
            humidityState = "Normal";
        } else if (decoded.humidity > 25) {
            humidityState = "Dry";
        } else {
            humidityState = "Very Dry";
        }
        
        // Temperature classification
        let tempState = "Mild";
        if (decoded.temperature > 30) {
            tempState = "Hot";
        } else if (decoded.temperature > 25) {
            tempState = "Warm";
        } else if (decoded.temperature > 15) {
            tempState = "Mild";
        } else if (decoded.temperature > 5) {
            tempState = "Cool";
        } else if (decoded.temperature > 0) {
            tempState = "Cold";
        } else {
            tempState = "Freezing";
        }
        
        // Combined weather assessment
        decoded.weather_state = weatherState;
        decoded.weather_icon = weatherIcon;
        decoded.humidity_state = humidityState;
        decoded.temperature_state = tempState;
        
        // Weather trend (based on sea level pressure - Swiss/Alpine regions)
        if (weatherPressure > 1020) {
            decoded.weather_trend = "Stable to Improving";
        } else if (weatherPressure > 1005) {
            decoded.weather_trend = "Variable";
        } else if (weatherPressure > 990) {
            decoded.weather_trend = "Unsettled";  
        } else {
            decoded.weather_trend = "Deteriorating";
        }
        
        decoded.payload_type = "weather_extended";
        
    } else if (type === 0x04 && bytes.length === 8) {
        // Basic format: T+H+Batt+Solar
        
        let temp_raw = (bytes[1] << 8) | bytes[2];
        if (temp_raw > 32767) temp_raw -= 65536;
        decoded.temperature = temp_raw / 100;
        
        const hum_raw = (bytes[3] << 8) | bytes[4];
        decoded.humidity = hum_raw / 100;
        
        decoded.battery_percent = bytes[5];
        
        const solar_mv = (bytes[6] << 8) | bytes[7];
        decoded.solar_voltage = solar_mv / 1000;
        
        decoded.payload_type = "weather_basic";
        
    } else {
        return {
            data: { 
                error: "Unsupported payload type",
                type: type,
                length: bytes.length
            }
        };
    }
    
    return {
        data: decoded
    };
}