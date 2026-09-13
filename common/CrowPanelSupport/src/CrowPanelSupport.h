#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>

namespace CrowPanelPins
{
constexpr uint8_t EPD_POWER = 7;
constexpr uint8_t EPD_BUSY  = 9;
constexpr uint8_t EPD_RESET = 10;
constexpr uint8_t EPD_MOSI  = 11;
constexpr uint8_t EPD_SCK   = 12;
constexpr uint8_t EPD_DC    = 13;
constexpr uint8_t EPD_CS    = 14;
}

using CrowPanelDisplayDriver = GxEPD2_213_GDEY0213B74;

using CrowPanelDisplay =
    GxEPD2_BW<
        CrowPanelDisplayDriver,
        CrowPanelDisplayDriver::HEIGHT
    >;

void powerOnCrowPanelDisplay();

void initializeCrowPanelDisplay(
    CrowPanelDisplay& display,
    uint32_t diagnosticBaudRate = 115200UL
);