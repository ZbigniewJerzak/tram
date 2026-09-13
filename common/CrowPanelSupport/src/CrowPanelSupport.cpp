#include "CrowPanelSupport.h"

using namespace CrowPanelPins;

void powerOnCrowPanelDisplay()
{
    pinMode(EPD_POWER, OUTPUT);
    digitalWrite(EPD_POWER, HIGH);
    delay(200);
}

void initializeCrowPanelDisplay(
    CrowPanelDisplay& display,
    const uint32_t diagnosticBaudRate
)
{
    powerOnCrowPanelDisplay();

    SPI.begin(
        EPD_SCK,
        -1,
        EPD_MOSI,
        EPD_CS
    );

    display.epd2.selectSPI(
        SPI,
        SPISettings(
            4000000UL,
            MSBFIRST,
            SPI_MODE0
        )
    );

    display.init(diagnosticBaudRate);
    display.setRotation(1);
}