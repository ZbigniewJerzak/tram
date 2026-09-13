#include <Arduino.h>

#include <cstring>

#include "ElecrowEpd579.h"

namespace
{
ElecrowEpd579 display;
uint8_t* displayBuffer = nullptr;

bool allocateDisplayBuffer()
{
    displayBuffer = static_cast<uint8_t*>(
        ps_malloc(ElecrowEpd579::FRAMEBUFFER_SIZE)
    );

    if (displayBuffer == nullptr)
    {
        return false;
    }

    memset(displayBuffer, 0xFF, ElecrowEpd579::FRAMEBUFFER_SIZE);
    return true;
}

void setVisiblePixel(const uint16_t x, const uint16_t y)
{
    constexpr uint16_t CONTROLLER_SEAM_X = 396;
    constexpr uint16_t SEAM_ADDRESS_OFFSET = 8;
    constexpr size_t DRIVER_ROW_BYTES = ElecrowEpd579::DRIVER_WIDTH / 8;

    if (x >= ElecrowEpd579::VISIBLE_WIDTH || y >= ElecrowEpd579::HEIGHT)
    {
        return;
    }

    const uint16_t shiftedX =
        x < CONTROLLER_SEAM_X ? x : x + SEAM_ADDRESS_OFFSET;
    const uint16_t driverX =
        ElecrowEpd579::DRIVER_WIDTH - shiftedX - 1;
    const uint16_t driverY = ElecrowEpd579::HEIGHT - y - 1;
    const size_t address =
        static_cast<size_t>(driverY) * DRIVER_ROW_BYTES + driverX / 8;
    displayBuffer[address] &=
        static_cast<uint8_t>(~(0x80U >> (driverX % 8)));
}

void fillRectangle(
    const uint16_t x,
    const uint16_t y,
    const uint16_t width,
    const uint16_t height
)
{
    for (uint16_t row = y; row < y + height; ++row)
    {
        for (uint16_t column = x; column < x + width; ++column)
        {
            setVisiblePixel(column, row);
        }
    }
}

void drawHorizontalLine(
    const uint16_t x,
    const uint16_t y,
    const uint16_t width
)
{
    fillRectangle(x, y, width, 1);
}

void drawVerticalLine(
    const uint16_t x,
    const uint16_t y,
    const uint16_t height
)
{
    fillRectangle(x, y, 1, height);
}

void drawRectangle(
    const uint16_t x,
    const uint16_t y,
    const uint16_t width,
    const uint16_t height
)
{
    drawHorizontalLine(x, y, width);
    drawHorizontalLine(x, y + height - 1, width);
    drawVerticalLine(x, y, height);
    drawVerticalLine(x + width - 1, y, height);
}

void drawSevenSegmentDigit(
    const uint16_t x,
    const uint16_t y,
    const uint16_t width,
    const uint16_t height,
    const uint16_t thickness,
    const uint8_t digit
)
{
    constexpr uint8_t SEGMENT_MASKS[10] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66,
        0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };
    const uint8_t segments = SEGMENT_MASKS[digit];
    const uint16_t halfHeight = height / 2;

    if ((segments & 0x01U) != 0U)
        fillRectangle(x + thickness, y, width - 2 * thickness, thickness);
    if ((segments & 0x02U) != 0U)
        fillRectangle(x + width - thickness, y + thickness, thickness,
                      halfHeight - thickness);
    if ((segments & 0x04U) != 0U)
        fillRectangle(x + width - thickness, y + halfHeight, thickness,
                      halfHeight - thickness);
    if ((segments & 0x08U) != 0U)
        fillRectangle(x + thickness, y + height - thickness,
                      width - 2 * thickness, thickness);
    if ((segments & 0x10U) != 0U)
        fillRectangle(x, y + halfHeight, thickness,
                      halfHeight - thickness);
    if ((segments & 0x20U) != 0U)
        fillRectangle(x, y + thickness, thickness,
                      halfHeight - thickness);
    if ((segments & 0x40U) != 0U)
        fillRectangle(x + thickness, y + halfHeight - thickness / 2,
                      width - 2 * thickness, thickness);
}

void drawTestPattern()
{
    memset(displayBuffer, 0xFF, ElecrowEpd579::FRAMEBUFFER_SIZE);

    drawRectangle(
        0,
        0,
        ElecrowEpd579::VISIBLE_WIDTH,
        ElecrowEpd579::HEIGHT
    );
    drawRectangle(
        4,
        4,
        ElecrowEpd579::VISIBLE_WIDTH - 8,
        ElecrowEpd579::HEIGHT - 8
    );

    drawHorizontalLine(24, 36, ElecrowEpd579::VISIBLE_WIDTH - 48);
    drawHorizontalLine(24, 235, ElecrowEpd579::VISIBLE_WIDTH - 48);
    drawVerticalLine(395, 36, 200);
    drawVerticalLine(396, 36, 200);

    drawSevenSegmentDigit(255, 67, 105, 136, 14, 6);
    drawSevenSegmentDigit(432, 67, 105, 136, 14, 1);

    fillRectangle(10, 10, 18, 18);
    fillRectangle(ElecrowEpd579::VISIBLE_WIDTH - 46, 10, 14, 18);
    fillRectangle(ElecrowEpd579::VISIBLE_WIDTH - 28, 10, 18, 18);
    fillRectangle(10, ElecrowEpd579::HEIGHT - 28, 18, 5);
    fillRectangle(10, ElecrowEpd579::HEIGHT - 19, 18, 9);
    drawRectangle(
        ElecrowEpd579::VISIBLE_WIDTH - 28,
        ElecrowEpd579::HEIGHT - 28,
        18,
        18
    );
}

bool refreshDisplay()
{
    Serial.println("Display: Fast-Mode initialisieren");
    if (!display.initializeFastMode())
    {
        Serial.println("FEHLER: BUSY-Timeout bei der Initialisierung.");
        return false;
    }

    Serial.println("Display: Controller-Speicher loeschen");
    display.clearDisplayMemory();

    Serial.println("Display: vollstaendigen Loeschzyklus starten");
    if (!display.updateFull())
    {
        Serial.println("FEHLER: BUSY-Timeout beim Loeschzyklus.");
        return false;
    }

    Serial.println("Display: fuer Testbild neu initialisieren");
    if (!display.initializeFastMode())
    {
        Serial.println("FEHLER: BUSY-Timeout vor dem Testbild.");
        return false;
    }

    Serial.println("Display: Testbild uebertragen");
    if (!display.writeFramebuffer(displayBuffer))
    {
        Serial.println("FEHLER: Framebuffer konnte nicht uebertragen werden.");
        return false;
    }

    Serial.println("Display: schnellen Bildaufbau starten");
    if (!display.updateFast())
    {
        Serial.println("FEHLER: BUSY-Timeout beim Bildaufbau.");
        return false;
    }

    display.deepSleep();
    return true;
}
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("CrowPanel 5.79-inch display test");

    if (!psramFound())
    {
        Serial.println("FEHLER: OPI-PSRAM wurde nicht erkannt.");
        return;
    }

    if (!allocateDisplayBuffer())
    {
        Serial.println("FEHLER: Displaybuffer konnte nicht angelegt werden.");
        return;
    }

    drawTestPattern();

    display.begin();

    if (refreshDisplay())
    {
        Serial.println("Displaytest abgeschlossen; Display ist im Deep Sleep.");
    }
}

void loop()
{
    delay(1000);
}
