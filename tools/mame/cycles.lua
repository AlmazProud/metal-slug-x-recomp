-- Real per-instruction 68000 cycles from MAME (requires -debug -debugger none).
--
-- Enables the debugger trace between frames PD_DESDE and PD_HASTA. Each line is prefixed
-- with the CPU's total cycle counter ("C=<totalcycles>"), so the difference between
-- two consecutive lines is the real cost of the instruction (plus the interrupt entry
-- when the PC jumps to the handler). Output: PD_TRAZA (debugger trace file).

local dbg = manager.machine.debugger
local scr = manager.machine.screens[":screen"]
local first = tonumber(os.getenv("PD_DESDE") or "100")
local last = tonumber(os.getenv("PD_HASTA") or "102")
local path = os.getenv("PD_TRAZA") or "cycles.txt"
local trace_cpu = os.getenv("PD_CPU") or "maincpu"   -- "audiocpu" for the Z80
local active = false

-- PD_DESDE=-1: trace from power-on (the script runs before the first instruction).
if first < 0 then
    dbg:command(string.format('trace %s,%s,noloop,{tracelog "C=%%d ",totalcycles}', path, trace_cpu))
    active = true
end

cycles_sub = emu.add_machine_frame_notifier(function()
    local f = scr:frame_number()
    if not active and first >= 0 and f >= first and f < last then
        dbg:command("focus " .. trace_cpu)           -- tracelog writes to the CPU that has the focus
        dbg:command(string.format('trace %s,%s,noloop,{tracelog "C=%%d ",totalcycles}', path, trace_cpu))
        active = true
    elseif active and f >= last then
        dbg:command("trace off," .. trace_cpu)
        active = false
        manager.machine:exit()
    end
end)
