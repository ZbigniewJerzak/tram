#include <Adafruit_GFX.h>
#include <Arduino.h>

#include <cstring>

#include "ElecrowEpd579.h"

namespace
{
constexpr uint16_t COLOR_WHITE = 0;
constexpr uint16_t COLOR_BLACK = 1;
constexpr uint16_t CONTROLLER_SEAM_X = 396;
constexpr uint16_t SEAM_ADDRESS_OFFSET = 8;
constexpr size_t DRIVER_ROW_BYTES = ElecrowEpd579::DRIVER_WIDTH / 8;

ElecrowEpd579 display;
uint8_t* displayBuffer = nullptr;

class TextCanvas : public Adafruit_GFX
{
public:
    explicit TextCanvas(uint8_t* const frameBuffer)
        : Adafruit_GFX(ElecrowEpd579::VISIBLE_WIDTH, ElecrowEpd579::HEIGHT),
          frameBuffer_(frameBuffer)
    {
    }

    void clear()
    {
        memset(frameBuffer_, 0xFF, ElecrowEpd579::FRAMEBUFFER_SIZE);
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override
    {
        if (x < 0 || y < 0 ||
            x >= ElecrowEpd579::VISIBLE_WIDTH ||
            y >= ElecrowEpd579::HEIGHT)
        {
            return;
        }

        const uint16_t visibleX = static_cast<uint16_t>(x);
        const uint16_t visibleY = static_cast<uint16_t>(y);
        const uint16_t shiftedX = visibleX < CONTROLLER_SEAM_X
            ? visibleX
            : visibleX + SEAM_ADDRESS_OFFSET;
        const uint16_t driverX =
            ElecrowEpd579::DRIVER_WIDTH - shiftedX - 1;
        const uint16_t driverY = ElecrowEpd579::HEIGHT - visibleY - 1;
        const size_t address =
            static_cast<size_t>(driverY) * DRIVER_ROW_BYTES + driverX / 8;
        const uint8_t mask = static_cast<uint8_t>(0x80U >> (driverX % 8));

        if (color == COLOR_BLACK)
        {
            frameBuffer_[address] &= static_cast<uint8_t>(~mask);
        }
        else
        {
            frameBuffer_[address] |= mask;
        }
    }

private:
    uint8_t* frameBuffer_;
};

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

void drawCenteredText(
    TextCanvas& canvas,
    const char* const text,
    const int16_t y,
    const uint8_t textSize
)
{
    int16_t boundsX = 0;
    int16_t boundsY = 0;
    uint16_t boundsWidth = 0;
    uint16_t boundsHeight = 0;

    canvas.setTextSize(textSize);
    canvas.getTextBounds(
        text,
        0,
        y,
        &boundsX,
        &boundsY,
        &boundsWidth,
        &boundsHeight
    );

    const int16_t x =
        (canvas.width() - static_cast<int16_t>(boundsWidth)) / 2 - boundsX;
    canvas.setCursor(x, y);
    canvas.print(text);
}

void drawRightAlignedText(
    TextCanvas& canvas,
    const char* const text,
    const int16_t rightX,
    const int16_t y
)
{
    int16_t boundsX = 0;
    int16_t boundsY = 0;
    uint16_t boundsWidth = 0;
    uint16_t boundsHeight = 0;

    canvas.getTextBounds(
        text,
        0,
        y,
        &boundsX,
        &boundsY,
        &boundsWidth,
        &boundsHeight
    );
    canvas.setCursor(
        rightX - static_cast<int16_t>(boundsWidth) - boundsX,
        y
    );
    canvas.print(text);
}

void drawTextSpecimen(TextCanvas& canvas)
{
    canvas.clear();
    canvas.setTextColor(COLOR_BLACK);
    canvas.setTextWrap(false);

    canvas.drawRect(0, 0, canvas.width(), canvas.height(), COLOR_BLACK);
    canvas.drawRect(4, 4, canvas.width() - 8, canvas.height() - 8, COLOR_BLACK);

    canvas.setTextSize(1);
    canvas.setCursor(12, 10);
    canvas.print("TOP LEFT");
    drawRightAlignedText(canvas, "TOP RIGHT", canvas.width() - 12, 10);

    drawCenteredText(canvas, "CROWPANEL 5.79", 24, 5);
    canvas.drawFastHLine(20, 68, canvas.width() - 40, COLOR_BLACK);

    drawCenteredText(canvas, "TEXT RENDERING TEST", 76, 4);
    canvas.drawFastHLine(20, 115, canvas.width() - 40, COLOR_BLACK);

    drawCenteredText(canvas, "ESP32-S3 | SSD1683 x2", 124, 3);
    drawCenteredText(canvas, "792 x 272 visible pixels", 160, 2);
    drawCenteredText(canvas, "ae oe ue ss | 0123456789", 187, 2);

    drawCenteredText(
        canvas,
        "size 1: The quick brown fox jumps over 1234567890.",
        221,
        1
    );

    canvas.setTextSize(1);
    canvas.setCursor(12, 252);
    canvas.print("BOTTOM LEFT");
    drawRightAlignedText(canvas, "BOTTOM RIGHT", canvas.width() - 12, 252);
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

    Serial.println("Display: fuer Textbild neu initialisieren");
    if (!display.initializeFastMode())
    {
        Serial.println("FEHLER: BUSY-Timeout vor dem Textbild.");
        return false;
    }

    Serial.println("Display: Textbild uebertragen");
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
    Serial.println("CrowPanel 5.79-inch text rendering test");

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

    TextCanvas canvas(displayBuffer);
    drawTextSpecimen(canvas);

    display.begin();

    if (refreshDisplay())
    {
        Serial.println("Texttest abgeschlossen; Display ist im Deep Sleep.");
    }
}

void loop()
{
    delay(1000);
}
