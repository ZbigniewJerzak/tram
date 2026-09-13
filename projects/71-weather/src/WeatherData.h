#pragma once

#include <Arduino.h>

enum class WeatherCondition : uint8_t
{
    UNKNOWN,
    SUN,
    CLOUDS,
    RAIN
};

struct LocalWeatherReading
{
    bool found = false;
    bool statusValid = false;
    float temperature = 0.0F;
    WeatherCondition condition = WeatherCondition::UNKNOWN;
};

struct ForecastSummary
{
    bool valid = false;
    uint8_t sampleCount = 0;
    bool daytimeTemperatureValid = false;
    bool nighttimeTemperatureValid = false;
    float daytimeWeightedTemperature = 0.0F;
    float nighttimeWeightedTemperature = 0.0F;
    float minimumTemperature = 0.0F;
    float maximumTemperature = 0.0F;
    bool rainExpected = false;
    uint8_t firstRainProbability = 0;
    float firstRainMillimeters = 0.0F;
    int8_t firstRainHour = -1;
};

struct SolarTimes
{
    bool valid = false;
    uint8_t sunriseHour = 0;
    uint8_t sunriseMinute = 0;
    uint8_t sunsetHour = 0;
    uint8_t sunsetMinute = 0;
};

struct WeatherForecast
{
    ForecastSummary today;
    ForecastSummary tomorrow;
    SolarTimes todaySolarTimes;
};
