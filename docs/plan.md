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
- [x] `tools/run_original.sh`: Wine reference run (renders correctly; virtual
      desktop set in the prefix's registry since Wine 11)

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
- [x] Replacing generated functions by hand-written C: `src/core/`, listed
      in `replaced.h`; differential build (`--preset diff`) checks each one
      against the generated version on every call in the regression
      recordings (recompiler.md, "Replacing generated functions")
- [x] Replaced so far: `Core_Random`, the sprite blitter (`Core_DrawSprite`,
      `Core_DrawSpriteClipped`) and its one-colour variant (`Core_DrawSilhouette`), and the pool helpers in `pools.c`; 141 functions in all (`src/core/`: sprites, pools, vectors, tables, init, spawn, keys, setters, shellcall, small, hitbox, counters, lists, misc), listed in recompiler.md
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
- [ ] Remaining generated functions: those that patch their own code (the 3D/texture set-up), use the FPU, recurse, or share tails with their neighbours (see recompiler.md, "Limits")
- [ ] Wave spawner (default script handler `41f757–4227e1`) and remaining opcodes
- [ ] Game states: intro, logos, menu, play, pause, game over, ending
- [ ] Entities: player, ship combining, energy, shields, enemies, bullets,
      capsules/power-ups, ground units, collision
- [ ] Scrolling, levels, scoring, lives, 2-player rules, input mapping
- [x] Resolution option: the mechanism is known (see modules.md, "Display").
      Decided: 4:3 scaling with sharp/nearest/smooth filters, plus reduced
      320x240 resolution and 16:10 gameplay aspect (port.md, "Resolution and
      gameplay aspect")
- [ ] Shared struct definitions (Ghidra ⇄ C headers)

## Phase 4 — Port (C + SDL3) ✅

- [x] Asset loaders, then an 8-bit 640×480 framebuffer with palette conversion
- [x] Main loop with the original timing, then menus
- [x] Gameplay (levels 1, 2 and 6 checked by eye; all levels run in the test suite)
- [x] Sound effects (SDL3 audio) and FLAC music streaming (dr_flac)
- [x] Gamepads, Alt+Enter fullscreen, saved settings
- [x] Packaging: CD image + FLACs → standalone game directory, single-archive packages

## Phase 5 — Checking against the original

- [x] Input recording and deterministic playback (`ASTRO_RECORD` /
      `ASTRO_REPLAY`, any frontend → headless); ctest regression suite in
      `tests/` (see port.md, "Test suite")
- [x] Scripted recordings: game over with a hall of fame entry, a
      two-player game set up through the options menu
- [x] `astro-bot`: playthroughs by search (fork-based rollouts and save
      states); its playthrough of the whole game, ending and hall of fame
      entry is a test (`replay-full-bot-full-game`)
- [x] The final boss: only RAIDEN's mega bomb hurts it (modules.md, "Game
      logic: special ships and bosses")
- [ ] Two-player playthroughs
- [x] Side-by-side runs against Wine: menus and attract mode identical (RGB565),
      level 1 gameplay the same frame sequence (see port.md, "Checking against
      the original"); timing not comparable (unpaced under Wine)
