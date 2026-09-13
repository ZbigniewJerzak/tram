#pragma once

#include <Arduino.h>

#include <time.h>

constexpr uint8_t BVG_MAX_DEPARTURES = 2;

struct BvgDeparture
{
    String line;
    String direction;
    time_t when = 0;
    int delayMinutes = 0;
};

struct BvgDepartureBoard
{
    BvgDeparture departures[BVG_MAX_DEPARTURES];
    uint8_t count = 0;
    time_t updatedAt = 0;
    bool valid = false;
};

class BvgDepartureClient
{
public:
    explicit BvgDepartureClient(const char* stopSearchTerm);

    bool fetch(BvgDepartureBoard& result, String& error);
    const String& stopName() const;

private:
    String stopSearchTerm_;
    String stopId_;
    String stopName_;

    bool resolveStop(String& error);
    bool fetchDepartures(BvgDepartureBoard& result, String& error);
};
