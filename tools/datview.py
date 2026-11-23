#!/usr/bin/env python3
"""Inspect Astro Assembler .DAT files (output of extract_assets.py).

Subcommands:
  segment FILE                      Map FILE into RLE-sprite runs and other data.
  sheet   FILE START COUNT OUT.png  Render COUNT consecutive sprites from offset
                                    START (hex) into a PNG contact sheet.
  raw     FILE START WIDTH HEIGHT OUT.png
                                    Render bytes as an 8-bit indexed image.

Common options:
  --exe PATH       ASTRO.exe to read palettes from (default: extracted/game/ASTRO.exe)
  --palette N|VA   Level palette 1-6 or a palette address in ASTRO.exe (hex VA).
                   Default: 1.

Sprite encoding (see docs/formats.md): per line, runs of
  u16 skip, u16 count, u8 pixels[count]; 0xFFFE = next line; 0xFFFF = end.
Width/height are not stored; they come from the game's descriptor tables.
Here they are derived from the data.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

DATA_VA, DATA_RAW = 0x46E000, 0x6BE00          # ASTRO.exe .data section mapping
LEVEL_PALETTES = {1: 0x4C0298, 2: 0x4C0B98, 3: 0x4C1198, 4: 0x4C1498, 5: 0x4C1798, 6: 0x4C1A98}
MAX_W, MAX_H = 640, 480


def load_palette(exe: bytes, va: int) -> list[tuple[int, int, int]]:
    """256 RGB entries; the game stores 6-bit VGA components (0..63)."""
    off = va - DATA_VA + DATA_RAW
    return [tuple(min(255, exe[off + 3 * i + k] << 2) for k in range(3)) for i in range(256)]


def decode_sprite(d: bytes, p: int):
    """Decode one sprite at p. Returns (end, width, rows) with rows = [[(x, bytes)]],
    or None if the bytes are not a valid sprite."""
    n = len(d)
    rows: list[list[tuple[int, bytes]]] = [[]]
    x = width = 0
    while True:
        if p + 2 > n:
            return None
        a = d[p] | d[p + 1] << 8
        p += 2
        if a == 0xFFFF:
            return (p, width, rows) if width else None
        if a == 0xFFFE:
            rows.append([])
            x = 0
            if len(rows) > MAX_H:
                return None
            continue
        if p + 2 > n:
            return None
        cnt = d[p] | d[p + 1] << 8
        p += 2
        x += a
        if cnt == 0 or x + cnt > MAX_W or p + cnt > n:
            return None
        rows[-1].append((x, d[p:p + cnt]))
        x += cnt
        width = max(width, x)
        p += cnt


def sprite_end(d: bytes, p: int):
    r = decode_sprite(d, p)
    return r[0] if r else None


def segment(d: bytes, min_chain: int = 3):
    """Yield ('sprites', start, end, count) / ('data', start, end, zero_bytes)."""
    def chain(p):
        for _ in range(min_chain):
            p = sprite_end(d, p)
            if p is None:
                return False
        return True

    p, n = 0, len(d)
    while p < n:
        if chain(p):
            s, c = p, 0
            while (q := sprite_end(d, p)) is not None:
                p, c = q, c + 1
            yield ("sprites", s, p, c)
        else:
            s = p
            p += 1
            while p < n and not chain(p):
                p += 1
            yield ("data", s, p, d[s:p].count(0))


def write_png(path: str, width: int, height: int, pixels: list[list[tuple[int, int, int]]]):
    raw = b"".join(b"\0" + bytes(v for px in row for v in px) for row in pixels)

    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n"
                           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9))
                           + chunk(b"IEND", b""))


def cmd_segment(args, d, _pal):
    for kind, s, e, c in segment(d):
        if kind == "sprites":
            print(f"sprites {s:8x}-{e:8x} {e - s:9d} bytes {c:6d} sprites")
        else:
            print(f"data    {s:8x}-{e:8x} {e - s:9d} bytes {100 * c // max(1, e - s):5d}% zero")


def cmd_sheet(args, d, pal):
    sprites, p = [], int(args.start, 16)
    for _ in range(args.count):
        r = decode_sprite(d, p)
        if not r:
            print(f"stopped at {p:x} (not a sprite)", file=sys.stderr)
            break
        sprites.append(r)
        p = r[0]
    width, x, y, row_h, place = 640, 0, 0, 0, []
    for _end, w, rows in sprites:
        if x + w > width:
            x, y, row_h = 0, y + row_h + 2, 0
        place.append((x, y))
        x += w + 2
        row_h = max(row_h, len(rows))
    height = max(1, y + row_h)
    img = [[(40, 0, 40)] * width for _ in range(height)]
    for (ox, oy), (_end, _w, rows) in zip(place, sprites):
        for ry, runs in enumerate(rows):
            for rx, px in runs:
                for k, c in enumerate(px):
                    if ox + rx + k < width:
                        img[oy + ry][ox + rx + k] = pal[c]
    write_png(args.out, width, height, img)
    print(f"{len(sprites)} sprites, {args.start}..{p:x} -> {args.out}")


def cmd_raw(args, d, pal):
    start = int(args.start, 16)
    img = [[pal[d[start + y * args.width + x]] if start + y * args.width + x < len(d) else (0, 0, 0)
            for x in range(args.width)] for y in range(args.height)]
    write_png(args.out, args.width, args.height, img)
    print(f"-> {args.out}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default="extracted/game/ASTRO.exe")
    ap.add_argument("--palette", default="1")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("segment"); s.add_argument("file")
    s = sub.add_parser("sheet"); s.add_argument("file"); s.add_argument("start"); s.add_argument("count", type=int); s.add_argument("out")
    s = sub.add_parser("raw"); s.add_argument("file"); s.add_argument("start"); s.add_argument("width", type=int); s.add_argument("height", type=int); s.add_argument("out")
    args = ap.parse_args(argv)

    d = Path(args.file).read_bytes()
    pal_va = LEVEL_PALETTES[int(args.palette)] if args.palette in "123456" and len(args.palette) == 1 else int(args.palette, 16)
    pal = load_palette(Path(args.exe).read_bytes(), pal_va)
    {"segment": cmd_segment, "sheet": cmd_sheet, "raw": cmd_raw}[args.cmd](args, d, pal)
    return 0


if __name__ == "__main__":
    sys.exit(main())
