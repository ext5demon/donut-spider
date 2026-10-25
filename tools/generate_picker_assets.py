#!/usr/bin/env python3
"""Generate the public menu atlas and original synthesized cues (Pillow required).

No game files are read. Font glyphs come from the project's existing JetBrains
Mono debug atlas; see assets/picker/OFL-JetBrains-Mono.txt. Icons and cues are
original Spider Donut assets, distributed with the runner under MPL-2.0.
"""
from pathlib import Path
import json
import math
import re
import struct
import wave
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets/picker"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "src/debug_font/debug_font.c").read_text(encoding="utf-8")
    pixel_source, glyph_source = source.split("const DebugFontGlyphEntry", 1)
    pixels = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", pixel_source))
    assert len(pixels) == 256 * 256
    glyphs = [list(map(int, m.split(","))) for m in re.findall(r"\{\s*([\d,\s-]+)\}", glyph_source)]
    assert len(glyphs) == 95
    font = Image.frombytes("L", (256, 256), pixels)
    atlas = Image.new("RGBA", (256, 128), (255, 255, 255, 0))
    x = y = row = 0

    def put(im):
        nonlocal x, y, row
        if x + im.width + 1 > atlas.width:
            x = 0
            y += row + 1
            row = 0
        assert y + im.height <= atlas.height
        atlas.paste(im, (x, y))
        rect = [x, y, im.width, im.height]
        x += im.width + 1
        row = max(row, im.height)
        return rect

    layout = {"fonts": [], "icons": [], "heart": []}
    for name, size in [("fnt_main", (7, 13)), ("fnt_small", (5, 9))]:
        entries = []
        for code, (gx, gy, w, h, ox, oy, advance) in enumerate(glyphs, 32):
            cell = Image.new("L", (24, 43))
            if w and h:
                cell.paste(font.crop((gx, gy, gx + w, gy + h)), (ox + 2, oy))
            cell = cell.resize(size, Image.Resampling.BOX).point(lambda v: 255 if v >= 70 else 0)
            rgba = Image.new("RGBA", size, "white")
            rgba.putalpha(cell)
            entries.append([code, *put(rgba), size[0], 0])
        layout["fonts"].append({"name": name, "height": size[1], "glyphs": entries})

    # Seven original badges: book, banner, circuit, broadcast, spark, compass,
    # lantern. Keep existing sprite indices so the UI and animations stay intact.
    for icon in range(7):
        im = Image.new("RGBA", (16, 16), (255, 255, 255, 0))
        d = ImageDraw.Draw(im)
        white = (255, 255, 255, 255)
        if icon == 0:
            d.rectangle((2, 3, 13, 13), outline=white);d.line((7, 3, 7, 13), fill=white)
        elif icon == 1:
            d.line((3, 1, 3, 14), fill=white, width=2)
            d.polygon([(5, 2), (13, 2), (10, 6), (5, 6)], fill=white)
        elif icon == 2:
            d.rectangle((5, 5, 10, 10), outline=white)
            for q in (3, 7, 12):
                d.line((q, 1, q, 3), fill=white);d.line((q, 12, q, 14), fill=white)
                d.line((1, q, 3, q), fill=white);d.line((12, q, 14, q), fill=white)
        elif icon == 3:
            d.rectangle((2, 5, 13, 12), outline=white)
            d.line((4, 1, 8, 4, 12, 1), fill=white);d.rectangle((5, 7, 10, 10), fill=white)
        elif icon == 4:
            d.line((8, 1, 8, 14), fill=white);d.line((1, 8, 14, 8), fill=white)
            d.line((3, 3, 12, 12), fill=white);d.line((12, 3, 3, 12), fill=white)
        elif icon == 5:
            d.ellipse((1, 1, 14, 14), outline=white)
            d.polygon([(8, 3), (11, 10), (8, 8), (4, 12)], fill=white)
        else:
            d.arc((5, 0, 10, 6), 180, 360, fill=white)
            d.rectangle((4, 5, 11, 13), outline=white);d.rectangle((7, 7, 8, 10), fill=white)
            d.line((3, 14, 12, 14), fill=white)
        layout["icons"].append(put(im))
    cursor = Image.new("RGBA", (8, 9), (255, 255, 255, 0))
    ImageDraw.Draw(cursor).polygon([(0, 0), (7, 4), (0, 8)], fill=(255, 0, 0, 255))
    layout["heart"].append(put(cursor))
    atlas.save(OUT / "atlas.png")
    (OUT / "layout.json").write_text(json.dumps(layout, indent=2) + "\n", encoding="utf-8")

    def cue(name, duration, notes):
        rate = 44100
        count = round(duration * rate)
        samples = []
        for i in range(count):
            t = i / rate
            note = min(len(notes) - 1, int(t / duration * len(notes)))
            local = (t / duration * len(notes)) % 1
            envelope = min(1, local * 25) * min(1, (1 - local) * 10)
            envelope *= min(1, t / .006) * min(1, (duration - t) / .015)
            value = math.sin(2 * math.pi * notes[note] * t)
            value += .18 * math.sin(4 * math.pi * notes[note] * t)
            samples.append(round(6000 * envelope * value))
        with wave.open(str(OUT / (name + ".wav")), "wb") as f:
            f.setparams((1, 2, rate, count, "NONE", "not compressed"))
            f.writeframes(struct.pack("<" + "h" * count, *samples))

    cue("move", .055, [880])
    cue("select", .12, [660, 990])
    for chapter in range(1, 6):
        root = 220 * 2 ** ((chapter - 1) / 12)
        cue("chapter" + str(chapter), .8 + chapter * .1, [root, root * 1.25, root * 1.5, root * 2])
    print("Generated public menu atlas and seven original audio cues.")


if __name__ == "__main__":
    main()
