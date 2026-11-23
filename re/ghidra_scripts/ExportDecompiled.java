// Decompile every function whose entry lies in [start, end) and write the
// C output to a single file.
// Args: <out-file> <start-hex> <end-hex>
// @category AstroAssembler

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.PrintWriter;

public class ExportDecompiled extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        Address start = toAddr(Long.parseLong(args[1], 16));
        Address end = toAddr(Long.parseLong(args[2], 16));
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        int n = 0, failed = 0;
        try (PrintWriter out = new PrintWriter(args[0])) {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(start, true);
            while (it.hasNext()) {
                Function f = it.next();
                if (f.getEntryPoint().compareTo(end) >= 0) break;
                if (f.isThunk()) continue;
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                out.println("// ==== " + f.getName() + " @ " + f.getEntryPoint());
                if (r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
                else { out.println("// decompile failed: " + r.getErrorMessage()); failed++; }
                n++;
            }
        }
        di.dispose();
        println("exported " + n + " functions (" + failed + " failed) to " + args[0]);
    }
}
