#include "bvg/ApiParser.h"
#include <cstring>
#include <ctime>

namespace bvg
{

// ---------------------------------------------------------------------------
// Timestamp parsing (from main.cpp)
// ---------------------------------------------------------------------------

static int parseDigits(const char* text, size_t start, size_t count)
{
    int value = 0;
    for (size_t i = 0; i < count; ++i) {
        const char c = text[start + i];
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

// Number of days since 1970-01-01 (Gregorian calendar)
static int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned adjustedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned dayOfYear = (153 * adjustedMonth + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

time_t parseApiTime(const char* text)
{
    if (!text || strlen(text) < 19) return 0;

    const int year = parseDigits(text, 0, 4);
    const int month = parseDigits(text, 5, 2);
    const int day = parseDigits(text, 8, 2);
    const int hour = parseDigits(text, 11, 2);
    const int minute = parseDigits(text, 14, 2);
    const int second = parseDigits(text, 17, 2);

    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
        return 0;
    }

    int64_t epoch = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400LL +
                    static_cast<int64_t>(hour) * 3600LL +
                    static_cast<int64_t>(minute) * 60LL +
                    second;

    const char* timezone = nullptr;
    for (const char* cursor = text + 19; *cursor != '\0'; ++cursor) {
        if (*cursor == 'Z' || *cursor == '+' || *cursor == '-') {
            timezone = cursor;
            break;
        }
    }

    if (!timezone) {
        // No explicit timezone, use local time
        struct tm local_tm {};
        local_tm.tm_year = year - 1900;
        local_tm.tm_mon = month - 1;
        local_tm.tm_mday = day;
        local_tm.tm_hour = hour;
        local_tm.tm_min = minute;
        local_tm.tm_sec = second;
        local_tm.tm_isdst = -1;
        return mktime(&local_tm);
    }

    if (*timezone == 'Z') {
        return static_cast<time_t>(epoch);
    }

    if (strlen(timezone) < 6 || timezone[3] != ':') return 0;

    const int offsetHours = parseDigits(timezone, 1, 2);
    const int offsetMinutes = parseDigits(timezone, 4, 2);
    if (offsetHours < 0 || offsetHours > 23 || offsetMinutes < 0 || offsetMinutes > 59) {
        return 0;
    }

    const int offsetSeconds = offsetHours * 3600 + offsetMinutes * 60;
    // +02:00 means local clock time is two hours ahead of UTC
    epoch += (*timezone == '+') ? -offsetSeconds : offsetSeconds;
    return static_cast<time_t>(epoch);
}

int roundDelayMinutes(int delaySeconds)
{
    const long magnitude = delaySeconds < 0 ? -static_cast<long>(delaySeconds) : static_cast<long>(delaySeconds);
    const int rounded = static_cast<int>((magnitude + 30L) / 60L);
    return delaySeconds < 0 ? -rounded : rounded;
}

}  // namespace bvg