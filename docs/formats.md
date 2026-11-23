# Data formats

Status: Phase 2, first pass. The sprite encoding, palettes and memory layout
are confirmed by rendering. Descriptor tables, MENU.DAT and the non-sprite
regions of the `.DAT` files are still open.

## Memory layout at runtime

All files are read raw (no parsing) into one `VirtualAlloc` block:

```
g_allocBase
  +0              MENU.DAT (970 200 bytes)
  then            PORTADA, FONDO1, FONDO2, NAVES, LETRAS .BMP, each converted by
                  LoadImage+StretchBlt to the 640x480 menu mode's pixel format
                  (640*480*bytesPerPixel each)
  then            one more 640x480 slot (g_imgScreenBackup)
g_arenaBase
  +0x000000       g_frameBuffer: 320x200 8-bit frame
  +0x00FA00       second 320x200 buffer
  +0x01FF40       GAME.DAT  (2 960 905 bytes, ends at +0x2F2D49)
  +0x2F2D49       LEVELn.DAT (reloaded at the same place for every level)
  g_levelDataEnd  level scratch buffers (tile layers, masks), placed by the level
                  setup right after the level data
```

The core addresses this data with **hard-coded arena offsets** (constants added
to `g_arenaBase`) and with offsets stored in tables in its own `.data`. For
example, `Core_Level1Setup` stores `0x4cb107` (= LEVEL1.DAT + 0x1D83BE, the start
of a sprite bank). Command 7 (`Core_RelocateGameData`) adds the arena address to
two offset tables inside GAME.DAT once, after loading.

**Consequence:** the `.DAT` files are not self-describing archives. Their layout
is part of the program. Moving anything inside them means rewriting every
reference in code and tables.

## RLE sprites

No header. Width and height are not stored; callers pass them to
`Core_DrawSprite` (`0x442454`) from descriptor tables, entity fields or
constants.

```
sprite := line ( 0xFFFE line )* 0xFFFF
line   := run*
run    := u16 skip      ; transparent pixels, >= 0 (sign bit clear)
          u16 count     ; opaque pixels
          u8  pixel[count]
```

- The last line ends directly with `0xFFFF` (no `0xFFFE` before it).
- Empty lines are a bare `0xFFFE`.
- Pixel values are palette indices; transparency only comes from skips.
- Some sprites are preceded by a 1-bit mask, one big-endian `u32` per line
  (e.g. GAME.DAT `0x3fd`, a 5x5 circle). Probably collision masks; which sprites
  have them is not known yet.
- The blitter draws into the 320x200 frame buffer, with a fast path when the
  sprite is fully on screen and a clipping path otherwise.

`tools/datview.py segment|sheet` maps and renders sprite runs.

### Sprite regions found by segmentation

GAME.DAT: ~2 300 sprites in `0x0–0x1d4f33` (fonts, HUD, explosions, shockwaves,
shots…), then tables (below), then 16 more sprites at `0x2c38a6`.

LEVEL1.DAT (the other levels have the same kind of layout):

| Range | Contents |
|---|---|
| `0x000000–0x05915e` | 390 sprites: enemy ships with rotation/animation frames |
| `0x05915e–0x1cf02e` | 1.5 MB of **background tiles**, 20×20 pixels, 400 bytes each (tile set base `[0x4b1fc8]`, drawn by `0x41ef8b`) |
| `0x1cf02e–0x1d22f2` | 38 sprites |
| `0x1d83be–0x266b9f` | 454 sprites; sprite-bank table entries point here |
| `0x266b9f–0x35dbad` | 1 MB, including four **64 KB blend tables** (`table[new][old]`, used for translucent particles; addresses patched into `0x413a8x…` by `0x409658`) |
| `0x35dbad–0x4ef507` | 120 large sprites (asteroids, ~13.7 KB each) |

## GAME.DAT tables

| Offset in file | Contents |
|---|---|
| `0x1d5034` | Sin/cos table: 720 pairs of `s16` (sin, cos), 14-bit fixed point (16384 = 1.0), half-degree steps |
| `0x1e77e4`, `0x1f8ae4`, `0x209de4` | Three blocks of 0x11300 bytes of small `s16` pairs (particle/debris offsets?) |
| `0x2967c1` + `0x2a3625` | 8-byte record array + directory of 4875 entries `{u32 offset into the records, u32 flag}`; offsets relocated by command 7. Probably animation sequences |
| `0x2ace79` + `0x2b9e55` | Second array + directory, 4953 entries, same structure |

## Palettes

- Stored in **ASTRO.exe** `.data`, 768 bytes (256 x RGB), **6-bit components**
  (0..63; the game shifts them left by 2).
- Level palettes: `0x4c0298` (L1), `0x4c0b98` (L2), `0x4c1198` (L3),
  `0x4c1498` (L4), `0x4c1798` (L5), `0x4c1a98` (L6). There are more slots between
  L1 and L2 (used by script fades).
- Only the first **240 colours** take part in fades. `Palette_StartFade` keeps
  them as 8.8 fixed point with a per-frame delta; colours 240–255 stay fixed.
- For 16/24/32 bpp output the core builds lookup tables with self-modifying code
  (masks/shifts patched in), which the port does not need.

## Level scripts

- Also in ASTRO.exe `.data`. `LevelScriptTable` (`0x4c1da4`) holds a pointer per
  level to 12 script pointers: 4 sections x 3 difficulties (`g_cfgDifficulty`).
  A level's section 0 is the previous level's section 3.
- Interpreted by `Script_Run` (`0x41f109`). Words with high byte `0xEF..0xFD` are
  opcodes; any other word goes to a default handler (wait/spawn flags,
  variable length; not decoded yet).

| Opcode word | Operand bytes | Meaning |
|---|---|---|
| `EFxx` | 0 | set `0x47004a` = 0x1f3 |
| `F0xx` | 8 | call `0x44703d(eax, ebx)` |
| `F1xx` | 20 | call native code pointer at +16 with eax/ecx/edx/ebx |
| `F2xx` | 0 | stop CD audio |
| `F3xx` | 2 | play CD track |
| `F4xx` | 4 | set two words (`0x470ff4/6`) |
| `F500..F502` | 20 | spawn/setup (arena-relative pointers) |
| `F6xx` | 4, or 8 if low byte != 0 | set pointers `0x4ba90c/10` |
| `F700..F702` | 0 | flags |
| `F8nn` | 4 | random branch: jump to the u32 pointer unless `Core_Random() % nn == 0` |
| `F9xx` | 6 | palette fade: u16 frames, u32 target palette |
| `FAxx` | 0 | flag `0x47004a` = 1 |
| `FBxx` | 24 | … |
| `FCxx` | 72 | … |
| `FDxx` | 2 | … |
| `FE00..FE02` | 6 | … |
| `FF00..FF02` | 2 | … |

## Music: CD tracks → FLAC

Confirmed by the code (commands) and the level scripts (`F3`):

| CD track | FLAC | Use |
|---|---|---|
| 2 | 01 (0:41) | menu, special state |
| 3 | 02 (5:05) | title / menu |
| 4, 6, 8, 10, 12, 14 | 03, 05, 07, 09, 11, 13 | levels 1–6 |
| 5, 7, 9, 11, 13 | 04, 06, 08, 10, 12 | bosses of levels 1–5 (near the end of each level script) |
| 15 | 14 (9:05) | level 6 final battle |
| 16 | 15 (0:48) | ending |

`CD_PlayTrack` plays from the track **to the end of the disc**, so on a real CD
a level track that ran out before the boss would continue straight into the
boss track.

## Sound effects

- 78 `WAV` resources (PCM mono, 8/16-bit, 11 025/22 050 Hz). The sound table
  `0x46f1d0` (`{char *name; int voices}` x 78) gives each a global **sound id
  0–77** (the value the core passes in `g_shellParam` to `Sound_Play`) and a
  number of DirectSound buffers ("voices") that can play at the same time.
- Sets (`-1`-terminated id lists) are loaded per game phase by `Sound_LoadSet`:
  menu `0x46ec18` (ids 0–2), levels `0x46ec28`, `0x46ed10`, `0x46ee10`,
  `0x46ef08`, `0x46f000`, `0x46f0e8`. Every level set contains ids 3–56
  (shots, impacts, explosions, ship assembly, HUD) plus its own enemy, door and
  ambience sounds (57–77).
- The packaged `sounds.json` lists id, name, voices and sets.

## RNG

`Core_Random` (`0x408f8c`): `seed = ror16(seed * 13, 2) + w1 + … + w8`, where the
`w` are live game-state words (`0x470164`, `0x470166`, `0x4702bc`, `0x4702be`,
`0x46ffa0`, `0x470050`, `0x4701a8`). Deterministic, but it depends on game state,
so the port has to keep those variables bit-exact.

## Still open

- Sprite descriptor tables (sizes, hotspots) and the full sprite catalogue
- Collision masks: which sprites have them
- Rest of the non-sprite level data (tile maps, scripts' data)
- MENU.DAT (66 distinct byte values, mostly 0 and 255, not RLE, not a plain image)
- Default script handler and the remaining opcodes

## Packaged assets (`tools/package_assets.py`)

```
assets/
  data/GAME.DAT, LEVEL1-6.DAT, MENU.DAT   byte-identical
  data/ASTRO_TEXT.BIN   ASTRO.exe .text image (VA 0x401000): tables embedded in
                        the core's code and the fields it patches at runtime
  data/ASTRO_DATA.BIN   ASTRO.exe .data image (VA 0x46e000, file size 0xe8600,
                        virtual size 0xf1880; the rest is zero at runtime).
                        Holds palettes, level scripts, tables, sound table.
                        Pointers in it are absolute VAs (image base 0x400000).
  images/*.png          the ten 640x480 BMPs, lossless
  sfx/NN_Name.wav       by sound id
  music/trackNN.flac    CD tracks 02..16 (from flac/NN.flac, NN = track - 1)
  sounds.json, manifest.json (source SHA-256s, file list), Manual.pdf
```

The script refuses to run (without `--force`) if `ASTRO.exe` or a `.DAT` file
does not match the known release, because the port depends on exact offsets.
