#include "BvgDepartureClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cstring>

namespace
{
constexpr char API_BASE_URL[] = "https://v6.bvg.transport.rest";
constexpr unsigned long HTTP_TIMEOUT_MS = 15000UL;
constexpr uint8_t HTTP_MAX_ATTEMPTS = 3;
constexpr unsigned long HTTP_BACKOFF_MS[HTTP_MAX_ATTEMPTS] = {
    0UL, 2000UL, 6000UL
};

bool isTransientHttpFailure(const int status)
{
    return status < 0 || status == HTTP_CODE_REQUEST_TIMEOUT ||
        status == 425 || status == 429 ||
        status == HTTP_CODE_INTERNAL_SERVER_ERROR ||
        status == HTTP_CODE_BAD_GATEWAY ||
        status == HTTP_CODE_SERVICE_UNAVAILABLE ||
        status == HTTP_CODE_GATEWAY_TIMEOUT;
}

bool getJson(
    const String& url,
    JsonDocument& result,
    const JsonDocument& filter,
    const char* const operation,
    String& error
)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        error = "WLAN nicht verbunden";
        return false;
    }

    Serial.println(operation);
    for (uint8_t attempt = 0; attempt < HTTP_MAX_ATTEMPTS; ++attempt)
    {
        if (HTTP_BACKOFF_MS[attempt] > 0)
        {
            Serial.printf(
                "BVG: Wiederholung %u/%u in %lu ms\n",
                attempt + 1,
                HTTP_MAX_ATTEMPTS,
                HTTP_BACKOFF_MS[attempt]
            );
            delay(HTTP_BACKOFF_MS[attempt]);
        }

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.setTimeout(HTTP_TIMEOUT_MS);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        if (!http.begin(client, url))
        {
            error = "HTTPS-Initialisierung fehlgeschlagen";
            continue;
        }
        http.addHeader("Accept", "application/json");
        http.addHeader("User-Agent", "CrowPanel-BVG-Weather/1.0");

        const unsigned long started = millis();
        const int status = http.GET();
        Serial.printf(
            "BVG DEBUG: HTTP %d nach %lu ms, Versuch %u/%u\n",
            status,
            millis() - started,
            attempt + 1,
            HTTP_MAX_ATTEMPTS
        );
        if (status == HTTP_CODE_OK)
        {
            const DeserializationError jsonError = deserializeJson(
                result,
                http.getStream(),
                DeserializationOption::Filter(filter)
            );
            http.end();
            if (jsonError)
            {
                error = "BVG JSON: " + String(jsonError.c_str());
                return false;
            }
            error = "";
            return true;
        }

        const String statusText = http.errorToString(status);
        http.end();
        error = "BVG HTTP " + String(status) + " (" + statusText + ")";
        if (!isTransientHttpFailure(status) || attempt + 1 >= HTTP_MAX_ATTEMPTS)
        {
            return false;
        }
    }
    return false;
}

int64_t daysFromCivil(int year, const unsigned month, const unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned adjustedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned dayOfYear = (153 * adjustedMonth + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 -
        yearOfEra / 100 + dayOfYear;
    return static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
}

int parseDigits(const char* const text, const size_t start, const size_t count)
{
    int value = 0;
    for (size_t index = 0; index < count; ++index)
    {
        const char character = text[start + index];
        if (character < '0' || character > '9')
        {
            return -1;
        }
        value = value * 10 + character - '0';
    }
    return value;
}

time_t parseApiTime(const char* const text)
{
    if (text == nullptr || strlen(text) < 19)
    {
        return 0;
    }

    const int year = parseDigits(text, 0, 4);
    const int month = parseDigits(text, 5, 2);
    const int day = parseDigits(text, 8, 2);
    const int hour = parseDigits(text, 11, 2);
    const int minute = parseDigits(text, 14, 2);
    const int second = parseDigits(text, 17, 2);
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 60)
    {
        return 0;
    }

    int64_t epoch = daysFromCivil(year, month, day) * 86400LL +
        static_cast<int64_t>(hour) * 3600LL + minute * 60LL + second;
    const char* timezone = nullptr;
    for (const char* cursor = text + 19; *cursor != '\0'; ++cursor)
    {
        if (*cursor == 'Z' || *cursor == '+' || *cursor == '-')
        {
            timezone = cursor;
            break;
        }
    }
    if (timezone == nullptr)
    {
        tm local = {};
        local.tm_year = year - 1900;
        local.tm_mon = month - 1;
        local.tm_mday = day;
        local.tm_hour = hour;
        local.tm_min = minute;
        local.tm_sec = second;
        local.tm_isdst = -1;
        return mktime(&local);
    }
    if (*timezone == 'Z')
    {
        return static_cast<time_t>(epoch);
    }
    if (strlen(timezone) < 6 || timezone[3] != ':')
    {
        return 0;
    }
    const int offsetHours = parseDigits(timezone, 1, 2);
    const int offsetMinutes = parseDigits(timezone, 4, 2);
    if (offsetHours < 0 || offsetHours > 23 ||
        offsetMinutes < 0 || offsetMinutes > 59)
    {
        return 0;
    }
    const int offsetSeconds = offsetHours * 3600 + offsetMinutes * 60;
    epoch += *timezone == '+' ? -offsetSeconds : offsetSeconds;
    return static_cast<time_t>(epoch);
}

int roundDelayMinutes(const int delaySeconds)
{
    const long magnitude = delaySeconds < 0
        ? -static_cast<long>(delaySeconds)
        : static_cast<long>(delaySeconds);
    const int rounded = static_cast<int>((magnitude + 30L) / 60L);
    return delaySeconds < 0 ? -rounded : rounded;
}

void insertSorted(
    BvgDeparture output[BVG_MAX_DEPARTURES],
    uint8_t& count,
    const BvgDeparture& value
)
{
    uint8_t position = count;
    for (uint8_t index = 0; index < count; ++index)
    {
        if (value.when < output[index].when)
        {
            position = index;
            break;
        }
    }
    if (position >= BVG_MAX_DEPARTURES)
    {
        return;
    }
    const uint8_t last = count < BVG_MAX_DEPARTURES
        ? count
        : BVG_MAX_DEPARTURES - 1;
    for (uint8_t index = last; index > position; --index)
    {
        output[index] = output[index - 1];
    }
    output[position] = value;
    if (count < BVG_MAX_DEPARTURES)
    {
        ++count;
    }
}
}

BvgDepartureClient::BvgDepartureClient(const char* const stopSearchTerm)
    : stopSearchTerm_(stopSearchTerm), stopName_(stopSearchTerm)
{
}

const String& BvgDepartureClient::stopName() const
{
    return stopName_;
}

bool BvgDepartureClient::resolveStop(String& error)
{
    JsonDocument filter;
    filter[0]["type"] = true;
    filter[0]["id"] = true;
    filter[0]["name"] = true;
    filter[0]["products"]["tram"] = true;

    const String url = String(API_BASE_URL) + "/locations?query=" +
        stopSearchTerm_ +
        "&poi=false&addresses=false&results=8&language=de&pretty=false";
    JsonDocument response;
    if (!getJson(url, response, filter, "BVG: Haltestelle suchen", error))
    {
        return false;
    }
    for (JsonObject item : response.as<JsonArray>())
    {
        const String name = item["name"] | "";
        if (String(item["type"] | "") == "stop" &&
            (item["products"]["tram"] | false) &&
            name.indexOf(stopSearchTerm_) >= 0)
        {
            stopId_ = String(item["id"] | "");
            stopName_ = name;
            Serial.printf(
                "BVG: Haltestelle %s, ID %s\n",
                stopName_.c_str(),
                stopId_.c_str()
            );
            return !stopId_.isEmpty();
        }
    }
    error = "BVG-Haltestelle nicht gefunden";
    return false;
}

bool BvgDepartureClient::fetchDepartures(
    BvgDepartureBoard& result,
    String& error
)
{
    JsonDocument filter;
    filter["departures"][0]["direction"] = true;
    filter["departures"][0]["line"]["name"] = true;
    filter["departures"][0]["line"]["product"] = true;
    filter["departures"][0]["when"] = true;
    filter["departures"][0]["plannedWhen"] = true;
    filter["departures"][0]["delay"] = true;
    filter["departures"][0]["cancelled"] = true;

    const String url = String(API_BASE_URL) + "/stops/" + stopId_ +
        "/departures?results=8&duration=60&tram=true&suburban=false" 
        "&subway=false&bus=false&ferry=false&express=false&regional=false"
        "&remarks=false&linesOfStops=false&language=de&pretty=false";
    JsonDocument response;
    if (!getJson(url, response, filter, "BVG: Abfahrten abrufen", error))
    {
        return false;
    }

    BvgDepartureBoard fresh;
    const time_t now = time(nullptr);
    for (JsonObject item : response["departures"].as<JsonArray>())
    {
        if ((item["cancelled"] | false) ||
            String(item["line"]["product"] | "") != "tram")
        {
            continue;
        }
        const char* timestamp = item["when"];
        if (timestamp == nullptr)
        {
            timestamp = item["plannedWhen"];
        }
        BvgDeparture departure;
        departure.line = String(item["line"]["name"] | "?");
        departure.direction = String(item["direction"] | "UNBEKANNT");
        departure.when = parseApiTime(timestamp);
        departure.delayMinutes = roundDelayMinutes(item["delay"] | 0);
        if (departure.when >= now - 60)
        {
            insertSorted(fresh.departures, fresh.count, departure);
        }
    }
    fresh.updatedAt = now;
    fresh.valid = true;
    result = fresh;
    error = "";
    return true;
}

bool BvgDepartureClient::fetch(BvgDepartureBoard& result, String& error)
{
    if (stopId_.isEmpty() && !resolveStop(error))
    {
        return false;
    }
    return fetchDepartures(result, error);
}
