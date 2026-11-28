#!/usr/bin/env python3
"""Boundary audit for stage C (docs/plan.md): which registers and flags the
callers of a core function use after the call before overwriting them.

A subsystem rewritten in plain C (HAND_STATE in src/core/replaced.h) is
checked by the game state it leaves, not by its registers, so its callers
must not depend on the registers or flags it returns (or those become its
return values). For every call site and tail jump this follows the caller's
code after the call, path by path, and reports the registers (by byte, as
EAX/AX/AL...) and flags read before they are written. A CALL along the way
counts as reading what its callee reads first (computed the same way); a RET
with registers still unwritten passes them to the caller's caller, which is
reported (and audited one level up with --depth); computed calls are
reported as unknown. Pushes do not count as uses (the save and restore
pattern), so a value that travels through the stack is missed: this is a
reading aid, and the differential build's register poisoning after every
game-state check is the exact test for the recorded paths.

usage: boundary_audit.py <hex address>... [--depth N] [--pcode build/recomp/core.pcode]
"""
import argparse, sys
from functools import lru_cache
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "re/recomp"))
import recomp as R  # noqa: E402

REGS = {0x00: "EAX", 0x04: "ECX", 0x08: "EDX", 0x0c: "EBX", 0x10: "ESP", 0x14: "EBP", 0x18: "ESI", 0x1c: "EDI"}
FLAGS = {0x200: "CF", 0x202: "PF", 0x204: "AF", 0x206: "ZF", 0x207: "SF", 0x20a: "DF", 0x20b: "OF"}
IGNORED = set(range(0x10, 0x14)) | {0x20a}      # ESP (checked by the harness) and DF (always clear)


def tracked(off):
    return (off < 0x20 or off in FLAGS) and off not in IGNORED


def name(byte_set):
    out = []
    for base, n in REGS.items():
        b = {o - base for o in byte_set if base <= o < base + 4}
        if not b:
            continue
        byte_regs = n in ("EAX", "ECX", "EDX", "EBX")
        short = {frozenset({0, 1, 2, 3}): n, frozenset({0, 1}): n[1:], frozenset({2, 3}): n + "[hi]",
                 frozenset({0}): n[1] + "L" if byte_regs else None,
                 frozenset({1}): n[1] + "H" if byte_regs else None}.get(frozenset(b))
        out.append(short or f"{n}[{','.join(map(str, sorted(b)))}]")
    out += [FLAGS[o] for o in sorted(byte_set) if o in FLAGS]
    return " ".join(out)


class Audit:
    def __init__(self, pcode):
        self.names, self.insns = R.load_pcode(pcode)
        R.apply_smc(self.insns, REPO / "re/smc_fields.txt")
        R.resolve_jump_tables(self.insns, REPO / "re/inline_tables.txt",
                              (REPO / "assets/data/ASTRO_TEXT.BIN").read_bytes())
        self.entries = set(a for a in self.names if R.CORE_START <= a < R.CORE_END)
        for ins in self.insns.values():
            for op in ins.ops:
                if op.name == "CALL" and op.ins[0].space == "ram":
                    self.entries.add(op.ins[0].off)
        self.bodies = {}
        for e in self.entries:
            if e in self.insns:
                self.bodies[e] = set(R.FuncGen(e, self.insns, self.entries, self.names).body())
        self.owner = {}
        for e, b in self.bodies.items():
            for a in b:
                self.owner.setdefault(a, set()).add(e)

    def effects(self, ins):
        """(registers read before written in this instruction, registers written, call target or None)."""
        rd, wr, call = set(), set(), None
        words = ins.text.replace(",", " ").split()
        saves = words[0] in ("PUSH", "PUSHAD", "PUSHFD", "PUSHF")              # save/restore, not a use
        zeroing = words[0] in ("XOR", "SUB", "SBB") and len(words) == 3 and words[1] == words[2] and words[0] != "SBB"
        saves = saves or zeroing                                               # XOR r, r only writes
        for op in ins.ops:
            if op.name == "CALL" and op.ins[0].space == "ram":
                call = op.ins[0].off
                continue
            for v in op.ins:
                if v and v.space == "register" and not saves:
                    for o in range(v.off, v.off + v.size):
                        if tracked(o) and o not in wr:
                            rd.add(o)
            if op.out is not None and op.out.space == "register":
                for o in range(op.out.off, op.out.off + op.out.size):
                    if tracked(o):
                        wr.add(o)
        return rd, wr, call

    def succ(self, ins, func):
        out = []
        last = ins.ops[-1].name if ins.ops else None
        for op in ins.ops:
            if op.name in ("BRANCH", "CBRANCH") and op.ins[0].space == "ram":
                out.append(op.ins[0].off)
        if any(op.name == "BRANCHIND" for op in ins.ops):
            out += ins.flows
        if last not in ("BRANCH", "RETURN", "BRANCHIND"):
            out.append(ins.fallthrough)
        return out

    def exposed(self, start, func, written=frozenset(), limit=600):
        """Registers read before written from `start` within `func`: (reads, returns-with, notes)."""
        reads, at_ret, notes = set(), set(), set()
        seen = set()
        stack = [(start, frozenset(written), 0)]
        while stack:
            a, w, steps = stack.pop()
            if (a, w) in seen or steps > limit:
                continue
            seen.add((a, w))
            ins = self.insns.get(a)
            if ins is None:
                continue
            if a != start and a in self.entries and a not in self.bodies.get(func, ()):
                ins_reads = self.inputs(a)                          # tail jump into another function
                if self.recording:
                    for o in ins_reads - w:
                        self.where.setdefault(o, f"jump into {a:x}")
                reads |= ins_reads - w
                continue
            rd, wr, call = self.effects(ins)
            if self.recording:
                for o in rd - w:
                    self.where.setdefault(o, f"{a:x} {ins.text}")
            reads |= rd - w
            if any(op.name == "CALLIND" for op in ins.ops):
                notes.add(f"computed call at {a:x} with {name(set(range(0, 0x20)) - w - IGNORED) or 'nothing'} unwritten")
            w2 = w | wr
            if call is not None:
                callee_in = self.inputs(call)
                if self.recording:
                    for o in callee_in - w2:
                        self.where.setdefault(o, f"{a:x} {ins.text} (its callee reads it)")
                reads |= self.inputs(call) - w2
                w2 = w2 | self.outputs_hint(call)
            if ins.ops and ins.ops[-1].name == "RETURN":
                at_ret |= set(o for o in list(range(0, 0x20)) + list(FLAGS) if tracked(o)) - w2
                continue
            for s in self.succ(ins, func):
                stack.append((s, frozenset(w2), steps + 1))
        return reads, at_ret, notes

    @lru_cache(maxsize=None)
    def inputs(self, func):
        """What a function reads before writing (its register inputs); cycles count as nothing."""
        if func in self._busy:
            return frozenset()
        self._busy.add(func)
        rec, self.recording = self.recording, False
        try:
            r, _, _ = self.exposed(func, func)
        finally:
            self._busy.discard(func)
            self.recording = rec
        return frozenset(r)

    @lru_cache(maxsize=None)
    def outputs_hint(self, func):
        """Registers a function writes on every path to its RET (a must-analysis
        over its instructions); a cycle through it counts as writing nothing."""
        if func in self._busy_out or func not in self.bodies:
            return frozenset()
        self._busy_out.add(func)
        try:
            body = self.bodies[func]
            ALL = frozenset(o for o in list(range(0, 0x20)) + list(FLAGS) if tracked(o))
            preds = {a: [] for a in body}
            for a in body:
                for s in self.succ(self.insns[a], func):
                    if s in preds:
                        preds[s].append(a)
            w_out = {a: ALL for a in body}
            changed = True
            while changed:
                changed = False
                for a in sorted(body):
                    ins = self.insns[a]
                    w_in = frozenset() if a == func else (
                        frozenset.intersection(*[w_out[p] for p in preds[a]]) if preds[a] else frozenset())
                    _, wr, call = self.effects(ins)
                    w = w_in | wr
                    if call is not None:
                        w |= self.outputs_hint(call)
                    if w != w_out[a]:
                        w_out[a] = w
                        changed = True
            rets = [w_out[a] for a in body if self.insns[a].ops and self.insns[a].ops[-1].name == "RETURN"]
            return frozenset.intersection(*rets) if rets else frozenset()
        finally:
            self._busy_out.discard(func)

    def call_sites(self, target):
        sites = []
        for a, ins in self.insns.items():
            for op in ins.ops:
                if op.name == "CALL" and op.ins[0].space == "ram" and op.ins[0].off == target:
                    sites.append(("call", a, ins.fallthrough))
                if op.name in ("BRANCH", "CBRANCH") and op.ins[0].space == "ram" and op.ins[0].off == target \
                        and target not in self.owner.get(a, set()):
                    sites.append(("jump", a, None))
            if (not ins.ops or ins.ops[-1].name not in ("BRANCH", "RETURN", "BRANCHIND")) \
                    and ins.fallthrough == target and target not in self.owner.get(a, set()):
                sites.append(("falls into", a, None))
        return sorted(sites, key=lambda s: s[1])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("addr", nargs="+")
    ap.add_argument("--depth", type=int, default=1, help="levels of callers to follow through RETs")
    ap.add_argument("--why", action="store_true", help="show where each reported register is first read")
    ap.add_argument("--pcode", type=Path, default=REPO / "build/recomp/core.pcode")
    args = ap.parse_args()
    au = Audit(args.pcode)
    au._busy = set()
    au._busy_out = set()
    au.where = {}
    au.recording = True
    ok = True

    def audit(target, depth, indent=""):
        nonlocal ok
        sites = au.call_sites(target)
        label = au.names.get(target, f"{target:x}")
        if not sites:
            print(f"{indent}{target:x} {label}: no direct callers (reached through pointers only?)")
        for kind, site, ret in sites:
            callers = sorted(au.owner.get(site, {site}))
            for func in callers:
                where = f"{site:x} in {au.names.get(func, f'{func:x}')}"
                if kind != "call":
                    print(f"{indent}  {kind} at {where}: the caller's own caller gets the registers (audit {func:x})")
                    if depth > 0:
                        audit(func, depth - 1, indent + "    ")
                    continue
                au.where = {}
                reads, at_ret, notes = au.exposed(ret, func)
                verdict = "clean" if not reads else "USES " + name(reads)
                if reads:
                    ok = False
                print(f"{indent}  call at {where}: {verdict}")
                if reads and args.why:
                    for o in sorted(reads):
                        print(f"{indent}    {name({o})}: first read at {au.where.get(o, '?')}")
                for n in sorted(notes):
                    print(f"{indent}    note: {n}")
                if at_ret and depth > 0:
                    print(f"{indent}    returns with {name(at_ret)} unwritten; its callers:")
                    audit(func, depth - 1, indent + "    ")

    for a in args.addr:
        audit(int(a, 16), args.depth)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
