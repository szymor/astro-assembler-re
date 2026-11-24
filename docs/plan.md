# Roadmap

Ground rules:

- **Faithful.** Reproduce the original's timing, RNG and quirks. Any
  divergence (bug fix, improvement) is proposed and agreed on before it is
  implemented.
- **No original data in git.** A legitimate owner runs the asset pipeline,
  which outputs a standalone game in modern formats.
- **Platforms:** Linux and Windows. **Tooling:** Ghidra, Python, CMake, C + SDL3.
- **Display:** strict 640×480 first, with Alt+Enter fullscreen through SDL3
  scaling. The original's resolution option (if any) is researched before
  anything beyond that is built.

## Phase 0 — Setup ✅

- [x] Git repo, `.gitignore` that keeps copyrighted data out
- [x] `tools/extract_assets.py`: CD image → ISO 9660 → CAB (MSZip) → files,
      plus WAV resources from the PE. Standard library only; output checked
      byte for byte against 7-Zip.
- [x] Ghidra project `re/ghidra/astro.gpr`, auto-analysis done, `WinMain` named
- [x] `tools/run_original.sh`: Wine reference run (starts without crashing;
      rendering not checked by eye yet)

## Phase 1 — Mapping the executable ✅

- [x] Disassemble the rest of `.text`: seeds from the ILT, call targets and
      relocations. 1627 functions; code coverage is now ~97% (99.3% in the core)
- [x] C runtime: only checked for what matters. MSVC `rand()` is **not** used,
      so the RNG lives in the core (to find in Phase 3)
- [~] DirectX COM calls identified by vtable offset in the shell functions;
      typed vtable structs in Ghidra deferred (the port replaces this layer)
- [x] `WinMain` → main loop → menu/levels/ending; WndProc; shell ↔ core
      command and callback interface
- [x] Module map in [`docs/modules.md`](modules.md); names in `re/symbols.tsv`

Open questions carried forward:

- Target tick rate (speed follows the display refresh rate)
- Does the core really write to its own code (code range made RWX)?
- Is FLAC NN = CD track NN+1? Check against the `0xF3` script opcode uses
- `MCI_PLAY` without an end position: music runs on into the next tracks

## Phase 2 — Data formats (in progress, see [formats.md](formats.md))

- [x] Runtime memory layout (allocation block, arena, file load offsets)
- [x] RLE sprite encoding, sprite regions in GAME.DAT / LEVELn.DAT
- [x] Palettes (in ASTRO.exe, 6-bit, 240 fadeable colours), level palettes
- [x] Level script location, difficulty variants, opcode lengths (partial)
- [x] CD track → FLAC mapping confirmed (FLAC NN = track NN+1)
- [x] RNG located (`Core_Random`)
- [x] `tools/datview.py`: segment / render sprite sheets / raw views
- [ ] Sprite descriptor tables (sizes, hotspots), collision masks
- [ ] Non-sprite regions of the level files, GAME.DAT animation directories
- [ ] `Menu.dat` format
- [x] WAV resources → sound ids, voices and sets (`sounds.json`)
- [x] Packaging script `tools/package_assets.py`: CD image + FLACs →
      standalone `assets/` directory (verified: DATs identical, PNGs
      pixel-identical to the BMPs)

Decided: the `.DAT` files are packaged **byte-identical**, since the core
addresses them by hard-coded offsets. Only BMP images, WAV sounds and music are
converted or renamed. Sprite PNG export (`tools/datview.py`) is for viewing only.

## Phase 3 — Game logic

### Strategy: hybrid (decided)

Static recompilation of the core first, hand-written SDL3 shell, then gradual
replacement of generated functions with readable C. See
[recompiler.md](recompiler.md).

- [x] p-code export, translator, runtime; whole core translates and links
- [x] Prototype tests pass (RNG, sprite blitter, dispatcher)
- [x] Self-modifying instructions read their immediates from memory (texture mapper test passes)
- [ ] Flag-liveness optimisation
- [x] SDL3 host layer (`src/`, see [port.md](port.md)): logos, title, menus
      and level 1 gameplay run on the recompiled core
- [x] Sound effects and CD music from FLAC (see port.md, "Audio")
- [x] Configuration (original format), quitting, ending
- [x] Gamepads as DirectInput joysticks; keyboard uses the game's own redefinable sets
- [x] Options menu mode list (exact record layout); all options submenus work
- [x] Decided: 60 Hz tick rate; 4:3 with sharp-bilinear default (nearest/smooth selectable)
- [x] Audio: look-ahead limiter on the final mix (clipping 3 673 → 0 samples)
- [x] Frontend interface (`src/platform.h`); frontends SDL3, SDL2, SDL 1.2 and
      headless (deterministic frame/audio hashes for regression tests)
- [x] libFLAC replaced by vendored dr_flac; PNG via zlib; no SDL in common code
- [x] Windows: MinGW-w64 cross-build tested under Wine (hashes identical to
      Linux); MSVC + vcpkg preset prepared, untested

### Survey that led to the decision

The core is 58 272 instructions of hand-written x86. Survey results:

- only 85 distinct mnemonics, plain integer code (a few `fsqrt`)
- 60 indirect calls (command, callback and handler tables in `.data`),
  4 jump tables, status returned in the carry flag in places
- self-modifying code limited to ~35 patched immediates in rendering routines
  (pixel format, palette, sprite, 3D/polygon, two in the menu); no control flow
  is rewritten
- no OS calls; everything platform-related goes through the shell callbacks
- data pointers are 32-bit absolute addresses into `.data` and the arena

Options:

1. **Static recompilation**: a translator turns each core function into C
   (registers and flags as variables, memory as a flat array laid out like the
   original address space). Bit-exact by construction and playable early, but
   the generated code is unreadable at first.
2. **Hand reimplementation**: rewrite the game logic in idiomatic C from
   understanding. Readable, but very slow for this size and hard to keep
   bit-exact (RNG inputs, fixed-point math, quirks).
3. **Hybrid (chosen)**: start with 1, rewrite the shell by hand on SDL3,
   then replace generated functions one by one with readable C, each checked
   against the generated version (same input state → same output state).

### Tasks

- [x] Timing model: no refresh rate is requested (driver default, typically
      60 Hz on Win98/2000 with DX7); logic runs once per frame. Decided: 60 Hz
- [ ] Wave spawner (default script handler `41f757–4227e1`) and remaining opcodes
- [ ] Game states: intro, logos, menu, play, pause, game over, ending
- [ ] Entities: player, ship combining, energy, shields, enemies, bullets,
      capsules/power-ups, ground units, collision
- [ ] Scrolling, levels, scoring, lives, 2-player rules, input mapping
- [ ] Resolution option: the mechanism is known (see modules.md, "Display").
      Still to decide how to reproduce the upscaling (filtering, aspect)
- [ ] Shared struct definitions (Ghidra ⇄ C headers)

## Phase 4 — Port (C + SDL3)

1. Asset loaders, then an 8-bit 640×480 framebuffer with palette conversion
2. Main loop with the original timing, then menus
3. Gameplay, level by level
4. Sound effects (SDL3 audio) and FLAC music streaming (dr_flac)
5. Gamepads, Alt+Enter fullscreen, saved settings
6. Packaging script: CD image + FLACs → standalone game directory

## Phase 5 — Checking against the original

- Side-by-side runs against Wine
- Optional input recording and deterministic playback
