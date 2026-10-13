-- Real per-instruction 68000 cycles from MAME (requires -debug -debugger none).
--
-- Enables the debugger trace between frames PD_FROM and PD_TO. Each line is prefixed
-- with the CPU's total cycle counter ("C=<totalcycles>"), so the difference between
-- two consecutive lines is the real cost of the instruction (plus the interrupt entry
-- when the PC jumps to the handler). Output: PD_TRACE (debugger trace file).

local dbg = manager.machine.debugger
local scr = manager.machine.screens[":screen"]
local first = tonumber(os.getenv("PD_FROM") or "100")
local last = tonumber(os.getenv("PD_TO") or "102")
local path = os.getenv("PD_TRACE") or "cycles.txt"
local trace_cpu = os.getenv("PD_CPU") or "maincpu"   -- "audiocpu" for the Z80
local active = false
-- PD_TRACE_REGS=1: also logs D0-D7 before every instruction (like the port's PD_TRACE_REGS).
local fmt = os.getenv("PD_TRACE_REGS") and '{tracelog "C=%%d D0=%%08X D1=%%08X D2=%%08X D3=%%08X D4=%%08X D5=%%08X D6=%%08X D7=%%08X ",totalcycles,d0,d1,d2,d3,d4,d5,d6,d7}' or '{tracelog "C=%%d ",totalcycles}'

-- PD_FROM=-1: trace from power-on (the script runs before the first instruction).
if first < 0 then
    dbg:command(string.format('trace %s,%s,noloop,' .. fmt, path, trace_cpu))
    active = true
end

cycles_sub = emu.add_machine_frame_notifier(function()
    local f = scr:frame_number()
    if not active and first >= 0 and f >= first and f < last then
        dbg:command("focus " .. trace_cpu)           -- tracelog writes to the CPU that has the focus
        dbg:command(string.format('trace %s,%s,noloop,' .. fmt, path, trace_cpu))
        active = true
    elseif active and f >= last then
        dbg:command("trace off," .. trace_cpu)
        active = false
        manager.machine:exit()
    end
end)
