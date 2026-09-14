#pragma once

#include <Arduino.h>

#include <time.h>

constexpr uint8_t BVG_MAX_DIRECTIONS = 2;
constexpr uint8_t BVG_DEPARTURES_PER_DIRECTION = 2;

struct BvgDeparture
{
    String line;
    String direction;
    String tripId;
    String currentStop;
    time_t when = 0;
    int delayMinutes = 0;
};

struct BvgDirectionBoard
{
    String direction;
    BvgDeparture departures[BVG_DEPARTURES_PER_DIRECTION];
    uint8_t count = 0;
};

struct BvgDepartureBoard
{
    BvgDirectionBoard directions[BVG_MAX_DIRECTIONS];
    uint8_t directionCount = 0;
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
    bool fetchCurrentStop(BvgDeparture& departure, String& error);
};
