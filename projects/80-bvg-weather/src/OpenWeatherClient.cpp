#include "OpenWeatherClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cmath>

namespace
{
constexpr unsigned long HTTP_TIMEOUT_MS = 20000UL;
constexpr float RAIN_PROBABILITY_THRESHOLD = 0.30F;
constexpr time_t FORECAST_SLOT_SECONDS = 3 * 60 * 60;
constexpr uint8_t FALLBACK_SUNRISE_HOUR = 6;
constexpr uint8_t FALLBACK_SUNSET_HOUR = 18;
constexpr char OPENWEATHER_HOST[] = "api.openweathermap.org";
constexpr char OPENWEATHER_CANONICAL_HOST[] = "eu-api.openweathermap.org";
constexpr uint8_t DNS_ATTEMPTS = 2;
constexpr uint32_t TCP_PROBE_TIMEOUT_MS = 5000UL;

const char* wifiStatusText(const wl_status_t status)
{
    switch (status)
    {
        case WL_NO_SSID_AVAIL:
            return "SSID nicht gefunden";
        case WL_SCAN_COMPLETED:
            return "Scan abgeschlossen";
        case WL_CONNECTED:
            return "verbunden";
        case WL_CONNECT_FAILED:
            return "Verbindung fehlgeschlagen";
        case WL_CONNECTION_LOST:
            return "Verbindung verloren";
        case WL_DISCONNECTED:
            return "getrennt";
        case WL_IDLE_STATUS:
            return "inaktiv";
        default:
            return "unbekannt";
    }
}

void logTransportFailure(
    WiFiClientSecure& client,
    const int httpStatus,
    const unsigned long elapsedMs,
    const IPAddress& preflightAddress
)
{
    const wl_status_t wifiStatus = WiFi.status();
    Serial.printf(
        "OpenWeather DEBUG: GET fehlgeschlagen nach %lu ms\n",
        elapsedMs
    );
    Serial.printf(
        "OpenWeather DEBUG: HTTPClient status=%d, text='%s'\n",
        httpStatus,
        HTTPClient::errorToString(httpStatus).c_str()
    );
    Serial.printf(
        "OpenWeather DEBUG: WLAN status=%d (%s), IP=%s, RSSI=%d dBm\n",
        static_cast<int>(wifiStatus),
        wifiStatusText(wifiStatus),
        WiFi.localIP().toString().c_str(),
        WiFi.RSSI()
    );
    Serial.printf(
        "OpenWeather DEBUG: konfigurierte DNS-Server: DNS1=%s, DNS2=%s\n",
        WiFi.dnsIP(0).toString().c_str(),
        WiFi.dnsIP(1).toString().c_str()
    );
    Serial.printf(
        "OpenWeather DEBUG: TLS connected=%s, free heap=%u Bytes\n",
        client.connected() ? "ja" : "nein",
        ESP.getFreeHeap()
    );

    char tlsErrorText[160] = {};
    const int tlsError = client.lastError(tlsErrorText, sizeof(tlsErrorText));
    Serial.printf(
        "OpenWeather DEBUG: TLS lastError=%d, text='%s'\n",
        tlsError,
        tlsError == 0 ? "kein TLS-Fehler gespeichert" : tlsErrorText
    );

    WiFiClient tcpProbe;
    const unsigned long probeStartedMs = millis();
    const bool tcpConnected = tcpProbe.connect(
        preflightAddress,
        443,
        TCP_PROBE_TIMEOUT_MS
    );
    Serial.printf(
        "OpenWeather DEBUG: TCP-Test %s:443 -> %s nach %lu ms\n",
        preflightAddress.toString().c_str(),
        tcpConnected ? "verbunden" : "fehlgeschlagen",
        millis() - probeStartedMs
    );
    tcpProbe.stop();

    IPAddress resolvedAddress;
    const int dnsResult = WiFi.hostByName(OPENWEATHER_HOST, resolvedAddress);
    Serial.printf(
        "OpenWeather DEBUG: DNS %s -> %s (result=%d)\n",
        OPENWEATHER_HOST,
        dnsResult == 1 ? resolvedAddress.toString().c_str() : "fehlgeschlagen",
        dnsResult
    );

    IPAddress canonicalAddress;
    const int canonicalDnsResult = WiFi.hostByName(
        OPENWEATHER_CANONICAL_HOST,
        canonicalAddress
    );
    Serial.printf(
        "OpenWeather DEBUG: DNS-Direkttest %s -> %s (result=%d)\n",
        OPENWEATHER_CANONICAL_HOST,
        canonicalDnsResult == 1
            ? canonicalAddress.toString().c_str()
            : "fehlgeschlagen",
        canonicalDnsResult
    );
}

bool resolveBeforeRequest(IPAddress& resolvedAddress)
{
    Serial.printf(
        "OpenWeather DEBUG: DNS-Preflight; DNS1=%s, DNS2=%s\n",
        WiFi.dnsIP(0).toString().c_str(),
        WiFi.dnsIP(1).toString().c_str()
    );
    for (uint8_t attempt = 1; attempt <= DNS_ATTEMPTS; ++attempt)
    {
        const unsigned long startedMs = millis();
        const int result = WiFi.hostByName(
            OPENWEATHER_HOST,
            resolvedAddress
        );
        Serial.printf(
            "OpenWeather DEBUG: DNS-Preflight %u/%u -> %s "
            "(result=%d, Dauer=%lu ms)\n",
            attempt,
            DNS_ATTEMPTS,
            result == 1
                ? resolvedAddress.toString().c_str()
                : "fehlgeschlagen",
            result,
            millis() - startedMs
        );
        if (result == 1)
        {
            return true;
        }
        delay(500);
    }
    return false;
}

struct SummaryAccumulator
{
    uint8_t count = 0;
    double daytimeWeightedTemperatureSum = 0.0;
    double daytimeWeightSeconds = 0.0;
    double nighttimeWeightedTemperatureSum = 0.0;
    double nighttimeWeightSeconds = 0.0;
    float minimumTemperature = 0.0F;
    float maximumTemperature = 0.0F;
    bool rainExpected = false;
    float firstRainProbability = 0.0F;
    float firstRainMillimeters = 0.0F;
    int8_t firstRainHour = -1;
};

bool isRainCondition(const int conditionId)
{
    return conditionId >= 200 && conditionId < 700;
}

void addSample(
    SummaryAccumulator& accumulator,
    const float temperature,
    const float probability,
    const float rainMillimeters,
    const int conditionId,
    const int8_t localHour,
    const double daytimeWeightSeconds,
    const double nighttimeWeightSeconds
)
{
    if (accumulator.count == 0)
    {
        accumulator.minimumTemperature = temperature;
        accumulator.maximumTemperature = temperature;
    }
    else
    {
        if (temperature < accumulator.minimumTemperature)
        {
            accumulator.minimumTemperature = temperature;
        }
        if (temperature > accumulator.maximumTemperature)
        {
            accumulator.maximumTemperature = temperature;
        }
    }

    ++accumulator.count;
    accumulator.daytimeWeightedTemperatureSum +=
        temperature * daytimeWeightSeconds;
    accumulator.daytimeWeightSeconds += daytimeWeightSeconds;
    accumulator.nighttimeWeightedTemperatureSum +=
        temperature * nighttimeWeightSeconds;
    accumulator.nighttimeWeightSeconds += nighttimeWeightSeconds;

    const bool rainInSample = rainMillimeters > 0.0F ||
        probability >= RAIN_PROBABILITY_THRESHOLD ||
        isRainCondition(conditionId);
    if (!accumulator.rainExpected && rainInSample)
    {
        accumulator.firstRainProbability = probability;
        accumulator.firstRainMillimeters = rainMillimeters;
        accumulator.firstRainHour = localHour;
    }
    accumulator.rainExpected = accumulator.rainExpected || rainInSample;
}

ForecastSummary finish(const SummaryAccumulator& accumulator)
{
    ForecastSummary summary;
    if (accumulator.count == 0)
    {
        return summary;
    }

    summary.valid = true;
    summary.sampleCount = accumulator.count;
    summary.daytimeTemperatureValid =
        accumulator.daytimeWeightSeconds > 0.0;
    summary.nighttimeTemperatureValid =
        accumulator.nighttimeWeightSeconds > 0.0;
    if (summary.daytimeTemperatureValid)
    {
        summary.daytimeWeightedTemperature = static_cast<float>(
            accumulator.daytimeWeightedTemperatureSum /
            accumulator.daytimeWeightSeconds
        );
    }
    if (summary.nighttimeTemperatureValid)
    {
        summary.nighttimeWeightedTemperature = static_cast<float>(
            accumulator.nighttimeWeightedTemperatureSum /
            accumulator.nighttimeWeightSeconds
        );
    }
    summary.minimumTemperature = accumulator.minimumTemperature;
    summary.maximumTemperature = accumulator.maximumTemperature;
    summary.rainExpected = accumulator.rainExpected;
    summary.firstRainProbability = static_cast<uint8_t>(
        lroundf(accumulator.firstRainProbability * 100.0F)
    );
    summary.firstRainMillimeters = accumulator.firstRainMillimeters;
    summary.firstRainHour = accumulator.firstRainHour;
    return summary;
}

double overlapSeconds(
    const time_t sampleTime,
    const time_t intervalStart,
    const time_t intervalEnd
)
{
    const time_t sampleStart = sampleTime - FORECAST_SLOT_SECONDS;
    const time_t sampleEnd = sampleTime;
    const time_t overlapStart = sampleStart > intervalStart
        ? sampleStart
        : intervalStart;
    const time_t overlapEnd = sampleEnd < intervalEnd
        ? sampleEnd
        : intervalEnd;
    return overlapEnd > overlapStart
        ? static_cast<double>(overlapEnd - overlapStart)
        : 0.0;
}

time_t localTimeForClock(
    const tm& day,
    const uint8_t hour,
    const uint8_t minute
)
{
    tm value = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = 0;
    value.tm_isdst = -1;
    return mktime(&value);
}

void addSampleForDay(
    SummaryAccumulator& accumulator,
    const time_t sampleTime,
    const time_t dayStart,
    const time_t dayEnd,
    const time_t sunrise,
    const time_t sunset,
    const float temperature,
    const float probability,
    const float rainMillimeters,
    const int conditionId,
    const int8_t localHour
)
{
    const double totalWeight = overlapSeconds(sampleTime, dayStart, dayEnd);
    if (totalWeight <= 0.0)
    {
        return;
    }
    const double daytimeWeight = overlapSeconds(sampleTime, sunrise, sunset);
    const double nighttimeWeight = totalWeight - daytimeWeight;
    addSample(
        accumulator,
        temperature,
        probability,
        rainMillimeters,
        conditionId,
        localHour,
        daytimeWeight,
        nighttimeWeight > 0.0 ? nighttimeWeight : 0.0
    );
}
}

OpenWeatherClient::OpenWeatherClient(
    const float latitude,
    const float longitude
)
    : latitude_(latitude), longitude_(longitude)
{
}

bool OpenWeatherClient::fetch(
    const String& apiKey,
    const time_t now,
    WeatherForecast& result,
    String& error
) const
{
    result = WeatherForecast{};
    IPAddress preflightAddress;
    if (!resolveBeforeRequest(preflightAddress))
    {
        error = "OpenWeather DNS fehlgeschlagen";
        return false;
    }

    const String url =
        "https://api.openweathermap.org/data/2.5/forecast?lat=" +
        String(latitude_, 6) + "&lon=" + String(longitude_, 6) +
        "&appid=" + apiKey + "&units=metric&lang=de";

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.useHTTP10(true);
    if (!http.begin(client, url))
    {
        Serial.println(
            "OpenWeather DEBUG: HTTPClient.begin() fehlgeschlagen; "
            "URL/API-Key werden nicht ausgegeben"
        );
        logTransportFailure(client, 0, 0, preflightAddress);
        error = "OpenWeather: HTTPS-Start fehlgeschlagen";
        return false;
    }

    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "CrowPanel-Weather/1.0");
    Serial.printf(
        "OpenWeather DEBUG: GET https://%s/... starten "
        "(connect/read timeout %lu ms)\n",
        OPENWEATHER_HOST,
        HTTP_TIMEOUT_MS
    );
    const unsigned long requestStartedMs = millis();
    const int status = http.GET();
    const unsigned long requestElapsedMs = millis() - requestStartedMs;
    Serial.printf(
        "OpenWeather DEBUG: GET beendet, status=%d, Dauer=%lu ms\n",
        status,
        requestElapsedMs
    );
    if (status != HTTP_CODE_OK)
    {
        if (status < 0)
        {
            logTransportFailure(
                client,
                status,
                requestElapsedMs,
                preflightAddress
            );
        }
        else
        {
            Serial.printf(
                "OpenWeather DEBUG: Server antwortete mit HTTP %d, "
                "Content-Length=%d\n",
                status,
                http.getSize()
            );
        }
        error = status < 0
            ? "OpenWeather nicht erreichbar"
            : "OpenWeather HTTP " + String(status);
        http.end();
        return false;
    }

    JsonDocument filter;
    filter["list"][0]["dt"] = true;
    filter["list"][0]["main"]["temp"] = true;
    filter["list"][0]["weather"][0]["id"] = true;
    filter["list"][0]["pop"] = true;
    filter["list"][0]["rain"]["3h"] = true;
    filter["city"]["sunrise"] = true;
    filter["city"]["sunset"] = true;

    JsonDocument document;
    const DeserializationError jsonError = deserializeJson(
        document,
        http.getStream(),
        DeserializationOption::Filter(filter)
    );
    http.end();
    if (jsonError)
    {
        error = "OpenWeather JSON: " + String(jsonError.c_str());
        return false;
    }
    if (!document["list"].is<JsonArray>())
    {
        error = "OpenWeather: list fehlt";
        return false;
    }

    tm localNow = {};
    localtime_r(&now, &localNow);
    tm todayStart = localNow;
    todayStart.tm_hour = 0;
    todayStart.tm_min = 0;
    todayStart.tm_sec = 0;
    todayStart.tm_isdst = -1;
    const time_t todayStartTime = mktime(&todayStart);
    tm tomorrowStart = todayStart;
    tomorrowStart.tm_mday += 1;
    tomorrowStart.tm_isdst = -1;
    const time_t tomorrowStartTime = mktime(&tomorrowStart);
    tm dayAfterTomorrow = tomorrowStart;
    dayAfterTomorrow.tm_mday += 1;
    dayAfterTomorrow.tm_isdst = -1;
    const time_t tomorrowEndTime = mktime(&dayAfterTomorrow);

    time_t todaySunrise = localTimeForClock(
        todayStart,
        FALLBACK_SUNRISE_HOUR,
        0
    );
    time_t todaySunset = localTimeForClock(
        todayStart,
        FALLBACK_SUNSET_HOUR,
        0
    );
    const time_t apiSunrise =
        document["city"]["sunrise"] | static_cast<time_t>(0);
    const time_t apiSunset =
        document["city"]["sunset"] | static_cast<time_t>(0);
    tm localSunrise = {};
    tm localSunset = {};
    if (apiSunrise > 0 && apiSunset > apiSunrise)
    {
        localtime_r(&apiSunrise, &localSunrise);
        localtime_r(&apiSunset, &localSunset);
        todaySunrise = localTimeForClock(
            todayStart,
            localSunrise.tm_hour,
            localSunrise.tm_min
        );
        todaySunset = localTimeForClock(
            todayStart,
            localSunset.tm_hour,
            localSunset.tm_min
        );
        result.todaySolarTimes.valid = true;
        result.todaySolarTimes.sunriseHour = localSunrise.tm_hour;
        result.todaySolarTimes.sunriseMinute = localSunrise.tm_min;
        result.todaySolarTimes.sunsetHour = localSunset.tm_hour;
        result.todaySolarTimes.sunsetMinute = localSunset.tm_min;
        Serial.printf(
            "OpenWeather: Sonnenzeiten täglich wiederverwenden: "
            "%02d:%02d..%02d:%02d\n",
            localSunrise.tm_hour,
            localSunrise.tm_min,
            localSunset.tm_hour,
            localSunset.tm_min
        );
    }
    if (!result.todaySolarTimes.valid)
    {
        Serial.println(
            "OpenWeather: keine gültigen Sonnenzeiten; "
            "Tageszeit intern 06:00..18:00"
        );
    }

    const time_t tomorrowSunrise = localTimeForClock(
        tomorrowStart,
        result.todaySolarTimes.valid
            ? result.todaySolarTimes.sunriseHour
            : FALLBACK_SUNRISE_HOUR,
        result.todaySolarTimes.valid
            ? result.todaySolarTimes.sunriseMinute
            : 0
    );
    const time_t tomorrowSunset = localTimeForClock(
        tomorrowStart,
        result.todaySolarTimes.valid
            ? result.todaySolarTimes.sunsetHour
            : FALLBACK_SUNSET_HOUR,
        result.todaySolarTimes.valid
            ? result.todaySolarTimes.sunsetMinute
            : 0
    );

    SummaryAccumulator todayAccumulator;
    SummaryAccumulator tomorrowAccumulator;
    for (JsonObjectConst item : document["list"].as<JsonArrayConst>())
    {
        const time_t timestamp = item["dt"] | static_cast<time_t>(0);
        if (timestamp <= 0)
        {
            continue;
        }

        tm localForecastTime = {};
        localtime_r(&timestamp, &localForecastTime);
        const float temperature = item["main"]["temp"] | 0.0F;
        const float probability = item["pop"] | 0.0F;
        const float rain = item["rain"]["3h"] | 0.0F;
        const int conditionId = item["weather"][0]["id"] | 0;

        addSampleForDay(
            todayAccumulator,
            timestamp,
            todayStartTime,
            tomorrowStartTime,
            todaySunrise,
            todaySunset,
            temperature,
            probability,
            rain,
            conditionId,
            static_cast<int8_t>(localForecastTime.tm_hour)
        );
        addSampleForDay(
            tomorrowAccumulator,
            timestamp,
            tomorrowStartTime,
            tomorrowEndTime,
            tomorrowSunrise,
            tomorrowSunset,
            temperature,
            probability,
            rain,
            conditionId,
            static_cast<int8_t>(localForecastTime.tm_hour)
        );
    }

    result.today = finish(todayAccumulator);
    result.tomorrow = finish(tomorrowAccumulator);
    if (!result.today.valid && !result.tomorrow.valid)
    {
        error = "OpenWeather: keine Tagesdaten";
        return false;
    }

    error = "";
    return true;
}
