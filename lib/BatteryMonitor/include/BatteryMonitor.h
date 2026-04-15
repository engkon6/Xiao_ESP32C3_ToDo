#pragma once
#include <cstdint>
#include <esp_adc/adc_cali.h>

class BatteryMonitor {
public:
    explicit BatteryMonitor(uint8_t adcPin, float dividerMultiplier = 2.0f);

    uint16_t readPercentage() const;
    uint16_t readMillivolts() const;
    double readVolts() const;
    static uint16_t percentageFromMillivolts(uint16_t millivolts);

private:
    uint8_t _adcPin;
    float _dividerMultiplier;
    mutable adc_cali_handle_t _cali_handle = nullptr;
    uint16_t readRawMillivolts() const;
};