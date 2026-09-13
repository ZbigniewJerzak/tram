#include <Adafruit_GFX.h>
#include <Arduino.h>

#include <climits>
#include <cstring>

#include "BitmapFont.h"
#include "ElecrowEpd579.h"
#include "MesloFontData.h"

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
        bounds.minimumX = 0;
        bounds.maximumX = 0;
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
    const int16_t cursorX = (canvas.width() - width) / 2 - bounds.minimumX;
    drawText(canvas, font, text, cursorX, baselineY);
}

void drawCenteredLabel(
    TextCanvas& canvas,
    const char* const label,
    const int16_t centerX,
    const int16_t baselineY
)
{
    const HorizontalBounds bounds = measureText(Meslo16, label);
    const int16_t width = bounds.maximumX - bounds.minimumX;
    drawText(
        canvas,
        Meslo16,
        label,
        centerX - width / 2 - bounds.minimumX,
        baselineY
    );
}

void drawCenteredIcon(
    TextCanvas& canvas,
    const uint32_t codepoint,
    const int16_t centerX,
    const int16_t baselineY
)
{
    BitmapGlyph glyph = {};
    if (!readGlyph(Meslo36, codepoint, glyph))
    {
        return;
    }

    const int16_t cursorX =
        centerX - glyph.xOffset - static_cast<int16_t>(glyph.width) / 2;
    drawGlyph(canvas, Meslo36, glyph, cursorX, baselineY);
}

void drawSizeLabel(
    TextCanvas& canvas,
    const uint8_t size,
    const uint8_t threshold,
    const int16_t baselineY
)
{
    char label[20] = {};
    snprintf(label, sizeof(label), "%u PX / T%u", size, threshold);
    drawText(canvas, Meslo16, label, 16, baselineY);
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

void drawMesloShowcase(TextCanvas& canvas)
{
    constexpr int16_t ICON_CENTERS[] = {220, 308, 396, 484, 572};
    constexpr uint32_t ICONS[] = {
        MesloIcons::TRAIN,
        MesloIcons::BUS,
        MesloIcons::CLOCK,
        MesloIcons::WIFI,
        MesloIcons::WARNING
    };
    constexpr const char* LABELS[] = {
        "TRAIN", "BUS", "TIME", "WIFI", "WARN"
    };

    canvas.clear();
    canvas.drawRect(0, 0, canvas.width(), canvas.height(), COLOR_BLACK);
    canvas.drawRect(4, 4, canvas.width() - 8, canvas.height() - 8, COLOR_BLACK);

    drawCenteredText(canvas, Meslo16, "MESLO COVERAGE TUNING | 1-BIT", 24);
    canvas.drawFastHLine(20, 35, canvas.width() - 40, COLOR_BLACK);

    drawSizeLabel(canvas, 36, Meslo36CoverageThreshold, 74);
    drawCenteredText(canvas, Meslo36, "Straße & Grüße", 75);

    drawSizeLabel(canvas, 24, Meslo24CoverageThreshold, 111);
    drawCenteredText(canvas, Meslo24, "ÄÖÜ äöü ß | 0123456789", 112);

    drawSizeLabel(canvas, 20, Meslo20CoverageThreshold, 140);
    drawCenteredText(canvas, Meslo20, "Berlin | München | Köln", 141);

    drawSizeLabel(canvas, 16, Meslo16CoverageThreshold, 166);
    drawCenteredText(canvas, Meslo16, "Fahrplan: nächste Tram in 6 Minuten", 166);
    canvas.drawFastHLine(20, 181, canvas.width() - 40, COLOR_BLACK);

    for (size_t index = 0; index < 5; ++index)
    {
        drawCenteredIcon(canvas, ICONS[index], ICON_CENTERS[index], 225);
        drawCenteredLabel(canvas, LABELS[index], ICON_CENTERS[index], 255);
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

    Serial.println("Display: fuer Meslo-Testbild neu initialisieren");
    if (!display.initializeFastMode())
    {
        Serial.println("FEHLER: BUSY-Timeout vor dem Meslo-Testbild.");
        return false;
    }

    Serial.println("Display: Meslo-Testbild uebertragen");
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
    Serial.println("CrowPanel 5.79-inch Meslo coverage tuning test");

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
    drawMesloShowcase(canvas);

    display.begin();

    if (refreshDisplay())
    {
        Serial.println("Font-Tuning abgeschlossen; Display ist im Deep Sleep.");
    }
}

void loop()
{
    delay(1000);
}
