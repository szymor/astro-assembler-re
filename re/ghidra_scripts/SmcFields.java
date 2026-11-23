// Prepare runtime-patched ("self-modifying") instructions for the recompiler.
//
// For every instruction containing patched fields, decode it twice (without
// modifying the program) with different sentinel values in all of its fields.
// The p-code of the first variant is emitted, with every constant that follows
// a field's sentinel marked as a read of that field. Using a nonzero sentinel
// matters: Ghidra omits e.g. a zero displacement from the p-code entirely.
//
// Args: <in-file> <out-file>
//   in:  lines "<field-addr-hex> <size>"
//   out: "V <insn-addr> <fields...>"        followed by the instruction's p-code:
//        "P <op> <out|-> <in>..."           varnodes as in ExportPcode.java, and
//                                           smc:<field>:<const-size>:<field-size>:<z|s>
//                                           for constants derived from a field
//        "X <insn-addr> <reason> <text>"    the field changes the encoding itself
// @category AstroAssembler

import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import ghidra.app.util.PseudoInstruction;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.Varnode;

import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class SmcFields extends GhidraScript {
    private static long sext(long v, int bytes) {
        int s = 64 - 8 * bytes;
        return (v << s) >> s;
    }

    private static String vn(Varnode v) {
        if (v == null) return "-";
        return v.getAddress().getAddressSpace().getName() + ":" + Long.toHexString(v.getOffset()) + ":" + v.getSize();
    }

    /** Distinct sentinel per (variant, field index, size); never 0. */
    private static long sentinel(int variant, int idx, int size) {
        long base = variant == 0 ? 0x5b3c2d1eL : 0x27496a0bL;
        long v = base ^ (0x01010101L * (idx + 1) * 0x11);
        long m = size >= 8 ? -1L : (1L << (8 * size)) - 1;
        v &= m;
        if (size == 1 && (v & 0x80) != 0) v ^= 0x40;       // keep imm8 sentinels small and distinct
        return v == 0 ? 1 : v;
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        PseudoDisassembler pd = new PseudoDisassembler(currentProgram);

        Map<Address, List<long[]>> byInsn = new LinkedHashMap<>();   // insn -> [fieldAddr, size]
        for (String line : Files.readAllLines(Paths.get(args[0]))) {
            String[] p = line.trim().split("\\s+");
            if (p.length < 2 || p[0].startsWith("#")) continue;
            long field = Long.parseLong(p[0], 16);
            Instruction ins = currentProgram.getListing().getInstructionContaining(toAddr(field));
            if (ins == null) { printerr("no instruction at " + p[0]); continue; }
            byInsn.computeIfAbsent(ins.getAddress(), k -> new ArrayList<>()).add(new long[]{field, Integer.parseInt(p[1])});
        }

        int ok = 0, bad = 0;
        try (PrintWriter out = new PrintWriter(args[1])) {
            for (Map.Entry<Address, List<long[]>> e : byInsn.entrySet()) {
                Instruction ins = currentProgram.getListing().getInstructionAt(e.getKey());
                List<long[]> fields = e.getValue();
                byte[] orig = ins.getBytes();
                PcodeOp[][] var = new PcodeOp[2][];
                for (int k = 0; k < 2; k++) {
                    byte[] b = orig.clone();
                    for (int f = 0; f < fields.size(); f++) {
                        int off = (int) (fields.get(f)[0] - ins.getAddress().getOffset());
                        int size = (int) fields.get(f)[1];
                        long s = sentinel(k, f, size);
                        for (int i = 0; i < size; i++) b[off + i] = (byte) (s >> (8 * i));
                    }
                    PseudoInstruction pi = pd.disassemble(ins.getAddress(), b);
                    var[k] = pi == null ? null : pi.getPcode();
                }
                String reason = null;
                if (var[0] == null || var[1] == null || var[0].length != var[1].length) reason = "structure-changes";
                StringBuilder body = new StringBuilder();
                int marked = 0;
                for (int i = 0; reason == null && i < var[0].length; i++) {
                    PcodeOp a = var[0][i], b = var[1][i];
                    if (a.getOpcode() != b.getOpcode() || a.getNumInputs() != b.getNumInputs()) { reason = "structure-changes"; break; }
                    StringBuilder s = new StringBuilder("P ").append(a.getMnemonic()).append(' ').append(vn(a.getOutput()));
                    for (int j = 0; j < a.getNumInputs(); j++) {
                        Varnode va = a.getInput(j), vb = b.getInput(j);
                        String text = vn(va);
                        if (va.isConstant() && vb.isConstant() && va.getOffset() != vb.getOffset()) {
                            int cs = va.getSize();
                            long m = cs >= 8 ? -1L : (1L << (8 * cs)) - 1;
                            String match = null;
                            for (int f = 0; f < fields.size() && match == null; f++) {
                                int size = (int) fields.get(f)[1];
                                long s0 = sentinel(0, f, size), s1 = sentinel(1, f, size);
                                if ((va.getOffset() & m) == (s0 & m) && (vb.getOffset() & m) == (s1 & m))
                                    match = "z";
                                else if ((va.getOffset() & m) == (sext(s0, size) & m) && (vb.getOffset() & m) == (sext(s1, size) & m))
                                    match = "s";
                                if (match != null) {
                                    text = "smc:" + Long.toHexString(fields.get(f)[0]) + ":" + cs + ":" + size + ":" + match;
                                    marked++;
                                }
                            }
                            if (match == null) { reason = "derived-constant"; break; }
                        } else if (!vn(va).equals(vn(vb))) {
                            reason = "structure-changes";
                            break;
                        }
                        s.append(' ').append(text);
                    }
                    body.append(s).append('\n');
                }
                if (reason == null && marked == 0) reason = "no-constant";
                if (reason != null) {
                    out.println("X " + ins.getAddress() + " " + reason + " " + ins);
                    bad++;
                } else {
                    StringBuilder hdr = new StringBuilder("V " + ins.getAddress());
                    for (long[] f : fields) hdr.append(' ').append(Long.toHexString(f[0])).append('/').append(f[1]);
                    out.println(hdr);
                    out.print(body);
                    ok++;
                }
            }
        }
        println("instructions rewritten=" + ok + " not mappable=" + bad);
    }
}
