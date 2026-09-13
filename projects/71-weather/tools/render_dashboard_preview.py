#!/usr/bin/env python3
"""Render the project-71 sidebar with the generated firmware bitmaps."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw


DISPLAY_WIDTH = 792
DISPLAY_HEIGHT = 272
PANEL_LEFT = 528
PANEL_WIDTH = 264
CONTENT_LEFT = 536
CONTENT_RIGHT = 784
FIRST_SYMBOL_X = 536
FIRST_VALUE_X = 568
SECOND_SYMBOL_X = 660
SECOND_VALUE_X = 692
FORECAST_VALUE_WIDTH = 80
CURRENT_ROW_BOTTOM = 64
TODAY_ROW_TOP = 65
TODAY_ROW_BOTTOM = 203
TOMORROW_ROW_TOP = 204


@dataclass(frozen=True)
class Glyph:
    bitmap_offset: int
    width: int
    height: int
    x_advance: int
    x_offset: int
    y_offset: int


@dataclass(frozen=True)
class Font:
    bitmap: tuple[int, ...]
    glyphs: dict[int, Glyph]


def load_font(header: str, name: str) -> Font:
    bitmap_match = re.search(
        rf"const uint8_t {name}Bitmaps\[\] PROGMEM = \{{(.*?)\n\}};",
        header,
        re.DOTALL,
    )
    glyph_match = re.search(
        rf"const BitmapGlyph {name}Glyphs\[\] PROGMEM = \{{(.*?)\n\}};",
        header,
        re.DOTALL,
    )
    if bitmap_match is None or glyph_match is None:
        raise ValueError(f"font section not found: {name}")

    bitmap = tuple(
        int(value, 16)
        for value in re.findall(r"0x([0-9A-F]{2})", bitmap_match.group(1))
    )
    glyphs: dict[int, Glyph] = {}
    for values in re.findall(
        r"\{\s*0x([0-9A-F]+),\s*(\d+),\s*(\d+),\s*(\d+),"
        r"\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}",
        glyph_match.group(1),
    ):
        codepoint, offset, width, height, advance, x_offset, y_offset = (
            int(value, 16) if index == 0 else int(value)
            for index, value in enumerate(values)
        )
        glyphs[codepoint] = Glyph(
            offset,
            width,
            height,
            advance,
            x_offset,
            y_offset,
        )
    return Font(bitmap, glyphs)


def text_bounds(font: Font, text: str) -> tuple[int, int]:
    minimum = 32767
    maximum = -32768
    cursor = 0
    fallback = font.glyphs[ord("?")]
    for character in text:
        glyph = font.glyphs.get(ord(character), fallback)
        if glyph.width:
            minimum = min(minimum, cursor + glyph.x_offset)
            maximum = max(maximum, cursor + glyph.x_offset + glyph.width)
        cursor += glyph.x_advance
    return (0, 0) if minimum == 32767 else (minimum, maximum)


def draw_text(
    image: Image.Image,
    font: Font,
    text: str,
    cursor_x: int,
    baseline_y: int,
) -> None:
    fallback = font.glyphs[ord("?")]
    for character in text:
        glyph = font.glyphs.get(ord(character), fallback)
        for row in range(glyph.height):
            for column in range(glyph.width):
                bit_index = row * glyph.width + column
                value = font.bitmap[glyph.bitmap_offset + bit_index // 8]
                if value & (0x80 >> (bit_index % 8)):
                    x = cursor_x + glyph.x_offset + column
                    y = baseline_y + glyph.y_offset + row
                    if PANEL_LEFT <= x < DISPLAY_WIDTH and 0 <= y < DISPLAY_HEIGHT:
                        image.putpixel((x, y), 0)
        cursor_x += glyph.x_advance


def draw_right(
    image: Image.Image,
    font: Font,
    text: str,
    right_x: int,
    baseline_y: int,
) -> None:
    _, maximum = text_bounds(font, text)
    draw_text(image, font, text, right_x - maximum, baseline_y)


def forecast_temperature(font: Font, value: float) -> str:
    precise = f"{value:.1f}°C"
    minimum, maximum = text_bounds(font, precise)
    if maximum - minimum <= FORECAST_VALUE_WIDTH:
        return precise
    return f"{round(value):.0f}°C"


def draw_summary(
    image: Image.Image,
    small: Font,
    medium: Font,
    row_top: int,
    heading: str,
    samples: int,
    day_temperature: float,
    night_temperature: float,
    minimum: float,
    maximum: float,
    rain_mm: float,
    rain_probability: int,
    rain_time: str,
    solar_times: tuple[str, str] | None,
    detailed: bool,
) -> None:
    heading_font = small
    heading_baseline = row_top + 14
    draw_text(image, heading_font, heading, CONTENT_LEFT, heading_baseline)
    draw_right(
        image,
        heading_font,
        f"{samples} WERTE",
        CONTENT_RIGHT,
        heading_baseline,
    )
    if solar_times is not None:
        drawing = ImageDraw.Draw(image)
        sunrise_x = FIRST_SYMBOL_X + 3
        sunset_x = SECOND_SYMBOL_X + 3
        triangle_top = row_top + 30
        triangle_bottom = row_top + 45
        drawing.polygon(
            (
                (sunrise_x + 9, triangle_top),
                (sunrise_x, triangle_bottom),
                (sunrise_x + 18, triangle_bottom),
            ),
            fill=0,
        )
        drawing.line(
            (
                (sunset_x, triangle_top),
                (sunset_x + 18, triangle_top),
                (sunset_x + 9, triangle_bottom),
                (sunset_x, triangle_top),
            ),
            fill=0,
        )
        draw_text(
            image,
            medium,
            solar_times[0],
            FIRST_VALUE_X,
            row_top + 47,
        )
        draw_text(
            image,
            medium,
            solar_times[1],
            SECOND_VALUE_X,
            row_top + 47,
        )
    average_font = medium
    average_baseline = row_top + (73 if detailed else 40)
    draw_text(
        image,
        average_font,
        "🌞",
        FIRST_SYMBOL_X,
        average_baseline,
    )
    draw_text(
        image,
        average_font,
        forecast_temperature(average_font, day_temperature),
        FIRST_VALUE_X,
        average_baseline,
    )
    draw_text(
        image,
        average_font,
        "🌙",
        SECOND_SYMBOL_X,
        average_baseline,
    )
    draw_text(
        image,
        average_font,
        forecast_temperature(average_font, night_temperature),
        SECOND_VALUE_X,
        average_baseline,
    )
    if detailed:
        temperature_baseline = row_top + 99
        arrow_top = temperature_baseline - 17
        minimum_x = FIRST_SYMBOL_X + 6
        maximum_x = SECOND_SYMBOL_X + 6
        drawing = ImageDraw.Draw(image)
        for left, points_up in ((minimum_x, False), (maximum_x, True)):
            center_x = left + 6
            bottom = arrow_top + 15
            drawing.line((center_x, arrow_top, center_x, bottom), fill=0)
            tip_y = arrow_top if points_up else bottom
            wing_y = tip_y + 6 if points_up else tip_y - 6
            drawing.line((center_x, tip_y, left, wing_y), fill=0)
            drawing.line((center_x, tip_y, left + 12, wing_y), fill=0)
        draw_text(
            image,
            medium,
            forecast_temperature(medium, minimum),
            FIRST_VALUE_X,
            temperature_baseline,
        )
        draw_text(
            image,
            medium,
            forecast_temperature(medium, maximum),
            SECOND_VALUE_X,
            temperature_baseline,
        )
    draw_text(
        image,
        medium if detailed else small,
        f"🌧 {rain_mm:.1f}mm {rain_probability}% {rain_time}",
        CONTENT_LEFT,
        row_top + (125 if detailed else 61),
    )


def render(header_path: Path, output_path: Path, winter: bool = False) -> None:
    header = header_path.read_text(encoding="utf-8")
    small = load_font(header, "Spleen8x16")
    medium = load_font(header, "Spleen12x24")
    large = load_font(header, "Spleen16x32")
    image = Image.new("1", (DISPLAY_WIDTH, DISPLAY_HEIGHT), 1)
    drawing = ImageDraw.Draw(image)
    drawing.rectangle(
        (PANEL_LEFT, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1),
        outline=0,
    )
    drawing.line((PANEL_LEFT, CURRENT_ROW_BOTTOM, DISPLAY_WIDTH - 1, CURRENT_ROW_BOTTOM), fill=0)
    drawing.line((PANEL_LEFT, TODAY_ROW_BOTTOM, DISPLAY_WIDTH - 1, TODAY_ROW_BOTTOM), fill=0)

    draw_text(image, small, "Sonntag, 13.09.2026", CONTENT_LEFT, 16)
    draw_right(image, medium, "16:42", CONTENT_RIGHT, 20)
    current_temperature = -15.0 if winter else 24.7
    draw_text(
        image,
        large,
        f"🌡 {current_temperature:.1f}°C",
        CONTENT_LEFT,
        56,
    )
    draw_right(image, large, "☁", CONTENT_RIGHT, 56)

    draw_summary(
        image,
        small,
        medium,
        TODAY_ROW_TOP,
        "HEUTE",
        4,
        -11.8 if winter else 24.1,
        -15.0 if winter else 18.2,
        -18.4 if winter else 17.8,
        -8.7 if winter else 25.4,
        1.2 if winter else 0.8,
        70 if winter else 60,
        "18:00" if winter else "20:00",
        ("06:37", "19:24"),
        True,
    )
    draw_summary(
        image,
        small,
        medium,
        TOMORROW_ROW_TOP,
        "MORGEN",
        9,
        -10.6 if winter else 22.8,
        -15.3 if winter else 15.6,
        -17.1 if winter else 13.9,
        -7.8 if winter else 24.6,
        0.4 if winter else 0.0,
        40 if winter else 0,
        "06:00" if winter else "--:--",
        None,
        False,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("font_header", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--winter-output", type=Path)
    args = parser.parse_args()
    render(args.font_header, args.output)
    if args.winter_output is not None:
        render(args.font_header, args.winter_output, winter=True)


if __name__ == "__main__":
    main()
