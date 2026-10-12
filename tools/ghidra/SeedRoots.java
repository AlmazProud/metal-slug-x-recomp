// Ghidra pre-script: disassembles and creates functions at the addresses (hex, one per line)
// of the file given as the first argument. Usage from analyzeHeadless:
//   -preScript SeedRoots.java <roots_file>
// @category ProjectoDecompile

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.nio.file.Files;
import java.nio.file.Path;

public class SeedRoots extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        int n = 0;
        for (String line : Files.readAllLines(Path.of(args[0]))) {
            line = line.trim();
            if (line.isEmpty()) continue;
            Address a = toAddr(Long.parseLong(line, 16));
            new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            if (getFunctionAt(a) == null) createFunction(a, null);
            n++;
        }
        println("PD: roots seeded: " + n);
    }
}
