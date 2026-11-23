#!/usr/bin/env python3
"""Build the standalone asset directory for the Astro Assembler port.

Inputs (owned by the person running the build):
  - the retail CD image (.cue/.bin or .iso)
  - optionally, the 15 soundtrack FLACs (01.flac .. 15.flac), which replace the
    CD-audio tracks 2..16 the game expects but that the disc never contained

Output layout (default: ./assets):
  data/       GAME.DAT, LEVEL1-6.DAT, MENU.DAT  - byte-identical to the original
              ASTRO_DATA.BIN                    - ASTRO.exe .data section image
  images/     LOGO1-3, PORTADA, FONDO1-2, NAVES, LETRAS, FINAL1-2 as PNG
  sfx/        NN_Name.wav (NN = the game's sound id)
  music/      trackNN.flac (NN = CD track number 02..16)
  sounds.json sound table: id, name, voices, sets
  manifest.json  source checksums, layout of ASTRO_DATA.BIN, file list
  Manual.pdf

The .DAT files are not converted: the game addresses them by hard-coded
offsets (see docs/formats.md).

Usage:
  python3 tools/package_assets.py img/Astro_Assembler.cue --flac flac -o assets
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("extract_assets", HERE / "extract_assets.py")
ex = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ex)

# SHA-256 of the files the port depends on (retail CD, installer ProductVersion 1.0.0).
KNOWN_SHA256 = {
    "ASTRO.exe": "74affc5024bee080e4ce244797e604776064b34e780e05617787d79b1ff4715e",
    "Game.dat": "756fe9e97befbf38e617ed1e5a1501808946ffd5b5f3dfb77004ed72a11f3e4c",
    "Level1.dat": "3a5c3eea622103f851fb8f7b624e2a4720b2cb698656b2d18f4e9178b26eca1f",
    "Level2.dat": "7a85e039390864096577afdd2884b09f73a697e94d403b219a3f3a7f8d445de6",
    "Level3.dat": "12e94ec52c9cb87662097ce7e227c551a16eded28ce51f7bd66df960dcd6e32f",
    "Level4.dat": "e165b51a762f7c19b7629299567555a7b7825e0737f0ce344e2fe492ebdd6444",
    "Level5.dat": "0b9a5e1af0e0616c620c308730d6200cd5e0109410bd91e616facb13f46efbe9",
    "Level6.dat": "f8a90e45651c41ecaf70b864b6571ee7d1ccfcb5fa474d05d71c1d06da4cb50b",
    "Menu.dat": "3f691d59997e3e8dc20f2712132904cffd11071c597ba7c402d1db73b252d9cb",
}

DAT_FILES = ["Game.dat", "Level1.dat", "Level2.dat", "Level3.dat", "Level4.dat",
             "Level5.dat", "Level6.dat", "Menu.dat"]
BMP_FILES = ["Logo1.bmp", "Logo2.bmp", "Logo3.bmp", "Portada.bmp", "Fondo1.bmp",
             "Fondo2.bmp", "Naves.bmp", "Letras.bmp", "Final1.bmp", "Final2.bmp"]

# ASTRO.exe tables (valid for the exe with the hash above)
SOUND_TABLE_VA = 0x46F1D0            # {char *name; int voices;} x 78
SOUND_SETS = {"menu": 0x46EC18, "level1": 0x46EC28, "level2": 0x46ED10,
              "level3": 0x46EE10, "level4": 0x46EF08, "level5": 0x46F000,
              "level6": 0x46F0E8}    # int ids[], terminated by -1


class Pe:
    def __init__(self, data: bytes):
        self.data = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        n = struct.unpack_from("<H", data, pe + 6)[0]
        opt_size = struct.unpack_from("<H", data, pe + 20)[0]
        self.image_base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
        self.sections = {}
        for i in range(n):
            name, vsize, va, raw_size, raw_ptr = struct.unpack_from("<8sIIII", data, pe + 24 + opt_size + 40 * i)
            self.sections[name.rstrip(b"\0").decode()] = (self.image_base + va, vsize, raw_ptr, raw_size)

    def off(self, va: int) -> int:
        for sva, vsize, raw, rsize in self.sections.values():
            if sva <= va < sva + rsize:
                return raw + va - sva
        raise ValueError(f"VA {va:#x} not in file")

    def u32(self, va: int) -> int:
        return struct.unpack_from("<I", self.data, self.off(va))[0]

    def i32(self, va: int) -> int:
        return struct.unpack_from("<i", self.data, self.off(va))[0]

    def cstr(self, va: int) -> str:
        o = self.off(va)
        return self.data[o:self.data.index(b"\0", o)].decode("latin-1")


def bmp_to_png(bmp: bytes) -> bytes:
    """Uncompressed 24-bit BMP -> RGB PNG (lossless)."""
    if bmp[:2] != b"BM":
        raise ValueError("not a BMP")
    off_bits = struct.unpack_from("<I", bmp, 10)[0]
    w, h, _planes, bpp, comp = struct.unpack_from("<iiHHI", bmp, 18)
    if bpp != 24 or comp != 0:
        raise ValueError(f"unsupported BMP ({bpp} bpp, compression {comp})")
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(abs(h)):
        src_y = abs(h) - 1 - y if h > 0 else y     # positive height = bottom-up
        line = bmp[off_bits + src_y * stride: off_bits + src_y * stride + w * 3]
        rgb = bytearray(line)
        rgb[0::3], rgb[2::3] = line[2::3], line[0::3]   # BGR -> RGB
        rows.append(b"\0" + bytes(rgb))

    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, abs(h), 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
            + chunk(b"IEND", b""))


def flac_info(data: bytes):
    """(sample_rate, channels, bits, total_samples) from the STREAMINFO block."""
    if data[:4] != b"fLaC":
        raise ValueError("not a FLAC file")
    si = data[8:8 + 34]
    v = int.from_bytes(si[10:18], "big")
    return v >> 44, ((v >> 41) & 7) + 1, ((v >> 36) & 31) + 1, v & ((1 << 36) - 1)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", type=Path, help="CD image (.cue, .bin or .iso)")
    ap.add_argument("--flac", type=Path, help="directory with 01.flac .. 15.flac")
    ap.add_argument("-o", "--output", type=Path, default=Path("assets"))
    ap.add_argument("--force", action="store_true", help="continue if the game files differ from the known release")
    args = ap.parse_args(argv)

    # --- read the disc ---------------------------------------------------------
    cd = ex.CdImage(ex.resolve_image(args.image))
    disc = ex.iso_files(cd)
    if ex.GAME_CAB_PATH not in disc:
        print(f"error: {ex.GAME_CAB_PATH} not found - is this the Astro Assembler CD?", file=sys.stderr)
        return 1
    files = {Path(k).name.lower(): v for k, v in ex.cab_extract(cd.read(*disc[ex.GAME_CAB_PATH])).items()}

    def get(name: str) -> bytes:
        return files[name.lower()]

    sources = {}
    mismatch = []
    for name, want in KNOWN_SHA256.items():
        h = hashlib.sha256(get(name)).hexdigest()
        sources[name] = h
        if h != want:
            mismatch.append(name)
    if mismatch:
        print("warning: these files differ from the release the port was built against: "
              + ", ".join(mismatch), file=sys.stderr)
        if not args.force:
            print("error: refusing to continue without --force", file=sys.stderr)
            return 1

    out = args.output
    if out.exists():
        shutil.rmtree(out)
    written: dict[str, dict] = {}

    def save(rel: str, data: bytes):
        p = out / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        written[rel] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}

    # --- data: byte-identical ------------------------------------------------
    for name in DAT_FILES:
        save(f"data/{name.upper()}", get(name))

    exe_bytes = get("ASTRO.exe")
    pe = Pe(exe_bytes)
    data_va, data_vsize, data_raw, data_rsize = pe.sections[".data"]
    save("data/ASTRO_DATA.BIN", exe_bytes[data_raw:data_raw + data_rsize])

    # --- images ----------------------------------------------------------------
    for name in BMP_FILES:
        save(f"images/{Path(name).stem.upper()}.png", bmp_to_png(get(name)))

    # --- sound effects ---------------------------------------------------------
    wav_by_name = {n.upper(): d for t, n, _l, d in ex.pe_resources(exe_bytes) if t == "WAV"}
    sounds = []
    for i in range(1000):
        p = pe.u32(SOUND_TABLE_VA + 8 * i)
        if not (data_va <= p < data_va + data_vsize):
            break
        sounds.append({"id": i, "name": pe.cstr(p), "voices": pe.u32(SOUND_TABLE_VA + 8 * i + 4), "sets": []})
    for set_name, va in SOUND_SETS.items():
        k = 0
        while (sid := pe.i32(va + 4 * k)) != -1:
            sounds[sid]["sets"].append(set_name)
            k += 1
    for s in sounds:
        s["file"] = f"sfx/{s['id']:02d}_{s['name']}.wav"
        save(s["file"], wav_by_name[s["name"].upper()])
    (out / "sounds.json").write_text(json.dumps(sounds, indent=1) + "\n")

    # --- music -------------------------------------------------------------------
    music = []
    if args.flac:
        for n in range(1, 16):
            src = args.flac / f"{n:02d}.flac"
            if not src.exists():
                print(f"warning: {src} missing", file=sys.stderr)
                continue
            data = src.read_bytes()
            rate, ch, bits, samples = flac_info(data)
            track = n + 1
            save(f"music/track{track:02d}.flac", data)
            music.append({"track": track, "file": f"music/track{track:02d}.flac", "seconds": round(samples / rate, 2),
                          "format": f"{rate} Hz, {ch} ch, {bits} bit"})
    else:
        print("note: no --flac given, the package has no music", file=sys.stderr)

    save("Manual.pdf", get("Manual.pdf"))

    manifest = {
        "format": 1,
        "sources": sources,
        "astro_data": {"file": "data/ASTRO_DATA.BIN", "va": data_va, "virtual_size": data_vsize,
                       "note": "ASTRO.exe .data image; bytes past the file size are zero at runtime"},
        "music": music,
        "files": written,
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n")

    total = sum(f["size"] for f in written.values())
    print(f"wrote {len(written)} files ({total / 1e6:.1f} MB) to {out}: "
          f"{len(DAT_FILES)} data, {len(BMP_FILES)} images, {len(sounds)} sounds, {len(music)} music tracks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
