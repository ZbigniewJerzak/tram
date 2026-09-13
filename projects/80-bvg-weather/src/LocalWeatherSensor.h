#pragma once

#include <Arduino.h>

#include "WeatherData.h"

class LocalWeatherSensor
{
public:
    explicit LocalWeatherSensor(const char* endpoint);

    bool fetch(LocalWeatherReading& result, String& error) const;

private:
    const char* endpoint_;
};
