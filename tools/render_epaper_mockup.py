"""Render the approved CrowPanel mockup with Elecrow's bitmap fonts."""

from __future__ import annotations

import argparse
import hashlib
import re
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

from fontTools.pens.recordingPen import RecordingPen
from fontTools.svgLib.path import parse_path
from PIL import Image, ImageDraw


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOGO = REPOSITORY_ROOT / "web" / "logo.svg"
DEFAULT_OUTPUT = REPOSITORY_ROOT / "docs" / "epaper-board-mockup.png"
FONT_HEADER_URL = (
    "https://raw.githubusercontent.com/Elecrow-RD/"
    "CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792/"
    "453aa9ec9ccb94bc0c91c81c68eaeef851317aee/"
    "example/arduino/Examples/5.79_Global_refresh/EPDfont.h"
)
FONT_HEADER_SHA256 = "c1aaf9cc6a4de013d8414f2008c7cc4bf16d2db55bfa01d58901fedc99fbc03f"


def read_font_header(path: Path | None) -> str:
    if path is None:
        with urllib.request.urlopen(FONT_HEADER_URL, timeout=30) as response:
            data = response.read()
        source = "the pinned Elecrow source"
    else:
        data = path.read_bytes()
        source = str(path)

    # Git may check out this header with CRLF on Windows. Hash normalized
    # content so the official local file and GitHub's raw LF file both work.
    data = data.replace(b"\r\n", b"\n")
    actual_hash = hashlib.sha256(data).hexdigest()
    if actual_hash != FONT_HEADER_SHA256:
        raise RuntimeError(
            f"Font checksum mismatch for {source}: expected "
            f"{FONT_HEADER_SHA256}, got {actual_hash}"
        )
    return data.decode("utf-8", errors="replace")


def load_font(source: str, name: str, byte_count: int) -> list[list[int]]:
    match = re.search(
        rf"{re.escape(name)}\s*\[\]\[{byte_count}\]\s*=\s*\{{(.*?)\n\}};",
        source,
        flags=re.DOTALL,
    )
    if not match:
        raise RuntimeError(f"Could not find {name} in the Elecrow font header")
    rows = re.findall(r"\{([^{}]+)\}", match.group(1))
    glyphs = [
        [int(value, 16) for value in re.findall(r"0x[0-9A-Fa-f]+", row)]
        for row in rows
    ]
    if len(glyphs) != 95 or any(len(glyph) != byte_count for glyph in glyphs):
        raise RuntimeError(f"Unexpected {name} dimensions")
    return glyphs


def logo_bitmap(path: Path, width: int, height: int) -> Image.Image:
    source = ET.parse(path).getroot()
    large = Image.new("L", (400, 500), 255)
    draw = ImageDraw.Draw(large)

    for element in source.findall("{http://www.w3.org/2000/svg}path"):
        pen = RecordingPen()
        parse_path(element.attrib["d"], pen)
        points: list[tuple[float, float]] = []
        current = (0.0, 0.0)
        for operation, operands in pen.value:
            if operation == "moveTo":
                if points:
                    draw.polygon(points, fill=0)
                current = operands[0]
                points = [(current[0] * 4, current[1] * 4)]
            elif operation == "lineTo":
                current = operands[0]
                points.append((current[0] * 4, current[1] * 4))
            elif operation == "curveTo":
                start_x, start_y = current
                control1, control2, end = operands
                for step in range(1, 17):
                    t = step / 16
                    inverse = 1 - t
                    x = (
                        inverse**3 * start_x
                        + 3 * inverse**2 * t * control1[0]
                        + 3 * inverse * t**2 * control2[0]
                        + t**3 * end[0]
                    )
                    y = (
                        inverse**3 * start_y
                        + 3 * inverse**2 * t * control1[1]
                        + 3 * inverse * t**2 * control2[1]
                        + t**3 * end[1]
                    )
                    points.append((x * 4, y * 4))
                current = end
            elif operation in {"closePath", "endPath"} and points:
                draw.polygon(points, fill=0)
                points = []
        if points:
            draw.polygon(points, fill=0)

    return large.resize((width, height), Image.Resampling.LANCZOS).point(
        lambda value: 255 if value > 180 else 0,
        mode="1",
    )


def draw_char(
    image: Image.Image,
    fonts: dict[int, list[list[int]]],
    x: int,
    y: int,
    char: str,
    size: int,
    ink: int,
) -> None:
    code = ord(char)
    if code < 32 or code > 126:
        code = ord("?")
    glyph = fonts[size][code - 32]
    width = size // 2
    for offset, value in enumerate(glyph):
        page = offset // width
        column = offset % width
        for bit in range(8):
            row = page * 8 + bit
            if row < size and value & (1 << bit):
                image.putpixel((x + column, y + row), ink)


def draw_text(
    image: Image.Image,
    fonts: dict[int, list[list[int]]],
    x: int,
    y: int,
    value: str,
    size: int,
    ink: int,
) -> None:
    for char in value:
        draw_char(image, fonts, x, y, char, size, ink)
        x += size // 2


def wrap_words(value: str, columns: int) -> list[str]:
    lines: list[str] = []
    current = ""
    for word in value.split():
        candidate = word if not current else f"{current} {word}"
        if len(candidate) <= columns:
            current = candidate
        else:
            if current:
                lines.append(current)
            current = word[:columns]
    if current:
        lines.append(current)
    return lines


def render_screen(font_source: str, logo_path: Path) -> Image.Image:
    fonts = {
        16: load_font(font_source, "ascii_1608", 16),
        24: load_font(font_source, "ascii_2412", 36),
    }
    image = Image.new("1", (792, 272), 1)
    draw = ImageDraw.Draw(image)

    draw.rectangle((0, 0, 791, 271), outline=0, width=2)
    draw.line((0, 50, 791, 50), fill=0, width=2)
    draw.line((0, 219, 791, 219), fill=0, width=2)
    image.paste(logo_bitmap(logo_path, 34, 43), (12, 4))
    draw_text(image, fonts, 60, 13, "STRAWBERRY POST", 24, 0)
    draw.rectangle((388, 9, 575, 40), fill=0)
    draw_text(image, fonts, 400, 17, "MISSED CONNECTION", 16, 1)
    draw_text(image, fonts, 650, 17, "NOTICE 1/3", 16, 0)

    message = (
        "To the glittery cowboy near the river stage: "
        "your dance moves changed lives."
    )
    for line_index, line in enumerate(wrap_words(message, 61)):
        draw_text(image, fonts, 28, 72 + line_index * 34, line, 24, 0)

    footer = [
        (18, "WAITING", "12"),
        (210, "WRITTEN", "7"),
        (395, "OUT WITH POSTIE", "4"),
        (622, "DELIVERED", "31"),
    ]
    for x, label, value in footer:
        draw_text(image, fonts, x, 229, value, 24, 0)
        draw_text(image, fonts, x + 40, 235, label, 16, 0)
    return image


def write_cpp_logo(image: Image.Image, output: Path) -> None:
    values: list[int] = []
    bytes_per_row = (image.width + 7) // 8
    for y in range(image.height):
        for byte_index in range(bytes_per_row):
            value = 0
            for bit in range(8):
                x = byte_index * 8 + bit
                if x < image.width and image.getpixel((x, y)) == 0:
                    value |= 0x80 >> bit
            values.append(value)
    rows = [
        "    " + ", ".join(f"0x{value:02x}" for value in values[index:index + 12])
        for index in range(0, len(values), 12)
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "#pragma once\n\n#include <stdint.h>\n\n"
        "// Generated from web/logo.svg at 34 by 43 pixels. Each row is padded "
        "to 40 bits.\nconstexpr uint8_t kEpaperLogo34x43[] = {\n"
        + ",\n".join(rows)
        + "\n};\n",
        encoding="utf-8",
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--font-header",
        type=Path,
        help="Use a local copy of the checksum-verified Elecrow EPDfont.h",
    )
    parser.add_argument("--logo", type=Path, default=DEFAULT_LOGO)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--cpp-logo-output",
        type=Path,
        help="Also regenerate the packed 34 by 43 C++ logo header",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify the output image matches without overwriting it",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    image = render_screen(read_font_header(arguments.font_header), arguments.logo)
    if arguments.cpp_logo_output:
        write_cpp_logo(logo_bitmap(arguments.logo, 34, 43),
                       arguments.cpp_logo_output)
    if arguments.check:
        if not arguments.output.exists():
            raise RuntimeError(f"Expected mockup does not exist: {arguments.output}")
        with Image.open(arguments.output) as expected:
            expected.load()
            if expected.mode != image.mode or expected.size != image.size:
                raise RuntimeError("Mockup dimensions or colour mode do not match")
            if expected.tobytes() != image.tobytes():
                raise RuntimeError("Mockup pixels do not match the generated design")
        print(f"Mockup matches: {arguments.output}")
        return 0

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        dir=arguments.output.parent,
        suffix=".png",
        delete=False,
    ) as temporary:
        temporary_path = Path(temporary.name)
    try:
        image.save(temporary_path, format="PNG", optimize=True)
        temporary_path.replace(arguments.output)
    finally:
        temporary_path.unlink(missing_ok=True)
    print(f"Wrote: {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
