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

## Phase 1 — Mapping the executable

- [ ] Get the remaining `.text` disassembled (see findings: analysis covers
      only ~95 KB of 431 KB)
- [ ] Name the C runtime functions (debug CRT, source file names in asserts)
- [ ] Wrap DirectDraw/DirectInput/DirectSound COM calls with typed vtables
- [ ] `WinMain` → window procedure → main loop → game state machine
- [ ] Module map in `docs/modules.md`

## Phase 2 — Data formats

- [ ] `.dat` sprite container: frame split, sizes, hotspots, palette location
- [ ] `Menu.dat` format
- [ ] Large tables in `.data` (~950 KB): level scripts, waves, paths
- [ ] WAV resource name → usage
- [ ] CD-audio track number → situation, so the FLACs can be mapped
- [ ] Python viewer/exporter (PNG sprite sheets) to check formats by eye
- [ ] Converter to the port's runtime formats

## Phase 3 — Game logic

- [ ] Timing model (fixed rate vs vSync)
- [ ] Game states: intro, logos, menu, play, pause, game over, ending
- [ ] Entities: player, ship combining, energy, shields, enemies, bullets,
      capsules/power-ups, ground units, collision
- [ ] Scrolling, levels, scoring, lives, 2-player rules, input mapping
- [ ] Resolution option: how it worked and whether to reproduce it
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
