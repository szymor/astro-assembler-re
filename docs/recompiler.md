# Static recompiler

Part of the hybrid porting strategy (see [plan.md](plan.md)): the hand-written
assembly core (`0x408c44–0x447da7`) is translated to C mechanically, so the
port is bit-exact from the start. Generated functions are later replaced one by
one with readable C, each checked against the generated version.

**The generated C is derived from `ASTRO.exe` and must never be committed.**
It is produced at build time into `build/recomp/` (git-ignored), like the
assets.

## Pipeline

```
ASTRO.exe ──Ghidra──▶ ExportPcode.java ──▶ core.pcode ──recomp.py──▶ gen_*.c, gen_table.c, gen_funcs.h
                     (p-code per instruction)                         + re/recomp/recomp_rt.{h,c}
```

1. Ghidra database prepared as in [modules.md](modules.md#rebuilding-the-database),
   including `FixInlineTables.java re/inline_tables.txt`
2. `ExportPcode.java build/recomp/core.pcode 408c44 447da7`
3. `python3 re/scripts/find_seeds.py extracted/game/ASTRO.exe build/recomp/seeds.txt`
   (remove the four jump-table bases listed in `re/inline_tables.txt`), giving
   `build/recomp/entries.txt`
4. `python3 re/recomp/recomp.py build/recomp/core.pcode -o build/recomp/gen --entries build/recomp/entries.txt --smc re/smc_fields.txt --tables re/inline_tables.txt --text assets/data/ASTRO_TEXT.BIN --replaced src/core/replaced.h`

Once `build/recomp/core.pcode` and `entries.txt` exist, CMake runs step 4
itself whenever the recompiler or the list of hand-written functions
changes (it takes a few seconds; only changed files are rewritten, so only
those are recompiled). Without them, CMake uses the files in
`build/recomp/gen` as they are, and the game refuses to start if they were
generated for a different list.

`re/smc_fields.txt` is produced by `SmcFields.java` from the list of patched
code fields (every p-code write into the core's code range); it is kept in the
repository because it only holds addresses and p-code templates.

Translating p-code means no x86 semantics are written by hand: Ghidra's
SLEIGH spec defines every instruction, including all flags. The translator
only maps about 40 p-code operations to C.

## Execution model

- **Registers**: `cpu.r[]`, laid out like Ghidra's x86 register space (so
  partial registers like `AH`/`AX` alias correctly). Flags are separate bytes.
  x87 registers hold `double` (only `fsqrt` + `fistp` are used, where double
  gives identical results).
- **Memory**: one flat 64 MB array `mem[]` addressed with the original 32-bit
  addresses. `.data` is loaded at its original VA (`0x46e000`) from
  `ASTRO_DATA.BIN`, so the absolute pointers inside it stay valid. The stack and
  the game's allocation block get fixed addresses chosen by the host. Every
  access is bounds-checked.
- **Calls**: a direct `CALL` becomes a C call. The x86 return address is still
  pushed on the emulated stack (by the p-code). After the call, the address the
  callee's `RET` popped is compared with this call's return address. If they
  differ, the callee returned to an outer frame: the core does this on purpose
  (e.g. `0x41851f` discards its return address to exit from its caller
  `0x419481` as well). The C function then returns immediately too, and each
  caller compares again until a frame matches (unwinding). A return to an
  address no frame expects ends at the outermost caller, which aborts.
- **Branches**: gotos inside a function; a branch or fall-through into another
  function's entry is a tail call.
- **Computed calls** (`rt_callind`): generated functions are looked up in a
  sorted table; addresses in the ILT (`0x401000–0x401244`) go to
  `host_shell_call()`, which the SDL3 platform layer implements (these are the
  shell callbacks listed in [modules.md](modules.md)).
- **Computed jumps**: the 4 jump tables become `switch` statements. Their targets are read from the table contents in the code image (`--tables`/`--text`), not from Ghidra's references, which the table repair can drop.
- **Special instructions**: lock markers are ignored; the dead DOS `OUT` palette
  writes abort if ever reached.
- **Self-modifying code**: the core patches 68 code fields at runtime (all
  direct writes; there are no writes into code through pointers). The code
  section is loaded at its VA from `ASTRO_TEXT.BIN`, so the patches land in
  emulated memory, and the 62 affected instructions read their patched
  constants from there. `SmcFields.java` finds those constants by decoding each
  instruction with two different sentinel values in its fields: constants that
  follow the sentinel are field reads (zero- or sign-extended). The p-code of
  the sentinel variant replaces the original, because Ghidra drops constants
  that are 0 in the original bytes (e.g. `[edx+0x0]` becomes `[edx]`).
  Three instructions (`0x41373f`, `0x413742`, `0x413745`) get their ModRM byte
  patched, which changes the operation itself. They belong to the 16/24/32 bpp
  palette converter, which the port never runs, and trap if reached.
- **Handler pointers**: code addresses that the core stores as values (and
  calls later) are made entries automatically.

## Status

- Whole core: **1272 functions, 0 translation problems**, self-modifying code
  handled; compiles and links (gcc `-O1`, ~3 min for 32 files in parallel).
- `re/recomp/test_proto.c` checks generated code against independent reference
  implementations. All pass:
  - `Core_Random`: 10 000 random states
  - `Core_DrawSprite`: fast path, clipping on every edge, fully off screen,
    small and large sprites (pixel-identical frame buffer)
  - `Core_Dispatch` command 0: computed call through the command table,
    `pushf/pushad` … `popad/popf` preserve registers and flags
- `re/recomp/test_smc.c` runs the self-modifying affine texture mapper
  (`0x442ed6`; patches its shift count, texture base and step values) against
  a hand transcription: pixel-identical in 4 cases, with the patched fields
  pre-filled with garbage so stale values cannot pass. It is generated on its
  own (`--only 442ed6`) because that address is inside a function.

Building the test:

```bash
gcc -O1 -std=c11 -w -Ire/recomp -Ibuild/recomp/gen -Isrc/core re/recomp/recomp_rt.c re/recomp/test_proto.c src/core/*.c build/recomp/gen/gen_*.c -lm -o build/recomp/test_full
```

```bash
build/recomp/test_full assets
```

## Replacing generated functions with hand-written C

The second half of the hybrid plan: generated functions are replaced one by
one with readable C, each checked against the generated version.

This section describes stage B of the plan ([plan.md](plan.md), Phase 3):
register-exact replacements, checked on every call. It stopped on 2026-11-28
in favour of stage C, rewriting whole subsystems into plain C checked by the
game state after each subsystem call; the machinery below (differential
build, fuzz check, coverage and profiling builds) carries over.

- **Where**: `src/core/`. `replaced.h` lists the replaced functions
  (`HAND(<hex>, <name>)`); each is written as `HAND_FN(<hex>) { ... }`
  (`core.h`) in a source file of its own subject (`random.c`, ...), and the
  file is added to `ASTRO_HAND_SOURCES` in `CMakeLists.txt`.
- **How it is linked in**: the recompiler generates listed functions as
  `ref_<hex>` instead of `f_<hex>`, so every direct, tail and computed call
  reaches the hand-written `f_<hex>`.
- **What it must do**: leave exactly what the original instructions left:
  registers (including the halves it does not use), the flags of the last
  flag-setting instruction (in p-code semantics: additions do not set AF),
  memory, the emulated stack, and on return the popped address in ESP, in
  Ghidra's EIP slot (`0x284`) and in `cpu.ret_addr` (`x86_ret()`).
  `core.h` has helpers for the common cases.
- **Names for the data**: `game.h` names the game state whose meaning is known:
  the frame (game time, random seed, horizontal scroll, arena), the player
  record (`P1_*` fields; `player_field()` moves one to player 2, 0x158
  further), the target player, the hall of fame, the control words, the
  level-2 and level-3 bosses with their parts (a path record followed by a
  reach record, `PATH_*` / `REACH_*`) and the nine enemy classes' tables.
  Functions that work on these use the names and take the player or part as a
  parameter where the original repeats the code (`init_ship`, `energy_up`,
  `copy_record`, `step_reach`); fields still unknown stay as addresses.
- **How it is checked**: the differential build (`cmake --preset diff`,
  `build/diff`, headless only) defines `RT_DIFF`. Every call of a replaced
  function then runs the generated version, records and undoes its memory
  writes, restores the registers, runs the hand-written version and compares
  the whole register file, the return address and every byte either version
  wrote (the stack below the final ESP is dead and ignored). A difference
  aborts with the call number, the register or address, and both values; at
  exit each function's number of checked calls is printed. Running the
  regression suite in that build checks the replacements on every state the
  recordings reach:

  ```bash
  cmake --preset diff && cmake --build --preset diff -j
  ctest --test-dir build/diff -j8 --output-on-failure
  ```

  The normal build's suite must then still give the same hashes.
- **Nested replacements**: a replaced function called during a check runs
  its generated version on the generated side and its hand-written version on
  the other, so the whole call tree is compared (this is how a function that
  is only ever reached from another one, like the blitter's clipping path,
  gets checked).
- **Shell callbacks**: a function that reaches a shell callback (sound,
  input, presentation) must not run the host twice. During a check the
  generated side really calls the host once and records the callback and every
  byte it wrote (`rt_diff_shell`); the hand-written side must make the same
  callbacks in the same order and gets the recorded writes replayed. A
  different sequence aborts the check. Host code must write emulated memory
  through the memory macros or call `rt_diff_note_range` first (the keyboard
  poll does); a write that bypasses both is invisible to the replay and makes
  the hand-written side diverge.
- **Dispatchers**: `Core_Dispatch` runs a whole shell command (a level, a
  menu, the ending) per call. Checking it would compare once per level, say
  nothing about which function differs, let errors that later frames overwrite
  go unnoticed, and in recordings that end inside a level never run the
  hand-written side at all. It is listed as `HAND_UNCHECKED` in `replaced.h`:
  differential builds run its hand-written version without a check of its own
  (`rt_diff_pass`), so everything it reaches is checked per call.
- **Game-state check** (stage C, `HAND_STATE` in `replaced.h`): for a
  subsystem rewritten in plain C, `rt_diff_check_state` compares only memory
  (minus scratch registered with `rt_diff_add_scratch`), ESP and the return
  address, not registers or flags. Afterwards it fills every register except
  ESP, and every flag, with garbage, so a caller that uses what the subsystem
  leaves in them makes the game diverge and the replay hashes fail (checked:
  `Core_Random` marked this way fails both level-1 tests at once, because its
  callers use AX). `re/scripts/boundary_audit.py <address>` lists, per call
  site, the registers and flags the caller reads after the call before
  writing them (callees counted by what they read first and write on every
  path); it is a reading aid, since partial registers and cycles make it
  over-report, and the poisoning is the test.
- **Limits**: a function nobody calls in any recording cannot be checked, so
  it is not kept unless the fuzz check below calls it (`0x40b866`, `0x4422c2`, and 15 of the 18 one-store setters
  in the `0x4240xx`-`0x4244xx` range).
- **What is left** (survey of 2026-11-27, `ASTRO_PROFILE` below): of 1 272
  functions 1 106 are still generated, about 660 of them called in the
  recordings. Few have anything in the way: none of the obstacles once
  expected stops a replacement. Self-modifying code is read from (and patched
  into) the code bytes in emulated memory, the x87 square root has a helper
  (`x87_isqrt`, which leaves the FPU registers as the p-code does), recursion
  and jumps into other functions are calls (`HAND_CALL`, or a plain call and
  return for a jump), and jump tables are switches. Of the functions with such
  features, these are still generated: four that patch or read patched code
  (`0x413616`, the palette converter, whose trapping ModRM patches lie on a
  path no recording takes; `0x443d40`, the textured polygon filler; `0x44413a`
  in the menu; `0x442b14`, never called), the level-script interpreter
  `Script_Run` (2 577 instructions), and 16 menu functions with jump tables
  (11 never called). 137 functions share instructions with a neighbour, which
  only means the generated code has copies; the hand-written one replaces its
  own entry.
- **Call counts** (`cmake -DASTRO_PROFILE=ON`): every generated function
  starts with `RT_ENTER(<address>)`, empty in normal builds; in a profiling
  build it counts the calls, and at exit the counts are appended to the file
  named by `ASTRO_PROFILE_OUT` (`<hex address> <calls>` per line, one block per
  run), e.g. `ASTRO_PROFILE_OUT=calls.txt ctest` in that build directory.
- **Coverage**: a check only covers the paths that get run. The coverage
  build (`cmake --preset coverage && cmake --build --preset coverage -j`, then
  `ctest` in `build/coverage` and `gcov -b` on the `.gcda` files in
  `build/coverage/CMakeFiles/astro_core.dir/src/core`) instruments only the
  hand-written sources. The 14 recordings alone (measured 2026-11-26) reach
  every line and branch of 145 of the 189 functions and helpers: some lists
  are always empty (eight per-frame steps only ever took their early exit),
  some tables never full, some pointers never null.
- **Fuzz check** (`src/fuzz.c`, test `fuzz-core`): `ASTRO_FUZZ=all` (or a
  comma-separated list of names) calls 42 hand-written functions on random but
  consistent inputs after the shell's start-up and exits: their tables are
  filled with edge values (counts matching the live entries, a free slot where
  the code scans for one, full tables, null path offsets, non-zero divisors).
  In the differential build every call is compared with the generated version;
  `ASTRO_FUZZ_ITER` (default 2000, the test uses 5000) and `ASTRO_FUZZ_SEED`
  vary the run. It found two differences no recording reaches:
  `Core_FindEntryOrFree` overwrote ESI when the table is full (the original
  jumps out before setting it), and `Core_FillRandomPairs` left the carry of
  its `ADD AX, BX` instead of that of `ADD EBP, 2`. With it, 175 of the 189
  are fully covered (measured 2026-11-27). Still not reached: three edge cases
  of the blitters (a silhouette entirely right of the screen, zero-length lines
  and runs in `Core_DrawSpriteRemap`), one condition that cannot be true
  (`Core_InsertRank`: the count after the DEC is never negative), and paths not
  expected to run: the unwinding after a call whose callee returned elsewhere
  (`HAND_CALL`), the divide-overflow abort in `Core_PlayerDistance`, and the
  startup list mismatch.

Replaced so far (counts are of the hand-written side's calls, including
those inside the checks of other replaced functions):

| Address | Name | File | Checked on |
|---|---|---|---|
| `0x408f8c` | `Core_Random` | `random.c` | every call in the 14 regression recordings (166 538 in the full-game one); a wrong flag or a missing write is reported on the first call |
| `0x442454` | `Core_DrawSprite` | `sprite.c` | 5 103 188 calls in the full-game recording; a clipping edge one pixel off is reported at the first sprite touching it |
| `0x4424f0` | `Core_DrawSpriteClipped` | `sprite.c` | only reached from `Core_DrawSprite`: 300 685 calls inside its checks |
| `0x4425dc` | `Core_DrawSilhouette` | `sprite.c` | 1 464 calls in the full-game recording, every branch reached (fully visible 819, clipped 627 incl. lines above and runs cut or dropped at both edges); a clipping edge one pixel off is reported at call 174 |
| `0x4137f4` | `Core_AddEntry8` | `pools.c` | 2 444 calls in the full-game recording |
| `0x41c033` | `Core_FindFreeSlot200` | `pools.c` | 219 063 calls in the full-game recording |
| `0x41cb30` | `Core_FindFreeSlot30` | `pools.c` | 5 335 calls in the full-game recording |
| `0x42be97` | `Core_WrapPosition` | `pools.c` | 291 838 calls in the full-game recording |
| `0x41c27d` | `Core_FindFreeSlot100` | `pools.c` | 2 376 calls in the full-game recording |
| `0x41c9b2` | `Core_FindFreeSlot25` | `pools.c` | 61 calls in the full-game recording |
| `0x41cdb2` | `Core_FindFreeSlot10` | `pools.c` | 88 calls in the full-game recording |
| `0x41cf6b` | `Core_WrapIndex` | `pools.c` | 1 390 calls in the full-game recording |
| `0x42beb7` | `Core_FineToIndex` | `pools.c` | 352 560 calls in the full-game recording |
| `0x408cf1` | `Core_GetLevelScript` | `spawn.c` | 14 calls in the full-game recording |
| `0x41bea9` | `Core_StartObject28` | `spawn.c` | 1 043 calls in the full-game recording |
| `0x4435ec` | `Core_AddVectors` | `vectors.c` | 194 389 calls in the full-game recording |
| `0x44359c` | `Core_StepAngles` | `vectors.c` | 175 525 calls in the full-game recording |
| `0x443554` | `Core_NormaliseVectors` | `vectors.c` | 165 637 calls in the full-game recording; the IDIV by a zero sum would fault as in the original (never reached) |
| `0x41b54b` | `Core_WrapAngle16` | `vectors.c` | 3 670 calls in the full-game recording |
| `0x423f3f` | `Core_StartTimedEntry` | `spawn.c` | 69 calls in the full-game recording |
| `0x41c49a` | `Core_FindFreeSlot50` | `pools.c` | 46 calls in the full-game recording |
| `0x42be54` | `Core_SplitFixed` | `vectors.c` | 94 419 calls in the full-game recording (caught EAX/EDX being left shifted) |
| `0x409658` | `Core_StoreFrameConstants` | `init.c` | 9 calls in the full-game recording |
| `0x409e91` / `0x409f04` | `Core_InitPlayer1Ship` / `2Ship` | `init.c` | 7 and 6 calls in the full-game recording |
| `0x445a93` | `Core_ClearTables` | `init.c` | 52 calls in the full-game recording |
| `0x40982e` | `Core_FillZeroThenOnes` | `init.c` | 17 calls in the full-game recording |
| `0x409818` | `Core_CopyRows` | `init.c` | 21 calls in the full-game recording |
| `0x41851f` / `0x418559` | `Player1_CanFire` / `Player2_CanFire` | `init.c` | 129 991 calls in the full-game recording (player 1); 3 403 in the two-player recording (player 2). They drop the caller's return address too (`ADD ESP, 4; RET`) |
| `0x4422d6` | `Core_PickNearerPlayer` | `init.c` | 384 489 calls in the full-game recording, 8 640 in the two-player one |
| `0x416ba1`, `0x416ba8`, `0x416baf`, `0x416bb6` / `0x416bff`, `0x416c06`, `0x416c0d`, `0x416c14` | `Input_KeyboardP1_Layout1`-`4` / `Input_KeyboardP2_Layout1`-`4` | `keys.c` | P1 layout 1: 163 657 calls, P2 layout 2: 3 403 (all recordings). The other six are **not called by any recording**: they share the checked body and differ only in the start of the key list; the fuzz check calls them |
| `0x41a93d` | `Core_ClearFinishedA` | `tables.c` | 130 092 calls in the full-game recording |
| `0x41b026` | `Core_ClearFinishedB` | `tables.c` | 130 092 calls in the full-game recording |
| `0x415e97` | `Core_FindEntryOrFree` | `tables.c` | 3 318 calls in the full-game recording |
| `0x413849` | `Core_NextFrames` | `tables.c` | 130 092 calls in the full-game recording |
| `0x42b030` | `Boss2_StartArmMoves_q` | `init.c` | 4 calls in the full-game recording |
| `0x42d0fc`, `0x42f888`, `0x43029b` | `Boss3_DamageFlash`, `Boss4_DamageFlash`, `Boss5_DamageFlash` | `tables.c` | 4 303, 5 945 and 7 188 calls in the full-game recording |
| `0x4135ba` | `Palette_StartFade` | `tables.c` | one call (its 720 components) in the full-game recording |
| `0x42e61e` | `Boss3_StartLimbMoves_q` | `tables.c` | 15 calls in the full-game recording |
| `0x430874` | `Core_SetRecordFields` | `tables.c` | 9 calls in the full-game recording (which of its four cases are reached is not recorded) |
| `0x41b676` | `Core_SpawnShot` | `vectors.c` | 6 607 calls in the full-game recording |
| `0x42d318` | `Boss3_FollowPath` | `vectors.c` | 4 303 calls in the full-game recording |
| `0x419819` / `0x41aa6d` | `Core_StepEntries24` / `Core_StepEntries14` | `tables.c` | 130 092 calls each in the full-game recording |
| `0x42975e`, `0x431c25` | `Boss2_DamageFlash`, `Boss6Mid_DamageFlash` | `tables.c` | 17 475 and 11 726 calls in the full-game recording |
| `0x4097d9` | `Core_CopyTiles` | `init.c` | 2 calls in the full-game recording |
| `0x415a58` | `Core_MaskBlit` | `init.c` | 3 calls in the full-game recording |
| `0x433acd` | `Core_Cross4` | `init.c` | 151 974 calls in the full-game recording |
| `0x4299b8` | `Boss2_FollowPath` | `vectors.c` | 3 495 calls in the full-game recording |
| `0x41599f` | `Core_MaskBlitRows` | `init.c` | 974 calls in the full-game recording |
| `0x42d1a0` | `Boss3_StepPhase_q` | `init.c` | 4 303 calls in the full-game recording |
| `0x42ad07` | `Boss2_StepArmReach_q` | `tables.c` | 3 495 calls in the full-game recording |
| `0x42aee4` | `Boss2_StepArmPaths_q` | `vectors.c` | 3 495 calls in the full-game recording |
| `0x41be2a` | `Core_ProjectPoints` | `vectors.c` | 430 584 calls in the full-game recording |
| `0x442758` | `Core_DrawSpriteRemap` | `sprite.c` | 6 485 695 calls in the full-game recording, every clipping branch reached (left start 341 598, entirely left 25 616, past the right edge 787 979, entirely right 59 840, above 2 420 396, below 138 919) |
| `0x42e241` | `Boss3_StepLimbReach_q` | `tables.c` | 5 020 calls in the full-game recording |
| `0x42e495` | `Boss3_StepLimbPaths_q` | `vectors.c` | 5 020 calls in the full-game recording |
| `0x4434c4` | `Math_BuildRotation` | `vectors.c` | one call per recording that reaches it |
| `0x41cd6f`, `0x41cae3` | `Core_FillRandomPairs`, `Core_FillRandomFlags` | `spawn.c` | 88 and 5 335 calls in the full-game recording; they call `Core_Random` (the first hand-written functions to call another core function, through `HAND_CALL` in `core.h`) |
| `0x41b494` | `Core_PlayerDistance` | `init.c` | 200 720 calls in the full-game recording; calls `Core_PickNearerPlayer` |
| `0x437c3a` | `Core_RunObjectExit` | `spawn.c` | 10 calls in the full-game recording (calls a handler through a pointer) |
| `0x437ea0` | `Core_ObjectStart1` | `spawn.c` | 74 calls in the full-game recording |
| `0x4384b7` | `Core_ObjectStart2` | `spawn.c` | 128 calls in the full-game recording |
| `0x42414a`, `0x424154`, `0x42415e` | `Script_ProtectSpeed`, `Script_ProtectWeapon`, `Script_ProtectWeapon2` | `setters.c` | 2, 1 and 1 calls (one-store script handlers) |
| `0x408cb5` | `Core_CallShell` | `shellcall.c` | 455 182 calls in the full-game recording (454 962 checked on their own); the shell callbacks it makes are recorded and replayed |
| `0x408c44` | `Core_Dispatch` | `shellcall.c` | not checked itself (`HAND_UNCHECKED`): one call runs a whole command, 4 to 12 per recording; what it calls is checked per call |
| `0x409639` | `Core_ApplyCdVolume` | `shellcall.c` | 1 to 8 calls |
| `0x4096ae`, `0x4096df` | `Core_CopyRecord1/2` | `shellcall.c` | 1 to 3 calls each |
| `0x409791` | `Core_FillMarkerPairs` | `shellcall.c` | 2 calls |
| `0x408cc2` | `Core_Cmd00_GetRanges` | `small.c` | 11 calls |
| `0x40b81c` | `Core_ClearBlock55cab0` | `small.c` | 19 calls |
| `0x416c5d` | `Core_PollJoy00` | `small.c` | 1 128 calls (the other three joystick polls and six more small routines are not called by any recording, so they were left generated) |
| `0x41f0e1` | `Core_SetScrollWindow` | `small.c` | 1 call |
| `0x4342e0`, `0x43430a`, `0x434338`, `0x43435e`, `0x43438b`, `0x4343d4`, `0x4343f9`, `0x434429`, `0x43457c` | `Enemy1_TwinPod_Box_q`, `Enemy2_Orb_…`, `Enemy3_Dome_…`, `Enemy4_Claw_…`, `Enemy5_Gunship_…`, `Enemy6_Mech_…`, `Enemy7_Cruiser_…`, `Enemy8_Bomber_…`, `Enemy9_Turner_Box_q` | `hitbox.c` | 5 371 to 67 776 calls each (bounding boxes of objects) |
| `0x43444f`, `0x434472`, `0x434495`, `0x4344b8`, `0x4344db` | `BossArm_SegmentBox1_q`-`5_q` | `hitbox.c` | about 31 000 calls each |
| `0x429b26`, `0x42d48f`, `0x429bc4` | `Core_AddListEntry18a/18b/14a` | `hitbox.c` | 4, 1 and 1 calls (`0x42d529` is not called by any recording) |
| `0x42abf9` | `Boss2_StepArmFrames` | `counters.c` | 3 495 calls |
| `0x4240e1`, `0x424120` | `Core_CountUp1`, `Core_GrowSlot1` | `counters.c` | 9 and 1 calls (thin: the second player's counterparts `0x4243b6` and `0x4243f5` are not called by any recording) |
| `0x41b710`, `0x41b9b6` | `Core_ReloadEntries1/2` | `counters.c` | 46 calls each |
| `0x41bd40`, `0x419446`, `0x419971`, `0x41abba` | `Core_StepDeadlines`, `Core_StepEntriesA/B/C` | `lists.c` | 46 and 137 037 calls each (per-frame steps over entry lists) |
| `0x446bc6`, `0x41b944` | `Core_AddEntry5`, `Core_AddPhaseEntry` | `lists.c` | 6 542 and 62 calls |
| `0x42ac52` | `Boss2_StepFrames` | `lists.c` | 3 495 calls |
| `0x424030` | `Core_NextRecord18` | `lists.c` | 7 calls |
| `0x415b51` | `Core_MaskRows8` | `lists.c` | 1 144 calls (`0x4423b2` and `0x408fd2` are not called by any recording) |
| `0x42d38b`, `0x42d3c8`, `0x429a5f` | `Boss3_StepPathSpeed`, `Boss3_StepPathScale`, `Boss2_StepPathSpeed` | `misc.c` | 4 303, 4 303 and 3 495 calls (clamped steppers) |
| `0x41cf17`, `0x415fe8`, `0x416283` | `Core_FindFreeSlot50b`, `Core_FindFreeSlot100b`, `Core_FindFreeSlot200b` | `misc.c` | 80, 740 and 611 calls (carry = table full; named by capacity, like those in `pools.c`) |
| `0x415d5c`, `0x415d7f`, `0x4233c9` | `Core_BoxOutside`, `Core_ScaledPos`, `Core_VectorOffset` | `misc.c` | 63 341, 5 986 and 30 845 calls |
| `0x44688f`, `0x446c99` | `Core_CopyRun`, `Core_CopyScreen` | `misc.c` | 1 200 057 and 22 690 calls (`0x42d529`, `0x446e32` and `0x424320` are not called by any recording) |
| `0x444a7c`, `0x41b740`, `0x414a91` | `Core_InsertRank`, `Core_LookupSigned`, `Core_StepTimerPairs` | `stage.c` | 1 (thin: the carry-set path with the backward copy), 1 530 and 274 074 calls |
| `0x41faa8`, `0x42aab2` | `Math_PlanMove3D`, `Math_VectorAngle` | `math.c` | 226 calls (inside other checks) and 61 933 calls; the x87 square root through `x87_isqrt` |
| `0x44360c` | `Render_DrawFaces` | `math.c` | 148 132 calls (the 3D renderer: culling, depth sort, shading with the x87 square root) |
| `0x4438bc` | `Core_SortDescending` | `math.c` | 22 420 calls and 9 104 765 recursive ones inside them (quicksort; registers pass through `cpu` around the recursive calls) |
| `0x4431db`, `0x44334b` | `Math_RotateVertices`, `Math_RotateVerticesScaled` | `math.c` | 165 637 and 71 443 calls in the full-game recording (they patch the rotation matrix into their own IMUL immediates) |
| `0x41ec5f`, `0x41cf84`, `0x41f0d0` | `Level_Scroll`, `Level_ScrollAndDraw`, `Level_SkipAhead` | `level.c` | 130 168 calls (inside `Level_ScrollAndDraw`'s), 130 157 and 4; the scroll jumps into `Script_Run` |
| `0x427312`, `0x4273e3` | `Obj_PickPath_q`, `Obj_NextPathPoint_q` | `level.c` | 4 and 253 calls (they fall into each other) |
| `0x442e3d`, `0x442ecc` | `Render_RemapSpan2_q`, `Render_TextureAffine` | `texture.c` | 2 716 226 and 233 515 calls; the texture mapper patches its shift, texture and steps into its own loop |

### Stage C modules

Whole subsystems in plain C, checked by the game state (`HAND_STATE`, see
"Game-state check"). Their entry points are listed in `replaced.h`; the
functions below them are plain C and no longer listed, and the register-exact
functions they superseded were removed.

Plain C calls a function that is still register-level (generated or
register-exact) by setting the registers the callee reads and calling
`core_call` (`core.h`), which pushes the original return address and stops if
the callee returns anywhere else.

| Entry | Name | File | What it covers | Checked on |
|---|---|---|---|---|
| `0x41ed18` | `Background_LayerANewStrip` | `background.c` | layer A's new strips (two planes; superseded `Core_CopyStripA`) | 2 514 calls in the full-game recording (inside `Level_ScrollAndDraw`'s checks) |
| `0x41ede7`, `0x41eefd` | `Background_ScrollLayerB`, `Background_ScrollLayerC` | `background.c` | the parallax scroll of layers B and C and their new strips (superseded `Core_CopyStripB`, `Core_CopyBlock16x20`) | 46 797 and 69 621 calls (inside `Level_ScrollAndDraw`'s checks) |
| `0x41cfaf` | `Background_DrawLayers` | `background.c` | the frame composed from the layers: the layer views, plain copies and clears, the three composing routines and layer A in front, the decoration sprites (stepped and drawn through `Core_DrawSprite`, a register-level call), and the starfield `0x413887` (moves, colour cycling, five plotting ways) as an internal call (superseded `Core_LayerOffset0/1/2`, `Core_ClearBlock3e80`, `Core_CopyRowsSkip`, and earlier the seven record loops of `recs.c`, `Core_StepBytes10`, `Core_StepWords10` and the three record-set drivers) | 130 168 calls (inside `Level_ScrollAndDraw`'s checks) |

## Known gaps / next steps

- **Compile time / speed**: flags are written to `cpu.r` after every
  instruction even when dead. A flag-liveness pass in `recomp.py` would remove
  most of that code.
- **Reference testing**: an x86 emulator (e.g. the Unicorn engine, not
  installed) would allow differential tests of every function against the
  original machine code.
- **Host layer**: `host_shell_call()` and the memory/stack/arena setup are the
  next piece: the SDL3 shell.
