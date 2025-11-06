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
        
        // Sea level pressure
        if (decoded.altitude > 0) {
            const factor = Math.pow(1 - (0.0065 * decoded.altitude) / (decoded.temperature + 273.15), -5.257);
            decoded.sea_level_pressure = Math.round(decoded.pressure / factor * 10) / 10;
        } else {
            decoded.sea_level_pressure = decoded.pressure;
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
        
        // Weather state
        if (decoded.pressure > 1020) {
            decoded.weather_state = "Stable";
        } else if (decoded.pressure > 1013) {
            decoded.weather_state = "Amélioration";
        } else if (decoded.pressure > 1000) {
            decoded.weather_state = "Variable";
        } else if (decoded.pressure > 990) {
            decoded.weather_state = "Dégradation";
        } else {
            decoded.weather_state = "Instable";
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