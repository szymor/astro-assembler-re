#!/usr/bin/env python3
"""Static recompiler: Ghidra p-code (from ExportPcode.java) -> C.

Every core function becomes a C function `void f_<addr>(void)` that operates on
the emulated CPU state (`cpu.r`, a register file laid out like Ghidra's x86
register space) and the emulated memory (`mem`, flat 32-bit address space).
The x86 stack lives in emulated memory, so ESP-relative code works unchanged;
CALL/RET are mapped onto C calls, and every RET is checked against the return
address its CALL pushed (see recomp_rt.h).

Control flow inside a function becomes gotos. A branch or fall-through to
another function's entry becomes a tail call. Computed calls go through
rt_callind(), which dispatches to generated functions or to the shell
(addresses below the core, i.e. ILT slots).

The output is derived from ASTRO.exe and must not be committed.

Functions rewritten by hand (src/core/replaced.h, `HAND(<hex>, <name>)`, passed
with --replaced) are generated as `ref_<addr>` instead: every call, including
computed calls through rt_func_table, then reaches the hand-written `f_<addr>`,
and the generated version stays available as the reference the differential
build checks it against (recomp_rt.h, RT_DIFF).

Usage:
  python3 re/recomp/recomp.py core.pcode -o build/recomp/gen [--only 408f8c,442454]
"""

from __future__ import annotations

import argparse
import collections
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

CORE_START, CORE_END = 0x408C44, 0x447DA7


@dataclass
class Vn:
    space: str
    off: int
    size: int
    field_size: int = 0      # space "smc": runtime-patched code field at `off`
    sign: bool = False       #   and whether the constant is its sign extension


@dataclass
class Op:
    name: str
    out: Vn | None
    ins: list[Vn]


@dataclass
class Insn:
    addr: int
    length: int
    text: str
    ops: list[Op] = field(default_factory=list)
    flows: list[int] = field(default_factory=list)

    @property
    def fallthrough(self) -> int:
        return self.addr + self.length


def parse_vn(s: str) -> Vn | None:
    if s == "-":
        return None
    space, off, size = s.split(":")
    return Vn(space, int(off, 16), int(size))


def load_pcode(path: Path):
    funcs: dict[int, str] = {}
    insns: dict[int, Insn] = {}
    cur = None
    with open(path) as f:
        for line in f:
            tag = line[0]
            if tag == "P":
                parts = line.split()
                cur.ops.append(Op(parts[1], parse_vn(parts[2]), [parse_vn(p) for p in parts[3:]]))
            elif tag == "I":
                _, a, n, _hex, text = line.rstrip("\n").split(" ", 4)
                cur = Insn(int(a, 16), int(n), text)
                insns[cur.addr] = cur
            elif tag == "J":
                cur.flows = [int(p, 16) for p in line.split()[2:]]
            elif tag == "F":
                _, a, name = line.split()
                funcs[int(a, 16)] = name
    return funcs, insns


def resolve_jump_tables(insns: dict[int, Insn], tables_path: Path, text: bytes, text_va: int = 0x401000) -> int:
    """Targets of `jmp [reg*4 + table]` from the table contents, so the generated
    switch does not depend on references Ghidra may have lost."""
    sizes = {}
    for line in tables_path.read_text().split("\n"):
        p = line.split()
        if len(p) >= 4 and p[0] == "T" and p[3] == "code":
            sizes[int(p[1], 16)] = int(p[2])
    n = 0
    for ins in insns.values():
        if not any(o.name == "BRANCHIND" for o in ins.ops):
            continue
        m = re.search(r"\[\w+\*0x4 \+ 0x([0-9a-f]+)\]", ins.text)
        if not m or int(m.group(1), 16) not in sizes:
            continue
        base = int(m.group(1), 16)
        targets = []
        for i in range(sizes[base]):
            off = base - text_va + 4 * i
            v = int.from_bytes(text[off:off + 4], "little")
            if v and v not in targets:
                targets.append(v)
        ins.flows = sorted(set(ins.flows) | set(targets))
        n += 1
    return n


STRUCTURAL_SMC: set[int] = set()


def parse_smc_vn(s: str) -> Vn | None:
    if s.startswith("smc:"):
        _, field, csize, fsize, ext = s.split(":")
        return Vn("smc", int(field, 16), int(csize), int(fsize), ext == "s")
    return parse_vn(s)


def apply_smc(insns: dict[int, Insn], path: Path) -> tuple[int, set[int]]:
    """Replace the p-code of runtime-patched instructions with the version from
    SmcFields.java, in which constants derived from patched fields are reads
    of those fields. Returns the number of rewritten instructions and the
    instructions whose encoding itself is patched (they trap at runtime)."""
    rewritten: dict[int, list[Op]] = {}
    structural: set[int] = set()
    cur = None
    for line in path.read_text().split("\n"):
        p = line.split()
        if not p or p[0].startswith("#"):
            continue
        if p[0] == "X":
            structural.add(int(p[1], 16))
            cur = None
        elif p[0] == "V":
            cur = int(p[1], 16)
            rewritten[cur] = []
        elif p[0] == "P" and cur is not None:
            rewritten[cur].append(Op(p[1], parse_smc_vn(p[2]), [parse_smc_vn(x) for x in p[3:]]))
    for a, ops in rewritten.items():
        if a not in insns:
            raise SystemExit(f"smc: instruction {a:#x} not in the p-code export")
        old = insns[a].ops
        flow = [o.name for o in old if o.name in ("BRANCH", "CBRANCH", "CALL", "RETURN", "BRANCHIND", "CALLIND")]
        if flow != [o.name for o in ops if o.name in ("BRANCH", "CBRANCH", "CALL", "RETURN", "BRANCHIND", "CALLIND")]:
            raise SystemExit(f"smc: control flow of {a:#x} differs between the variants")
        insns[a].ops = ops
    return len(rewritten), structural

# ----------------------------------------------------------------------------
# expression helpers


def mask(size: int) -> str:
    return "0xffffffffffffffffULL" if size >= 8 else f"0x{(1 << (8 * size)) - 1:x}ULL"


class FuncGen:
    def __init__(self, entry: int, insns: dict[int, Insn], entries: set[int], names: dict[int, str]):
        self.entry = entry
        self.insns = insns
        self.entries = entries
        self.names = names
        self.uniques: set[int] = set()
        self.doubles: set[int] = set()
        self.labels: set[int] = set()
        self.calls: set[int] = set()
        self.problems: list[str] = []
        self.structural_smc: set[int] = STRUCTURAL_SMC

    # -- flow ---------------------------------------------------------------
    def body(self) -> list[int]:
        """Addresses of all instructions reachable from the entry without
        entering another function's entry."""
        seen, todo = set(), [self.entry]
        while todo:
            a = todo.pop()
            if a in seen:
                continue
            if a != self.entry and a in self.entries:
                continue
            ins = self.insns.get(a)
            if ins is None:
                self.problems.append(f"flow reaches {a:#x} which is not disassembled")
                continue
            seen.add(a)
            for op in ins.ops:
                if op.name in ("BRANCH", "CBRANCH") and op.ins[0].space == "ram":
                    todo.append(op.ins[0].off)
            if any(op.name == "BRANCHIND" for op in ins.ops):
                todo.extend(ins.flows)
            if not ins.ops or ins.ops[-1].name not in ("BRANCH", "RETURN", "BRANCHIND"):
                todo.append(ins.fallthrough)
        return sorted(seen)

    # -- varnodes -------------------------------------------------------------
    def rd(self, v: Vn) -> str:
        if v.space == "smc":
            raw = f"MEM_RD(0x{v.off:x}u, {v.field_size})"
            return f"((uint64_t)SEXT({raw}, {v.field_size}) & {mask(v.size)})" if v.sign else raw
        if v.space == "const":
            return f"0x{v.off & ((1 << (8 * min(v.size, 8))) - 1):x}ULL"
        if v.size == 10:
            if v.space == "register":
                return f"RD_F80(0x{v.off:x})"
            if v.space == "unique":
                self.doubles.add(v.off)
                return f"d_{v.off:x}"
            raise ValueError(f"10-byte varnode in {v.space}")
        if v.space == "register":
            return f"RD_REG(0x{v.off:x}, {v.size})"
        if v.space == "unique":
            self.uniques.add(v.off)
            return f"u_{v.off:x}"
        if v.space == "ram":
            return f"MEM_RD(0x{v.off:x}u, {v.size})"
        raise ValueError(f"unknown space {v.space}")

    def wr(self, v: Vn, expr: str) -> str:
        if v.size == 10:
            if v.space == "register":
                return f"WR_F80(0x{v.off:x}, {expr});"
            self.doubles.add(v.off)
            return f"d_{v.off:x} = {expr};"
        if v.space == "register":
            return f"WR_REG(0x{v.off:x}, {v.size}, {expr});"
        if v.space == "unique":
            self.uniques.add(v.off)
            return f"u_{v.off:x} = ({expr}) & {mask(v.size)};"
        if v.space == "ram":
            return f"MEM_WR(0x{v.off:x}u, {v.size}, {expr});"
        raise ValueError(f"cannot write {v.space}")

    def sx(self, v: Vn) -> str:
        """Operand sign-extended to int64."""
        return f"SEXT({self.rd(v)}, {v.size})"

    # -- control transfer to an address ----------------------------------------
    def goto(self, target: int, body: set[int]) -> str:
        if target in body:
            self.labels.add(target)
            return f"goto L_{target:x};"
        if target in self.entries:
            self.calls.add(target)
            return f"{{ f_{target:x}(); return; }}"
        self.problems.append(f"branch to {target:#x} outside any function")
        return f'RT_FAIL("branch to unknown 0x{target:x}");'

    # -- one instruction ---------------------------------------------------------
    def insn(self, ins: Insn, body: set[int], nxt: int | None) -> list[str]:
        out = [f"  /* {ins.addr:08x}: {ins.text} */"]
        if ins.addr in self.labels_needed:
            out.append(f" L_{ins.addr:x}:")
        if ins.addr in self.structural_smc:
            out.append(f'    RT_FAIL("instruction at 0x{ins.addr:x} has a runtime-patched encoding (not supported)");')
        # p-code relative branch targets (inside one instruction)
        rel_targets = set()
        for i, op in enumerate(ins.ops):
            if op.name in ("BRANCH", "CBRANCH") and op.ins[0].space == "const":
                rel_targets.add(i + _s32(op.ins[0].off))
        for i, op in enumerate(ins.ops):
            if i in rel_targets:
                out.append(f" P_{ins.addr:x}_{i}:;")
            out.append("    " + self.op(ins, i, op, body))
        if len(ins.ops) in rel_targets:
            out.append(f" P_{ins.addr:x}_{len(ins.ops)}:;")
        last = ins.ops[-1].name if ins.ops else None
        if last not in ("BRANCH", "RETURN", "BRANCHIND"):
            ft = ins.fallthrough
            if nxt != ft:
                out.append("    " + self.goto(ft, body))
        return out

    def op(self, ins: Insn, i: int, op: Op, body: set[int]) -> str:
        n, o, a = op.name, op.out, op.ins
        r = self.rd
        if n == "COPY":
            return self.wr(o, r(a[0]))
        if n == "LOAD":
            return self.wr(o, f"MEM_RD((uint32_t){r(a[1])}, {o.size})")
        if n == "STORE":
            return f"MEM_WR((uint32_t){r(a[1])}, {a[2].size}, {r(a[2])});"
        if n in ("INT_ADD", "INT_SUB", "INT_MULT", "INT_AND", "INT_OR", "INT_XOR"):
            sym = {"INT_ADD": "+", "INT_SUB": "-", "INT_MULT": "*", "INT_AND": "&", "INT_OR": "|", "INT_XOR": "^"}[n]
            return self.wr(o, f"({r(a[0])} {sym} {r(a[1])})")
        if n == "INT_DIV":
            return self.wr(o, f"RT_UDIV({r(a[0])}, {r(a[1])})")
        if n == "INT_REM":
            return self.wr(o, f"RT_UREM({r(a[0])}, {r(a[1])})")
        if n == "INT_SDIV":
            return self.wr(o, f"(uint64_t)RT_SDIV({self.sx(a[0])}, {self.sx(a[1])})")
        if n == "INT_SREM":
            return self.wr(o, f"(uint64_t)RT_SREM({self.sx(a[0])}, {self.sx(a[1])})")
        if n == "INT_LEFT":
            return self.wr(o, f"SHL({r(a[0])}, {r(a[1])})")
        if n == "INT_RIGHT":
            return self.wr(o, f"SHR({r(a[0])}, {r(a[1])})")
        if n == "INT_SRIGHT":
            return self.wr(o, f"SAR({self.sx(a[0])}, {r(a[1])})")
        if n == "INT_2COMP":
            return self.wr(o, f"(0 - {r(a[0])})")
        if n == "INT_NEGATE":
            return self.wr(o, f"(~{r(a[0])})")
        if n == "INT_ZEXT":
            return self.wr(o, r(a[0]))
        if n == "INT_SEXT":
            return self.wr(o, f"(uint64_t){self.sx(a[0])}")
        if n == "INT_EQUAL":
            return self.wr(o, f"({r(a[0])} == {r(a[1])})")
        if n == "INT_NOTEQUAL":
            return self.wr(o, f"({r(a[0])} != {r(a[1])})")
        if n == "INT_LESS":
            return self.wr(o, f"({r(a[0])} < {r(a[1])})")
        if n == "INT_LESSEQUAL":
            return self.wr(o, f"({r(a[0])} <= {r(a[1])})")
        if n == "INT_SLESS":
            return self.wr(o, f"({self.sx(a[0])} < {self.sx(a[1])})")
        if n == "INT_SLESSEQUAL":
            return self.wr(o, f"({self.sx(a[0])} <= {self.sx(a[1])})")
        if n == "INT_CARRY":
            return self.wr(o, f"CARRY({r(a[0])}, {r(a[1])}, {a[0].size})")
        if n == "INT_SCARRY":
            return self.wr(o, f"SCARRY({r(a[0])}, {r(a[1])}, {a[0].size})")
        if n == "INT_SBORROW":
            return self.wr(o, f"SBORROW({r(a[0])}, {r(a[1])}, {a[0].size})")
        if n == "BOOL_NEGATE":
            return self.wr(o, f"(!{r(a[0])})")
        if n in ("BOOL_AND", "BOOL_OR", "BOOL_XOR"):
            sym = {"BOOL_AND": "&", "BOOL_OR": "|", "BOOL_XOR": "^"}[n]
            return self.wr(o, f"(({r(a[0])} {sym} {r(a[1])}) & 1)")
        if n == "POPCOUNT":
            return self.wr(o, f"RT_POPCOUNT({r(a[0])})")
        if n == "SUBPIECE":
            return self.wr(o, f"SHR({r(a[0])}, {8 * a[1].off})")
        if n == "PIECE":
            return self.wr(o, f"(SHL({r(a[0])}, {8 * a[1].size}) | {r(a[1])})")
        if n == "INT2FLOAT":
            return self.wr(o, f"(double){self.sx(a[0])}")
        if n == "FLOAT_SQRT":
            return self.wr(o, f"sqrt({r(a[0])})")
        if n == "ROUND":            # x87 default rounding: to nearest even
            return self.wr(o, f"nearbyint({r(a[0])})")
        if n == "TRUNC":
            return self.wr(o, f"(uint64_t)(int64_t)({r(a[0])})")
        if n == "BRANCH":
            t = a[0]
            if t.space == "const":
                return f"goto P_{ins.addr:x}_{i + _s32(t.off)};"
            return self.goto(t.off, body)
        if n == "CBRANCH":
            t, c = a
            if t.space == "const":
                return f"if ({r(c)}) goto P_{ins.addr:x}_{i + _s32(t.off)};"
            return f"if ({r(c)}) {self.goto(t.off, body)}"
        if n == "CALL":
            t = a[0].off
            if t not in self.entries:
                self.problems.append(f"call to non-entry {t:#x}")
            self.calls.add(t)
            return f"RT_CALL(f_{t:x}, 0x{ins.fallthrough:x}u);"
        if n == "CALLIND":
            return (f"rt_callind((uint32_t){r(a[0])}, 0x{ins.fallthrough:x}u); "
                    f"if (cpu.ret_addr != 0x{ins.fallthrough:x}u) return;")
        if n == "BRANCHIND":
            cases = " ".join(f"case 0x{t:x}: {self.goto(t, body)}" for t in ins.flows)
            return f"switch ((uint32_t){r(a[0])}) {{ {cases} default: rt_jumpind((uint32_t){r(a[0])}); return; }}"
        if n == "RETURN":
            return f"RT_RET((uint32_t){r(a[0])}); return;"
        if n == "CALLOTHER":
            kind = a[0].off
            if kind in (0x11, 0x12):     # LOCK / UNLOCK
                return "/* lock */;"
            args = ", ".join(f"(uint64_t){r(v)}" for v in a[1:] if v.size <= 8) or "0"
            return f"rt_callother(0x{kind:x}, 0x{ins.addr:x}u, {args});"
        self.problems.append(f"unsupported p-code {n} at {ins.addr:#x}")
        return f'RT_FAIL("unsupported {n}");'

    def generate(self) -> str:
        addrs = self.body()
        body = set(addrs)
        # first pass to collect labels
        self.labels = set()
        self.labels_needed = set()
        for k, a in enumerate(addrs):
            nxt = addrs[k + 1] if k + 1 < len(addrs) else None
            self.insn(self.insns[a], body, nxt)
        self.labels_needed = self.labels | {self.entry}
        lines = []
        for k, a in enumerate(addrs):
            nxt = addrs[k + 1] if k + 1 < len(addrs) else None
            lines += self.insn(self.insns[a], body, nxt)
        decl = []
        if self.uniques:
            decl.append("  uint64_t " + ", ".join(f"u_{u:x} = 0" for u in sorted(self.uniques)) + ";")
        if self.doubles:
            decl.append("  double " + ", ".join(f"d_{u:x} = 0" for u in sorted(self.doubles)) + ";")
        name = self.names.get(self.entry, "")
        prefix = "ref" if self.entry in REPLACED else "f"
        head = [f"/* {name} */", f"void {prefix}_{self.entry:x}(void)", "{"] + decl
        if addrs and addrs[0] != self.entry:
            head.append(f"  goto L_{self.entry:x};")
        return "\n".join(head + lines + ["}", ""])


def _s32(v: int) -> int:
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


REPLACED: dict[int, str] = {}


def write_if_changed(path: Path, text: str) -> None:
    """Leave files whose content is unchanged alone (no needless rebuilds)."""
    if path.exists() and path.read_text() == text:
        return
    path.write_text(text)


def load_replaced(path: Path) -> dict[int, str]:
    """HAND(<hex>, <name>) lines of src/core/replaced.h."""
    out = {}
    for m in re.finditer(r"^\s*HAND\(\s*([0-9a-fA-F]+)\s*,\s*(\w+)\s*\)", path.read_text(), re.M):
        out[int(m.group(1), 16)] = m.group(2)
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pcode", type=Path)
    ap.add_argument("-o", "--out", type=Path, required=True)
    ap.add_argument("--only", help="comma-separated hex entries to generate (plus their callees)")
    ap.add_argument("--files", type=int, default=32, help="number of C files to split into")
    ap.add_argument("--entries", type=Path,
                    help="extra entry points (output of re/scripts/find_seeds.py: 'F|D <hex>' lines); "
                         "code pointers in tables are targets of computed calls")
    ap.add_argument("--smc", type=Path, help="runtime-patched code fields (output of SmcFields.java)")
    ap.add_argument("--tables", type=Path, help="inline tables (re/inline_tables.txt) for computed jump targets")
    ap.add_argument("--text", type=Path, help="code section image (assets/data/ASTRO_TEXT.BIN, VA 0x401000)")
    ap.add_argument("--replaced", type=Path,
                    help="functions rewritten by hand (src/core/replaced.h): generated as ref_<addr>")
    args = ap.parse_args(argv)
    if args.replaced:
        REPLACED.update(load_replaced(args.replaced))

    names, insns = load_pcode(args.pcode)
    if args.smc:
        n, structural = apply_smc(insns, args.smc)
        STRUCTURAL_SMC.update(structural)
        print(f"self-modifying code: {n} instructions read patched fields from memory, {len(structural)} trap")
    if args.tables and args.text:
        n = resolve_jump_tables(insns, args.tables, args.text.read_bytes())
        print(f"jump tables: {n} computed jumps resolved from the code image")
    entries = set(a for a in names if CORE_START <= a < CORE_END)
    # code addresses stored as values (handler pointers called later) are entries
    for ins in insns.values():
        if ins.text.startswith(("CALL", "JMP", "F")):
            continue
        for op in ins.ops:
            if op.name in ("CALL", "BRANCH", "CBRANCH"):
                continue
            for v in op.ins:
                if v.space == "const" and v.size == 4 and v.off in insns and v.off != ins.addr:
                    entries.add(v.off)
    extra = set()
    if args.entries:
        for line in args.entries.read_text().split("\n"):
            if line.strip():
                a = int(line.split()[1], 16)
                if CORE_START <= a < CORE_END:
                    extra.add(a)
        entries |= extra
    # every direct call target is an entry
    for ins in insns.values():
        for op in ins.ops:
            if op.name == "CALL" and op.ins[0].space == "ram":
                entries.add(op.ins[0].off)
    entries = {e for e in entries if e in insns}

    if args.only:
        todo = [int(x, 16) for x in args.only.split(",")]
    else:
        todo = sorted(entries)

    gens: dict[int, FuncGen] = {}
    while todo:
        e = todo.pop()
        if e in gens:
            continue
        g = FuncGen(e, insns, entries, names)
        g.code = g.generate()
        gens[e] = g
        for c in g.calls:
            if c not in gens:
                todo.append(c)

    args.out.mkdir(parents=True, exist_ok=True)
    order = sorted(gens)
    missing = sorted(set(REPLACED) - set(order))
    if missing:
        print("replaced functions that are not core functions: " + ", ".join(f"{a:x}" for a in missing))
        return 1
    proto = "\n".join(f"void f_{e:x}(void);" for e in order)
    proto += "\n/* generated versions of the hand-written functions (src/core/replaced.h) */\n"
    proto += "\n".join(f"void ref_{e:x}(void);" for e in sorted(REPLACED))
    listed = " ".join(f"{e:x}" for e in sorted(REPLACED))
    proto += f'\n#define GEN_REPLACED "{listed}"\n'
    write_if_changed(args.out / "gen_funcs.h", "/* generated - do not edit */\n#pragma once\n" + proto + "\n")
    table = ",\n".join(f"  {{0x{e:x}u, f_{e:x}}}" for e in order)
    write_if_changed(args.out / "gen_table.c", 
        '/* generated - do not edit */\n#include "recomp_rt.h"\n#include "gen_funcs.h"\n'
        f"const rt_func_entry rt_func_table[] = {{\n{table}\n}};\n"
        f"const unsigned rt_func_count = {len(order)};\n")
    nfiles = max(1, min(args.files, len(order)))
    per = (len(order) + nfiles - 1) // nfiles
    for k in range(nfiles):
        chunk = order[k * per:(k + 1) * per]
        if not chunk:
            continue
        src = ['/* generated from ASTRO.exe - do not edit, do not commit */',
               '#include "recomp_rt.h"', '#include "gen_funcs.h"', '#include <math.h>', '']
        src += [gens[e].code for e in chunk]
        write_if_changed(args.out / f"gen_{k:02d}.c", "\n".join(src))

    problems = [(e, p) for e, g in gens.items() for p in g.problems]
    ninsn = sum(len(g.body()) for g in gens.values())
    print(f"generated {len(gens)} functions ({ninsn} instructions incl. duplicates) into {args.out}")
    cnt = collections.Counter(p.split(" 0x")[0] for _e, p in problems)
    for k, v in cnt.most_common(10):
        print(f"  {v:5d} x {k}")
    if problems:
        (args.out / "problems.txt").write_text("\n".join(f"{e:x}: {p}" for e, p in problems) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
