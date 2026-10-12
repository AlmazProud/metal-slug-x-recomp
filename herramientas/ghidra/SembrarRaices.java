// Pre-script de Ghidra: desensambla y crea funciones en las direcciones (hex, una por línea)
// del archivo indicado como primer argumento. Uso desde analyzeHeadless:
//   -preScript SembrarRaices.java <archivo_raices>
// @category ProjectoDecompile

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.nio.file.Files;
import java.nio.file.Path;

public class SembrarRaices extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        int n = 0;
        for (String linea : Files.readAllLines(Path.of(args[0]))) {
            linea = linea.trim();
            if (linea.isEmpty()) continue;
            Address a = toAddr(Long.parseLong(linea, 16));
            new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            if (getFunctionAt(a) == null) createFunction(a, null);
            n++;
        }
        println("PD: raices sembradas: " + n);
    }
}
