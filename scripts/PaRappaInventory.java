// Ghidra script: inventory and decompile the PaRappa PS-X entry function.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import java.io.File;
import java.io.PrintWriter;

public class PaRappaInventory extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String output = args.length > 0 ? args[0] : "analysis/inventory.txt";
        File outFile = new File(output);
        outFile.getParentFile().mkdirs();
        try (PrintWriter out = new PrintWriter(outFile, "UTF-8")) {
            out.println("Program: " + currentProgram.getName());
            out.println("Language: " + currentProgram.getLanguageID());
            out.println("Compiler: " + currentProgram.getCompilerSpec().getCompilerSpecID());
            out.println("Image base: " + currentProgram.getImageBase());
            Address entry = toAddr(0x80028590L);
            FunctionManager fm = currentProgram.getFunctionManager();
            Function fn = fm.getFunctionAt(entry);
            if (fn == null) {
                disassemble(entry);
                fn = createFunction(entry, "psx_entry");
            }
            if (fn == null) fn = fm.getFunctionContaining(entry);
            out.println("PS-X header entry: " + entry);
            out.println("Entry function: " + (fn == null ? "<not established>" : fn.getName() + " " + fn.getEntryPoint()));
            out.println("Function count: " + fm.getFunctionCount());
            out.println("\nFunction inventory (first 250 by address):");
            int n = 0;
            FunctionIterator it = fm.getFunctions(true);
            while (it.hasNext() && n++ < 250) {
                Function f = it.next();
                out.println(f.getEntryPoint() + "  " + f.getName() + "  " + f.getBody().getNumAddresses() + " bytes");
            }
            if (fn != null) {
                DecompInterface decomp = new DecompInterface();
                decomp.openProgram(currentProgram);
                DecompileResults result = decomp.decompileFunction(fn, 60, monitor);
                out.println("\n--- ENTRY DECOMPILATION ---");
                if (result != null && result.decompileCompleted() && result.getDecompiledFunction() != null)
                    out.println(result.getDecompiledFunction().getC());
                else out.println("Decompilation failed: " + (result == null ? "no result" : result.getErrorMessage()));
                decomp.dispose();
            }
        }
        println("Wrote PaRappa analysis to " + output);
    }
}
