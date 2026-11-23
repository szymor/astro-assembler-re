// Apply names and comments from re/symbols.tsv.
// Line format: address<TAB>kind(f|l)<TAB>name[<TAB>comment]; '#' starts a comment line.
// Functions are created if missing; labels become primary symbols.
// @category AstroAssembler

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;

import java.nio.file.Files;
import java.nio.file.Paths;

public class ApplySymbols extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        int funcs = 0, labels = 0, errors = 0;
        for (String raw : Files.readAllLines(Paths.get(args[0]))) {
            if (raw.isBlank() || raw.startsWith("#")) continue;
            String[] c = raw.split("\t");
            if (c.length < 3) { printerr("bad line: " + raw); errors++; continue; }
            Address a = toAddr(Long.parseLong(c[0], 16));
            String name = c[2];
            String comment = c.length > 3 ? c[3] : null;
            try {
                if (c[1].equals("f")) {
                    Function f = getFunctionAt(a);
                    if (f == null) {
                        if (getInstructionAt(a) == null) disassemble(a);
                        f = createFunction(a, name);
                    }
                    if (f == null) { printerr("no function at " + a); errors++; continue; }
                    f.setName(name, SourceType.USER_DEFINED);
                    if (comment != null) f.setComment(comment);
                    funcs++;
                } else {
                    Symbol s = getSymbolAt(a);
                    if (s == null || !s.getName().equals(name)) {
                        createLabel(a, name, true, SourceType.USER_DEFINED);
                    }
                    // drop stale user-defined names left at this address by earlier runs
                    for (Symbol old : currentProgram.getSymbolTable().getSymbols(a)) {
                        if (!old.getName().equals(name) && old.getSource() == SourceType.USER_DEFINED) old.delete();
                    }
                    if (comment != null) setPlateComment(a, comment);
                    labels++;
                }
            } catch (Exception e) {
                printerr(c[0] + " " + name + ": " + e.getMessage());
                errors++;
            }
        }
        println(String.format("functions=%d labels=%d errors=%d", funcs, labels, errors));
    }
}
