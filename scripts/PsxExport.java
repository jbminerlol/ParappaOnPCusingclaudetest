// Ghidra post-analysis script: exports the function inventory and full decompilation
// of the functions that live in a program's own code block ("text" for the main EXE,
// "overlay" for a COMODx.BIN program).
//
// Args: <output-dir> [<start-hex> <end-hex>]
//       default range: block "overlay" if present, else the main EXE text 0x80010000-0x8006F000
//@category PaRappa
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighFunctionDBUtil;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.pcode.HighSymbol;
import ghidra.program.model.pcode.HighVariable;
import ghidra.program.model.pcode.Varnode;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;

public class PsxExport extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (!currentProgram.getName().matches("SCUS_941\\.83|COMOD\\d\\.BIN")) return;  // helper programs
        File outDir = new File(args[0], currentProgram.getName().replaceAll("[^A-Za-z0-9_.]", "_"));
        outDir.mkdirs();

        Address start, end;
        if (args.length >= 3) {
            start = toAddr(Long.decode(args[1]));
            end = toAddr(Long.decode(args[2]) - 1);
        } else {
            MemoryBlock own = currentProgram.getMemory().getBlock("overlay");
            start = own != null ? own.getStart() : toAddr(0x80010000L);
            end = own != null ? own.getEnd() : toAddr(0x8006EFFFL);
        }

        List<Function> funcs = new ArrayList<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(start, true)) {
            if (f.getEntryPoint().compareTo(end) > 0) break;
            funcs.add(f);
        }

        DecompInterface dec = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        opts.grabFromProgram(currentProgram);
        dec.setOptions(opts);
        dec.toggleCCode(true);
        dec.toggleSyntaxTree(true);
        dec.setSimplificationStyle("decompile");
        dec.openProgram(currentProgram);

        // Pass 1: commit the decompiler's inferred parameters/returns for functions whose signature
        // was never set, so callers decompiled in pass 2 pass every argument. Two rounds let
        // signatures settle through call chains.
        {
            // Only game code: library entry points (BIOS stubs especially) never read their argument
            // registers, so the decompiler would commit them as taking no parameters.
            List<Function> commit = new ArrayList<>();
            for (Function f : funcs) {
                long a = f.getEntryPoint().getOffset();
                if (a >= 0x801C3870L || (a >= 0x800127C4L && a < 0x80028588L)) commit.add(f);
            }
            if (currentProgram.getMemory().getBlock("overlay") != null) {
                // Overlay programs carry their own copy of the main EXE: settle the game functions too.
                for (Function f : currentProgram.getFunctionManager().getFunctions(toAddr(0x800127C4L), true)) {
                    if (f.getEntryPoint().getOffset() >= 0x80028588L) break;
                    commit.add(f);
                }
            }
            for (int round = 0; round < 2; round++) {
                for (Function f : commit) {
                    monitor.checkCancelled();
                    if (f.getSignatureSource() != SourceType.DEFAULT) continue;
                    DecompileResults r = dec.decompileFunction(f, 60, monitor);
                    if (r == null || !r.decompileCompleted() || r.getHighFunction() == null) continue;
                    HighFunctionDBUtil.commitParamsToDatabase(r.getHighFunction(), true,
                        HighFunctionDBUtil.ReturnCommitOption.COMMIT, SourceType.ANALYSIS);
                }
            }
        }

        int ok = 0, fail = 0;
        List<String> globals = new ArrayList<>();  // one row per (function, global) as typed there
        try (PrintWriter idx = new PrintWriter(new File(outDir, "functions.tsv"), "UTF-8");
             PrintWriter c = new PrintWriter(new File(outDir, "decomp.c"), "UTF-8")) {
            idx.println("address\tname\tsize\tcallers\tsignature");
            for (Function f : funcs) {
                monitor.checkCancelled();
                int callers = 0;
                for (Reference r : getReferencesTo(f.getEntryPoint()))
                    if (r.getReferenceType().isCall()) callers++;
                idx.println(f.getEntryPoint() + "\t" + f.getName() + "\t" + f.getBody().getNumAddresses() + "\t" +
                    callers + "\t" + f.getSignature().getPrototypeString());
                c.println("// ==== " + f.getEntryPoint() + " " + f.getName() + " size=" + f.getBody().getNumAddresses());
                DecompileResults r = dec.decompileFunction(f, 120, monitor);
                if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null) {
                    c.println(r.getDecompiledFunction().getC());
                    ok++;
                    HighFunction hf = r.getHighFunction();
                    if (hf != null) {
                        java.util.Iterator<HighSymbol> it = hf.getGlobalSymbolMap().getSymbols();
                        while (it.hasNext()) {
                            HighSymbol hs = it.next();
                            if (hs.getStorage() == null || !hs.getStorage().isMemoryStorage()) continue;
                            // Widest access in this function: Ghidra prints "_DAT_x" when it reads a
                            // global at a size different from its declared type.
                            int access = hs.getDataType().getLength();
                            HighVariable hv = hs.getHighVariable();
                            if (hv != null)
                                for (Varnode vn : hv.getInstances()) access = Math.max(access, vn.getSize());
                            globals.add(f.getEntryPoint() + "\t" + hs.getName() + "\t" + hs.getStorage().getMinAddress() +
                                "\t" + hs.getDataType().getDisplayName() + "\t" + hs.getDataType().getLength() + "\t" + access);
                        }
                    }
                } else {
                    c.println("// DECOMPILE FAILED: " + (r == null ? "null" : r.getErrorMessage()));
                    fail++;
                }
            }
        } finally {
            dec.dispose();
        }
        try (PrintWriter g = new PrintWriter(new File(outDir, "globals.tsv"), "UTF-8")) {
            g.println("function\tname\taddress\ttype\tsize\taccess");
            for (String row : globals) g.println(row);
        }
        println("PsxExport: " + currentProgram.getName() + " functions=" + funcs.size() + " ok=" + ok + " failed=" + fail +
            " -> " + outDir);
    }
}
