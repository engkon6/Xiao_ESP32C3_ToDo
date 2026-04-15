#include "BatteryMonitor.h"
#include <Arduino.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

BatteryMonitor::BatteryMonitor(uint8_t adcPin, float dividerMultiplier)
  : _adcPin(adcPin), _dividerMultiplier(dividerMultiplier), _cali_handle(nullptr)
{
}

uint16_t BatteryMonitor::readPercentage() const
{
    return percentageFromMillivolts(readMillivolts());
}

uint16_t BatteryMonitor::readMillivolts() const
{
    const uint16_t raw = readRawMillivolts();
    return static_cast<uint16_t>(raw * _dividerMultiplier);
}

uint16_t BatteryMonitor::readRawMillivolts() const
{
    // Initialize calibration handle only once
    if (_cali_handle == nullptr) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        adc_cali_create_scheme_curve_fitting(&cali_config, &_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
            .default_vref = 1100,
        };
        adc_cali_create_scheme_line_fitting(&cali_config, &_cali_handle);
#endif
    }

    // Read and average 16 samples for stability
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        const uint16_t raw = analogRead(_adcPin);
        int voltage = 0;
        adc_cali_raw_to_voltage(_cali_handle, raw, &voltage);
        sum += voltage;
        delay(1);
    }
    
    return static_cast<uint16_t>(sum / 16);
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