#!/usr/bin/env python3
"""Pack the built game and its assets into a single archive.

The result is a ready-to-run folder, archived as .tar.gz (Linux) or .zip
(Windows):

  astro-assembler-<version>-<platform>-<arch>/
    astro / astro.exe        the game (SDL3 frontend; more with --frontends)
    lib/libSDL3.so.0         Linux: bundled SDL3 (found via RUNPATH $ORIGIN/lib)
    SDL3.dll, ...            Windows: the DLLs CMake copied next to astro.exe
    assets/                  the output of tools/package_assets.py, verified
                             against its manifest
    README.txt               how to run it, settings, controls
    licenses/                third-party licenses (SDL3, zlib, dr_flac)

The package contains the original game's data and code derived from
ASTRO.exe (the recompiled core): it is for the owner of the game who built it,
not for redistribution.

Usage:
  python3 tools/make_package.py linux               # from build/cmake
  python3 tools/make_package.py windows             # from build/mingw
  python3 tools/make_package.py linux --build-dir build/x --frontends astro,astro-sdl2
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

DEFAULT_BUILD = {"linux": "build/cmake", "windows": "build/mingw"}

# Where distributions keep the license files of the libraries we ship.
SDL_LICENSES = {
    "linux": ["/usr/share/licenses/sdl3/LICENSE", "/usr/share/licenses/SDL3/LICENSE.txt",
              "/usr/share/doc/libsdl3-0/copyright", "/usr/share/doc/SDL3/LICENSE.txt"],
    "windows": ["/usr/share/licenses/mingw-w64-sdl3/LICENSE",
                "/usr/x86_64-w64-mingw32/share/licenses/SDL3/LICENSE.txt"],
}
ZLIB_LICENSES = ["/usr/share/licenses/zlib/LICENSE", "/usr/share/licenses/mingw-w64-zlib/LICENSE",
                 "/usr/share/doc/zlib1g/copyright"]


def fail(msg: str) -> None:
    sys.exit(f"make_package: {msg}")


def run(cmd: list[str]) -> str:
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def git_version() -> str:
    try:
        return run(["git", "-C", str(ROOT), "describe", "--tags", "--always", "--dirty"]).strip()
    except (OSError, subprocess.CalledProcessError):
        return "dev"


def source_epoch() -> int:
    """Timestamp for the archive entries: SOURCE_DATE_EPOCH, else the last commit."""
    if "SOURCE_DATE_EPOCH" in os.environ:
        return int(os.environ["SOURCE_DATE_EPOCH"])
    try:
        return int(run(["git", "-C", str(ROOT), "log", "-1", "--format=%ct"]).strip())
    except (OSError, subprocess.CalledProcessError, ValueError):
        return int(time.time())


def verify_assets(assets: Path) -> None:
    manifest = assets / "manifest.json"
    if not manifest.is_file():
        fail(f"{manifest} not found; build the assets with tools/package_assets.py")
    files = json.loads(manifest.read_text())["files"]
    bad = []
    for rel, info in files.items():
        p = assets / rel
        if not p.is_file() or p.stat().st_size != info["size"]:
            bad.append(rel)
            continue
        h = hashlib.sha256()
        with p.open("rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        if h.hexdigest() != info["sha256"]:
            bad.append(rel)
    if bad:
        fail("assets do not match their manifest: " + ", ".join(bad[:5]) + (" ..." if len(bad) > 5 else ""))
    print(f"assets: {len(files)} files verified")


def first_existing(candidates: list[str]) -> Path | None:
    for c in candidates:
        if Path(c).is_file():
            return Path(c)
    return None


def strip(path: Path, plat: str) -> None:
    tool = "x86_64-w64-mingw32-strip" if plat == "windows" else "strip"
    if shutil.which(tool):
        subprocess.run([tool, "--strip-debug", str(path)], check=True)
    else:
        print(f"warning: {tool} not found, {path.name} keeps its debug information")


def bundle_linux_libs(exe: Path, dest: Path) -> list[str]:
    """Copy the SDL library the executable links against into lib/."""
    dyn = run(["readelf", "-d", str(exe)])
    if "$ORIGIN/lib" not in dyn:
        print(f"warning: {exe.name} has no RUNPATH $ORIGIN/lib (rebuild with the current "
              "CMakeLists.txt); the bundled library would not be used")
    copied = []
    for line in run(["ldd", str(exe)]).splitlines():
        m = re.match(r"\s*(libSDL[^ ]*\.so[^ ]*) => (\S+)", line)
        if not m:
            continue
        soname, path = m.groups()
        target = dest / "lib" / soname
        if not target.exists():
            target.parent.mkdir(exist_ok=True)
            shutil.copy2(os.path.realpath(path), target)
            os.chmod(target, 0o755)
            copied.append(f"lib/{soname}")
    return copied


def readme(plat: str, exe_names: list[str], version: str) -> str:
    main = exe_names[0]
    run_line = {"linux": f"./{main}", "windows": f"{main} (double-click)"}[plat]
    config = {"linux": "~/.local/share/Astro Assembler/ (or $XDG_DATA_HOME/Astro Assembler/)",
              "windows": "%APPDATA%\\Astro Assembler\\"}[plat]
    extra = ""
    if plat == "linux":
        extra = ("\nThe package brings its own SDL3 (lib/). It needs a Linux system with a C\n"
                 "library at least as new as the one it was built on, and zlib.\n")
    others = ""
    if len(exe_names) > 1:
        others = "\nOther frontends: " + ", ".join(exe_names[1:]) + " (use the system's SDL2 / SDL 1.2).\n"
    return f"""Astro Assembler - port, version {version}
==========================================

Start: {run_line}

The game finds its data in the "assets" folder next to the program. To use
another folder, pass it as the first argument.
{extra}{others}
Settings and saves
------------------
The game's own configuration (ASTRO_CC.CFG: players, controls, high scores,
volumes, difficulty) and the port's settings (astro-port.ini) are kept in

  {config}

astro-port.ini (written on the first exit):
  filter = sharp | nearest | smooth     how the picture is enlarged
  fullscreen = 0 | 1                    Alt+Enter toggles fullscreen
  resolution = 640x480 | 320x240        detail of the picture
  gameplay_aspect = 4:3 | 16:10         4:3 as the original, 16:10 square pixels
  exclusive = 0 | 1                     fullscreen in a real display mode of
                                        the picture's size, if available

Controls (defaults, redefinable in the game's options)
------------------------------------------------------
  Player 1: O / P / Q / A to move, Space = fire, Enter = fire 2
  Player 2: arrow keys, Right Shift = fire, Keypad 0 = fire 2
  Gamepads: stick or d-pad, south button = fire, east button = fire 2
  Escape on the title screen quits.

This package contains the original game's data and code derived from its
program. It was built by the owner of the game for their own use; do not
redistribute it. Third-party licenses are in the "licenses" folder.
"""


def write_licenses(dest: Path, plat: str, sdl_license: str | None) -> list[str]:
    lic = dest / "licenses"
    lic.mkdir()
    notes = []
    sdl = Path(sdl_license) if sdl_license else first_existing(SDL_LICENSES[plat])
    if sdl and sdl.is_file():
        shutil.copy2(sdl, lic / "SDL3.txt")
    else:
        print("warning: SDL3 license file not found (pass --sdl-license); "
              "SDL3 is under the zlib license, see https://libsdl.org/license.php")
        notes.append("SDL3: zlib license, https://libsdl.org/license.php")
    if plat == "windows":                       # zlib is linked statically into astro.exe
        z = first_existing(ZLIB_LICENSES)
        if z:
            shutil.copy2(z, lic / "zlib.txt")
        else:
            notes.append("zlib: zlib license, https://zlib.net/zlib_license.html")
    header = (ROOT / "third_party/dr_flac/dr_flac.h").read_text(encoding="utf-8", errors="replace")
    start = header.find("This software is available as a choice")
    if start >= 0:
        (lic / "dr_flac.txt").write_text("dr_flac by David Reid (https://github.com/mackron/dr_libs)\n\n"
                                         + header[start:].rstrip(" */\n") + "\n")
    summary = ["Third-party software in this package", "",
               "- SDL3 (https://libsdl.org): zlib license, licenses/SDL3.txt",
               "- dr_flac (https://github.com/mackron/dr_libs): public domain or MIT-0, licenses/dr_flac.txt"]
    if plat == "windows":
        summary.append("- zlib (https://zlib.net), linked into astro.exe: zlib license, licenses/zlib.txt")
        if (dest / "libssp-0.dll").exists():
            summary.append("- libssp-0.dll (GCC stack protector runtime, needed by this SDL3.dll): "
                           "GPLv3 with the GCC Runtime Library Exception, https://gcc.gnu.org")
    else:
        summary.append("- zlib: used from the system")
    summary += [""] + notes
    (lic / "README.txt").write_text("\n".join(summary).rstrip() + "\n")
    return notes


def archive(stage: Path, out: Path, plat: str, epoch: int) -> Path:
    base = stage.name
    files = sorted(p for p in stage.rglob("*"))
    if plat == "linux":
        path = out / f"{base}.tar.gz"

        def norm(ti: tarfile.TarInfo) -> tarfile.TarInfo:
            ti.uid = ti.gid = 0
            ti.uname = ti.gname = ""
            ti.mtime = epoch
            return ti

        with tarfile.open(path, "w:gz", compresslevel=9) as tar:
            tar.add(stage, arcname=base, recursive=False, filter=norm)
            for p in files:
                tar.add(p, arcname=f"{base}/{p.relative_to(stage).as_posix()}", recursive=False, filter=norm)
    else:
        path = out / f"{base}.zip"
        stamp = time.gmtime(max(epoch, 315532800))[:6]          # zip dates start in 1980
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for p in files:
                name = f"{base}/{p.relative_to(stage).as_posix()}" + ("/" if p.is_dir() else "")
                info = zipfile.ZipInfo(name, stamp)
                info.external_attr = ((0o755 if p.is_dir() or os.access(p, os.X_OK) else 0o644) << 16) \
                    | (0x10 if p.is_dir() else 0)
                if p.is_dir():
                    z.writestr(info, b"")
                else:
                    info.compress_type = zipfile.ZIP_DEFLATED
                    with p.open("rb") as f:
                        z.writestr(info, f.read())
    return path


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("platform", choices=["linux", "windows"])
    ap.add_argument("--build-dir", help="CMake build directory (default: build/cmake or build/mingw)")
    ap.add_argument("--assets", default="assets", help="asset folder from tools/package_assets.py")
    ap.add_argument("-o", "--out", default="dist", help="output folder (default: dist)")
    ap.add_argument("--frontends", default="astro",
                    help="comma-separated executables to include (default: astro)")
    ap.add_argument("--version", help="version in the package name (default: git describe)")
    ap.add_argument("--sdl-license", help="SDL3 license file, if not found automatically")
    ap.add_argument("--no-strip", action="store_true", help="keep debug information")
    ap.add_argument("--keep-folder", action="store_true", help="keep the unpacked folder next to the archive")
    args = ap.parse_args()

    plat = args.platform
    build = (ROOT / (args.build_dir or DEFAULT_BUILD[plat])).resolve()
    assets = (ROOT / args.assets).resolve()
    out = (ROOT / args.out).resolve()
    version = args.version or git_version()
    arch = "x86_64" if plat == "windows" else platform.machine()
    suffix = ".exe" if plat == "windows" else ""

    exes = []
    for name in [n.strip() for n in args.frontends.split(",") if n.strip()]:
        p = build / f"{name}{suffix}"
        if not p.is_file():
            fail(f"{p} not found; build it first (cmake --build --preset {plat if plat == 'linux' else 'mingw'})")
        exes.append(p)
    verify_assets(assets)

    stage = out / f"astro-assembler-{version}-{plat}-{arch}"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    for exe in exes:
        target = stage / exe.name
        shutil.copy2(exe, target)
        if not args.no_strip:
            strip(target, plat)
    bundled = []
    if plat == "linux":
        bundled = bundle_linux_libs(exes[0], stage)
    else:
        for dll in sorted(build.glob("*.dll")):
            shutil.copy2(dll, stage / dll.name)
            bundled.append(dll.name)
    print("bundled libraries: " + (", ".join(bundled) or "none"))

    shutil.copytree(assets, stage / "assets")
    (stage / "README.txt").write_text(readme(plat, [e.name for e in exes], version),
                                      newline="\r\n" if plat == "windows" else "\n")
    write_licenses(stage, plat, args.sdl_license)

    path = archive(stage, out, plat, source_epoch())
    if not args.keep_folder:
        shutil.rmtree(stage)
    print(f"{path} ({path.stat().st_size / 1e6:.1f} MB)")
    print("note: the package contains the original game's data; it is for your own use, do not redistribute it")


if __name__ == "__main__":
    main()
