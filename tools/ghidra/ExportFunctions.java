// Post-script de Ghidra: exporta las funciones (inicio, bytes del cuerpo) y el total de bytes
// de instrucciones a un CSV. Uso desde analyzeHeadless:
//   -postScript ExportFunctions.java <funciones_csv> <instrucciones_csv>
// @category ProjectoDecompile

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

import java.io.PrintWriter;

public class ExportarFunciones extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        long bytesInstr = 0;
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        try (PrintWriter wi = new PrintWriter(args[1])) {     // inicio,longitud de cada instrucción
            while (it.hasNext()) {
                Instruction i = it.next();
                bytesInstr += i.getLength();
                wi.println(String.format("%X,%d", i.getAddress().getOffset(), i.getLength()));
            }
        }
        try (PrintWriter w = new PrintWriter(args[0])) {
            w.println("#bytes_instrucciones," + bytesInstr);
            w.println("inicio,bytes_cuerpo");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                w.println(String.format("%06X,%d", f.getEntryPoint().getOffset(), f.getBody().getNumAddresses()));
            }
        }
        println("PD: funciones exportadas: " + currentProgram.getFunctionManager().getFunctionCount());
    }
}
