#!/usr/bin/env python3
"""Generate src/web/ PROGMEM headers from data/ (single source of truth).

- data/index.html           -> src/web/index_html.h  (INDEX_HTML, INDEX_HTML_LEN)
- data/manifest.webmanifest -> src/web/assets.h      (MANIFEST_JSON)
- radar icons               -> src/web/assets.h      (ICON_<SIZE>; pure-python PNG)

The generated headers are committed so Arduino-IDE builds (no PlatformIO, no
extra_scripts) still work. A pio pre-build run only regenerates when data/
inputs are newer than the outputs; --check verifies they are in sync.

No third-party python deps.
"""

import math
import struct
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "data"
OUT = REPO / "src" / "web"

ICONS = [  # (symbol, size, maskable)
    ("ICON_192", 192, False),
    ("ICON_512", 512, False),
    ("ICON_192_M", 192, True),
    ("ICON_512_M", 512, True),
    ("ICON_180", 180, False),
    ("ICON_32", 32, False),
]

BG = (0x0B, 0x10, 0x20)
ACCENT = (0x7F, 0xD4, 0xFF)
WEDGE = (0x2C, 0x4A, 0x73)  # accent blended ~30% over bg
DOT = (0xE8, 0xEC, 0xF4)


# ---------------------------------------------------------------- PNG encoder

def _chunk(tag: bytes, body: bytes) -> bytes:
    frame = tag + body
    return (struct.pack(">I", len(body)) + frame
            + struct.pack(">I", zlib.crc32(frame) & 0xFFFFFFFF))


def encode_png(width: int, height: int, rows: list) -> bytes:
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8-bit RGB
    raw = b"".join(b"\x00" + bytes(row) for row in rows)  # filter 0 per row
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr)
            + _chunk(b"IDAT", zlib.compress(raw, 9)) + _chunk(b"IEND", b""))


# ------------------------------------------------------------- icon raster ->

def _sample(px, py, size, content_r, stroke):
    """Color at one canvas sample point (stroke in device px)."""
    cx = cy = size / 2.0
    dist = math.hypot(px - cx, py - cy)
    u = dist / content_r  # 0 at center, 1 at outermost ring
    if u > 1.0:
        return BG
    # three concentric rings
    for target in (0.33, 0.67, 1.0):
        if abs(u - target) * content_r <= stroke:
            return ACCENT
    # sweep wedge pointing up-right (0 deg = screen-up, +clockwise)
    dx, dy = px - cx, py - cy
    ang = math.degrees(math.atan2(dx, -dy))
    if 50.0 <= ang <= 95.0 and 0.33 < u < 1.0:
        return WEDGE
    # sweeping dot sits mid-wedge
    dot_x = 0.66 * content_r * math.sin(math.radians(72))
    dot_y = 0.66 * content_r * math.cos(math.radians(72))
    if math.hypot(px - (cx + dot_x), py - (cy + dot_y)) <= max(1.8, 0.09 * content_r):
        return DOT
    return BG


def render_icon(size: int, maskable: bool) -> bytes:
    # maskable: content must sit inside the safe zone (center ~80%)
    content_r = size * (0.38 if maskable else 0.435)
    stroke = max(1.4, size * 0.024)
    ss = 3 if size >= 128 else 4
    f = ss * ss
    rows = []
    for y in range(size):
        row = bytearray(size * 3)
        for x in range(size):
            r = g = b = 0
            for si in range(ss):
                for sj in range(ss):
                    s = _sample(x + (sj + 0.5) / ss, y + (si + 0.5) / ss,
                                size, content_r, stroke)
                    r += s[0]; g += s[1]; b += s[2]
            row[3 * x:3 * x + 3] = bytes((r // f, g // f, b // f))
        rows.append(row)
    return encode_png(size, size, rows)


# ---------------------------------------------------------------------- emit

def c_string_literal(symbol: str, lenname: str, data: bytes, per_line: int = 96) -> str:
    esc = data.replace(b"\\", b"\\\\").replace(b'"', b'\\"')
    esc = esc.replace(b"\n", b"\\n").replace(b"\r", b"\\r").replace(b"\t", b"\\t")
    out = bytearray()
    for b in esc:  # control/high bytes -> 3-digit octal (unambiguous in C)
        if b < 0x20 or b >= 0x7F:
            out += ("\\%03o" % b).encode("ascii")
        else:
            out.append(b)
    esc = bytes(out)
    # split into atomic fragments (an escape never gets cut across lines)
    frags, buf, i = [], bytearray(), 0
    while i < len(esc):
        c = esc[i:i + 1]
        if c == b"\\":  # one escape token: backslash + (octal:3 | one) chars
            nxt = esc[i + 1:i + 2]
            if nxt.isdigit():
                buf += esc[i:i + 4]; i += 4
            else:
                buf += esc[i:i + 2]; i += 2
            frags.append(bytes(buf)); buf = bytearray()
        else:
            buf += c; i += 1
    if buf:
        frags.append(bytes(buf))
    lines, cur = [], ""
    for frag in frags:
        text = frag.decode("ascii")
        if cur and len(cur) + len(text) > per_line:
            lines.append(cur); cur = ""
        cur += text
    if cur:
        lines.append(cur)
    quoted = "\n".join('  "%s"' % l for l in lines) if lines else '  ""'
    return (f"static const char {symbol}[] PROGMEM =\n{quoted};\n\n"
            f"static const size_t {lenname} = sizeof({symbol}) - 1;\n")


def byte_array(name: str, data: bytes) -> str:
    toks = ["0x%02x" % b for b in data]
    lines, cur = [], toks[0]
    for t in toks[1:]:
        if len(cur) + 2 + len(t) > 66:
            lines.append(cur)
            cur = t
        else:
            cur += ", " + t
    lines.append(cur)
    return (f"static const unsigned char {name}[] PROGMEM = {{\n  "
            + ",\n  ".join(lines) + ",\n};\n\n"
            f"static const size_t {name}_LEN = sizeof({name});\n")


def main() -> int:
    check = "--check" in sys.argv
    force = "--force" in sys.argv

    html = (DATA / "index.html").read_bytes()
    manifest = (DATA / "manifest.webmanifest").read_bytes()

    header_html = ("#pragma once\n// GENERATED from data/index.html by tools/"
                   "gen_web_assets.py -- do not edit.\n\n"
                   + c_string_literal("INDEX_HTML", "INDEX_HTML_LEN", html))
    parts = ("#pragma once\n// GENERATED from data/ by tools/gen_web_assets.py"
             " -- do not edit.\n\n"
             + byte_array("MANIFEST_JSON", manifest)
             + "".join(byte_array(name, render_icon(size, maskable))
                       for name, size, maskable in ICONS))

    if check:
        ok = True
        for path, want in ((OUT / "index_html.h", header_html),
                           (OUT / "assets.h", parts)):
            have = path.read_text() if path.exists() else None
            if have != want:
                ok = False
                print(f"STALE: {path.name} does not match data/ — run"
                      " python3 tools/gen_web_assets.py")
        print("assets in sync" if ok else "assets out of sync")
        return 0 if ok else 1

    OUT.mkdir(parents=True, exist_ok=True)
    header_path, assets_path = OUT / "index_html.h", OUT / "assets.h"
    if not force and header_path.exists() and assets_path.exists():
        newest_src = max(p.stat().st_mtime for p in
                         [DATA / "index.html", DATA / "manifest.webmanifest"])
        gen_mtime = min(header_path.stat().st_mtime, assets_path.stat().st_mtime)
        if gen_mtime >= newest_src:
            return 0  # up to date (keeps every pio build fast)
    header_path.write_text(header_html)
    assets_path.write_text(parts)
    print(f"generated {len(html)}B html + manifest + "
          f"{len(ICONS)} icons -> {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())