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
4. `python3 re/recomp/recomp.py build/recomp/core.pcode -o build/recomp/gen --entries build/recomp/entries.txt --smc re/smc_fields.txt --tables re/inline_tables.txt --text assets/data/ASTRO_TEXT.BIN`

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
gcc -O1 -std=c11 -w -Ire/recomp -Ibuild/recomp/gen re/recomp/recomp_rt.c re/recomp/test_proto.c build/recomp/gen/gen_*.c -lm -o build/recomp/test_full
```

```bash
build/recomp/test_full assets
```

## Known gaps / next steps

- **Compile time / speed**: flags are written to `cpu.r` after every
  instruction even when dead. A flag-liveness pass in `recomp.py` would remove
  most of that code.
- **Reference testing**: an x86 emulator (e.g. the Unicorn engine, not
  installed) would allow differential tests of every function against the
  original machine code.
- **Host layer**: `host_shell_call()` and the memory/stack/arena setup are the
  next piece: the SDL3 shell.
