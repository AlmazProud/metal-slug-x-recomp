-- Logs MAME's 68000 cycle counter at every execution of one address (requires -debug -debugger none
-- and -log: the lines go to error.log as "E <totalcycles> <frame>"). PD_ENTRY_PC: the address
-- (hex), e.g. the VBlank handler, to compare the interrupt timing with the port's PD_WATCH_PC.
-- MAME runs the autoboot script again after a soft reset: the breakpoint is set once.
if pd_entry_cycles_loaded then return end
pd_entry_cycles_loaded = true
manager.machine.debugger:command(string.format('bpset %s,1,{logerror "E %%d %%d\\n",totalcycles,frame; g}',
                                               os.getenv("PD_ENTRY_PC")))
