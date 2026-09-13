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

namespace MesloIcons
{
constexpr uint32_t CLOCK = 0xF017;
constexpr uint32_t WARNING = 0xF071;
constexpr uint32_t BUS = 0xF207;
constexpr uint32_t TRAIN = 0xF238;
constexpr uint32_t WIFI = 0xF1EB;
}
