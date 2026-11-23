// Report instruction coverage of .text per region, and list the largest
// gaps that are neither instructions nor 0xCC padding.
// @category AstroAssembler

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;

import java.util.ArrayList;
import java.util.List;

public class CoverageReport extends GhidraScript {
    @Override
    protected void run() throws Exception {
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        long start = text.getStart().getOffset();
        long end = start + 0x69558;
        long[][] regions = {{0x401000, 0x401490}, {0x401490, 0x408c44}, {0x408c44, 0x447da7}, {0x447da7, 0x457a00}, {0x457a00, end}};
        byte[] buf = new byte[(int) (end - start)];
        text.getBytes(text.getStart(), buf);
        boolean[] code = new boolean[buf.length];
        for (Instruction ins = getFirstInstruction(); ins != null; ins = getInstructionAfter(ins)) {
            long a = ins.getAddress().getOffset();
            if (a < start || a >= end) continue;
            for (int i = 0; i < ins.getLength() && a - start + i < buf.length; i++) code[(int) (a - start + i)] = true;
        }
        for (long[] r : regions) {
            long c = 0, pad = 0, other = 0;
            for (long a = r[0]; a < r[1]; a++) {
                int i = (int) (a - start);
                if (code[i]) c++; else if ((buf[i] & 0xff) == 0xCC) pad++; else other++;
            }
            println(String.format("%08x-%08x code=%d pad=%d unknown=%d (%.1f%% of non-pad is code)", r[0], r[1], c, pad, other,
                    100.0 * c / Math.max(1, c + other)));
        }
        List<long[]> gaps = new ArrayList<>();
        long gs = -1;
        for (int i = 0; i <= buf.length; i++) {
            boolean unk = i < buf.length && !code[i] && (buf[i] & 0xff) != 0xCC;
            if (unk && gs < 0) gs = i;
            if (!unk && gs >= 0) { gaps.add(new long[]{start + gs, i - gs}); gs = -1; }
        }
        gaps.sort((x, y) -> Long.compare(y[1], x[1]));
        println("largest unknown gaps:");
        for (int i = 0; i < Math.min(25, gaps.size()); i++)
            println(String.format("  %08x len %d", gaps.get(i)[0], gaps.get(i)[1]));
        println("total gaps: " + gaps.size());
    }
}
