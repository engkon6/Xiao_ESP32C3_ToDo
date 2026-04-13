#include "BatteryMonitor.h"
#include <esp_idf_version.h>
#include <Arduino.h>
#if ESP_IDF_VERSION_MAJOR < 5
#include <esp_adc_cal.h>
#endif

BatteryMonitor::BatteryMonitor(uint8_t adcPin, float dividerMultiplier)
  : _adcPin(adcPin), _dividerMultiplier(dividerMultiplier)
{
}

uint16_t BatteryMonitor::readPercentage() const
{
    return percentageFromMillivolts(readMillivolts());
}

uint16_t BatteryMonitor::readMillivolts() const
{
#if ESP_IDF_VERSION_MAJOR < 5
    // ESP-IDF 4.x doesn't have analogReadMilliVolts, so we need to do the calibration manually
    uint32_t sum = 0;
    for(int i = 0; i < 16; i++) {
        const uint16_t raw = analogRead(_adcPin);
        esp_adc_cal_characteristics_t adc_chars;
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);
        sum += esp_adc_cal_raw_to_voltage(raw, &adc_chars);
        delay(1);
    }
    const uint16_t mv = sum / 16;
#else
    // ESP-IDF 5.x has analogReadMilliVolts - average 16 samples for stability
    uint32_t sum = 0;
    for(int i = 0; i < 16; i++) {
        sum += analogReadMilliVolts(_adcPin);
        delay(1);
    }
    const uint16_t mv = sum / 16;
#endif

    return static_cast<uint16_t>(mv * _dividerMultiplier);
}

double BatteryMonitor::readVolts() const
{
    return static_cast<double>(readMillivolts()) / 1000.0;
}

uint16_t BatteryMonitor::percentageFromMillivolts(uint16_t millivolts)
{
    // Simple linear: 3.0V = 0%, 4.2V = 100%
    // y = 100 * (volts - 3.0) / (4.2 - 3.0)
    double volts = millivolts / 1000.0;
    double y = 100.0 * (volts - 3.0) / 1.2;

    // Clamp to [0,100] and round
    y = std::max(y, 0.0);
    y = std::min(y, 100.0);
    y = round(y);
    return y;
}
