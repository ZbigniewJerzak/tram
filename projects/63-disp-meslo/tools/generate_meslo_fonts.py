#!/usr/bin/env python3
"""Generate compact 1-bit Meslo Nerd Font data for the ESP32 showcase."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from PIL import ImageFont


GERMAN_CODEPOINTS = (0x00C4, 0x00D6, 0x00DC, 0x00DF, 0x00E4, 0x00F6, 0x00FC)
ICON_CODEPOINTS = (0xF017, 0xF071, 0xF1EB, 0xF207, 0xF238)
CODEPOINTS = tuple(range(0x20, 0x7F)) + GERMAN_CODEPOINTS + ICON_CODEPOINTS
SIZES = (16, 24, 36)


def pack_bitmap(mask: object) -> list[int]:
    pixels = [1 if value else 0 for value in mask]
    packed: list[int] = []

    for start in range(0, len(pixels), 8):
        value = 0
        for bit_index, pixel in enumerate(pixels[start : start + 8]):
            if pixel:
                value |= 0x80 >> bit_index
        packed.append(value)

    return packed


def format_bytes(values: list[int]) -> str:
    lines = []
    for start in range(0, len(values), 12):
        chunk = values[start : start + 12]
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in chunk))
    return ",\n".join(lines)


def generate_font(font_path: Path, size: int) -> tuple[list[int], list[tuple[int, ...]], int, int]:
    font = ImageFont.truetype(str(font_path), size)
    ascent, descent = font.getmetrics()
    bitmap: list[int] = []
    glyphs: list[tuple[int, ...]] = []

    for codepoint in CODEPOINTS:
        mask, offset = font.getmask2(chr(codepoint), mode="1", anchor="ls")
        width, height = mask.size
        glyphs.append(
            (
                codepoint,
                len(bitmap),
                width,
                height,
                round(font.getlength(chr(codepoint))),
                offset[0],
                offset[1],
            )
        )
        bitmap.extend(pack_bitmap(mask))

    return bitmap, glyphs, ascent + descent, ascent


def write_header(font_path: Path, output_path: Path) -> None:
    source_hash = hashlib.sha256(font_path.read_bytes()).hexdigest()
    sections = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        '#include "BitmapFont.h"',
        "",
        "// Generated from MesloLGS NF Regular.ttf.",
        f"// Source SHA-256: {source_hash}",
        "// Subset: printable ASCII, German umlauts/eszett and five icons.",
        "",
    ]

    for size in SIZES:
        bitmap, glyphs, line_height, ascent = generate_font(font_path, size)
        name = f"Meslo{size}"
        glyph_lines = []
        for glyph in glyphs:
            glyph_lines.append(
                "    { 0x%04X, %5d, %2d, %2d, %2d, %3d, %3d }"
                % glyph
            )

        sections.extend(
            [
                f"const uint8_t {name}Bitmaps[] PROGMEM = {{",
                format_bytes(bitmap),
                "};",
                "",
                f"const BitmapGlyph {name}Glyphs[] PROGMEM = {{",
                ",\n".join(glyph_lines),
                "};",
                "",
                f"const BitmapFont {name} = {{",
                f"    {name}Bitmaps,",
                f"    {name}Glyphs,",
                f"    {len(glyphs)},",
                f"    {line_height},",
                f"    {ascent}",
                "};",
                "",
            ]
        )

    output_path.write_text("\n".join(sections), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("font", type=Path, help="MesloLGS NF Regular TTF")
    parser.add_argument("output", type=Path, help="generated C++ header")
    args = parser.parse_args()

    if not args.font.is_file():
        parser.error(f"font file not found: {args.font}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_header(args.font, args.output)


if __name__ == "__main__":
    main()
