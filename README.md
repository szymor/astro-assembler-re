# Astro Assembler — reverse engineering & C/SDL3 port

A faithful re-implementation of *Astro Assembler* (Windows, DirectX 7, ~2001)
in C with SDL3, targeting Linux and Windows.

This repository contains **no original game data**. To build a playable game
you need your own copy of the retail CD image. The build pipeline converts
it into a standalone game that does not need the disc at runtime.

## Layout

| Path | Contents |
|---|---|
| `tools/` | Asset extraction and conversion scripts (Python 3, stdlib only) |
| `docs/` | Reverse-engineering notes and file format docs |
| `src/` | The C/SDL3 port (not started yet) |
| `re/recomp/` | Static recompiler (p-code → C) and its runtime; see [docs/recompiler.md](docs/recompiler.md) |
| `re/symbols.tsv` | Canonical function/global names for `ASTRO.exe` |
| `re/scripts/`, `re/ghidra_scripts/` | RE helper scripts (seed finding, symbol import, exports) |
| `re/ghidra/`, `re/wineprefix/` | Local Ghidra project and Wine prefix (git-ignored) |
| `img/`, `flac/` | Your original CD image and soundtrack (git-ignored) |
| `extracted/` | Output of the extraction script (git-ignored) |

## Extracting the original files

```bash
python3 tools/extract_assets.py img/Astro_Assembler.cue
```

This writes `extracted/game/` (the installed game files),
`extracted/wav/` (sound effects embedded in `ASTRO.exe`) and
`extracted/manifest.json` (SHA-256 of every file).

## Building the asset package

```bash
python3 tools/package_assets.py img/Astro_Assembler.cue --flac flac -o assets
```

Creates `assets/` with everything the port needs: the original `.DAT` files
(unchanged), the `ASTRO.exe` data section, PNG images, WAV sounds, music as
`trackNN.flac`, and a manifest. Nothing in it may be committed.

## Inspecting the data files

```bash
python3 tools/datview.py segment extracted/game/Level1.dat
```

```bash
python3 tools/datview.py sheet extracted/game/Level1.dat 0 200 level1.png
```

See [docs/formats.md](docs/formats.md) for the formats.

## Running the original (reference)

```bash
tools/run_original.sh
```

Runs `ASTRO.exe` under Wine in its own prefix with a 640×480 virtual desktop.

## Status

See [docs/plan.md](docs/plan.md) for the roadmap,
[docs/findings.md](docs/findings.md) for what is known so far and
[docs/modules.md](docs/modules.md) for the structure of `ASTRO.exe`.
