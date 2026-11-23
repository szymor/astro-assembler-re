// Export raw p-code for every instruction in [start, end) plus the function
// list, as input for re/recomp/recomp.py.
// Args: <out-file> <start-hex> <end-hex>
//
// Output lines:
//   F <entry> <name>                      function entry
//   I <addr> <len> <bytes> <disassembly>  instruction
//   J <addr> <target>...                  known targets of a computed jump/call
//   P <op> <out|-> <in>...                p-code op of the preceding instruction
// Varnodes are written as space:offset:size (offset in hex).
// @category AstroAssembler

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.pcode.PcodeOp;
import ghidra.program.model.pcode.Varnode;

import java.io.PrintWriter;

public class ExportPcode extends GhidraScript {
    private static String vn(Varnode v) {
        if (v == null) return "-";
        return v.getAddress().getAddressSpace().getName() + ":" + Long.toHexString(v.getOffset()) + ":" + v.getSize();
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        Address start = toAddr(Long.parseLong(args[1], 16));
        Address end = toAddr(Long.parseLong(args[2], 16));
        int ni = 0, np = 0;
        try (PrintWriter out = new PrintWriter(args[0])) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(start, true)) {
                if (f.getEntryPoint().compareTo(end) >= 0) break;
                out.println("F " + f.getEntryPoint() + " " + f.getName());
            }
            Instruction ins = getInstructionAt(start);
            if (ins == null) ins = getInstructionAfter(start);
            for (; ins != null && ins.getAddress().compareTo(end) < 0; ins = getInstructionAfter(ins)) {
                StringBuilder hex = new StringBuilder();
                for (byte b : ins.getBytes()) hex.append(String.format("%02x", b & 0xff));
                out.println("I " + ins.getAddress() + " " + ins.getLength() + " " + hex + " " + ins);
                boolean computed = false;
                for (PcodeOp op : ins.getPcode()) {
                    if (op.getOpcode() == PcodeOp.BRANCHIND || op.getOpcode() == PcodeOp.CALLIND) computed = true;
                }
                if (computed) {
                    StringBuilder j = new StringBuilder("J " + ins.getAddress());
                    for (Address t : ins.getFlows()) j.append(' ').append(t);
                    out.println(j);
                }
                for (PcodeOp op : ins.getPcode()) {
                    StringBuilder s = new StringBuilder("P ").append(op.getMnemonic()).append(' ').append(vn(op.getOutput()));
                    for (Varnode in : op.getInputs()) s.append(' ').append(vn(in));
                    out.println(s);
                    np++;
                }
                ni++;
            }
        }
        println("exported " + ni + " instructions, " + np + " p-code ops to " + args[0]);
    }
}
