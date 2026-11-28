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

### Strategy

Three stages; the first two are done, the third is the way to plain C.

- **A. Static recompilation (done).** The core is translated from Ghidra's
  p-code into C at build time (bit-exact by construction), with a hand-written
  SDL3 shell around it. See [recompiler.md](recompiler.md).
- **B. Register-exact replacements (done, stopped 2026-11-28).** 166 generated
  functions rewritten by hand (`src/core/`, listed in `replaced.h`), each
  leaving exactly the x86 state its original leaves and checked against it on
  every call (differential build `--preset diff`, plus the fuzz check). That is
  about 4 100 of the core's ~60 500 instructions. The approach works but yields
  C shaped like the x86 (registers, flags, an emulated stack) that would need a
  second rewrite to become plain C, so it is not continued. Its functions stay
  as checked helpers until their subsystems are rewritten (stage C).
- **C. Subsystem rewrite into plain C (now).** Top down, a whole subsystem at
  a time, straight into ordinary C: inside a subsystem there are no registers,
  flags or emulated stack, just C functions with parameters and return values.
  The check changes from "the same x86 state after every call" to "the same
  game state after every subsystem call": the generated version and the new C
  run from the same state and all game memory must match afterwards (scratch
  and the stack aside); the replay hashes remain the end-to-end check. Game
  state stays in emulated memory, read through typed views (`src/core/game.h`),
  until nothing compares against the generated code any more. Ghidra remains
  the tool for understanding the code, but no longer a build input at the end.

### Stage C tasks

- [x] **Game-state check mode** in the differential harness (`HAND_STATE`):
      memory only (no registers or flags) after a subsystem call, scratch and
      the stack excluded; the registers and flags are poisoned afterwards, so a
      caller that depends on them fails the replay hashes (recompiler.md,
      "Game-state check"). Proven on `Level_ScrollAndDraw`: all 15 tests pass
      with it poisoned; `Core_Random` poisoned fails them, as it must
- [x] **Boundary audit** tooling: `re/scripts/boundary_audit.py` (static,
      a reading aid) and the poisoning (the test); each subsystem is audited
      when it is rewritten (where a register is used, it becomes the
      subsystem's return value)
- [ ] **Frame loop in C**: the level's per-frame loop (`Core_RunLevel`)
      rewritten, still calling the generated per-frame steps
- [ ] **Subsystems**, roughly in this order (each: rewrite, game-state check
      on all recordings, delete its entries from `replaced.h` and its
      generated functions from the default build):
  - [ ] Background and scrolling, from `Level_ScrollAndDraw` (`0x41cf84`):
        the first one, to prove the approach. 14 generated functions (2 853
        instructions) plus 13 already hand-written; `Script_Run` is the one
        call out. The layer drawing `0x41cfaf` and its routines `0x41e284`
        (970 instructions), `0x41d60d` (647), `0x41dcb5` (570), `0x41d2ad`
        (327), `0x41d2a5`; the overlay sprites `0x413dc3` / `0x413e50`;
        the animated tiles `0x413887`, `0x4138aa`, `0x413904`; the strip
        copies `0x41ed18`, `0x41ede7`, `0x41eefd`
    - [x] In plain C (`src/core/background.c`): the three layers' strips and
          scrolling, and the starfield (`0x413887` with its five plotting
          ways); the 15 register-exact functions they superseded are removed
    - [ ] The layer drawing `0x41cfaf` and its four big routines, the overlay
          sprites, then `Level_ScrollAndDraw` / `Level_Scroll` themselves
  - [ ] Renderer: sprites, silhouettes, remapped sprites, polygons, the
        texture mapper and the polygon fillers (`0x44392c`, `0x443d40`)
  - [ ] The nine enemy classes (update, collision, drawing)
  - [ ] Player: controls, movement, weapons, special ships, assembling, hits,
        lives and the two-player rules
  - [ ] Bosses of levels 1-6
  - [ ] Capsules, ship parts, items and other pickups
  - [ ] Level script interpreter (`Script_Run`) and the wave spawner
  - [ ] HUD, score, lives, hall of fame
  - [ ] Commands outside gameplay: logos, title, menu and options, ending
  - [ ] Start-up and per-level set-up commands
- [ ] **Coverage**: more recordings, since a check only covers what is played:
      two-player playthroughs, the menus and every option, all control
      layouts (four keyboard sets, joysticks), difficulty settings, rare
      situations (the bot can produce many of these)
- [ ] **Unreachable code**: functions that cannot run in the port are not
      rewritten; each is confirmed unreachable and listed with the reason
      (the 16/24/32 bpp blitters and their set-up, the DOS VGA palette path,
      the debug routine `0x40b866`, ...)
- [ ] **Embedded data**: jump tables and the constants the game patches into
      its own code become C tables and variables, so the code image
      (`ASTRO_TEXT.BIN`) is no longer needed
- [ ] **Game state in C structs** (Ghidra ⇄ C headers), once the subsystems
      no longer compare against the generated code
- [ ] **Build without the recompiler**: the generated code moves to an
      optional reference build (kept as the oracle for checks); the default
      build needs neither the Ghidra export nor `recomp.py`

### Other tasks

- [x] Timing model: no refresh rate is requested (driver default, typically
      60 Hz on Win98/2000 with DX7); logic runs once per frame. Decided: 60 Hz
- [x] Resolution option: the mechanism is known (see modules.md, "Display").
      Decided: 4:3 scaling with sharp/nearest/smooth filters, plus reduced
      320x240 resolution and 16:10 gameplay aspect (port.md, "Resolution and
      gameplay aspect")
- [ ] Flag-liveness optimisation of the generated code (only matters while it
      is still built by default)

### Done so far (stages A and B)

- [x] p-code export, translator, runtime; whole core translates and links
- [x] Prototype tests pass (RNG, sprite blitter, dispatcher)
- [x] Self-modifying instructions read their immediates from memory (texture mapper test passes)
- [x] Differential build: each hand-written function checked against the
      generated version on every call in the regression recordings
      (recompiler.md, "Replacing generated functions"); fuzz check for paths
      the recordings miss; coverage build (gcov) of the hand-written code;
      profiling build counting the calls of every generated function
- [x] 166 functions replaced (recompiler.md lists them), including ones with
      self-modifying code, the x87 square root and recursion
- [x] SDL3 host layer (`src/`, see [port.md](port.md)): logos, title, menus
      and gameplay run on the recompiled core
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

### Survey that led to the hybrid decision (2026-11-23)

The core is 58 272 instructions of hand-written x86. Survey results:

- only 85 distinct mnemonics, plain integer code (a few `fsqrt`)
- 60 indirect calls (command, callback and handler tables in `.data`),
  4 jump tables, status returned in the carry flag in places
- self-modifying code limited to ~35 patched immediates in rendering routines
  (pixel format, palette, sprite, 3D/polygon, two in the menu); no control flow
  is rewritten
- no OS calls; everything platform-related goes through the shell callbacks
- data pointers are 32-bit absolute addresses into `.data` and the arena

Options then:

1. **Static recompilation**: a translator turns each core function into C
   (registers and flags as variables, memory as a flat array laid out like the
   original address space). Bit-exact by construction and playable early, but
   the generated code is unreadable at first.
2. **Hand reimplementation**: rewrite the game logic in idiomatic C from
   understanding. Readable, but very slow for this size and hard to keep
   bit-exact (RNG inputs, fixed-point math, quirks).
3. **Hybrid (chosen)**: start with 1, rewrite the shell by hand on SDL3,
   then replace generated functions with readable C, each checked against the
   generated version (same input state → same output state). Stage C above
   is how this ends: the check is the same idea, applied per subsystem.

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
