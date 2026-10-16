// Ghidra script: dumps the disassembly (and decompilation, if any) of the given functions.
// Args: <output-file> <hex-address>...
//@category PaRappa
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.io.PrintWriter;

public class PsxDisasm extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Function f = getFunctionAt(toAddr(Long.parseLong(args[i], 16)));
                if (f == null) {
                    out.println("// no function at " + args[i]);
                    continue;
                }
                out.println("// ==== " + f.getEntryPoint() + " " + f.getName() + " " + f.getSignature().getPrototypeString());
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    String label = getSymbolAt(ins.getAddress()) != null ? getSymbolAt(ins.getAddress()).getName() + ":" : "";
                    out.printf("%s %-14s %s%n", ins.getAddress(), label, ins);
                }
                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                if (r != null && r.decompileCompleted())
                    out.println(r.getDecompiledFunction().getC());
                else
                    out.println("// DECOMPILE FAILED: " + (r == null ? "null" : r.getErrorMessage()));
            }
        } finally {
            dec.dispose();
        }
    }
}
