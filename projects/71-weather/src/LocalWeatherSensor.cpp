#include "LocalWeatherSensor.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

namespace
{
constexpr unsigned long HTTP_TIMEOUT_MS = 15000UL;
constexpr char ENVIRONMENT_SENSOR_MODEL[] = "32000064_S";
}

LocalWeatherSensor::LocalWeatherSensor(const char* const endpoint)
    : endpoint_(endpoint)
{
}

bool LocalWeatherSensor::fetch(
    LocalWeatherReading& result,
    String& error
) const
{
    result = LocalWeatherReading{};
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.useHTTP10(true);
    if (!http.begin(endpoint_))
    {
        error = "Lokalsensor: HTTP-Start fehlgeschlagen";
        return false;
    }

    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "CrowPanel-Weather/1.0");
    const int status = http.GET();
    if (status != HTTP_CODE_OK)
    {
        error = status < 0
            ? "Lokalsensor nicht erreichbar"
            : "Lokalsensor HTTP " + String(status);
        http.end();
        return false;
    }

    JsonDocument filter;
    filter["meters"][0]["deviceNumber"] = true;
    filter["meters"][0]["statusValid"] = true;
    filter["meters"][0]["readings"]["temperature_primary"] = true;
    filter["meters"][0]["readings"]["sun_detected"] = true;
    filter["meters"][0]["readings"]["rain_detected"] = true;

    JsonDocument document;
    const DeserializationError jsonError = deserializeJson(
        document,
        http.getStream(),
        DeserializationOption::Filter(filter)
    );
    http.end();
    if (jsonError)
    {
        error = "Lokalsensor JSON: " + String(jsonError.c_str());
        return false;
    }

    for (JsonObjectConst meter : document["meters"].as<JsonArrayConst>())
    {
        const JsonObjectConst readings = meter["readings"].as<JsonObjectConst>();
        const bool knownModel =
            String(meter["deviceNumber"] | "") == ENVIRONMENT_SENSOR_MODEL;
        const bool hasTemperature =
            !readings["temperature_primary"].isNull();
        if (!knownModel && !hasTemperature)
        {
            continue;
        }

        result.found = true;
        result.statusValid = meter["statusValid"] | false;
        result.temperature = readings["temperature_primary"] | 0.0F;
        const bool rain = readings["rain_detected"] | false;
        const bool sun = readings["sun_detected"] | false;
        result.condition = rain
            ? WeatherCondition::RAIN
            : (sun ? WeatherCondition::SUN : WeatherCondition::CLOUDS);
        error = "";
        return true;
    }

    error = "Umweltsensor nicht gefunden";
    return false;
}
