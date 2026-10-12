// Ghidra post-script: exports the functions (start, body bytes) and the total number of
// instruction bytes to a CSV. Usage from analyzeHeadless:
//   -postScript ExportFunctions.java <functions_csv> <instructions_csv>
// @category ProjectoDecompile

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

import java.io.PrintWriter;

public class ExportFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        long instrBytes = 0;
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        try (PrintWriter wi = new PrintWriter(args[1])) {     // start,length of each instruction
            while (it.hasNext()) {
                Instruction i = it.next();
                instrBytes += i.getLength();
                wi.println(String.format("%X,%d", i.getAddress().getOffset(), i.getLength()));
            }
        }
        try (PrintWriter w = new PrintWriter(args[0])) {
            // Header lines are part of the CSV format: keep them.
            w.println("#bytes_instrucciones," + instrBytes);
            w.println("inicio,bytes_cuerpo");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                w.println(String.format("%06X,%d", f.getEntryPoint().getOffset(), f.getBody().getNumAddresses()));
            }
        }
        println("PD: functions exported: " + currentProgram.getFunctionManager().getFunctionCount());
    }
}
