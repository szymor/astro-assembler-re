// Repair tables embedded in the assembly core's code.
// Input file lines:
//   T <base-hex> <count> <code|data>  table of <count> dwords: clear any
//                                     instructions there and define pointers
//   C <addr-hex>                      address that must be an instruction start:
//                                     clear overlapping code, then disassemble
// The list for ASTRO.exe is re/inline_tables.txt (see docs/recompiler.md).
// @category AstroAssembler

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Instruction;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

public class FixInlineTables extends GhidraScript {
    private final List<Address[]> tableRanges = new ArrayList<>();

    @Override
    protected void run() throws Exception {
        List<String> lines = Files.readAllLines(Paths.get(getScriptArgs()[0]));
        int tables = 0, fixed = 0, failed = 0;
        List<Address> code = new ArrayList<>();
        tableRanges.clear();
        for (String line : lines) {
            if (line.startsWith("#")) continue;
            String[] p = line.trim().split("\\s+");
            if (p.length < 2) continue;
            Address a = toAddr(Long.parseLong(p[1], 16));
            if (p[0].equals("T")) {
                int n = Integer.parseInt(p[2]);
                Address end = a.add(4L * n - 1);
                // clear instructions overlapping the table (also ones starting before it)
                CodeUnit first = currentProgram.getListing().getCodeUnitContaining(a);
                Address from = first != null ? first.getMinAddress() : a;
                clearListing(from, end);
                for (int i = 0; i < n; i++) createData(a.add(4L * i), PointerDataType.dataType);
                tableRanges.add(new Address[]{a, end});
                if (from.compareTo(a) < 0) code.add(from);   // re-disassemble code cut before the table
                tables++;
            } else if (p[0].equals("C")) {
                code.add(a);
            }
        }
        for (Address a : code) {
            Instruction ins = getInstructionAt(a);
            if (ins != null) continue;
            CodeUnit cu = currentProgram.getListing().getCodeUnitContaining(a);
            boolean inTable = false;
            for (Address[] t : tableRanges) if (a.compareTo(t[0]) >= 0 && a.compareTo(t[1]) <= 0) inTable = true;
            if (inTable) { failed++; printerr(a + " lies inside a table"); continue; }
            // clear one maximum-length instruction (15 bytes), stopping at the next table
            Address end = a.add(14);
            for (Address[] t : tableRanges) if (t[0].compareTo(a) > 0 && t[0].compareTo(end) <= 0) end = t[0].subtract(1);
            Address from = cu != null && cu.getMinAddress().compareTo(a) < 0 ? cu.getMinAddress() : a;
            clearListing(from, end);
            DisassembleCommand cmd = new DisassembleCommand(a, null, true);
            cmd.applyTo(currentProgram, monitor);
            if (getInstructionAt(a) != null) fixed++; else { failed++; printerr("could not disassemble " + a); }
        }
        println("tables=" + tables + " code starts fixed=" + fixed + " failed=" + failed);
    }
}
