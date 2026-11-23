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
| `re/` | Local RE workspace: Ghidra project, Wine prefix (git-ignored) |
| `img/`, `flac/` | Your original CD image and soundtrack (git-ignored) |
| `extracted/` | Output of the extraction script (git-ignored) |

## Extracting the original files

```bash
python3 tools/extract_assets.py img/Astro_Assembler.cue
```

This writes `extracted/game/` (the installed game files),
`extracted/wav/` (sound effects embedded in `ASTRO.exe`) and
`extracted/manifest.json` (SHA-256 of every file).

## Running the original (reference)

```bash
tools/run_original.sh
```

Runs `ASTRO.exe` under Wine in its own prefix with a 640×480 virtual desktop.

## Status

See [docs/plan.md](docs/plan.md) for the roadmap and
[docs/findings.md](docs/findings.md) for what is known so far.
