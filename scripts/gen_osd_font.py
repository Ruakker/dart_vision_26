#!/usr/bin/env python3
"""Generate the checked-in ASCII 8x12 OSD font using ImageMagick."""
import argparse
import subprocess
from pathlib import Path


def render(font, char):
    escaped = "\\" + char if char in "\\\"'" else char
    command = [
        "convert", "-size", "8x12", "xc:black", "-font", font,
        "-pointsize", "11", "-fill", "white", "-gravity", "center",
        "-annotate", "+0+0", escaped, "-threshold", "50%", "-depth", "8",
        "gray:-",
    ]
    pixels = subprocess.check_output(command)
    if len(pixels) != 96:
        raise RuntimeError(f"unexpected raster size for {char!r}: {len(pixels)}")
    return [sum((1 << (7 - x)) for x in range(8) if pixels[y * 8 + x] >= 128)
            for y in range(12)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", default="/usr/share/fonts/noto/NotoSansMono-Regular.ttf")
    parser.add_argument("--out", default="src/osd_font8x12.h")
    args = parser.parse_args()

    rows = [render(args.font, chr(code)) for code in range(0x20, 0x7f)]
    out = [
        "#ifndef DART_VISION_OSD_FONT8X12_H_",
        "#define DART_VISION_OSD_FONT8X12_H_",
        "",
        "#include <stdint.h>",
        "",
        "#define OSD_FONT_W 8",
        "#define OSD_FONT_H 12",
        "#define OSD_FONT_ASCII_START 0x20",
        "#define OSD_FONT_ASCII_COUNT 95",
        "",
        "static const uint8_t g_osd_font8x12[OSD_FONT_ASCII_COUNT][OSD_FONT_H] = {",
    ]
    for code, glyph in zip(range(0x20, 0x7f), rows):
        values = ", ".join(f"0x{value:02x}" for value in glyph)
        out.append(f"  /* 0x{code:02x} {chr(code)!r} */ {{ {values} }},")
    out.extend(["};", "", "#endif", ""])
    Path(args.out).write_text("\n".join(out), encoding="ascii")


if __name__ == "__main__":
    main()
