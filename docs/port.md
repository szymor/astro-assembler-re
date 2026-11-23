# The port: SDL3 platform layer

The game logic is the recompiled assembly core ([recompiler.md](recompiler.md)).
`src/` replaces the original C++ shell: it prepares the emulated memory the way
the shell did, drives the core with its commands, and implements the shell
callbacks on SDL3.

## Building and running

1. Package the assets: `python3 tools/package_assets.py img/Astro_Assembler.cue --flac flac -o assets`
2. Generate the core: see [recompiler.md](recompiler.md) (output in `build/recomp/gen`)
3. Build:

```bash
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

```bash
cmake --build build/cmake -j
```

4. Run: `build/cmake/astro assets`

Keyboard: arrows to move, Space = fire (second button and the remaining
bindings still to be mapped), Enter to choose in menus, Alt+Enter toggles
fullscreen.

## Source layout

| File | Role |
|---|---|
| `src/astro.h` | Emulated address layout, addresses of the original globals |
| `src/shell.c` | `WinMain` flow: memory setup, `Config_Validate`, mode table, menu/level loop (`Game_RunMenu`, `Game_RunLevels`) |
| `src/callbacks.c` | `host_shell_call()`: the ~20 shell callbacks (ILT slots) |
| `src/video.c` | SDL window, 640×480 menu surface, 320×200 game frame, 60 Hz pacing |
| `src/input.c` | SDL events, DirectInput-style key state (DIK codes) |
| `src/assets.c` | Loading the packaged assets into emulated memory |

## Emulated memory

| Address | Contents |
|---|---|
| `0x00300000` | top of the x86 stack used for core calls |
| `0x00401000` | `.text` image (`ASTRO_TEXT.BIN`) |
| `0x0046e000` | `.data` image (`ASTRO_DATA.BIN`) |
| `0x01000000` | the shell's allocation block: MENU.DAT, menu images, arena (frame buffers, GAME.DAT, LEVELn.DAT, level scratch buffers) |
| `0x03000000` | 640×480×32 "primary surface" for the menus |

## Display

- **Menus** (commands 9, 15, 22) draw into the primary surface, which the port
  presents on `DD_WaitVBlank`. The menu mode is 640×480×32 XRGB; the menu
  images are converted to that format at load time, as `Gfx_LoadBitmap` did.
- **Gameplay**: the port configures the game mode as 8 bpp with the mode flag
  that makes the core keep a converted palette (256 `PALETTEENTRY` at
  `0x55c3f0`). `Video_PresentFrame` reads the 320×200 frame and that palette
  and lets SDL scale it. This avoids the core's 16/24/32 bpp converters (which
  contain the instruction-changing self-modification).
- Scaling filter and aspect handling are still to be decided (plan.md).

## Timing

Frames are paced at 60 Hz (`video_wait_frame`), standing in for the vertical
blank waits. One core logic tick happens per presented frame, as in the
original. The exact original rate is still to be confirmed (plan.md).

## Debugging aids

| Variable | Effect |
|---|---|
| `ASTRO_DUMP=<dir>` | save every 60th frame as BMP (`frame*.bmp` menus, `game*.bmp` gameplay; gameplay also `.raw` = indices + palette) |
| `ASTRO_KEYS="frame:dik[:frames],…"` | hold DirectInput key `dik` (hex) from a given frame; e.g. `800:39` presses Space at frame 800 |
| `ASTRO_WATCH=<byte>` | debug builds with `-DRT_WATCH` only: histogram of writes of that byte into the frame buffer, or into a level-relative range with `ASTRO_WATCH_RANGE=<lo>:<hi>` |

A watch build: `cmake -S . -B build/cmake-watch -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=-DRT_WATCH`.

## Status

Working: logos, title, hall of fame, menu navigation, level 1 gameplay (tiles,
blended particles, sprites, 3D objects, explosions, scoring, lives).

Not yet implemented:

- sound effects (`Sound_Play`, `Sound_Stop`) and music (`CD_PlayTrack`, FLAC)
- configuration load/save, quitting cleanly
- joystick/gamepad, full key mapping (control schemes 8/12)
- ending (`Gfx_LoadEnding`, command 15), per-level sound sets
- options menu's display-mode list (records built approximately)
