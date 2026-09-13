#include "SpleenCanvas.h"

#include <climits>
#include <cstring>

#include "ElecrowEpd579.h"

namespace
{
constexpr uint16_t COLOR_BLACK = 1;
constexpr uint16_t CONTROLLER_SEAM_X = 396;
constexpr uint16_t SEAM_ADDRESS_OFFSET = 8;
constexpr size_t DRIVER_ROW_BYTES = ElecrowEpd579::DRIVER_WIDTH / 8;
}

SpleenCanvas::SpleenCanvas(uint8_t* const frameBuffer)
    : Adafruit_GFX(ElecrowEpd579::VISIBLE_WIDTH, ElecrowEpd579::HEIGHT),
      frameBuffer_(frameBuffer),
      clipRight_(ElecrowEpd579::VISIBLE_WIDTH),
      clipBottom_(ElecrowEpd579::HEIGHT)
{
}

void SpleenCanvas::clear()
{
    memset(frameBuffer_, 0xFF, ElecrowEpd579::FRAMEBUFFER_SIZE);
}

void SpleenCanvas::setClipRect(
    const int16_t x,
    const int16_t y,
    const int16_t clipWidth,
    const int16_t clipHeight
)
{
    clipLeft_ = x;
    clipTop_ = y;
    clipRight_ = x + clipWidth;
    clipBottom_ = y + clipHeight;
}

void SpleenCanvas::drawPixel(
    const int16_t x,
    const int16_t y,
    const uint16_t color
)
{
    if (
        x < clipLeft_ || y < clipTop_ ||
        x >= clipRight_ || y >= clipBottom_ ||
        x < 0 || y < 0 || x >= width() || y >= height()
    )
    {
        return;
    }

    const uint16_t visibleX = static_cast<uint16_t>(x);
    const uint16_t visibleY = static_cast<uint16_t>(y);
    const uint16_t shiftedX = visibleX < CONTROLLER_SEAM_X
        ? visibleX
        : visibleX + SEAM_ADDRESS_OFFSET;
    const uint16_t driverX = ElecrowEpd579::DRIVER_WIDTH - shiftedX - 1;
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

bool SpleenCanvas::readGlyph(
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

uint32_t SpleenCanvas::readUtf8Codepoint(const char*& text)
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
    else if ((first & 0xF8U) == 0xF0U)
    {
        const uint8_t second = static_cast<uint8_t>(*text++);
        const uint8_t third = static_cast<uint8_t>(*text++);
        const uint8_t fourth = static_cast<uint8_t>(*text++);
        if (
            (second & 0xC0U) == 0x80U &&
            (third & 0xC0U) == 0x80U &&
            (fourth & 0xC0U) == 0x80U
        )
        {
            return ((first & 0x07U) << 18U) |
                ((second & 0x3FU) << 12U) |
                ((third & 0x3FU) << 6U) |
                (fourth & 0x3FU);
        }
    }
    return '?';
}

void SpleenCanvas::drawGlyph(
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
                drawPixel(
                    cursorX + glyph.xOffset + column,
                    baselineY + glyph.yOffset + row,
                    COLOR_BLACK
                );
            }
            ++bitIndex;
        }
    }
}

SpleenCanvas::HorizontalBounds SpleenCanvas::measureText(
    const BitmapFont& font,
    const char* text
)
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
            const int16_t left = cursorX + glyph.xOffset;
            const int16_t right = left + glyph.width;
            if (left < bounds.minimumX)
            {
                bounds.minimumX = left;
            }
            if (right > bounds.maximumX)
            {
                bounds.maximumX = right;
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

void SpleenCanvas::drawText(
    const BitmapFont& font,
    const String& textValue,
    int16_t cursorX,
    const int16_t baselineY
)
{
    const char* text = textValue.c_str();
    while (*text != '\0')
    {
        const uint32_t codepoint = readUtf8Codepoint(text);
        BitmapGlyph glyph = {};
        if (!readGlyph(font, codepoint, glyph))
        {
            readGlyph(font, '?', glyph);
        }
        drawGlyph(font, glyph, cursorX, baselineY);
        cursorX += glyph.xAdvance;
    }
}

void SpleenCanvas::drawCenteredText(
    const BitmapFont& font,
    const String& text,
    const int16_t baselineY
)
{
    const HorizontalBounds bounds = measureText(font, text.c_str());
    const int16_t textWidth = bounds.maximumX - bounds.minimumX;
    drawText(
        font,
        text,
        (width() - textWidth) / 2 - bounds.minimumX,
        baselineY
    );
}

void SpleenCanvas::drawRightAlignedText(
    const BitmapFont& font,
    const String& text,
    const int16_t rightX,
    const int16_t baselineY
)
{
    const HorizontalBounds bounds = measureText(font, text.c_str());
    drawText(font, text, rightX - bounds.maximumX, baselineY);
}

int16_t SpleenCanvas::textWidth(
    const BitmapFont& font,
    const String& text
)
{
    const HorizontalBounds bounds = measureText(font, text.c_str());
    return bounds.maximumX - bounds.minimumX;
}
