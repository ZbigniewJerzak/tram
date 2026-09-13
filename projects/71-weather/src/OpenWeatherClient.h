#pragma once

#include <Arduino.h>
#include <time.h>

#include "WeatherData.h"

class OpenWeatherClient
{
public:
    OpenWeatherClient(float latitude, float longitude);

    bool fetch(
        const String& apiKey,
        time_t now,
        WeatherForecast& result,
        String& error
    ) const;

private:
    float latitude_;
    float longitude_;
};
