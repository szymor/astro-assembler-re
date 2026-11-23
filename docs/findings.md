# Findings

## Distribution

- CD image: one MODE1/2352 data track (ISO 9660 + Joliet, burned with Nero).
  **No audio tracks.**
- Wise/MSI installer in `install/` (setup per language: eng, d, esp, fr, ita, nl).
  ProductCode `{E07888C3-282E-11D5-8ED5-0050BF5CB907}`, version 1.0.0.
  It is a v1 GUID, and its timestamp decodes to 2001-04-03. The game's own
  credits say "Astro Assembler is an Advanced Projects game in the year 1999":
  the game dates from 1999, this CD/installer from 2001.
- Game files are in `install/astro1.cab` (MS Cabinet, MSZip, 21 files).
- Extras on disc: DirectX 8 installers, Acrobat Reader 4.05.

## Game files

| File | Format |
|---|---|
| `ASTRO.exe` | PE32 i386 GUI, MSVC 5 (linker 5.0), debug CRT, incremental linking |
| `Portada/Logo1-3/Fondo1-2/Final1-2/Letras/Naves.bmp` | 640×480 24-bit BMP |
| `Game.dat`, `Level1-6.dat` | 8-bit palette-indexed RLE sprites (see below) |
| `Menu.dat` | Unknown, 970 200 bytes, starts with zero runs |
| `Manual.pdf` | 8 pages, German text with English headings |
| `icon1.ico` | 32×32 icon |

The original project folder was
`C:\Archivos de programa\DevStudio\MyProjects\Astro-Crystal(comercial)\`
(Spanish developer; working title "Astro-Crystal").

## ASTRO.exe

- Imports: KERNEL32, USER32, GDI32, ole32, DDRAW, DINPUT, DSOUND, WINMM.
- Needs DirectX 7 (`"This application requires DirectX 7 or later."`),
  uses `IDirectDraw2`.
- Sections: `.text` 0x69558, `.data` 0xE8600 (large, probably level tables),
  `.rsrc` 0x3BEFE1 (mostly WAVs).
- Entry `0x00458f30` (CRT start-up) → `WinMain` at `0x004023e0`.
- A 30 KB C++ shell plus a **250 KB hand-written assembly core** that holds
  all game logic and rendering. Full structure in [modules.md](modules.md).
- Strings worth noting: `Cdplayer.cpp`, `aw_env.c`/`aw_map.c`/`aw_str.c`
  (CRT locale), `ONE PLAYER`, `PLAYER 1/2`, `PLAYER X CONTROL`, all asset
  file names in upper case.

## Sound

- 78 `WAV` resources, PCM mono, 8/16-bit, 11 025/22 050 Hz. Names are
  Spanish (`AIREH`, `AMBIENTENAVE9`, `BICHOCHI01`…).
- Music was meant to be **CD audio**, played through `mciSendCommandA`
  (`Cdplayer.cpp`). The released disc has no audio tracks, so the game
  never played any music. The 15 FLAC tracks in `flac/` (from the
  khinsider rip) fill that gap. The code uses tracks 2–16, which matches
  15 FLACs as FLAC NN = CD track NN+1 (see modules.md, "Sound and music").

## `.dat` sprite encoding (first look)

Each line is a sequence of runs:

```
u16 skip      ; transparent pixels to skip
u16 count     ; opaque pixels that follow
u8  pixels[count]
...
FE FF         ; end of line
```

Still unknown: how frames are split, frame sizes and hotspots, and where
the palette is (probably the exe or a BMP).

## Gameplay (from the manual)

Vertical shooter. The A-1 ship has lasers (L) and missiles (M) plus a speed
stat (S), upgraded from blue capsules shot with missiles. Pickups:
F energy, P points, C keep stats after death (yellow speed, blue laser,
red missiles), U extra energy capacity, X extra ship. Black or upside-down
letters are negative. Nine extra ships, each built from three parts found
in capsules, can be combined with the main ship using energy. They have
time- and hit-limited shields ("Armor" only loses energy when hit).
Second fire button opens, cycles and assembles ships; a double tap assembles
or disassembles. 6 levels, 1 or 2 players.
