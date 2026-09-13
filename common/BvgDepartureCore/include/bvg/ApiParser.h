#pragma once

#include <ctime>

namespace bvg
{

// Forward declaration
struct Stop;
struct Departure;

/**
 * Parse ISO 8601 timestamp like "2026-07-27T14:30:00+02:00" or "...Z".
 * Returns Unix timestamp in local time.
 */
time_t parseApiTime(const char* text);

/**
 * Round delay seconds to nearest minute.
 */
int roundDelayMinutes(int delaySeconds);

}  // namespace bvg