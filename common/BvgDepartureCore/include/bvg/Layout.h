#pragma once

#include <cstdint>

namespace bvg
{

/**
 * Display layout constants for the 250x122 e-paper display (rotation=1).
 * 
 * Vertical bands (non-overlapping):
 * - Header: 0..20
 * - Departure 1: 24..67
 * - Departure 2: 72..111
 * - Footer: 114..121
 */
struct Layout
{
    // Display dimensions
    static constexpr int16_t WIDTH = 250;
    static constexpr int16_t HEIGHT = 122;

    // Horizontal margins
    static constexpr int16_t MARGIN_X = 4;
    static constexpr int16_t RIGHT_X = 246;

    // Header band (y = 0..20)
    static constexpr int16_t HEADER_STOP_Y = 5;
    static constexpr int16_t HEADER_TIME_Y = 2;
    static constexpr int16_t HEADER_RULE_Y = 21;

    // Departure rows
    static constexpr int16_t FIRST_DEPARTURE_Y = 25;
    static constexpr int16_t DEPARTURE_ROW_HEIGHT = 48;
    static constexpr int16_t DESTINATION_Y_OFFSET = 21;
    static constexpr int16_t DEPARTURE_DIVIDER_Y = 69;

    // Footer band (y = 114..121)
    static constexpr int16_t FOOTER_Y = 114;

    // Text sizes
    static constexpr uint8_t HEADER_STOP_TEXT_SIZE = 1;
    static constexpr uint8_t HEADER_TIME_TEXT_SIZE = 2;
    static constexpr uint8_t DEPARTURE_TEXT_SIZE = 2;
    static constexpr uint8_t FOOTER_TEXT_SIZE = 1;

    // Max departures to display
    static constexpr int MAX_DISPLAY_DEPARTURES = 2;
};

}  // namespace bvg