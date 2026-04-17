#!/usr/bin/env python3
"""
Extracts UM_LOGO_PNG and UM_LOGO_SMALL_PNG from um_logo.h,
makes the black background transparent (alpha = 0 where RGB is near-black),
and appends UM_LOGO_TRANSPARENT_PNG / UM_LOGO_SMALL_TRANSPARENT_PNG to um_logo.h.
"""

import re
import io
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Install Pillow first:  pip3 install Pillow")

LOGO_H = Path(__file__).parent.parent / "um_logo.h"
# Pixels with R+G+B < this threshold are considered "background black"
BLACK_THRESHOLD = 60


def extract_array(source: str, name: str) -> bytes:
    pattern = rf"static const uint8_t {re.escape(name)}\[\]\s*=\s*\{{(.*?)\}};"
    m = re.search(pattern, source, re.DOTALL)
    if not m:
        sys.exit(f"Could not find array {name} in um_logo.h")
    hex_tokens = re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))
    return bytes(int(h, 16) for h in hex_tokens)


def make_transparent(png_bytes: bytes, threshold: int) -> bytes:
    img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    pixels = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = pixels[x, y]
            if r < threshold and g < threshold and b < threshold:
                pixels[x, y] = (r, g, b, 0)
    out = io.BytesIO()
    img.save(out, format="PNG", optimize=True)
    return out.getvalue()


def bytes_to_c_array(name: str, data: bytes) -> str:
    lines = [f"static const uint8_t {name}[] = {{"]
    row = []
    for i, b in enumerate(data):
        row.append(f"0x{b:02x}")
        if len(row) == 12 or i == len(data) - 1:
            lines.append("  " + ", ".join(row) + ",")
            row = []
    lines.append("};")
    lines.append(f"static const size_t {name}_LEN = sizeof({name});")
    return "\n".join(lines)


def main():
    source = LOGO_H.read_text()

    # Don't regenerate if already present
    if "UM_LOGO_TRANSPARENT_PNG" in source:
        print("Transparent arrays already present — removing old ones first.")
        # Strip from first occurrence of the sentinel comment to end
        source = re.sub(
            r"\n// ---.*?transparent.*?---.*$", "", source,
            flags=re.DOTALL | re.IGNORECASE
        )

    pairs = [
        ("UM_LOGO_PNG",       "UM_LOGO_TRANSPARENT_PNG"),
        ("UM_LOGO_SMALL_PNG", "UM_LOGO_SMALL_TRANSPARENT_PNG"),
    ]

    appended = "\n// --- transparent variants (black bg removed) ---\n"
    for src_name, dst_name in pairs:
        print(f"Processing {src_name} …")
        raw = extract_array(source, src_name)
        transparent = make_transparent(raw, BLACK_THRESHOLD)
        appended += "\n" + bytes_to_c_array(dst_name, transparent) + "\n"
        print(f"  {src_name}: {len(raw)} bytes  →  {dst_name}: {len(transparent)} bytes")

    LOGO_H.write_text(source.rstrip() + "\n" + appended)
    print(f"\nDone. Updated {LOGO_H}")


if __name__ == "__main__":
    main()
