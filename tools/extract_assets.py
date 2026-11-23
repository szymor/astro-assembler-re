#!/usr/bin/env python3
"""Extract the original Astro Assembler files from the retail CD image.

Pipeline (standard library only, works on Linux and Windows):
  CD image (.cue/.bin raw 2352 or .iso 2048)
    -> ISO 9660 filesystem
    -> install/astro1.cab (MS Cabinet, MSZip)
    -> ASTRO.exe + *.dat + *.bmp + Manual.pdf
    -> WAV resources embedded in ASTRO.exe

Output layout (default: ./extracted):
  extracted/game/   files from the CAB (original names)
  extracted/wav/    WAV resources from ASTRO.exe, named after their resource name
  extracted/manifest.json   sizes and SHA-256 of every extracted file

Usage:
  python3 tools/extract_assets.py img/Astro_Assembler.cue [-o extracted]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

GAME_CAB_PATH = "INSTALL/ASTRO1.CAB"


# --------------------------------------------------------------------------
# CD image -> 2048-byte sector reader
# --------------------------------------------------------------------------

class CdImage:
    """Read user-data sectors from a raw MODE1/2352 .bin or a cooked .iso."""

    def __init__(self, path: Path):
        self.f = open(path, "rb")
        size = path.stat().st_size
        self.f.seek(16 * 2048 + 1)
        if self.f.read(5) == b"CD001":
            self.sector_size, self.data_offset = 2048, 0
        else:
            self.f.seek(16 * 2352 + 16 + 1)
            if self.f.read(5) != b"CD001":
                raise ValueError(f"{path}: not an ISO 9660 MODE1 image")
            self.sector_size, self.data_offset = 2352, 16
        self.num_sectors = size // self.sector_size

    def read(self, lba: int, length: int) -> bytes:
        out = bytearray()
        while len(out) < length:
            self.f.seek(lba * self.sector_size + self.data_offset)
            out += self.f.read(min(2048, length - len(out)))
            lba += 1
        return bytes(out)


def resolve_image(path: Path) -> Path:
    """Accept a .cue (resolve its FILE entry, case-insensitively) or a data file."""
    if path.suffix.lower() != ".cue":
        return path
    for line in path.read_text(errors="replace").splitlines():
        line = line.strip()
        if line.upper().startswith("FILE"):
            name = line.split('"')[1] if '"' in line else line.split()[1]
            candidate = path.parent / name
            if candidate.exists():
                return candidate
            for p in path.parent.iterdir():
                if p.name.lower() == name.lower():
                    return p
            raise FileNotFoundError(f"{path}: referenced file {name!r} not found")
    raise ValueError(f"{path}: no FILE entry")


# --------------------------------------------------------------------------
# ISO 9660
# --------------------------------------------------------------------------

def iso_files(cd: CdImage) -> dict[str, tuple[int, int]]:
    """Return {UPPERCASE/PATH: (lba, size)} for every file on the disc."""
    pvd = cd.read(16, 2048)
    root = pvd[156:156 + 34]
    files: dict[str, tuple[int, int]] = {}

    def walk(lba: int, size: int, prefix: str):
        data = cd.read(lba, size)
        pos = 0
        while pos < len(data):
            rec_len = data[pos]
            if rec_len == 0:  # records never span sectors; skip to next one
                pos = (pos // 2048 + 1) * 2048
                continue
            rec = data[pos:pos + rec_len]
            pos += rec_len
            ext_lba = struct.unpack_from("<I", rec, 2)[0]
            ext_size = struct.unpack_from("<I", rec, 10)[0]
            flags = rec[25]
            name_len = rec[32]
            name = rec[33:33 + name_len]
            if name in (b"\x00", b"\x01"):
                continue
            name = name.decode("ascii").split(";")[0].rstrip(".").upper()
            if flags & 2:
                walk(ext_lba, ext_size, prefix + name + "/")
            else:
                files[prefix + name] = (ext_lba, ext_size)

    walk(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "")
    return files


# --------------------------------------------------------------------------
# Microsoft Cabinet (MSZip only - that is what the game uses)
# --------------------------------------------------------------------------

def cab_extract(cab: bytes) -> dict[str, bytes]:
    if cab[:4] != b"MSCF":
        raise ValueError("not a cabinet file")
    (cb_cabinet, coff_files, _vmin, _vmaj, c_folders, c_files, flags,
     _set_id, _i_cab) = struct.unpack_from("<8xI4xI4xBBHHHHH", cab, 0)
    pos = 36
    cb_folder_reserve = cb_data_reserve = 0
    if flags & 0x0004:  # cfhdrRESERVE_PRESENT
        cb_header_reserve, cb_folder_reserve, cb_data_reserve = struct.unpack_from("<HBB", cab, pos)
        pos += 4 + cb_header_reserve
    for bit in (0x0001, 0x0002):  # skip prev/next cabinet + disk names
        if flags & bit:
            for _ in range(2):
                pos = cab.index(b"\x00", pos) + 1
    if flags & 0x0003:
        raise ValueError("multi-volume cabinets are not supported")

    folders = []
    for _ in range(c_folders):
        coff_start, c_data, type_compress = struct.unpack_from("<IHH", cab, pos)
        pos += 8 + cb_folder_reserve
        folders.append((coff_start, c_data, type_compress))

    folder_data: list[bytes] = []
    for coff_start, c_data, type_compress in folders:
        method = type_compress & 0x000F
        p = coff_start
        out = bytearray()
        history = b""
        for _ in range(c_data):
            _csum, cb_data, cb_uncomp = struct.unpack_from("<IHH", cab, p)
            p += 8 + cb_data_reserve
            block = cab[p:p + cb_data]
            p += cb_data
            if method == 0:
                out += block
            elif method == 1:  # MSZip: "CK" + raw deflate, 32K history carried across blocks
                if block[:2] != b"CK":
                    raise ValueError("bad MSZip block signature")
                d = zlib.decompressobj(-15, zdict=history) if history else zlib.decompressobj(-15)
                chunk = d.decompress(block[2:]) + d.flush()
                if len(chunk) != cb_uncomp:
                    raise ValueError("MSZip block size mismatch")
                out += chunk
                history = (history + chunk)[-32768:]
            else:
                raise ValueError(f"unsupported cabinet compression {method} (only MSZip)")
        folder_data.append(bytes(out))

    files: dict[str, bytes] = {}
    pos = coff_files
    for _ in range(c_files):
        cb_file, uoff, i_folder, _date, _time, _attr = struct.unpack_from("<IIHHHH", cab, pos)
        pos += 16
        end = cab.index(b"\x00", pos)
        name = cab[pos:end].decode("latin-1").replace("\\", "/")
        pos = end + 1
        files[name] = folder_data[i_folder][uoff:uoff + cb_file]
    return files


# --------------------------------------------------------------------------
# PE resources
# --------------------------------------------------------------------------

def pe_resources(exe: bytes) -> list[tuple[str, str, int, bytes]]:
    """Return [(type, name, language, data)] for every resource in a PE file."""
    pe_off = struct.unpack_from("<I", exe, 0x3C)[0]
    if exe[pe_off:pe_off + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    n_sections = struct.unpack_from("<H", exe, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", exe, pe_off + 20)[0]
    opt = pe_off + 24
    rsrc_rva = struct.unpack_from("<I", exe, opt + 96 + 2 * 8)[0]  # data dir #2

    sections = []
    sec = opt + opt_size
    for i in range(n_sections):
        _name, vsize, va, raw_size, raw_ptr = struct.unpack_from("<8sIIII", exe, sec + i * 40)
        sections.append((va, max(vsize, raw_size), raw_ptr))

    def rva2off(rva: int) -> int:
        for va, size, raw in sections:
            if va <= rva < va + size:
                return rva - va + raw
        raise ValueError(f"RVA {rva:#x} outside sections")

    base = rva2off(rsrc_rva)

    def read_name(off: int) -> str:
        n = struct.unpack_from("<H", exe, base + off)[0]
        return exe[base + off + 2: base + off + 2 + 2 * n].decode("utf-16-le")

    def entries(dir_off: int):
        n_named, n_id = struct.unpack_from("<HH", exe, base + dir_off + 12)
        for i in range(n_named + n_id):
            name_field, target = struct.unpack_from("<II", exe, base + dir_off + 16 + 8 * i)
            name = read_name(name_field & 0x7FFFFFFF) if name_field & 0x80000000 else str(name_field)
            yield name, target

    result = []
    for type_name, t in entries(0):
        for res_name, n in entries(t & 0x7FFFFFFF):
            for lang, d in entries(n & 0x7FFFFFFF):
                data_rva, size = struct.unpack_from("<II", exe, base + d)
                off = rva2off(data_rva)
                result.append((type_name, res_name, int(lang), exe[off:off + size]))
    return result


# --------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", type=Path, help="CD image (.cue, .bin or .iso)")
    ap.add_argument("-o", "--output", type=Path, default=Path("extracted"))
    args = ap.parse_args(argv)

    image = resolve_image(args.image)
    cd = CdImage(image)
    print(f"image: {image} ({cd.sector_size}-byte sectors, {cd.num_sectors} sectors)")

    disc = iso_files(cd)
    if GAME_CAB_PATH not in disc:
        print(f"error: {GAME_CAB_PATH} not found on disc - is this the Astro Assembler CD?", file=sys.stderr)
        return 1
    lba, size = disc[GAME_CAB_PATH]
    cab_files = cab_extract(cd.read(lba, size))

    manifest: dict[str, dict] = {}

    def save(rel: str, data: bytes):
        path = args.output / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        manifest[rel] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}

    for name, data in sorted(cab_files.items()):
        save(f"game/{name}", data)
    print(f"cab: {len(cab_files)} files -> {args.output / 'game'}")

    exe_name = next(n for n in cab_files if n.upper() == "ASTRO.EXE")
    wavs = [r for r in pe_resources(cab_files[exe_name]) if r[0] == "WAV"]
    for _type, name, _lang, data in wavs:
        save(f"wav/{name}.wav", data)
    print(f"exe: {len(wavs)} WAV resources -> {args.output / 'wav'}")

    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
