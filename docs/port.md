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

Dependencies: SDL3 3.4 or newer (uses `SDL_LoadPNG`) and libFLAC (CMake config
`FLAC::FLAC`; on Windows e.g. vcpkg `sdl3` and `flac`).

Controls are the original game's own (redefinable in its options menu and
saved in the configuration). Defaults with no gamepad connected:

| | Left | Right | Up | Down | Fire | Fire 2 |
|---|---|---|---|---|---|---|
| Player 1 (scheme 8) | O | P | Q | A | Space | Enter |
| Player 2 (scheme 12) | ← | → | ↑ | ↓ | Right Shift | Keypad 0 |

With gamepads connected on the first run, the game assigns joysticks first
(one pad: player 1 = joystick 1, player 2 = keyboard). Gamepads behave like the
original DirectInput joysticks: stick or d-pad (digital beyond half
deflection, as the original's 50% dead zone), south button = fire, east button
= fire 2. Escape on the title quits; Alt+Enter toggles fullscreen.

## Source layout

| File | Role |
|---|---|
| `src/astro.h` | Emulated address layout, addresses of the original globals |
| `src/shell.c` | `WinMain` flow: memory setup, `Config_Validate`, mode table, menu/level loop (`Game_RunMenu`, `Game_RunLevels`) |
| `src/callbacks.c` | `host_shell_call()`: the ~20 shell callbacks (ILT slots) |
| `src/video.c` | SDL window, 640×480 menu surface, 320×200 game frame, 60 Hz pacing |
| `src/input.c` | SDL events, DirectInput-style key state (DIK codes), gamepads as DirectInput joysticks |
| `src/assets.c` | Loading the packaged assets into emulated memory |
| `src/audio.c` | Sound-effect mixer with DirectSound semantics, CD-audio emulation from FLAC |
| `src/config.c` | `ASTRO_CC.CFG` in the original binary format, kept in the user's preference directory |
| `src/settings.c` | The port's own settings, `astro-port.ini` (filter, fullscreen) |

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
- Both pictures fill the largest **4:3** area of the window, like the
  original's screen (the 320×200 frame gets the same slightly tall pixels).
- **Filter** (decided): `sharp` by default: integer nearest-neighbour upscale,
  then a linear stretch of the remaining fraction. This keeps pixels crisp
  without the uneven rows plain nearest gives at 200 → 480 lines (2.4×).
  `nearest` and `smooth` (bilinear) are available. The original's look
  depended on the graphics card (unfiltered DirectDraw stretch or filtered
  Direct3D), so there is no single reference.
- **Options → Display** lists one mode, `640X480X32 SDL3`, with the text and
  rectangle records laid out exactly as the original's `DD_Init` built them.

## Audio

- **Sound effects** are mixed by `src/audio.c` in one SDL audio stream
  (44.1 kHz stereo float), reproducing what the original's DirectSound code
  did:
  - each sound id has the number of voices from the sound table; a play uses
    the current voice if idle, otherwise a 1-voice sound is not restarted and
    a multi-voice sound cuts off the next voice round-robin;
  - looping only for 1-voice sounds;
  - pan and volume (hundredths of dB, computed with the original's integer
    formulas) apply only when flagged and persist per voice;
  - sound sets are loaded around the menu and each level, as in the original.
- **Music** emulates the MCI CD player: `CD_PlayTrack` plays
  `music/trackNN.flac` and continues through the following tracks up to 16,
  like a CD played without an end position. Pause remembers the position,
  resume seeks back to it.
- **Volumes**: the configured wave and CD levels (originally `auxSetVolume`,
  0..0xFFFF) are applied as linear gains.
- **Focus**: when the window loses focus the CD pauses and the game waits, as
  the original `WndProc`/`App_PumpMessages` did.
- **Limiter** (decided): the original mixed the CD music in the sound card's
  analog mixer, separately from the effects, so the two never clipped against
  each other. The port sums them digitally, so a look-ahead limiter sits on
  the final mix: below 0.9 of full scale it does nothing (music alone never
  gets there), above it the whole mix is turned down just enough, seen 1.45 ms
  ahead so the gain arrives in time, and released over ~200 ms. Measured on an
  explosion-heavy run: 3 673 clipped samples before, 0 after, quiet passages
  unchanged.

## Configuration and quitting

The port's own settings live in `astro-port.ini` next to the game's config:
`filter = sharp | nearest | smooth` and `fullscreen = 0 | 1` (Alt+Enter
toggles and is remembered).

- `ASTRO_CC.CFG` uses the original layout (`RHO!` magic, run counter, players,
  controls, settings/high-score blocks, display mode, volumes, difficulty).
  The original wrote it to the Windows directory; the port uses the SDL
  preference directory (Linux: `~/.local/share/Astro Assembler/`). A file from
  the original game can be copied there. A file with a different magic is left
  alone and not overwritten, as in the original.
- Quitting from the game's menu (Escape on the title) ends the main loop and
  saves the configuration, as in the original.
- Closing the window also saves and exits. The original ignored the close
  button, since it ran in exclusive fullscreen; a windowed port has to react.

## Timing

Frames are paced at 60 Hz (`video_wait_frame`), standing in for the vertical
blank waits; one core logic tick per presented frame, as in the original. The
original used the driver's default refresh rate, which for a 640×480 DirectX 7
game on Windows 98/2000 was almost always 60 Hz (decided).

## Debugging aids

| Variable | Effect |
|---|---|
| `ASTRO_DUMP=<dir>` | save every 60th frame as BMP (`frame*.bmp` menus, `game*.bmp` gameplay; gameplay also `.raw` = indices + palette) |
| `ASTRO_DUMP_OUTPUT=<dir>` | save the scaled window image every 300th gameplay frame |
| `ASTRO_NO_FOCUS_PAUSE=1` | do not pause when the window loses focus (automated runs) |
| `SDL_AUDIO_DRIVER=disk` | SDL writes the mixed audio to `sdlaudio.raw` (F32 stereo 44.1 kHz) instead of playing it |
| `ASTRO_LEVEL=<1-6>` | start the game at that level (state from earlier levels is missing) |
| `ASTRO_ENDING=1` | show the ending right after the logos |
| `ASTRO_VPAD="frame:control[:frames],…"` | attach a virtual gamepad and press `a`, `b`, `left`, `right`, `up`, `down` at given frames |
| `ASTRO_KEYS="frame:dik[:frames],…"` | hold DirectInput key `dik` (hex) from a given frame; e.g. `800:39` presses Space at frame 800 |
| `ASTRO_WATCH=<byte>` | debug builds with `-DRT_WATCH` only: histogram of writes of that byte into the frame buffer, or into a level-relative range with `ASTRO_WATCH_RANGE=<lo>:<hi>` |

A watch build: `cmake -S . -B build/cmake-watch -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=-DRT_WATCH`.

## Status

Working: logos, title, hall of fame, menu navigation, level 1 gameplay (tiles,
blended particles, sprites, 3D objects, explosions, scoring, lives), sound
effects, music (title: track 3, levels: tracks 4/6/…/14, ending: 16),
configuration load/save, quitting, the ending sequence, keyboard and gamepad
controls (gamepad tested with a virtual SDL pad), all options submenus
(controls, display, sound, difficulty). Levels 2 and 6 checked
via `ASTRO_LEVEL`.

Not yet implemented:

