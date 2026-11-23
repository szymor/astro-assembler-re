# ASTRO.exe module map

Canonical names live in [`re/symbols.tsv`](../re/symbols.tsv) and are applied
to the Ghidra database with `re/ghidra_scripts/ApplySymbols.java`. The Ghidra
project itself is a local, rebuildable cache (see "Rebuilding the database").

## Layout of `.text`

| Range | Size | Contents |
|---|---|---|
| `401000–401244` | 0.6 KB | Incremental-link table (ILT): 116 `jmp rel32` slots, one per shell function |
| `401490–408c44` | 30 KB | **C++ shell**, MSVC 5 debug build: Win32, DirectX, CD audio, files, config |
| `408c44–447da7` | 250 KB | **Hand-written assembly core**: all game logic and rendering |
| `447da7–457a00` | 63 KB | `0xCC` padding |
| `457a00–46a558` | 76 KB | MSVC C runtime (debug) |

The core's data is `0x46ff3c–0x52d1b0` (in `.data`). `WinMain` makes the core's
**code** range (`0x408c44`, 0x3f163 bytes) read/write/execute with
`VirtualProtect`, which suggests the core writes to its own code. Not checked yet.

The core never calls Windows or DirectX directly. Everything platform-specific
goes through the shell.

## Shell ↔ core interface

```
shell → core:   g_coreCommand = N;  Core_Dispatch()      // 0x556ae4, 0x408c44
                Core_Dispatch: pushfw; pushad; call CoreCommandTable[N]; popad; popfw

core → shell:   g_coreCallback = <ILT slot>;  call Core_CallShell   // 0x46ffb0, 0x408cb5
                Core_CallShell: pushfw; pushad; call [g_coreCallback]; popad; popfw

parameters:     g_shellParam (0x556aec), g_cdTrack (0x556b2c, word),
                g_soundFlags/PanX/Volume (0x556af0..af8), g_surfacePtr/Pitch, …
```

### Core commands (`CoreCommandTable` @ `0x408c59`)

| # | Handler | Issued by | Meaning |
|---|---|---|---|
| 0 | `408cc2` | WinMain | Report core code/data ranges |
| 1–6 | `408d06`… | `Game_RunLevels` | Run level N (setup → `Core_RunLevel`) |
| 7 | `408ceb` | WinMain | Core init after `GAME.DAT` is loaded |
| 8 | — | — | null entry |
| 9 | `4443ae` | `Game_RunMenu` | Title / menu (incl. options such as display mode) |
| 10 / 19 / 16 | `40b8d5` / `40e24d` / `41103c` | `Video_PresentFrame` | Convert 320×200×8 frame to a 16 / 24 / 32 bpp surface |
| 11,12 / 20,21 / 17,18 | … | `Video_PresentFrame` | Same, into Direct3D texture A / B |
| 13 | `408fd2` | `Video_SetGameMode` | Init after entering a 16 bpp game mode |
| 14 | `413797` | `Video_PresentFrame` | 8 bpp modes: upload palette before the 1:1 copy |
| 15 | `446195` | `Game_RunLevels` | Ending |
| 22 | `44607a` | `Game_SetMenuModeAndInit` | Init after setting the 640×480 menu mode |

### Shell callbacks used by the core

`Video_PresentFrame`, `Surface_LockPrimary`/`Unlock`, `Palette_Set`,
`DD_WaitVBlank`, `Input_PollKeyboard`, `Input_PollJoystick`, `Sound_Play`,
`Sound_Stop`, `Sound_Prime*Set`, `CD_PlayTrack`, `CD_Stop`, `CD_Pause`,
`CD_Resume`, `CDVol_Set`, `CDVol_Apply`, `App_PumpMessages`.

This list is effectively the **platform API the port has to provide**.

## Program flow

```
WinMain
 ├─ checks: Windows version, RAM ≥ 12 MB, DirectX ≥ 7
 ├─ Mem_AllocArena, Core cmd 0, VirtualProtect(core code, RWX)
 ├─ Config_Load (%WINDIR%\ASTRO_CC.CFG)
 ├─ App_CreateWindow, DD_Init, DI_Init, DS_Init, CD init
 ├─ Gfx_LoadTitleAssets (MENU.DAT, PORTADA/FONDO1-2/NAVES/LETRAS.BMP)
 ├─ File_LoadList(GAME.DAT), Gfx_LoadLogos (LOGO1-3.BMP)
 ├─ Config_Validate, Core cmd 7
 ├─ Game_SetMenuModeAndInit   (640×480, cmd 22)
 └─ Game_RunMenu (cmd 9)
    while !g_quitGame:
        Game_RunLevels:
            Video_SetGameMode           (configured mode; picks the upscale path)
            for N in 1..6: load LEVELN.DAT + sound set N, cmd N
            if completed: Gfx_LoadEnding, cmd 15
        Video_SetMenuMode
        Game_RunMenu (cmd 9)
 Config_Save, App_Shutdown
```

## Assembly core: first-level structure

From direct calls only (`re/scripts/core_callgraph.py`):

| Entry | Reachable functions | Notes |
|---|---|---|
| Level N (cmd 1–6) | ~615 funcs, ~173 KB | `Core_PrefaultMemory` → `Core_LevelNSetup` (0.7–1.8 KB each) → **`Core_RunLevel` (`40ae24`)**, shared by all levels |
| Menu (cmd 9) | 63 funcs, 10 KB | Draws straight into the 640×480 primary surface |
| Ending (cmd 15) | 20 funcs, 3.4 KB | 640×480, plays CD track 16 |
| Post-mode init (cmd 22) | 18 funcs, 2.3 KB | 640×480 |
| Blitters (cmds 10–21) | 1 func each, ~5 KB | Unrolled pixel-format conversion |

705 of 1241 core functions are reachable through direct calls. The rest are
reached through the ~450 code pointers stored in `.data`, most likely entity
behaviour and script-opcode handlers. That is Phase 3 territory.

## Display

- **Menus, title, ending**: real 640×480. The core writes straight into the
  locked primary surface.
- **Gameplay**: the core renders a **320×200 8-bit frame** (`g_frameBuffer`).
  `Video_PresentFrame` then shows it according to the configured mode
  (`g_cfgModeIndex`, chosen from the enumerated mode list, also changeable in
  the in-game options):
  - 320×200×8 (incl. ModeX): 1:1 copy, palette through `Palette_Set`
  - 320×200×16/24/32: core blitter converts the frame
  - larger modes with hardware stretch: convert to a back buffer, stretch `Blt`
    (centred when the size is not a multiple of 320×200)
  - otherwise: Direct3D, frame uploaded as two textures and drawn as two
    pre-transformed quads scaled to the screen
- Default mode (when the config is missing or invalid): 640×480×16.

## Timing

`SetDisplayMode` is called with refresh rate 0 (driver default).
`Video_MeasureRefresh` times 64 vertical blanks and `Video_WaitFrame` waits for
the next one. The core reads no timer, so **one logic tick = one presented
frame, and game speed follows the refresh rate of the chosen mode**. The port
needs a fixed tick rate. Which one the game was tuned for is still open.

## Sound and music

- Sound effects: 78 `WAV` resources, loaded per set (menu, level 1–6) into
  DirectSound buffers. `Sound_Play` supports loop, pan (x in 0..640) and volume.
- Music: CD audio through MCI. `CD_PlayTrack` plays **from** track
  `g_cdTrack` **to the end of the disc** (no `MCI_TO`).
  Track numbers in code: menu 3 (and 2), levels 4/6/8/10/12/14, ending 16, plus
  a level-script opcode `0xF3 <track>` (`41f639`) for the others.
- The disc has no audio tracks. The 15 FLACs fit CD tracks 2–16 (FLAC NN =
  track NN+1). Durations match: levels 4:39–6:43, odd tracks ~3:36, track 2 is
  0:41 and track 16 is 0:48.

## Input

DirectInput keyboard (256-byte state) and one joystick per player (axes
−1000..1000 with dead zone, buttons 0 and 1). Each direction and button is kept
as a 2-bit history (previous, current) for edge detection.

## Rebuilding the database

1. `python3 tools/extract_assets.py img/Astro_Assembler.cue`
2. Ghidra: import `extracted/game/ASTRO.exe` (PE loader, no language override),
   then run auto-analysis
3. `python3 re/scripts/find_seeds.py extracted/game/ASTRO.exe seeds.txt`
4. Run `SeedFunctions.java seeds.txt`, then auto-analysis again
5. Run `ApplySymbols.java re/symbols.tsv`

Copy the scripts from `re/ghidra_scripts/` into your Ghidra script directory.
