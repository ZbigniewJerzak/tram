#!/usr/bin/env python3
"""Convert selected glyphs from Spleen BDF files into compact C++ data."""

from __future__ import annotations

import argparse
import hashlib
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


GERMAN_CODEPOINTS = (
    0x00B0,
    0x00C4,
    0x00D6,
    0x00DC,
    0x00DF,
    0x00E4,
    0x00F6,
    0x00FC,
)
ARROW_CODEPOINTS = (0x2190, 0x2191, 0x2192, 0x2193)
BOX_CODEPOINTS = (0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518)
POWERLINE_CODEPOINTS = (0xE0A0, 0xE0B0, 0xE0B1, 0xE0B2, 0xE0B3)
WEATHER_CODEPOINTS = {
    0x1F321: "thermometer",
    0x2600: "sun",
    0x2601: "cloud",
    0x1F327: "rain",
    0x1F31E: "day",
    0x1F319: "night",
}
NOTO_EMOJI_RASTER_SIZE = 256
NOTO_EMOJI_WEIGHT = 600
NOTO_EMOJI_THRESHOLD = 96
REQUIRED_CODEPOINTS = tuple(range(0x20, 0x7F)) + GERMAN_CODEPOINTS
CODEPOINTS = (
    tuple(range(0x20, 0x7F))
    + GERMAN_CODEPOINTS
    + ARROW_CODEPOINTS
    + BOX_CODEPOINTS
    + POWERLINE_CODEPOINTS
)
SOURCES = (
    ("Spleen8x16", "spleen-8x16.bdf"),
    ("Spleen12x24", "spleen-12x24.bdf"),
    ("Spleen16x32", "spleen-16x32.bdf"),
)

@dataclass(frozen=True)
class Glyph:
    codepoint: int
    width: int
    height: int
    x_advance: int
    x_offset: int
    y_offset: int
    rows: tuple[str, ...]


@dataclass(frozen=True)
class Font:
    ascent: int
    descent: int
    glyphs: dict[int, Glyph]


def parse_bdf(path: Path) -> Font:
    lines = path.read_text(encoding="ascii").splitlines()
    ascent = descent = None
    glyphs: dict[int, Glyph] = {}
    index = 0

    while index < len(lines):
        line = lines[index]
        if line.startswith("FONT_ASCENT "):
            ascent = int(line.split()[1])
        elif line.startswith("FONT_DESCENT "):
            descent = int(line.split()[1])
        elif line.startswith("STARTCHAR "):
            codepoint = None
            advance = 0
            box = (0, 0, 0, 0)
            rows: list[str] = []
            index += 1
            while index < len(lines) and lines[index] != "ENDCHAR":
                entry = lines[index]
                if entry.startswith("ENCODING "):
                    codepoint = int(entry.split()[1])
                elif entry.startswith("DWIDTH "):
                    advance = int(entry.split()[1])
                elif entry.startswith("BBX "):
                    values = tuple(int(value) for value in entry.split()[1:])
                    box = values  # type: ignore[assignment]
                elif entry == "BITMAP":
                    height = box[1]
                    rows = lines[index + 1 : index + 1 + height]
                    index += height

                index += 1

            if codepoint is not None and codepoint >= 0:
                width, height, x_offset, lower_offset = box
                glyphs[codepoint] = Glyph(
                    codepoint=codepoint,
                    width=width,
                    height=height,
                    x_advance=advance,
                    x_offset=x_offset,
                    y_offset=-(height + lower_offset),
                    rows=tuple(rows),
                )
        index += 1

    if ascent is None or descent is None:
        raise ValueError(f"missing font metrics in {path}")
    return Font(ascent=ascent, descent=descent, glyphs=glyphs)


def pack_glyph(glyph: Glyph) -> list[int]:
    bits: list[int] = []
    for row in glyph.rows:
        value = int(row, 16)
        padded_width = len(row) * 4
        for column in range(glyph.width):
            bits.append((value >> (padded_width - column - 1)) & 1)

    packed: list[int] = []
    for start in range(0, len(bits), 8):
        byte = 0
        for bit_index, pixel in enumerate(bits[start : start + 8]):
            if pixel:
                byte |= 0x80 >> bit_index
        packed.append(byte)
    return packed


def image_to_hex_rows(image: Image.Image) -> tuple[str, ...]:
    width, height = image.size
    hex_digits = (width + 3) // 4
    padded_width = hex_digits * 4
    rows: list[str] = []
    for y in range(height):
        value = 0
        for x in range(width):
            value = (value << 1) | (image.getpixel((x, y)) != 0)
        value <<= padded_width - width
        rows.append(f"{value:0{hex_digits}X}")
    return tuple(rows)


def load_noto_emoji(path: Path) -> ImageFont.FreeTypeFont:
    if not path.is_file():
        raise ValueError(f"Noto Emoji source not found: {path}")
    font = ImageFont.truetype(str(path), NOTO_EMOJI_RASTER_SIZE)
    try:
        font.set_variation_by_axes([NOTO_EMOJI_WEIGHT])
    except (AttributeError, OSError):
        # Older Pillow/FreeType combinations use the font's regular default.
        pass
    return font


def rasterize_noto_emoji(
    noto_font: ImageFont.FreeTypeFont,
    codepoint: int,
    cell_size: int,
) -> Image.Image:
    character = chr(codepoint)
    bounds = noto_font.getbbox(character)
    if bounds is None:
        raise ValueError(f"Noto Emoji lacks U+{codepoint:04X}")

    source = Image.new(
        "L",
        (bounds[2] - bounds[0], bounds[3] - bounds[1]),
        0,
    )
    ImageDraw.Draw(source).text(
        (-bounds[0], -bounds[1]),
        character,
        font=noto_font,
        fill=255,
    )
    ink_bounds = source.getbbox()
    if ink_bounds is None:
        raise ValueError(f"Noto Emoji produced no pixels for U+{codepoint:04X}")
    source = source.crop(ink_bounds)

    # Ein Pixel Rand verhindert, dass Strahlen oder Tropfen die Nachbarzeile
    # berühren. Das Seitenverhältnis des Noto-Originals bleibt erhalten.
    available = cell_size - 2
    scale = min(available / source.width, available / source.height)
    target_size = (
        max(1, round(source.width * scale)),
        max(1, round(source.height * scale)),
    )
    resized = source.resize(target_size, Image.Resampling.LANCZOS)
    monochrome = resized.point(
        lambda coverage: 255 if coverage >= NOTO_EMOJI_THRESHOLD else 0,
        mode="1",
    )
    result = Image.new("1", (cell_size, cell_size), 0)
    result.paste(
        monochrome,
        ((cell_size - target_size[0]) // 2, (cell_size - target_size[1]) // 2),
    )
    return result


def make_weather_glyph(
    font: Font,
    noto_font: ImageFont.FreeTypeFont,
    codepoint: int,
) -> Glyph:
    cell_size = font.ascent + font.descent
    image = rasterize_noto_emoji(noto_font, codepoint, cell_size)
    return Glyph(
        codepoint=codepoint,
        width=cell_size,
        height=cell_size,
        x_advance=cell_size,
        x_offset=0,
        y_offset=-font.ascent,
        rows=image_to_hex_rows(image),
    )


def format_bytes(values: list[int]) -> str:
    return ",\n".join(
        "    " + ", ".join(f"0x{value:02X}" for value in values[start : start + 12])
        for start in range(0, len(values), 12)
    )


def make_section(
    name: str,
    font: Font,
    noto_font: ImageFont.FreeTypeFont,
) -> str:
    missing = [
        codepoint for codepoint in REQUIRED_CODEPOINTS
        if codepoint not in font.glyphs
    ]
    if missing:
        formatted = ", ".join(f"U+{codepoint:04X}" for codepoint in missing)
        raise ValueError(f"{name} lacks required glyphs: {formatted}")

    selected_codepoints = [
        codepoint for codepoint in CODEPOINTS if codepoint in font.glyphs
    ]
    glyphs = [font.glyphs[codepoint] for codepoint in selected_codepoints]
    glyphs.extend(
        make_weather_glyph(font, noto_font, codepoint)
        for codepoint in WEATHER_CODEPOINTS
    )
    bitmap: list[int] = []
    glyph_rows: list[str] = []
    for glyph in glyphs:
        offset = len(bitmap)
        bitmap.extend(pack_glyph(glyph))
        glyph_rows.append(
            "    { 0x%04X, %5d, %2d, %2d, %2d, %3d, %3d }"
            % (
                glyph.codepoint,
                offset,
                glyph.width,
                glyph.height,
                glyph.x_advance,
                glyph.x_offset,
                glyph.y_offset,
            )
        )

    return "\n".join(
        (
            f"const uint8_t {name}Bitmaps[] PROGMEM = {{",
            format_bytes(bitmap),
            "};",
            "",
            f"const BitmapGlyph {name}Glyphs[] PROGMEM = {{",
            ",\n".join(glyph_rows),
            "};",
            "",
            f"const BitmapFont {name} = {{",
            f"    {name}Bitmaps,",
            f"    {name}Glyphs,",
            f"    {len(glyphs)},",
            f"    {font.ascent + font.descent},",
            f"    {font.ascent}",
            "};",
        )
    )


def write_header(source_dir: Path, noto_emoji_path: Path, output: Path) -> None:
    noto_font = load_noto_emoji(noto_emoji_path)
    hashes: list[str] = []
    sections: list[str] = []
    for name, filename in SOURCES:
        source = source_dir / filename
        if not source.is_file():
            raise ValueError(f"font source not found: {source}")
        hashes.append(f"// {filename} SHA-256: {hashlib.sha256(source.read_bytes()).hexdigest()}")
        sections.append(make_section(name, parse_bdf(source), noto_font))
    hashes.append(
        "// Noto Emoji SHA-256: "
        + hashlib.sha256(noto_emoji_path.read_bytes()).hexdigest()
    )

    header = "\n".join(
        (
            "#pragma once",
            "",
            "#include <Arduino.h>",
            "",
            '#include "BitmapFont.h"',
            "",
            "// Generated from native Spleen BDF strikes and monochrome Noto Emoji.",
            "// Subset: ASCII, German glyphs, arrows, box drawing, Powerline, weather.",
            f"// Noto Emoji weight {NOTO_EMOJI_WEIGHT}, 1-bit threshold {NOTO_EMOJI_THRESHOLD}.",
            *hashes,
            "",
            "\n\n".join(sections),
            "",
        )
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(header, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_dir", type=Path, help="directory containing Spleen BDF files")
    parser.add_argument("output", type=Path, help="generated C++ header")
    parser.add_argument(
        "--noto-emoji",
        required=True,
        type=Path,
        help="monochrome Noto Emoji variable TTF",
    )
    args = parser.parse_args()
    try:
        write_header(args.source_dir, args.noto_emoji, args.output)
    except ValueError as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
