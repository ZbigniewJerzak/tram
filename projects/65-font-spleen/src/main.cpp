#include <Adafruit_GFX.h>
#include <Arduino.h>

#include <climits>
#include <cstring>

#include "BitmapFont.h"
#include "ElecrowEpd579.h"
#include "SpleenFontData.h"

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
        if (x < 0 || y < 0 || x >= width() || y >= height())
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

struct HorizontalBounds
{
    int16_t minimumX;
    int16_t maximumX;
};

bool readGlyph(
    const BitmapFont& font,
    const uint32_t codepoint,
    BitmapGlyph& glyph
)
{
    for (uint16_t index = 0; index < font.glyphCount; ++index)
    {
        memcpy_P(&glyph, &font.glyphs[index], sizeof(glyph));
        if (glyph.codepoint == codepoint)
        {
            return true;
        }
    }
    return false;
}

uint32_t readUtf8Codepoint(const char*& text)
{
    const uint8_t first = static_cast<uint8_t>(*text++);
    if (first < 0x80U)
    {
        return first;
    }
    if ((first & 0xE0U) == 0xC0U)
    {
        const uint8_t second = static_cast<uint8_t>(*text++);
        if ((second & 0xC0U) == 0x80U)
        {
            return ((first & 0x1FU) << 6U) | (second & 0x3FU);
        }
    }
    else if ((first & 0xF0U) == 0xE0U)
    {
        const uint8_t second = static_cast<uint8_t>(*text++);
        const uint8_t third = static_cast<uint8_t>(*text++);
        if ((second & 0xC0U) == 0x80U && (third & 0xC0U) == 0x80U)
        {
            return ((first & 0x0FU) << 12U) |
                ((second & 0x3FU) << 6U) |
                (third & 0x3FU);
        }
    }
    return '?';
}

void drawGlyph(
    TextCanvas& canvas,
    const BitmapFont& font,
    const BitmapGlyph& glyph,
    const int16_t cursorX,
    const int16_t baselineY
)
{
    uint32_t bitIndex = 0;
    for (uint8_t row = 0; row < glyph.height; ++row)
    {
        for (uint8_t column = 0; column < glyph.width; ++column)
        {
            const uint8_t value = pgm_read_byte(
                font.bitmap + glyph.bitmapOffset + bitIndex / 8U
            );
            const uint8_t mask = static_cast<uint8_t>(0x80U >> (bitIndex % 8U));
            if ((value & mask) != 0U)
            {
                canvas.drawPixel(
                    cursorX + glyph.xOffset + column,
                    baselineY + glyph.yOffset + row,
                    COLOR_BLACK
                );
            }
            ++bitIndex;
        }
    }
}

int16_t drawCodepoint(
    TextCanvas& canvas,
    const BitmapFont& font,
    const uint32_t codepoint,
    const int16_t cursorX,
    const int16_t baselineY
)
{
    BitmapGlyph glyph = {};
    if (!readGlyph(font, codepoint, glyph))
    {
        readGlyph(font, '?', glyph);
    }
    drawGlyph(canvas, font, glyph, cursorX, baselineY);
    return cursorX + glyph.xAdvance;
}

HorizontalBounds measureText(const BitmapFont& font, const char* text)
{
    HorizontalBounds bounds = {INT16_MAX, INT16_MIN};
    int16_t cursorX = 0;
    while (*text != '\0')
    {
        const uint32_t codepoint = readUtf8Codepoint(text);
        BitmapGlyph glyph = {};
        if (!readGlyph(font, codepoint, glyph))
        {
            readGlyph(font, '?', glyph);
        }
        if (glyph.width > 0)
        {
            const int16_t glyphLeft = cursorX + glyph.xOffset;
            const int16_t glyphRight = glyphLeft + glyph.width;
            if (glyphLeft < bounds.minimumX)
            {
                bounds.minimumX = glyphLeft;
            }
            if (glyphRight > bounds.maximumX)
            {
                bounds.maximumX = glyphRight;
            }
        }
        cursorX += glyph.xAdvance;
    }
    if (bounds.minimumX == INT16_MAX)
    {
        bounds = {0, 0};
    }
    return bounds;
}

void drawText(
    TextCanvas& canvas,
    const BitmapFont& font,
    const char* text,
    int16_t cursorX,
    const int16_t baselineY
)
{
    while (*text != '\0')
    {
        cursorX = drawCodepoint(
            canvas,
            font,
            readUtf8Codepoint(text),
            cursorX,
            baselineY
        );
    }
}

void drawCenteredText(
    TextCanvas& canvas,
    const BitmapFont& font,
    const char* const text,
    const int16_t baselineY
)
{
    const HorizontalBounds bounds = measureText(font, text);
    const int16_t width = bounds.maximumX - bounds.minimumX;
    drawText(
        canvas,
        font,
        text,
        (canvas.width() - width) / 2 - bounds.minimumX,
        baselineY
    );
}

void drawCenteredLabel(
    TextCanvas& canvas,
    const char* const label,
    const int16_t centerX
)
{
    const HorizontalBounds bounds = measureText(Spleen8x16, label);
    const int16_t width = bounds.maximumX - bounds.minimumX;
    drawText(
        canvas,
        Spleen8x16,
        label,
        centerX - width / 2 - bounds.minimumX,
        257
    );
}

void drawTram(TextCanvas& canvas, const int16_t x, const int16_t y)
{
    canvas.drawLine(x - 8, y + 4, x, y, COLOR_BLACK);
    canvas.drawLine(x, y, x + 8, y + 4, COLOR_BLACK);
    canvas.drawLine(x, y, x, y + 6, COLOR_BLACK);
    canvas.drawRect(x - 14, y + 6, 29, 21, COLOR_BLACK);
    canvas.drawRect(x - 10, y + 9, 8, 8, COLOR_BLACK);
    canvas.drawRect(x + 2, y + 9, 8, 8, COLOR_BLACK);
    canvas.fillRect(x - 10, y + 21, 4, 3, COLOR_BLACK);
    canvas.fillRect(x + 7, y + 21, 4, 3, COLOR_BLACK);
    canvas.fillCircle(x - 8, y + 29, 2, COLOR_BLACK);
    canvas.fillCircle(x + 8, y + 29, 2, COLOR_BLACK);
    canvas.drawLine(x - 13, y + 32, x + 13, y + 32, COLOR_BLACK);
}

void drawTrain(TextCanvas& canvas, const int16_t x, const int16_t y)
{
    canvas.drawRect(x - 13, y + 4, 27, 23, COLOR_BLACK);
    canvas.drawRect(x - 9, y + 8, 7, 8, COLOR_BLACK);
    canvas.drawRect(x + 2, y + 8, 7, 8, COLOR_BLACK);
    canvas.fillCircle(x - 8, y + 21, 2, COLOR_BLACK);
    canvas.fillCircle(x + 8, y + 21, 2, COLOR_BLACK);
    canvas.fillCircle(x - 8, y + 29, 2, COLOR_BLACK);
    canvas.fillCircle(x + 8, y + 29, 2, COLOR_BLACK);
}

void drawBus(TextCanvas& canvas, const int16_t x, const int16_t y)
{
    canvas.drawRect(x - 16, y + 6, 33, 20, COLOR_BLACK);
    canvas.drawRect(x - 12, y + 9, 8, 8, COLOR_BLACK);
    canvas.drawRect(x - 1, y + 9, 8, 8, COLOR_BLACK);
    canvas.drawRect(x + 10, y + 9, 4, 8, COLOR_BLACK);
    canvas.fillCircle(x - 10, y + 28, 3, COLOR_BLACK);
    canvas.fillCircle(x + 11, y + 28, 3, COLOR_BLACK);
}

void drawClock(TextCanvas& canvas, const int16_t x, const int16_t y)
{
    canvas.drawCircle(x, y + 16, 14, COLOR_BLACK);
    canvas.drawCircle(x, y + 16, 13, COLOR_BLACK);
    canvas.drawLine(x, y + 16, x, y + 7, COLOR_BLACK);
    canvas.drawLine(x, y + 16, x + 7, y + 20, COLOR_BLACK);
    canvas.fillCircle(x, y + 16, 2, COLOR_BLACK);
}

void drawWifi(TextCanvas& canvas, const int16_t x, const int16_t y)
{
    canvas.drawLine(x - 15, y + 10, x, y + 2, COLOR_BLACK);
    canvas.drawLine(x, y + 2, x + 15, y + 10, COLOR_BLACK);
    canvas.drawLine(x - 10, y + 17, x, y + 11, COLOR_BLACK);
    canvas.drawLine(x, y + 11, x + 10, y + 17, COLOR_BLACK);
    canvas.drawLine(x - 5, y + 24, x, y + 21, COLOR_BLACK);
    canvas.drawLine(x, y + 21, x + 5, y + 24, COLOR_BLACK);
    canvas.fillCircle(x, y + 29, 2, COLOR_BLACK);
}

void drawWarning(TextCanvas& canvas, const int16_t x, const int16_t y)
{
    canvas.drawLine(x, y + 1, x - 16, y + 30, COLOR_BLACK);
    canvas.drawLine(x - 16, y + 30, x + 16, y + 30, COLOR_BLACK);
    canvas.drawLine(x + 16, y + 30, x, y + 1, COLOR_BLACK);
    canvas.fillRect(x - 1, y + 11, 3, 10, COLOR_BLACK);
    canvas.fillRect(x - 1, y + 25, 3, 3, COLOR_BLACK);
}

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

void drawSpleenShowcase(TextCanvas& canvas)
{
    constexpr int16_t CENTERS[] = {126, 234, 342, 450, 558, 666};
    constexpr const char* LABELS[] = {
        "TRAM", "TRAIN", "BUS", "TIME", "WIFI", "WARN"
    };

    canvas.clear();
    canvas.drawRect(0, 0, canvas.width(), canvas.height(), COLOR_BLACK);
    canvas.drawRect(4, 4, canvas.width() - 8, canvas.height() - 8, COLOR_BLACK);

    drawCenteredText(canvas, Spleen8x16, "SPLEEN NATIVE 1-BIT FONT | 3 SIZES", 24);
    canvas.drawFastHLine(20, 35, canvas.width() - 40, COLOR_BLACK);

    drawCenteredText(
        canvas,
        Spleen8x16,
        "8x16  Fahrplan: nächste Tram in 6 Minuten",
        61
    );
    drawCenteredText(
        canvas,
        Spleen12x24,
        "12x24  ÄÖÜ äöü ß | 0123456789",
        100
    );
    drawCenteredText(canvas, Spleen16x32, "16x32  BERLIN", 147);

    drawCenteredText(
        canvas,
        Spleen8x16,
        "← ↑ → ↓   ┌──┐ └──┘       ",
        176
    );
    canvas.drawFastHLine(20, 187, canvas.width() - 40, COLOR_BLACK);

    drawTram(canvas, CENTERS[0], 194);
    drawTrain(canvas, CENTERS[1], 194);
    drawBus(canvas, CENTERS[2], 194);
    drawClock(canvas, CENTERS[3], 194);
    drawWifi(canvas, CENTERS[4], 194);
    drawWarning(canvas, CENTERS[5], 194);
    for (size_t index = 0; index < 6; ++index)
    {
        drawCenteredLabel(canvas, LABELS[index], CENTERS[index]);
    }
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

    Serial.println("Display: fuer Spleen-Testbild neu initialisieren");
    if (!display.initializeFastMode())
    {
        Serial.println("FEHLER: BUSY-Timeout vor dem Spleen-Testbild.");
        return false;
    }

    Serial.println("Display: Spleen-Testbild uebertragen");
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
    Serial.println("CrowPanel 5.79-inch Spleen bitmap font test");
    Serial.println("Fonts: native 8x16, 12x24 and 16x32 BDF strikes");

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
    drawSpleenShowcase(canvas);
    display.begin();

    if (refreshDisplay())
    {
        Serial.println("Spleen-Test abgeschlossen; Display ist im Deep Sleep.");
    }
}

void loop()
{
    delay(1000);
}
