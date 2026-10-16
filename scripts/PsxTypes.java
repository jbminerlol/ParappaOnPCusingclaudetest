// Ghidra script: writes every data type in the program's data type manager as a C header.
// Args: <output-file>
//@category PaRappa
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.DataTypeWriter;

import java.io.FileWriter;

public class PsxTypes extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs()[0];
        try (FileWriter w = new FileWriter(out)) {
            DataTypeWriter dtw = new DataTypeWriter(currentProgram.getDataTypeManager(), w);
            dtw.write(currentProgram.getDataTypeManager(), monitor);
        }
        println("PsxTypes: wrote " + out);
    }
}
