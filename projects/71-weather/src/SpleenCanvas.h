#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "BitmapFont.h"

class SpleenCanvas : public Adafruit_GFX
{
public:
    explicit SpleenCanvas(uint8_t* frameBuffer);

    void clear();
    void setClipRect(int16_t x, int16_t y, int16_t width, int16_t height);
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void drawText(
        const BitmapFont& font,
        const String& text,
        int16_t cursorX,
        int16_t baselineY
    );
    void drawCenteredText(
        const BitmapFont& font,
        const String& text,
        int16_t baselineY
    );
    void drawRightAlignedText(
        const BitmapFont& font,
        const String& text,
        int16_t rightX,
        int16_t baselineY
    );
    int16_t textWidth(const BitmapFont& font, const String& text);

private:
    struct HorizontalBounds
    {
        int16_t minimumX;
        int16_t maximumX;
    };

    uint8_t* frameBuffer_;
    int16_t clipLeft_ = 0;
    int16_t clipTop_ = 0;
    int16_t clipRight_ = 0;
    int16_t clipBottom_ = 0;

    static bool readGlyph(
        const BitmapFont& font,
        uint32_t codepoint,
        BitmapGlyph& glyph
    );
    static uint32_t readUtf8Codepoint(const char*& text);
    static HorizontalBounds measureText(
        const BitmapFont& font,
        const char* text
    );
    void drawGlyph(
        const BitmapFont& font,
        const BitmapGlyph& glyph,
        int16_t cursorX,
        int16_t baselineY
    );
};
