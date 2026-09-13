#pragma once

#include <Arduino.h>

struct BitmapGlyph
{
    uint32_t codepoint;
    uint32_t bitmapOffset;
    uint8_t width;
    uint8_t height;
    uint8_t xAdvance;
    int8_t xOffset;
    int8_t yOffset;
};

struct BitmapFont
{
    const uint8_t* bitmap;
    const BitmapGlyph* glyphs;
    uint16_t glyphCount;
    uint8_t lineHeight;
    uint8_t ascent;
};
