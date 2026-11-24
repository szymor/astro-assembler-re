# The port: frontends and common code

The game logic is the recompiled assembly core ([recompiler.md](recompiler.md)).
The common code in `src/` replaces the original C++ shell: it prepares the
emulated memory the way the shell did, drives the core with its commands, and
implements the shell callbacks. Everything platform-specific sits behind a
small interface (`src/platform.h`) implemented by the **frontends**:

| Executable | Frontend | Notes |
|---|---|---|
| `astro` | SDL3 (`src/frontend/sdl3.c`) | the main one; GPU scaling, gamepads, Linux and Windows |
| `astro-sdl2` | SDL2 (`src/frontend/sdl2.c`) | SDL 2.0.12+; GPU scaling, game controllers |
| `astro-sdl1` | SDL 1.2 (`src/frontend/sdl1.c`) | also on sdl12-compat; software scaling (`src/scale.c`), raw joysticks |
| `astro-headless` | none (`src/frontend/headless.c`) | no window/sound/pacing; prints frame and audio hashes for tests |

## Building and running

1. Package the assets: `python3 tools/package_assets.py img/Astro_Assembler.cue --flac flac -o assets`
2. Generate the core: see [recompiler.md](recompiler.md) (output in `build/recomp/gen`)
3. Build (Linux; SDL2/SDL1 frontends are built when found):

```bash
cmake --preset linux
```

```bash
cmake --build --preset linux -j
```

4. Run: `build/cmake/astro assets` (or `astro-sdl2`, `astro-sdl1`)

Dependencies: zlib for every frontend, plus SDL3 (3.4+), SDL2 (2.0.12+) or
SDL 1.2 for the respective frontend. FLAC decoding uses the vendored
[dr_flac](../third_party/dr_flac/README.md) (public domain / MIT-0).

### Windows

- **MinGW-w64 cross-build from Linux** (tested; the result runs under Wine):
  `cmake --preset mingw` then `cmake --build --preset mingw -j`. Needs the
  MinGW toolchain with Windows builds of SDL3 and zlib (e.g. distribution
  `mingw-w64-sdl3` and `mingw-w64-zlib` packages; `cmake/mingw-w64-x86_64.cmake`
  takes `MINGW_ROOT`). Output in `build/mingw`: `astro.exe`,
  `astro-headless.exe`, `SDL3.dll`, and `libssp-0.dll` when the toolchain's
  SDL3 needs it. zlib and libgcc are linked statically.
- **Visual Studio + vcpkg** (prepared, not tested here): with `VCPKG_ROOT`
  set, `cmake --preset msvc` then `cmake --build --preset msvc`; dependencies
  come from `vcpkg.json`. The generated core compiles as plain C11 (no
  compiler builtins), but its very large functions may take a while with MSVC.
- Windows builds contain the SDL3 and headless frontends.
- The Windows headless build produces exactly the same frame and audio hashes
  as the Linux build for the same input script.

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
| `src/platform.h` | The frontend interface (present, pacing, events, keys, joysticks, audio output) |
| `src/astro.h` | Common declarations, emulated address layout, addresses of the original globals |
| `src/game.c` | `astro_main()`, called by every frontend's `main()` |
| `src/shell.c` | `WinMain` flow: memory setup, `Config_Validate`, mode table, menu/level loop (`Game_RunMenu`, `Game_RunLevels`) |
| `src/callbacks.c` | `host_shell_call()`: the ~20 shell callbacks (ILT slots) |
| `src/video.c` | Palette conversion of the game frame, frame counter, `ASTRO_DUMP` |
| `src/input.c` | DirectInput key state and joystick histories, USB-scancode → DIK table |
| `src/audio.c` | Sound-effect mixer with DirectSound semantics, CD-audio emulation from FLAC (dr_flac), limiter, WAV loading |
| `src/assets.c`, `src/png.c` | Loading the packaged assets; PNG reader (zlib) |
| `src/config.c` | `ASTRO_CC.CFG` in the original binary format |
| `src/settings.c` | The port's own settings, `astro-port.ini` (filter, fullscreen, resolution) |
| `src/util.c` | Logging, UTF-8 file access (Windows-safe), per-user folder, BMP writer, hash |
| `src/scale.c` | 4:3 rectangle and the software scaler (sharp/nearest/smooth) |
| `src/frontend/*.c` | SDL3, SDL2, SDL1 and headless frontends |

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
  `0x55c3f0`). `Video_PresentFrame` converts the 320×200 frame with that
  palette and the frontend scales it. This avoids the core's 16/24/32 bpp converters (which
  contain the instruction-changing self-modification).
- Both pictures fill the largest **4:3** area of the window, like the
  original's screen (the 320×200 frame gets the same slightly tall pixels).
- **Filter** (decided): `sharp` by default: integer nearest-neighbour upscale,
  then a linear stretch of the remaining fraction. This keeps pixels crisp
  without the uneven rows plain nearest gives at 200 → 480 lines (2.4×).
  `nearest` and `smooth` (bilinear) are available. SDL3/SDL2 do this on the
  GPU (render target), SDL1 with the software scaler; the results match. The original's look
  depended on the graphics card (unfiltered DirectDraw stretch or filtered
  Direct3D), so there is no single reference.
- **Options → Display** lists one mode, `640X480X32 <FRONTEND>` (SDL3, SDL2,
  SDL1), with the text and rectangle records laid out exactly as the
  original's `DD_Init` built them. With a reduced output resolution the label
  shows it (`320X240X32 SDL3`).

### Output resolution

`resolution = 640x480 | 320x240 | 320x200` in `astro-port.ini` sets the size of
the picture handed to the frontend (default 640x480, the behaviour above).
This is a port addition: the original's mode list started at 640×400, although
its presenter had 320×200 code paths.

| | menus, title, ending (drawn by the core at 640×480) | gameplay (320×200 frame) |
|---|---|---|
| 640x480 | as drawn | as is, scaled by the frontend |
| 320x240 | averaged 2×2 → 320×240 | stretched to 240 lines (`sharp`: area resampling, rows blended only where an output row straddles two; `nearest`/`smooth` as named) |
| 320x240, `letterbox = 1` | averaged 2×2 → 320×240 | 1:1 with 20-line black bars (pixel-exact; flatter than the original on a 4:3 screen, whose pixels were 1.2× tall) |
| 320x200 | area-averaged → 320×200 (2× across, 2.4× down) | 1:1 |

- The reduction happens in common code (`video.c`, `scale_area` in
  `scale.c`), so all frontends behave the same; they still fill the largest
  4:3 area of the window with the result. Averaging (not point sampling)
  keeps the thin strokes of the menu font; the text stays legible at both
  sizes.
- `letterbox` only affects 320x240; `ASTRO_LETTERBOX=0|1` overrides it
  without saving.
- Higher resolutions are not needed as settings: the frontend scales the
  picture to any window or fullscreen size (GPU in SDL3/SDL2, software scaler
  in SDL1), which covers what the original's 800×600 and 1024×768 modes did.
- Reduced resolutions open a 960×720 window (3× 320×240), or 640×480 on
  small desktops.
- `exclusive = 1`: fullscreen switches the display to a mode of exactly that
  size when the display offers one (CRTs, old hardware), and the picture
  fills it (the display shows it as 4:3). Otherwise fullscreen uses the
  desktop mode as usual. Only the fallback has been tested (modern displays
  do not offer 320×240).

## Audio

- **Sound effects** are mixed by `src/audio.c` (44.1 kHz stereo float; the
  frontend's audio callback pulls the mix), reproducing what the original's
  DirectSound code did. The 8/16-bit 11/22 kHz WAVs are converted once to the
  mix rate with linear interpolation, close to DirectSound's software mixer:
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
`filter = sharp | nearest | smooth`, `fullscreen = 0 | 1` (Alt+Enter
toggles and is remembered), `resolution = 640x480 | 320x240 | 320x200`,
`letterbox = 0 | 1` and `exclusive = 0 | 1` (see "Output resolution").

- `ASTRO_CC.CFG` uses the original layout (`RHO!` magic, run counter, players,
  controls, settings/high-score blocks, display mode, volumes, difficulty).
  The original wrote it to the Windows directory; the port uses a per-user
  folder shared by all frontends (Linux `~/.local/share/Astro Assembler/`,
  Windows `%APPDATA%\Astro Assembler\`; `ASTRO_CONFIG_DIR` overrides it). A file from
  the original game can be copied there. A file with a different magic is left
  alone and not overwritten, as in the original.
- Quitting from the game's menu (Escape on the title) ends the main loop and
  saves the configuration, as in the original.
- Closing the window also saves and exits. The original ignored the close
  button, since it ran in exclusive fullscreen; a windowed port has to react.

## Timing

Frames are paced at 60 Hz (`plat_wait_frame`), standing in for the vertical
blank waits; one core logic tick per presented frame, as in the original. The
original used the driver's default refresh rate, which for a 640×480 DirectX 7
game on Windows 98/2000 was almost always 60 Hz (decided).

## Debugging aids

| Variable | Effect |
|---|---|
| `ASTRO_DUMP=<dir>` | save every 60th frame as BMP (`frame*.bmp` menus, `game*.bmp` gameplay; gameplay also `.raw` = indices + palette; with a reduced resolution also `*-out.bmp`, the reduced picture) |
| `ASTRO_RESOLUTION=<WxH>` | override `resolution` (also for the headless frontend); not saved |
| `ASTRO_LETTERBOX=0\|1` | override `letterbox` likewise |
| `ASTRO_DUMP_OUTPUT=<dir>` | SDL frontends: save the scaled window image every 300th gameplay frame |
| `ASTRO_FRAMES=<n>` | headless: stop after n frames and print the hashes (default 3600) |
| `ASTRO_HASH_EVERY=<n>` | headless: also print the running hashes every n frames |
| `ASTRO_CONFIG_DIR=<dir>` | use this folder instead of the per-user one |
| `ASTRO_NO_FOCUS_PAUSE=1` | do not pause when the window loses focus (automated runs) |
| `SDL_AUDIO_DRIVER=disk` | SDL writes the mixed audio to `sdlaudio.raw` (F32 stereo 44.1 kHz) instead of playing it |
| `ASTRO_LEVEL=<1-6>` | start the game at that level (state from earlier levels is missing) |
| `ASTRO_ENDING=1` | show the ending right after the logos |
| `ASTRO_VPAD="frame:control[:frames],…"` | SDL3: attach a virtual gamepad and press `a`, `b`, `left`, `right`, `up`, `down` at given frames |
| `ASTRO_KEYS="frame:dik[:frames],…"` | hold DirectInput key `dik` (hex) from a given frame; e.g. `800:39` presses Space at frame 800 |
| `ASTRO_WATCH=<byte>` | debug builds with `-DRT_WATCH` only: histogram of writes of that byte into the frame buffer, or into a level-relative range with `ASTRO_WATCH_RANGE=<lo>:<hi>` |

A watch build: `cmake -S . -B build/cmake-watch -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=-DRT_WATCH`.

### Regression testing with the headless frontend

The headless frontend runs about 17× faster than real time and is fully
deterministic, so the same input script always gives the same hashes:

```bash
ASTRO_FRAMES=3000 ASTRO_KEYS="800:39,1100:1c,1700:39:600,1700:19:120" build/cmake/astro-headless assets
```

Reference for that script (menus, then level 1):
`frames=3000 video=e04b81cb0fd8b668 audio=40fe3f64feb8a717`, identical on
Linux and on the Windows build. With `ASTRO_RESOLUTION=320x240`:
`video=48fc6fa2b3e21f17`, with `320x240` and `ASTRO_LETTERBOX=1`: `video=98d15455507f8503`, with
`320x200`: `video=13b0762ae150acb5` (audio
unchanged). A changed hash after a code change means the
game's output changed; `ASTRO_HASH_EVERY` narrows down where.

## Status

Working: logos, title, hall of fame, menu navigation, level 1 gameplay (tiles,
blended particles, sprites, 3D objects, explosions, scoring, lives), sound
effects, music (title: track 3, levels: tracks 4/6/…/14, ending: 16),
configuration load/save, quitting, the ending sequence, keyboard and gamepad
controls (gamepad tested with a virtual SDL pad), all options submenus
(controls, display, sound, difficulty). Levels 2 and 6 checked
via `ASTRO_LEVEL`. All four frontends run into level 1 with music; the
Windows build (MinGW) runs under Wine.

Not yet implemented:

- MSVC build: prepared (`msvc` preset, `vcpkg.json`) but not tested
- Windows builds of the SDL2/SDL1 frontends (not planned for now)
- the long-term part of the hybrid plan: replacing generated core functions
  with readable C, each checked against the headless hashes
