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
4. `python3 re/recomp/recomp.py build/recomp/core.pcode -o build/recomp/gen --entries build/recomp/entries.txt`

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
  pushed on the emulated stack (by the p-code), and every `RET` is checked
  against the address its `CALL` pushed, so any return-address trickery would
  abort with a message rather than misbehave.
- **Branches**: gotos inside a function; a branch or fall-through into another
  function's entry is a tail call.
- **Computed calls** (`rt_callind`): generated functions are looked up in a
  sorted table; addresses in the ILT (`0x401000–0x401244`) go to
  `host_shell_call()`, which the SDL3 platform layer implements (these are the
  shell callbacks listed in [modules.md](modules.md)).
- **Computed jumps**: the 4 jump tables become `switch` statements.
- **Special instructions**: lock markers are ignored; the dead DOS `OUT` palette
  writes abort if ever reached.

## Status

- Whole core: **1269 functions, 0 translation problems**; compiles and links
  (gcc `-O1`, ~3 min for 32 files in parallel, 2.6 MB).
- `re/recomp/test_proto.c` checks generated code against independent reference
  implementations. All pass:
  - `Core_Random`: 10 000 random states
  - `Core_DrawSprite`: fast path, clipping on every edge, fully off screen,
    small and large sprites (pixel-identical frame buffer)
  - `Core_Dispatch` command 0: computed call through the command table,
    `pushf/pushad` … `popad/popf` preserve registers and flags

Building the test:

```bash
gcc -O1 -std=c11 -w -Ire/recomp -Ibuild/recomp/gen re/recomp/recomp_rt.c re/recomp/test_proto.c build/recomp/gen/gen_*.c -lm -o build/recomp/test_full
```

```bash
build/recomp/test_full assets
```

## Known gaps / next steps

- **Self-modifying code**: ~35 instructions get their immediates patched at
  runtime (see modules.md). The generated code still has the original
  constants baked in. Those instructions must read their immediate from `mem`
  instead (code bytes loaded at their VA). Affects the palette fade area,
  sprite routines, 3D/polygon code and two menu spots; must be done before
  running gameplay. The pixel-format converter does not matter, since the port
  replaces `Video_PresentFrame`.
- **Compile time / speed**: flags are written to `cpu.r` after every
  instruction even when dead. A flag-liveness pass in `recomp.py` would remove
  most of that code.
- **Reference testing**: an x86 emulator (e.g. the Unicorn engine, not
  installed) would allow differential tests of every function against the
  original machine code.
- **Host layer**: `host_shell_call()` and the memory/stack/arena setup are the
  next piece: the SDL3 shell.
